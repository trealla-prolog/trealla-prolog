#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sched.h>
#include <signal.h>

#include "module.h"
#include "query.h"

#if USE_THREADS

#if 0
#define THREAD_DEBUG if (1) fprintf(stderr, "*** %lld ", (long long)time(NULL));
#else
#define THREAD_DEBUG if (0)
#endif

static void msleep(int ms)
{
	struct timespec tv = {0};
	tv.tv_sec = (ms) / 1000;
	tv.tv_nsec = ((ms) % 1000) * 1000 * 1000;
	nanosleep(&tv, &tv);
}

void init_lock(lock *l)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&l->mutex, &attr);
}

void deinit_lock(lock *l)
{
	pthread_mutex_destroy(&l->mutex);
}

bool try_lock(lock *l)
{
	return pthread_mutex_trylock(&l->mutex) == 0;
}

void acquire_lock(lock *l)
{
	pthread_mutex_lock(&l->mutex);
}

void release_lock(lock *l)
{
	pthread_mutex_unlock(&l->mutex);
}

#else

void init_lock(lock *l) {}
void deinit_lock(lock *l) {}
void acquire_lock(lock *l) {}
void release_lock(lock *l) {}

#endif

#define is_threaded(t) (!(t)->is_queue_only && !(t)->is_mutex_only)

typedef struct msg_ {
	lnode hdr;						// must be first
	int from_chan;
	cell c[];
} msg;

#define is_thread(c) is_thread_or_alias(q, c)
#define is_mutex(c) is_mutex_or_alias(q, c)
#define is_queue(c) is_queue_or_alias(q, c)

#define check_thread_object(c) check_thread_or_alias_object(q, c)

#define check_thread(c) check_thread_or_alias(q, c)
#define check_mutex(c) check_mutex_or_alias(q, c)
#define check_queue(c) check_queue_or_alias(q, c)

// FIXME: there should be one overall alias map, not one per stream

static int get_named_thread(prolog *pl, const char *name, size_t len)
{
	prolog_lock(pl);
	thread *t = NULL;

	if (sl_get(pl->alias, name, (const void**)&t)) {
		prolog_unlock(pl);
		return t->chan;
	}

	prolog_unlock(pl);
	return -1;
}

// Reaching the thread table.
//
// Everything that looks one up or walks the table goes through these,
// so the storage underneath can be changed without touching the fifty
// call sites that use it. Over an array they are trivial; the point is
// that they are the only things that know it is an array.
//
// next_thread_after() takes an id rather than a position because the
// property predicates resume across backtracking from a saved id
// (q->st.v1). Ids are handed out in increasing order, so "the next one
// after N" stays meaningful even when threads come and go in between -
// which a raw index into a table that had been reshuffled would not.

thread *find_thread_by_id(prolog *pl, int chan)
{
	if ((chan < 0) || !pl->threads)
		return NULL;

	// sl_get()/sl_set()/sl_del() do their own linked-list splicing with
	// no locking of their own - only the skiplist's iterator functions
	// take its guard. new_thread() mutates pl->threads (sl_del + sl_set)
	// under prolog_lock() when it recycles a struct; this lookup used to
	// run with no lock at all, so a concurrent sl_del() could free the
	// node this is mid-traversal through. Traced from an intermittent
	// heap-use-after-free under concurrent thread churn - see
	// samples/skynet.pl.

	const bool mt = pl->is_multithreaded;

	if (mt)
		prolog_lock(pl);

	const void *v = NULL;
	bool found = sl_get(pl->threads, (const void*)(uintptr_t)chan, &v);

	if (mt)
		prolog_unlock(pl);

	return found ? (thread*)v : NULL;
}


static thread *main_thread(prolog *pl)
{
	return pl->main_thread;
}

// Which thread is running this query.
//
// There used to be a second one asking which *pthread* is executing,
// by scanning. Its only caller was the SIGALRM handler, which had no
// query to ask; timeouts are polled off the thread object now, so the
// scan is gone and with it the last thing that could not answer this
// question once a thread becomes a task rather than a pthread.
//
// The same q->thread_ptr-or-threads[0] idiom is used in query.h,
// toplevel.c, bif_os.c and bif_tabling.c; this just names it.

// The Prolog thread this OS thread runs, for code with no query to ask,
// such as assert_begin() resolving a thread_local/1 predicate.

static pl_thread_local thread *g_self_thread;

// The interrupted goal again, once the engine has run the thread_signal/2
// that interrupted it: as SWI does, a signal that throws abandons the
// read or wait it came in, and one that doesn't lets it carry on.

bool retry_after_signal(query *q)
{
	cell *goal = q->st.instr;
	cell *tmp = prepare_call(q, CALL_NOSKIP, goal, q->st.cur_ctx, 1);
	CHECKED(tmp);
	make_call(q, tmp+goal->num_cells);
	q->st.instr = tmp;
	return true;
}

// A wait thread_cancel/1 interrupted ends the thread there, as the
// thread_exit(cancelled) it was sent does when it is next between goals.

bool thread_exit_cancelled(query *q)
{
	thread *t = q->thread_ptr;
	cell *tmp = TPL_calloc(2, sizeof(cell));
	CHECKED(tmp);
	make_instr(tmp, new_atom(q->pl, "exited"), NULL, 1, 1);
	make_atom(tmp+1, new_atom(q->pl, "cancelled"));
	acquire_lock(&t->guard);			// thread_property/2 reads it under this

	if (t->exit_code) {
		unshare_cells(t->exit_code, t->exit_code->num_cells);
		TPL_free(t->exit_code);
	}

	t->exit_code = tmp;

	// The thread_exit(cancelled) still queued is done with: left there, it
	// would end the at_exit goal before that ran a goal.

	msg *m;

	while ((m = list_pop_front(&t->signals)) != NULL) {
		unshare_cells(m->c, m->c->num_cells);
		TPL_free(m);
	}

	release_lock(&t->guard);
	q->halt_code = 0;
	q->halt = q->error = true;
	return true;
}

thread *current_thread(prolog *pl)
{
	return g_self_thread ? g_self_thread : pl->main_thread;
}

// A thread's shadows of thread_local/1 predicates keep their names, for
// the next thread to get the same id, but lose their clauses with it.

void thread_local_release(thread *t)
{
	for (unsigned i = 0; i < t->tl_shadows_n; i++) {
		predicate *pr = t->tl_shadows[i];
		module *m = pr->m;
		prolog_lock_mod(m->pl, m);

		for (rule *r = pr->head; r; r = r->next) {
			if (!r->dbgen_retracted)
				retract_from_db(m, r);
		}

		prolog_unlock_mod(m->pl, m);
	}

	TPL_free(t->tl_shadows);
	TPL_free(t->tl_origs);
	t->tl_shadows = t->tl_origs = NULL;
	t->tl_shadows_n = t->tl_shadows_cap = 0;
}

thread *get_self_query(const query *q)
{
	return q->thread_ptr ? q->thread_ptr : main_thread(q->pl);
}

// The live list is kept in increasing chan order and ids are handed out
// increasing, so "the next one after N" is a walk from the head. That is
// what the property predicates want when they resume from a saved id,
// and it stays meaningful across entries coming and going in a way that
// an index into the table never would.

static thread *next_thread_after(prolog *pl, int chan)
{
	for (thread *t = pl->live_head; t; t = t->live_next) {
		if (t->chan > chan)
			return t;
	}

	return NULL;
}

static thread *first_thread(prolog *pl)
{
	return pl->live_head;
}

// Walks the intrusive list rather than the skiplist on purpose: this
// runs in the SIGALRM handler, where sl_first() would take a lock and
// allocate an iterator, and neither is async-signal-safe.

#define for_each_thread(pl, t) \
	for (thread *t = (pl)->live_head; t; t = t->live_next)

// Threads, message queues and mutexes all live in the same table and are
// told apart by two flags. The property predicates each enumerate one
// kind, resuming from a saved id across backtracking, so they all want
// the same thing: the next entry of this kind after that id.
// A first pass starts from -1, so id 0, the main thread, is not skipped.

enum thread_kind { TK_THREAD, TK_QUEUE, TK_MUTEX };

static thread *next_of_kind(prolog *pl, int chan, enum thread_kind kind)
{
	for (thread *t = next_thread_after(pl, chan); t; t = next_thread_after(pl, t->chan)) {
		bool want = kind == TK_QUEUE ? t->is_queue_only
			: kind == TK_MUTEX ? t->is_mutex_only
			: !t->is_queue_only && !t->is_mutex_only;

		if (want)
			return t;
	}

	return NULL;
}

static int get_thread(query *q, cell *p1)
{
	if (is_atom(p1)) {
		int n = get_named_thread(q->pl, C_STR(q, p1), C_STRLEN(q, p1));

		if (n < 0)
			return -1;

		return n;
	}

	if (p1->tag != TAG_INT)
		return -1;

	if (!(p1->flags & FLAG_INT_THREAD))
		return -1;

	int n = get_smallint(p1);

	thread *t = find_thread_by_id(q->pl, n);

	if (!t || !t->is_active)
		return -1;

	return n;
}

// Put an entry on the live list. Ids only ever increase, so the new one
// always belongs at the tail and the list stays sorted for free.

static void link_live(prolog *pl, thread *t)
{
	t->live_next = NULL;
	t->live_prev = pl->live_tail;

	if (pl->live_tail)
		pl->live_tail->live_next = t;
	else
		pl->live_head = t;

	pl->live_tail = t;
}

static void unlink_live(prolog *pl, thread *t)
{
	if (t->live_prev)
		t->live_prev->live_next = t->live_next;
	else
		pl->live_head = t->live_next;

	if (t->live_next)
		t->live_next->live_prev = t->live_prev;
	else
		pl->live_tail = t->live_prev;

	t->live_next = t->live_prev = NULL;
}

// Hand a retired entry back. The struct is not freed - it goes on the
// free list for the next new_thread() - so a stale `thread *` sees a
// recycled struct rather than freed memory, which is exactly what the
// fixed table did when it reissued a slot. What does *not* come back is
// the id: those keep counting up, so a message naming an id that has
// since been retired fails to find it instead of reaching a stranger.

static void retire_thread(prolog *pl, thread *t)
{
	// Same lock new_thread() takes, and in the same order relative to
	// t->guard: several callers retire while holding that, and nothing
	// anywhere takes pl->guard before it.

	prolog_lock(pl);

	if (!t->is_active) {
		prolog_unlock(pl);
		return;
	}

	if (t->alias) {
		sl_del(pl->alias, t->alias);
		TPL_free(t->alias);
		t->alias = NULL;
	}

	// The id stays in the map, pointing at the now-inactive struct, and
	// is dropped only when the struct is handed out again. get_thread()
	// rejects it either way because it tests is_active - but anything
	// that only wants to know what kind of object an id named, printing
	// most of all, still gets a straight answer for a stale handle.

	unlink_live(pl, t);
	t->is_active = false;

	// Its task ids carry the chan it is about to lose, so the registry
	// they resolve through goes now rather than when the struct is
	// handed out again - a lookup with a stale id must miss, not reach
	// whatever the next occupant registers.

	tasks_destroy(t);

	// Appended, not pushed: taking the oldest retired struct first means
	// a stale handle stays readable for as long as possible, which is
	// what the fixed table gave for free by cycling round its slots.

	t->free_next = NULL;

	if (pl->free_tail)
		pl->free_tail->free_next = t;
	else
		pl->free_head = t;

	pl->free_tail = t;
	prolog_unlock(pl);
}

// The main thread is an entry like any other, made first so it gets id
// 0 - which is what every "q->thread_ptr or the main thread" fallback in
// query.h, toplevel.c, bif_os.c and bif_tabling.c resolves to. It used
// to be threads[0] by construction; now it is explicit.

static int new_thread(prolog *pl);

// Free every struct, live or retired. Nothing is freed before this, so
// this is the only place they go away.

// What a thread still has at shutdown, the main one always: a retired thread gave all this up already, leaving NULLs.

static void thread_free_leftovers(thread *t)
{
	msg *m;

	while ((m = list_pop_front(&t->queue)) != NULL) {
		unshare_cells(m->c, m->c->num_cells);
		TPL_free(m);
	}

	while ((m = list_pop_front(&t->signals)) != NULL) {
		unshare_cells(m->c, m->c->num_cells);
		TPL_free(m);
	}

	if (t->ball) {
		unshare_cells(t->ball, t->ball->num_cells);
		TPL_free(t->ball);
		t->ball = NULL;
	}

	if (t->exit_code) {
		unshare_cells(t->exit_code, t->exit_code->num_cells);
		TPL_free(t->exit_code);
		t->exit_code = NULL;
	}

	// The shadow predicates are their modules', destroyed after this: only the thread's own arrays go here.

	TPL_free(t->tl_shadows);
	TPL_free(t->tl_origs);
	t->tl_shadows = t->tl_origs = NULL;
}

void threads_destroy(prolog *pl)
{
	for (thread *t = pl->live_head, *next; t; t = next) {
		next = t->live_next;
		tasks_destroy(t);
		sched_destroy(t);
		gvars_destroy(t);
		thread_free_leftovers(t);
		TPL_free(t);
	}

	for (thread *t = pl->free_head, *next; t; t = next) {
		next = t->free_next;
		tasks_destroy(t);
		sched_destroy(t);
		gvars_destroy(t);
		thread_free_leftovers(t);
		TPL_free(t);
	}

	pl->live_head = pl->live_tail = NULL;
	pl->free_head = pl->free_tail = pl->main_thread = NULL;

	if (pl->threads) {
		sl_destroy(pl->threads);
		pl->threads = NULL;
	}
}

static int new_thread(prolog *pl)
{
	prolog_lock(pl);
	thread *t = pl->free_head;

	if (t) {
		pl->free_head = t->free_next;

		if (!pl->free_head)
			pl->free_tail = NULL;

		t->free_next = NULL;

		// Reusing the struct is the point at which its old id stops
		// meaning anything, so that is when the key goes.

		sl_del(pl->threads, (const void*)(uintptr_t)t->chan);
	} else {
		t = TPL_calloc(1, sizeof(thread));

		if (!t) {
			prolog_unlock(pl);
			return -1;
		}
	}

	int n = (int)pl->next_thread_id++;

	if (!t->is_init) {
#if USE_THREADS
		pthread_cond_init(&t->cond, NULL);
		pthread_mutex_init(&t->mutex, NULL);
#endif
		init_lock(&t->guard);
		init_lock(&t->tasks_guard);
		t->is_init = true;
		t->pl = pl;
	}

	t->chan = n;
#if USE_THREADS
	t->guard.tid = n;
	t->id = pthread_self();
#endif
	t->is_detached = false;
	t->is_joining = false;
	t->cancel_requested = false;

	if (t->exit_code) {						// a detached thread's, never joined
		unshare_cells(t->exit_code, t->exit_code->num_cells);
		TPL_free(t->exit_code);
		t->exit_code = NULL;
	}

	t->alerted = false;
	t->alertable = false;
	gvars_destroy(t);					// a recycled struct starts with none
	t->gvar_seq = 0;
	t->is_queue_only = false;
	t->is_mutex_only = false;
	t->is_finished = false;
	t->is_exception = false;
	t->is_failed = false;
	t->locked_by = -1;
	t->num_locks = 0;
	t->at_exit_goal = NULL;
	t->goal = NULL;
	t->ball = NULL;
	t->alias = NULL;
	t->q = NULL;
	t->is_active = true;

	if (!sl_set(pl->threads, (const void*)(uintptr_t)n, t)) {
		t->is_active = false;
		t->free_next = NULL;

		if (pl->free_tail)
			pl->free_tail->free_next = t;
		else
			pl->free_head = t;

		pl->free_tail = t;
		prolog_unlock(pl);
		return -1;
	}

	link_live(pl, t);
	prolog_unlock(pl);
	return n;
}

// The table has to exist before anything can be looked up in it, and
// the main thread is the first entry made - which is what gives it id 0,
// the value every "q->thread_ptr or the main thread" fallback in
// query.h, toplevel.c, bif_os.c and bif_tabling.c used to reach as
// threads[0]. It is pl->main_thread now rather than an array slot.

void thread_initialize(prolog *pl)
{
	pl->threads = sl_create(NULL, NULL, NULL);
	ENSURE(pl->threads);
	int n = new_thread(pl);
	ENSURE(n == 0);
	thread *t = find_thread_by_id(pl, n);
	ENSURE(t);
	pl->main_thread = t;
	t->alertable = true;					// it runs as long as pl does
	t->alias = TPL_strdup("main");
	sl_app(pl->alias, t->alias, t);
	t->is_detached = true;
}

void thread_deinitialize(prolog *pl)
{
	// Not for_each_thread(): retiring unlinks the entry we are standing
	// on, so the successor has to be taken first.

	for (thread *t = pl->live_head, *next; t; t = next) {
		next = t->live_next;

		if (t->is_init)
			retire_thread(pl, t);
	}
}

#if USE_THREADS

// Release a thread/mutex/queue slot whose option list failed to parse.
//
// Clearing is_active alone is not enough once an alias(...) option has
// been seen: t->alias is a DUP_STRING that leaks, and it is still
// registered in pl->alias, so the skiplist keeps a pointer into a slot
// that has just been handed back. Reachable whenever a LATER option is
// bad - mutex_create(M, [alias(foo), bogus]) and the same shape for
// message_queue_create/2 and thread_create/3.
//
// thread_deinitialize() has always done the full teardown; these three
// option loops just never reached it.

static void unwind_thread(prolog *pl, thread *t)
{
	retire_thread(pl, t);
}

static bool is_thread_or_alias(query *q, cell *c)
{
	pl_ctx c_ctx = 0;

	if (is_var(c))
		return throw_error(q, c, c_ctx, "instantiation_error", "thread_or_alias");

	int n = get_thread(q, c);

	if (n < 0)
		return throw_error(q, c, c_ctx, "existence_error", "thread_or_alias");

	thread *t = find_thread_by_id(q->pl, n);

	if (!t->is_active || t->is_mutex_only || t->is_queue_only)
		return throw_error(q, c, c_ctx, "existence_error", "thread_or_alias");

	return true;
}

static bool is_mutex_or_alias(query *q, cell *c)
{
	pl_ctx c_ctx = 0;

	if (is_var(c))
		return throw_error(q, c, c_ctx, "instantiation_error", "mutex_or_alias");

	int n = get_thread(q, c);

	if (n < 0)
		return throw_error(q, c, c_ctx, "existence_error", "mutex_or_alias");

	thread *t = find_thread_by_id(q->pl, n);

	if (!t->is_active || t->is_queue_only)
		return throw_error(q, c, c_ctx, "existence_error", "mutex_or_alias");

	return true;
}

static bool is_queue_or_alias(query *q, cell *c)
{
	pl_ctx c_ctx = 0;

	if (is_var(c))
		return throw_error(q, c, c_ctx, "instantiation_error", "queue_or_alias");

	int n = get_thread(q, c);

	if (n < 0)
		return throw_error(q, c, c_ctx, "existence_error", "queue_or_alias");

	thread *t = find_thread_by_id(q->pl, n);

	if (!t->is_active || t->is_mutex_only)
		return throw_error(q, c, c_ctx, "existence_error", "queue_or_alias");

	return true;
}

static bool check_thread_or_alias_object(query *q, cell *c)
{
	if (is_var(c))
		return false;

	int n = get_thread(q, c);

	if (n < 0)
		return false;

	return true;
}

static bool check_thread_or_alias(query *q, cell *c)
{
	if (is_var(c))
		return false;

	int n = get_thread(q, c);

	if (n < 0)
		return false;

	thread *t = find_thread_by_id(q->pl, n);
	return !t->is_mutex_only && !t->is_queue_only;
}

static bool check_mutex_or_alias(query *q, cell *c)
{
	if (is_var(c))
		return false;

	int n = get_thread(q, c);

	if (n < 0)
		return false;

	thread *t = find_thread_by_id(q->pl, n);
	return t->is_mutex_only;
}

static bool check_queue_or_alias(query *q, cell *c)
{
	if (is_var(c))
		return false;

	int n = get_thread(q, c);

	if (n < 0)
		return false;

	thread *t = find_thread_by_id(q->pl, n);
	return t->is_queue_only;
}


void suspend_thread(thread *t, int ms)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	// FIX: normalise tv_nsec into [0,1e9); otherwise pthread_cond_timedwait returns EINVAL and busy-waits
	ts.tv_sec += ms / 1000;
	ts.tv_nsec += (long)(ms % 1000) * 1000 * 1000;
	if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
	pthread_mutex_lock(&t->mutex);
	pthread_cond_timedwait(&t->cond, &t->mutex, &ts);
	pthread_mutex_unlock(&t->mutex);
}

static void resume_thread(thread *t)
{
	pthread_mutex_lock(&t->mutex);
	pthread_cond_broadcast(&t->cond);
	pthread_mutex_unlock(&t->mutex);
}

// As SWI does: a thread blocked in a read gets SIGUSR2, whose handler does
// nothing and is installed without SA_RESTART, so the read comes back
// EINTR and the signal can run (see throw_timeout()). Sent under the guard
// while its OS thread still runs, which it stops saying under the same
// guard before it exits, so it never reaches a thread that's gone. A wait
// on a condition is woken as before, a polled one sees alerted.

#if !defined(_WIN32) && !defined(__wasi__)
static void alert_handler(int sig)
{
	(void) sig;
}

static void install_alert_handler(void)
{
	struct sigaction sa = {0};
	sa.sa_handler = alert_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGUSR2, &sa, NULL);
}
#endif

static void alert_thread(thread *t)
{
	t->alerted = true;
	resume_thread(t);

#if !defined(_WIN32) && !defined(__wasi__)
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	pthread_once(&once, install_alert_handler);
	acquire_lock(&t->guard);

	if (t->alertable && !pthread_equal((pthread_t)t->id, pthread_self()))
		pthread_kill((pthread_t)t->id, SIGUSR2);

	release_lock(&t->guard);
#endif
}

static unsigned queue_size(prolog *pl, unsigned chan)
{
	thread *t = find_thread_by_id(pl, chan);

	if (!t)
		return 0;

	return list_count(&t->queue);
}

static void wake_msg_waiters(thread *t);

// Takes an already-resolved thread rather than looking one up itself.
// It used to re-resolve chan with its own find_thread_by_id() call,
// which gave do_send_message() two independent lookups for one id - and
// between them, the thread the id named could retire and its struct get
// handed to a completely different thread_create/3. The stale pointer
// from the first lookup would then be used by resume_thread() to signal
// someone else's condvar. One lookup, used immediately, closes that.

static bool queue_to_chan(thread *t, const cell *c, unsigned from_chan, bool is_signal)
{
	msg *m = TPL_malloc(sizeof(msg) + (sizeof(cell)*c->num_cells));
	if (!m) return false;
	m->from_chan = from_chan;
	dup_cells(m->c, c, c->num_cells);
	acquire_lock(&t->guard);

	if (is_signal) {
		list_push_back(&t->signals, m);
	} else {
		list_push_back(&t->queue, m);
	}

	wake_msg_waiters(t);
	release_lock(&t->guard);
	return true;
}

static bool do_send_message(query *q, unsigned chan, cell *c, pl_ctx c_ctx, bool is_signal)
{
	thread *t = find_thread_by_id(q->pl, chan);

	// find_thread_by_id() can return NULL - the id may already have
	// been retired and reused for something else by the time this
	// runs. Fail rather than dereference it: this was crashing under
	// concurrent thread churn (samples/skynet.pl).

	if (!t)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "existence_error", "thread_object");

	CHECKED(init_tmp_heap(q));
	cell *tmp = clone_term_to_tmp(q, c, c_ctx);
	CHECKED(tmp);
	rebase_term(q, tmp, 0, false);
	CHECKED(queue_to_chan(t, tmp, q->my_chan, is_signal));
	resume_thread(t);
	return true;
}

static bool bif_thread_send_message_2(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	int n = get_thread(q, p1);

	if ((n < 0) || !is_queue(p1)) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	if (!do_send_message(q, n, p2, p2_ctx, false)) {
		return false;
	}

	THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
	return true;
}

// How long a parked task waits before looking again. It sits on the
// scheduler's timer heap rather than spinning, so the cost of a short
// interval is one pass round the scheduler - low enough that message
// latency stays invisible. A wait-list that a send could wake directly
// would remove the polling altogether, but that needs a scheduler which
// can be woken from another thread, which is phase 3.

#define MSG_TASK_POLL_MS 5

enum msg_wait {
	MSG_WAIT_AGAIN,						// waited in place, look again
	MSG_WAIT_EXPIRED,					// deadline passed
	MSG_WAIT_YIELDED					// task parked; unwind to the scheduler
};

// One wait, for whoever is asking.
//
// A task must not sit on the condvar. It would hold the only worker and
// every sibling task with it - which is exactly what a blocking receive
// inside a task used to do. It parks on the timer heap instead and the
// scheduler runs everyone else until it comes round again. A real
// thread has nothing else to hold up, so it still sleeps on the condvar
// and a send wakes it immediately.

// Put a task on the queue's waiter list while it is parked, so a send
// can find it. Both ends run under t->guard, the same lock the message
// list itself uses, so a message cannot slip in between a task deciding
// to park and becoming visible to the sender.

static void add_msg_waiter(thread *t, query *q)
{
	if (q->waiting_on)
		return;

	acquire_lock(&t->guard);
	q->waiting_on = t;
	q->wait_next = t->msg_waiters;
	t->msg_waiters = q;
	release_lock(&t->guard);
}

static void del_msg_waiter(query *q)
{
	thread *t = q->waiting_on;

	if (!t)
		return;

	acquire_lock(&t->guard);

	for (query **pp = &t->msg_waiters; *pp; pp = &(*pp)->wait_next) {
		if (*pp == q) {
			*pp = q->wait_next;
			break;
		}
	}

	q->wait_next = NULL;
	q->waiting_on = NULL;
	release_lock(&t->guard);
}

// Everyone parked on this queue, promoted. Called with t->guard held by
// the sender, right after the message goes on the list.

static void wake_msg_waiters(thread *t)
{
	while (t->msg_waiters) {
		query *q = t->msg_waiters;
		t->msg_waiters = q->wait_next;
		q->wait_next = NULL;
		q->waiting_on = NULL;
		sched_promote(q);
	}
}

static enum msg_wait do_wait_message(query *q, thread *t, uint64_t deadline)
{
	uint64_t now = wall_time_in_usec() / 1000;

	if (deadline && (now >= deadline))
		return MSG_WAIT_EXPIRED;

	uint64_t left = deadline ? deadline - now : 0;

	if (q->is_task) {
		uint64_t nap = MSG_TASK_POLL_MS;

		if (deadline && (nap > left))
			nap = left;

		// On the waiter list before parking, so a send that lands in the
		// gap promotes us rather than being missed. The nap is only a
		// backstop now - delivery wakes us directly.

		add_msg_waiter(t, q);
		return do_yield(q, nap < 1 ? 1 : (int)nap) ? MSG_WAIT_AGAIN : MSG_WAIT_YIELDED;
	}

	uint64_t nap = 100;

	if (deadline && (nap > left))
		nap = left;

	suspend_thread(t, nap < 1 ? 1 : (int)nap);
	return MSG_WAIT_AGAIN;
}

static bool do_match_message_(query *q, unsigned chan, bool is_peek, double timeout)
{
	GET_FIRST_ARG(pq,queue);
	thread *t = find_thread_by_id(q->pl, chan);

	// The deadline is absolute and lives on the query, because a task
	// that parks in here is retried from the top of the builtin: a
	// deadline recomputed on re-entry would reset the clock every time
	// and never expire. q->retry is what distinguishes a resumption
	// from a fresh call.

	if (!q->retry) {
		pl_int tmo_ms = timeout * 1000;
		q->msg_deadline = (tmo_ms >= 0) ? (wall_time_in_usec() / 1000) + tmo_ms : 0;
	}

	const uint64_t deadline = q->msg_deadline;

	while (!q->halt && !q->abort) {
		// A surrounding call_with_time_limit/2's '$alarm'/2 only fires at
		// CHECK_INTERRUPT() points; without this check this loop never
		// reaches one and blocks forever instead of timing out.
		if (interrupt_pending(q) && check_interrupt(q))
			return false;

		acquire_lock(&t->guard);

		if (!q->sig_block && list_count(&t->signals)) {
			release_lock(&t->guard);
			do_signal(t->q, t);
			start(t->q);
			continue;
		}

		if (!list_count(&t->queue)) {
			release_lock(&t->guard);

			if (is_peek)
				return false;

			if (do_wait_message(q, t, deadline) != MSG_WAIT_AGAIN)
				return false;

			continue;
		}

		msg *m = list_front(&t->queue);
		const frame *f = GET_CURR_FRAME();

		while (m) {
			CHECKED(push_choice(q), release_lock(&t->guard));
			cell *tmp = import_term(q, m->c, q->st.cur_ctx);
			CHECKED(tmp, release_lock(&t->guard));
			GET_FIRST_ARG(p1,queue);
			GET_NEXT_ARG(p2,any);

			if (unify(q, p2, p2_ctx, tmp, q->st.cur_ctx)) {
				q->cur_chan = m->from_chan;

				if (!is_peek)
					list_remove(&t->queue, m);

				release_lock(&t->guard);

				if (!is_peek) {
					unshare_cells(m->c, m->c->num_cells);
					TPL_free(m);
				}

				drop_choice(q);
				return true;
			}

			retry_choice(q);
			m = list_next(m);
		}

		release_lock(&t->guard);

		if (is_peek)
			break;

		// Nothing in the queue unified. Everything already there has
		// been tried, so walking it again changes nothing until a new
		// message arrives - and this is the only point at which a *non
		// empty* queue can honour its deadline. Before both waits went
		// through one place, the timeout was checked only in the branch
		// an empty queue takes, which a non-empty queue never reaches,
		// and a receive that matched nothing spun here forever.

		if (do_wait_message(q, t, deadline) != MSG_WAIT_AGAIN)
			return false;
	}

	return false;
}

// A task stays on the queue's waiter list only while it is parked. On
// any other way out - matched, timed out, gave up - it has to come off,
// or the next send would promote a query that has long since moved on.

static bool do_match_message(query *q, unsigned chan, bool is_peek, double timeout)
{
	bool ok = do_match_message_(q, chan, is_peek, timeout);

	if (!q->yielded)
		del_msg_waiter(q);

	return ok;
}

static bool bif_thread_get_message_2(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,any);
	int n = get_thread(q, p1);

	if ((n < 0) || !is_queue(p1)) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	bool ok = do_match_message(q, n, false, -1.0);
	THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
	return ok;
}

static bool bif_thread_get_message_3(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,any);
	int n = get_thread(q, p1);

	if ((n < 0) || !is_queue(p1)) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	GET_NEXT_ARG(p2,any);
	GET_NEXT_ARG(p3,list_or_nil);
	PROLOG_LIST_HANDLER(p3);
	cell *p3_orig = p3;
	pl_ctx p3_orig_ctx = p3_ctx;
	double timeout = -1.0, deadline = 0.0;
	bool has_deadline = false;

	while (is_iso_list(p3)) {
		cell *h = PROLOG_LIST_HEAD(p3);
		h = deref(q, h, p3_ctx);
		pl_ctx h_ctx = q->latest_ctx;

		if (!is_interned(h) || !is_compound(h)) {
			throw_error(q, h, h_ctx, "domain_error", "read_option");
			return false;
		}

		if (!CMP_STRING_TO_CSTR(q, h, "timeout")) {
			cell *c1 = deref(q, FIRST_ARG(h), h_ctx);
			pl_ctx c1_ctx = q->latest_ctx;

			if (!is_number(c1)) {
				throw_error(q, c1, h_ctx, "type_error", "read_option");
				return false;
			}

			timeout = is_float(c1) ? get_float(c1) : get_smallint(c1);
		} else if (!CMP_STRING_TO_CSTR(q, h, "deadline")) {
			cell *c1 = deref(q, FIRST_ARG(h), h_ctx);
			pl_ctx c1_ctx = q->latest_ctx;

			if (!is_number(c1)) {
				throw_error(q, c1, h_ctx, "type_error", "read_option");
				return false;
			}

			deadline = is_float(c1) ? get_float(c1) : get_smallint(c1);
			has_deadline = true;
		} else {
			throw_error(q, h, h_ctx, "domain_error", "read_option");
			return false;
		}

		p3 = PROLOG_LIST_TAIL(p3);
		p3 = deref(q, p3, p3_ctx);
		p3_ctx = q->latest_ctx;
	}

	if (is_var(p3)) {
		clear_write_options(q);
		return throw_error(q, p3_orig, p3_orig_ctx, "instantiation_error", "get_option");
	}

	if (!is_nil(p3)) {
		return throw_error(q, p3_orig, p3_orig_ctx, "type_error", "list");
	}

	// As in SWI, deadline(T) is on get_time/1's clock, and the earlier of it and a timeout wins.

	if (has_deadline) {
		double left = deadline - (wall_time_in_usec() / 1000000.0);

		if (left < 0.0)
			left = 0.0;

		if ((timeout < 0.0) || (left < timeout))
			timeout = left;
	}

	bool ok = do_match_message(q, n, false, timeout);
	THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
	return ok;
}

static bool bif_thread_peek_message_2(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,any);
	int n = get_thread(q, p1);

	if ((n < 0) || !is_queue(p1)) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	bool ok = do_match_message(q, n, true, 0.0);
	THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
	return ok;
}


// Release the mutexes me holds, each as often as taken and only once marked free, under the lock that guards the live list.

static void do_unlock_all(thread *me)
{
	prolog *pl = me->pl;
	prolog_lock(pl);

	for_each_thread(pl, t) {
		if (t->locked_by != me->chan)
			continue;

		unsigned n = t->num_locks;
		t->locked_by = -1;
		t->num_locks = 0;

		while (n--)
			release_lock(&t->guard);
	}

	prolog_unlock(pl);
}

// A goal ended by thread_exit/1 can leave choicepoints, such as a catch/3
// still open, pointing into the goal that is about to be freed. The at_exit
// goal runs on the same query and backtracked into them.

static void drop_leftover_choices(query *q)
{
	while (q->st.cp) {
		choice *ch = GET_CURR_CHOICE();

		if (ch->owns_pr)
			leave_predicate(q, ch->st.pr, ch->st.pr_dbgen, false);

		drop_choice(q);
	}
}

static void *start_routine_thread_create(thread *t)
{
	t->id = pthread_self();
	g_self_thread = t;
	g_max_depth = THREAD_STACK_SIZE / 1024;	// not the main thread's, which may be far bigger
	acquire_lock(&t->guard);
	t->alertable = true;					// see alert_thread()
	release_lock(&t->guard);
	execute(t->q, t->goal, t->num_vars);
	drop_leftover_choices(t->q);
	unshare_cells(t->goal, t->goal->num_cells);
	TPL_free(t->goal);
	t->goal = NULL;

	// The ball is already a detached image - find_exception_handler rebased it from 0 - so keep it flat, a plain copy that join can import like a thread message. dup_cells_by_ref reattached its variables to a frame the joiner never had.

	cell *ball = NULL;

	if (t->q->did_unhandled_exception && t->q->ball) {
		ball = TPL_calloc(t->q->ball->num_cells+1, sizeof(cell));
		if (ball) dup_cells(ball, t->q->ball, t->q->ball->num_cells);
	}

	// Under the guard, as thread_property/2 reads how it ended under it (#1178).
	acquire_lock(&t->guard);
	t->ball = ball;
	t->is_exception = t->q->did_unhandled_exception && t->ball;

	// A goal that simply failed is not an exception and leaves no exit
	// code, so without this join/2 could not tell it from success.
	//
	// It cannot be read from execute(), which returns true however the
	// query ended, nor from q->status, which the '$halt' appended to
	// every thread goal short-circuits. But that appended '$halt' is
	// itself the signal: it is only ever reached by a goal that
	// succeeded, so a query that stopped without halting either failed
	// or threw.

	t->is_failed = !t->q->halt && !t->is_exception;
	t->is_finished = true;				// last, so whoever sees it finished sees how it ended
	release_lock(&t->guard);

	if (t->at_exit_goal) {
		execute(t->q, t->at_exit_goal, t->at_exit_goal_num_vars);
		unshare_cells(t->at_exit_goal, t->at_exit_goal->num_cells);
		TPL_free(t->at_exit_goal);
		t->at_exit_goal = NULL;
	}

	do_unlock_all(t);

	// Tables are per-thread, so they die with the thread. Freed here
	// rather than only at pl_destroy() so a long-lived process that
	// spawns many tabling threads does not accumulate them. Safe for
	// both the detached and joinable paths: nothing outside this
	// thread can reach its tables.

	tabling_destroy_thread(t);
	acquire_lock(&t->guard);
	t->alertable = false;					// no signal reaches it from here
	release_lock(&t->guard);

	if (!t->is_detached)
		return 0;

	acquire_lock(&t->guard);
	sl_del(t->pl->alias, t->alias);
	TPL_free(t->alias);
	t->alias = NULL;
	query_destroy(t->q);
	gvars_destroy(t);
	thread_local_release(t);
	t->q = NULL;
	msg *m;

	while ((m = list_pop_front(&t->queue)) != NULL) {
		unshare_cells(m->c, m->c->num_cells);
		TPL_free(m);
	}

	while ((m = list_pop_front(&t->signals)) != NULL) {
		unshare_cells(m->c, m->c->num_cells);
		TPL_free(m);
	}

	if (t->ball) {
		unshare_cells(t->ball, t->ball->num_cells);
		TPL_free(t->ball);
		t->ball = NULL;
	}

	// No join will take it, and the next thread to get this struct must
	// not report it as its own.

	if (t->exit_code) {
		unshare_cells(t->exit_code, t->exit_code->num_cells);
		TPL_free(t->exit_code);
		t->exit_code = NULL;
	}

	// Unlock before retiring: retire_thread() puts t on the free list,
	// and t->guard is a mutex embedded in t. A concurrent new_thread()
	// reusing t writes t->guard.tid without reinitialising the mutex
	// (is_init stays true), so retiring while still holding this lock
	// let a second thread mutate a locked mutex, and let this release
	// unlock a mutex that by then belonged to whoever grabbed t next.
	// Traced from an intermittent SIGSEGV/SIGBUS under concurrent
	// thread churn - see samples/skynet.pl.
	release_lock(&t->guard);
	retire_thread(t->pl, t);
    return 0;
}

static bool bif_thread_create_3(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,callable);
	GET_NEXT_ARG(p2,var);
	GET_NEXT_ARG(p3,list_or_nil);

	// Options are validated BEFORE new_thread() hands out a slot, so
	// none of the exits below has anything to unwind. Previously the
	// slot (and any alias) was taken first and every error exit had to
	// release both by hand.
	//
	// One deliberate consequence: a bad option is now reported in
	// preference to resource_error(too_many_threads), because the
	// arguments are checked before any resource is consumed.

	cell *alias = NULL, *at_exit_goal = NULL;
	pl_ctx at_exit_goal_ctx = 0;
	bool is_detached = false;
	uint64_t stack_limit = 0;
	PROLOG_LIST_HANDLER(p3);

	while (is_list(p3)) {
		cell *h = PROLOG_LIST_HEAD(p3);
		cell *c = deref(q, h, p3_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (is_var(c))
			return throw_error(q, c, q->latest_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

		// Every option is name(Arg): anything else has no argument to read.

		if (!is_compound(c) || (get_arity(c) != 1))
			return throw_error(q, c, c_ctx, "domain_error", "thread_option");

		cell *name = c + 1;
		name = deref(q, name, c_ctx);
		pl_ctx name_ctx = q->latest_ctx;

		// SWI-Prolog's error terms, where these used to borrow the stream ones.

		if (!CMP_STRING_TO_CSTR(q, c, "alias")) {
			if (is_var(name))
				return throw_error(q, name, name_ctx, "instantiation_error", "thread_option");

			if (!is_atom(name))
				return throw_error(q, name, name_ctx, "type_error", "atom");

			if (get_named_thread(q->pl, C_STR(q, name), C_STRLEN(q, name)) >= 0)
				return throw_error(q, name, name_ctx, "permission_error", "create,thread");

			alias = name;
		} else if (!CMP_STRING_TO_CSTR(q, c, "at_exit")) {
			if (is_var(name))
				return throw_error(q, name, name_ctx, "instantiation_error", "thread_option");

			if (!is_callable(name))
				return throw_error(q, name, name_ctx, "type_error", "callable");

			at_exit_goal = name;
			at_exit_goal_ctx = name_ctx;
		} else if (!CMP_STRING_TO_CSTR(q, c, "detached")) {
			if (is_var(name))
				return throw_error(q, name, name_ctx, "instantiation_error", "thread_option");

			if (!is_boolean(name))
				return throw_error(q, name, name_ctx, "type_error", "bool");

			if (name->val_off == g_true_s)
				is_detached = true;
		} else if (!CMP_STRING_TO_CSTR(q, c, "stack_limit")) {
			if (is_var(name))
				return throw_error(q, name, name_ctx, "instantiation_error", "thread_option");

			if (!is_integer(name))
				return throw_error(q, name, name_ctx, "type_error", "integer");

			if (is_negative(name))
				return throw_error(q, name, name_ctx, "domain_error", "not_less_than_zero");

			stack_limit = is_smallint(name) ? (uint64_t)get_smallint(name) : 0;	// a bigint is no limit to speak of
		} else
			return throw_error(q, c, c_ctx, "domain_error", "thread_option");

		p3 = PROLOG_LIST_TAIL(p3);
		p3 = deref(q, p3, p3_ctx);
		p3_ctx = q->latest_ctx;

		if (is_var(p3))
			return throw_error(q, p3, p3_ctx, "instantiation_error", "args_not_sufficiently_instantiated");
	}

	// Commit.

	int n = new_thread(q->pl);

	if (n < 0)
		return throw_error(q, p2, p2_ctx, "resource_error", "too_many_threads");

	thread *t = find_thread_by_id(q->pl, n);

	if (alias) {
		t->alias = DUP_STRING(q, alias);
		sl_app(q->pl->alias, t->alias, t);
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, C_STR(q, alias)));
		unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else {
		cell tmp;
		make_int(&tmp, n);
		tmp.flags |= FLAG_INT_THREAD;
		unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
	CHECKED(init_tmp_heap(q));
	cell *tmp = clone_term_to_tmp(q, p1, p1_ctx);
	CHECKED(tmp);
	t->num_vars = rebase_term(q, tmp, 0, false);
	t->q = query_create_threaded(q->st.m);
	CHECKED(t->q);
	t->q->thread_ptr = t;
	t->q->my_chan = n;
	t->q->stack_limit = stack_limit;
	cell *tmp2 = TPL_calloc(1+tmp->num_cells+1+1, sizeof(cell));
	CHECKED(tmp2);
	pl_idx num_cells = 0;
	make_instr(tmp2+num_cells++, g_conjunction_s, bif_iso_conjunction_2, 2, tmp->num_cells+1);
	num_cells += dup_cells(tmp2+num_cells, tmp, tmp->num_cells);
	// '$halt' and not halt/0: halt/0 is ignore(atexit) then '$halt', so
	// appending it here ran the GLOBAL atexit/0 hook every time any
	// thread finished, not only at process exit. A library that
	// registers process-level cleanup that way - closing a connection,
	// finalizing an embedded interpreter - had it torn down under the
	// threads still using it, by whichever thread happened to end first.

	make_instr(tmp2+num_cells++, new_atom(q->pl, "$halt"), bif_iso_halt_0, 0, 0);
	t->goal = tmp2;

	if (at_exit_goal) {
		CHECKED(init_tmp_heap(q));
		cell *tmp = clone_term_to_tmp(q, at_exit_goal, at_exit_goal_ctx);
		CHECKED(tmp);
		t->at_exit_goal_num_vars = rebase_term(q, tmp, 0, false);
		t->at_exit_goal = TPL_calloc(tmp->num_cells+1, sizeof(cell));
		CHECKED(t->at_exit_goal);
		dup_cells(t->at_exit_goal, tmp, tmp->num_cells);
	}

	pthread_attr_t sa;
	pthread_attr_init(&sa);

	// macOS gives a thread 512K by default where the main thread gets 8M, so deep recursion
	// would hit a limit on a thread that it does not on the main one. Ask for the same 8M.

	pthread_attr_setstacksize(&sa, THREAD_STACK_SIZE);

	if (is_detached) {
		pthread_attr_setdetachstate(&sa, PTHREAD_CREATE_DETACHED);
		t->is_detached = true;
	}

	// Turn on the database locking in enter_predicate()/leave_predicate()
	// now, not earlier. It has to be set before the thread exists, since
	// the thread starts touching the database immediately - and
	// pthread_create() is the synchronisation point that publishes it to
	// the new thread. But setting it any sooner meant a thread_create/3
	// that failed while parsing options or allocating a slot left the
	// locking on for a process that never got a second thread, which is
	// the ~7% on dynamic calls the flag exists to avoid.
	//
	// Not cleared if pthread_create() itself fails: another thread may
	// already be running, and clearing it then would switch the locking
	// off underneath it.

	q->pl->is_multithreaded = true;
	t->q->inherited = inherit_capture(q);	// made inside with_output_to/2, it writes into that as in SWI

	if (pthread_create((pthread_t*)&t->id, &sa, (void*)start_routine_thread_create, (void*)t) != 0) {
		// retire_thread() puts t on the free list, where a *different*
		// thread's new_thread() can pop and start reusing it the moment
		// the lock inside is released. Everything that still touches t
		// - freeing goal/at_exit_goal, destroying t->q, dropping the
		// alias - has to happen BEFORE that, not after: this used to
		// retire first, then kept writing into fields a concurrent
		// thread_create/3 could already own. Only bit under load
		// (pthread_create has to actually fail) and only with a second
		// thread racing to grab the slot, which is why it looked like a
		// skynet-scale crash rather than a use-after-free.
		if (t->goal) { unshare_cells(t->goal, t->goal->num_cells); TPL_free(t->goal); t->goal = NULL; }
		if (t->at_exit_goal) { unshare_cells(t->at_exit_goal, t->at_exit_goal->num_cells); TPL_free(t->at_exit_goal); t->at_exit_goal = NULL; }
		query_destroy(t->q);
		gvars_destroy(t);
		thread_local_release(t);
		t->q = NULL;
		retire_thread(q->pl, t);
		return throw_error(q, p1, p1_ctx, "system_error", "pthread_create");
	}

	return true;
}

static bool bif_thread_join_2(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,nonvar);
	int n = get_thread(q, p1);

	if (n < 0) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	thread *t = find_thread_by_id(q->pl, n);

	// It can retire between get_thread()'s lookup and this one.
	if (!t)
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");

	if (!is_threaded(t))
		return throw_error(q, p1, p1_ctx, "permission_error", "join,not_thread");

	// A detached thread retires itself when it ends (see
	// start_routine_thread_create), so there is nothing left to join and
	// pthread_join() on one is undefined. It happened to fail into the
	// same error below, which is why this read as working - but ASan
	// rightly aborts on it instead. Refuse up front, and do not unwind:
	// a detached thread owns its own cleanup.
	if (t->is_detached)
		return throw_error(q, p1, p1_ctx, "domain_error", "not_joinable");

	// A blocking pthread_join() here would stall the whole scheduler
	// thread, not just this task - poll t->is_finished instead (already
	// used non-blockingly by thread_property/2's status(running) check)
	// and only join once it is set. That still waits out an at_exit goal,
	// which runs after the flag is set, but not the thread's whole goal.
	if (q->is_task && !t->is_finished)
		return do_yield(q, MSG_TASK_POLL_MS);

	// Claim the join under the guard, so the thread can't detach itself
	// (say from its at_exit goal) while this waits: pthread_join() on a
	// detached thread is undefined, and here it never returned.

	acquire_lock(&t->guard);

	if (t->is_detached) {
		release_lock(&t->guard);
		return throw_error(q, p1, p1_ctx, "domain_error", "not_joinable");
	}

	t->is_joining = true;
	release_lock(&t->guard);
	void *retval;

	if (pthread_join((pthread_t)t->id, &retval)) {
		unwind_thread(q->pl, t);
		return throw_error(q, p1, p1_ctx, "domain_error", "not_joinable");
	}

	// Status, in the same vocabulary thread_property(_, status(S)) uses:
	// exception/1 for an uncaught ball, exited/1 for thread_exit/1,
	// otherwise the plain true or false the goal ended with. Both
	// allocators below can move the heap, so the arguments are always
	// re-fetched after allocating and never before.

	if (t->is_exception && t->ball) {
		// Import the detached ball into this query first: a plain copy keeps its variables numbered against the dead thread's frames.
		cell *ball = import_term(q, t->ball, q->st.cur_ctx);
		CHECKED(ball);
		cell *tmp = alloc_heap(q, 1+ball->num_cells);
		CHECKED(tmp);
		make_instr(tmp, new_atom(q->pl, "exception"), NULL, 1, ball->num_cells);
		dup_cells(tmp+1, ball, ball->num_cells);
		GET_FIRST_ARG(p1,nonvar);
		GET_NEXT_ARG(p2,any);
		unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
	} else if (t->exit_code) {
		const frame *f = GET_CURR_FRAME();
		cell *tmp = import_term(q, t->exit_code, q->st.cur_ctx);
		CHECKED(tmp);
		GET_FIRST_ARG(p1,nonvar);
		GET_NEXT_ARG(p2,any);
		unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
	} else {
		GET_FIRST_ARG(p1,nonvar);
		GET_NEXT_ARG(p2,any);
		cell tmp;
		make_atom(&tmp, t->is_failed ? g_false_s : g_true_s);
		unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	acquire_lock(&t->guard);
	sl_del(q->pl->alias, t->alias);
	TPL_free(t->alias);
	t->alias = NULL;
	query_destroy(t->q);
	gvars_destroy(t);
	thread_local_release(t);
	t->q = NULL;
	msg *m;

	while ((m = list_pop_front(&t->queue)) != NULL) {
		unshare_cells(m->c, m->c->num_cells);
		TPL_free(m);
	}

	while ((m = list_pop_front(&t->signals)) != NULL) {
		unshare_cells(m->c, m->c->num_cells);
		TPL_free(m);
	}

	if (t->ball) {
		unshare_cells(t->ball, t->ball->num_cells);
		TPL_free(t->ball);
		t->ball = NULL;
	}

	if (t->exit_code) {
		unshare_cells(t->exit_code, t->exit_code->num_cells);
		TPL_free(t->exit_code);
		t->exit_code = NULL;
	}

	if (t->at_exit_goal) {
		unshare_cells(t->at_exit_goal, t->at_exit_goal->num_cells);
		TPL_free(t->at_exit_goal);
		t->at_exit_goal = NULL;
	}

	// Unlock before retiring - see the note in start_routine_thread_create().
	release_lock(&t->guard);
	retire_thread(q->pl, t);
	THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
	return true;
}

bool do_signal(query *q, void *thread_ptr)
{
	thread *t = (thread*)thread_ptr;
	acquire_lock(&t->guard);

	if (!list_count(&t->signals)) {
		release_lock(&t->guard);
		return false;
	}

	msg *m = list_pop_front(&t->signals);

	if (!list_count(&t->signals))
		t->alerted = false;					// every signal sent has run, or is running

	release_lock(&t->guard);
	THREAD_DEBUG DUMP_TERM("do_signal", m->c, q->st.cur_ctx, 0);
	cell *c = import_term(q, m->c, q->st.cur_ctx);
	CHECKED(c);
	unshare_cells(m->c, m->c->num_cells);	// FIX: release cell refs (was leaked)
	TPL_free(m);
	// The engine delivers this before running q->st.instr, so the handler
	// returns to that goal, not past it: make_call() skipped it, and any
	// signal, even true, could wreck the thread it interrupted.

	cell *tmp = prepare_call(q, CALL_NOSKIP, c, q->st.cur_ctx, 1);
	ENSURE(tmp);
	make_call_redo(q, tmp+c->num_cells);
	q->st.instr = tmp;
	return true;
}

static bool bif_thread_signal_2(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,nonvar);
	GET_NEXT_ARG(p2,callable);
	int n = get_thread(q, p1);

	if (n < 0) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	thread *t = find_thread_by_id(q->pl, n);

	if (!is_threaded(t))
		return throw_error(q, p1, p1_ctx, "permission_error", "signal,not_thread");

	if (!do_send_message(q, n, p2, p2_ctx, true)) {
		THREAD_DEBUG DUMP_TERM(" -  ", q->st.instr, q->st.cur_ctx, 1);
		return false;
	}

	alert_thread(t);
	return true;
}

// Drops everything t owns and retires the struct. Only safe once t's OS
// thread is confirmed to have stopped touching it.

static void retire_cancelled_thread(thread *t)
{
	acquire_lock(&t->guard);
	sl_del(t->pl->alias, t->alias);
	TPL_free(t->alias);
	t->alias = NULL;
	t->is_finished = false;
	msg *m;

	while ((m = list_pop_front(&t->queue)) != NULL) {
		unshare_cells(m->c, m->c->num_cells);
		TPL_free(m);
	}

	while ((m = list_pop_front(&t->signals)) != NULL) {
		unshare_cells(m->c, m->c->num_cells);
		TPL_free(m);
	}

	if (t->ball) {
		unshare_cells(t->ball, t->ball->num_cells);
		TPL_free(t->ball);
		t->ball = NULL;
	}

	if (t->exit_code) {
		unshare_cells(t->exit_code, t->exit_code->num_cells);
		TPL_free(t->exit_code);
		t->exit_code = NULL;
	}

	query_destroy(t->q);

	gvars_destroy(t);

	thread_local_release(t);
	t->q = NULL;
	//t->id = 0;

	// Unlock before retiring - see the note in start_routine_thread_create().
	release_lock(&t->guard);
	retire_thread(t->pl, t);
}

// pthread_join() has no timed variant, so it runs on a throwaway helper
// thread and we poll a flag instead, bounded by deadline.

struct join_ctx_ { pthread_t id; pl_atomic bool done; };

static void *do_join(void *arg)
{
	struct join_ctx_ *ctx = arg;
	pthread_join(ctx->id, NULL);
	ctx->done = true;
	return NULL;
}

// true once id has actually exited; false on timeout (ctx is then
// deliberately leaked - the caller exits the process next anyway).

static bool cancel_and_join(pthread_t id, uint64_t deadline)
{
#if defined(__ANDROID__)
	pthread_kill(id, 0);
	return true;
#else
	pthread_cancel(id);

	struct join_ctx_ *ctx = TPL_malloc(sizeof(*ctx));
	*ctx = (struct join_ctx_){ .id = id, .done = false };
	pthread_t joiner;

	if (pthread_create(&joiner, NULL, do_join, ctx) != 0) {
		pthread_join(id, NULL);
		TPL_free(ctx);
		return true;
	}

	while (!ctx->done) {
		if (monotonic_time_in_usec() >= deadline)
			return false;

		msleep(5);
	}

	pthread_join(joiner, NULL);
	TPL_free(ctx);
	return true;
#endif
}

static bool bif_thread_exit_1(query *q);

static bool bif_thread_cancel_1(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,nonvar);
	int n = get_thread(q, p1);

	if (n == 0)
		return throw_error(q, p1, p1_ctx, "permission_error", "detach,thread,main");

	if (n < 0) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	thread *t = find_thread_by_id(q->pl, n);

	if (!is_threaded(t))
		return throw_error(q, p1, p1_ctx, "permission_error", "cancel,not_thread");

	// Cancel by asking, not by force. Freeing the thread's query and then
	// calling pthread_cancel() crashed: a thread computing reaches no
	// cancellation point, so it ran on over the freed query. A signal
	// reaches it busy or blocked, thread_exit/1 can't be caught, and the
	// thread unwinds itself, running its at_exit goal. Detached, unless a
	// join is already waiting, so it still goes away without one.

	acquire_lock(&t->guard);

	if (!t->is_joining && !t->is_detached && t->is_active) {
		t->is_detached = true;
		pthread_detach(t->id);
	}

	release_lock(&t->guard);
	t->cancel_requested = true;			// for a thread blocked in a read or accept, see tpl_wait_fd()
	cell goal[2];
	make_instr(goal, new_atom(q->pl, "thread_exit"), bif_thread_exit_1, 1, 1);
	make_atom(goal+1, new_atom(q->pl, "cancelled"));
	CHECKED(queue_to_chan(t, goal, q->my_chan, true));
	alert_thread(t);
	THREAD_DEBUG DUMP_TERM(" -  ", q->st.instr, q->st.cur_ctx, 1);
	return true;
}

static bool bif_thread_detach_1(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,nonvar);
	int n = get_thread(q, p1);

	if (n == 0)
		return throw_error(q, p1, p1_ctx, "permission_error", "detach,thread,main");

	if (n < 0) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	thread *t = find_thread_by_id(q->pl, n);

	if (!is_threaded(t))
		return throw_error(q, p1, p1_ctx, "permission_error", "detach,not_thread");

	// As in SWI, a thread another is already joining can't be detached.

	acquire_lock(&t->guard);

	if (t->is_joining) {
		release_lock(&t->guard);
		return throw_error(q, p1, p1_ctx, "permission_error", "detach,thread");
	}

	if (t->is_active) {
		t->is_detached = true;
		pthread_detach(t->id);
	}

	release_lock(&t->guard);

	THREAD_DEBUG DUMP_TERM(" -  ", q->st.instr, q->st.cur_ctx, 1);
	return true;
}

static bool bif_thread_self_1(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,var);
	thread *t = get_self_query(q);

	if (!t) {
		THREAD_DEBUG DUMP_TERM(" -  ", q->st.instr, q->st.cur_ctx, 1);
		return false;
	}

	if (t->chan == 0) {
		t->q = q;
		q->thread_ptr = t;
	}

	// A thread with an alias is known by it, as thread_create/3 answers and SWI-Prolog does: main for the main thread.

	cell tmp;

	if (t->alias)
		make_atom(&tmp, new_atom(q->pl, t->alias));
	else {
		make_int(&tmp, (int)t->chan);
		tmp.flags |= FLAG_INT_THREAD;
	}

	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	THREAD_DEBUG DUMP_TERM(" -  ", q->st.instr, q->st.cur_ctx, 1);
	return ok;
}

bool bif_sleep_1(query *q);

// sleep/1, except that a negative time doesn't sleep rather than raising an error.

static bool bif_thread_sleep_1(query *q)
{
	GET_FIRST_ARG(p1,number);

	if (is_negative(p1) && !is_bigint(p1))
		return true;

	return bif_sleep_1(q);
}

static bool bif_thread_yield_0(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	sched_yield();
	THREAD_DEBUG DUMP_TERM(" -  ", q->st.instr, q->st.cur_ctx, 1);
	return true;
}

static bool bif_thread_exit_1(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,nonvar);
	thread *t = get_self_query(q);

	if (!t)	// FIX: guard NULL self (cf. thread_self/1)
		return false;

	//if (t->is_finished)
	//	return throw_error(q, p1, p1_ctx, "permission_error", "fished,thread");

	CHECKED(init_tmp_heap(q));
	cell *tmp = clone_term_to_tmp(q, p1, p1_ctx);
	CHECKED(tmp);
	rebase_term(q, tmp, 0, false);
	cell *tmp2 = TPL_calloc(1+tmp->num_cells+1, sizeof(cell));
	CHECKED(tmp2);
	make_instr(tmp2, new_atom(q->pl, "exited"), NULL, 1, tmp->num_cells);
	dup_cells(tmp2+1, tmp, tmp->num_cells);
	acquire_lock(&t->guard);			// an at_exit goal sets it while thread_property/2 may read it

	if (t->exit_code) {
		unshare_cells(t->exit_code, t->exit_code->num_cells);
		TPL_free(t->exit_code);
	}

	t->exit_code = tmp2;
	release_lock(&t->guard);
	q->halt_code = 0;
	q->halt = t->q->error = true;
	THREAD_DEBUG DUMP_TERM(" -  ", q->st.instr, q->st.cur_ctx, 1);
	return true;
}

// Detached cleanup and thread_join/2 free t's alias, ball and exit_code
// under t->guard, so thread_property/2 reads them under it (#1178). False,
// with the guard released, once t has retired or is retiring.

static bool lock_live_thread(thread *t, int chan)
{
	acquire_lock(&t->guard);

	if (t->is_active && (t->chan == chan) && (!t->is_finished || t->q))
		return true;

	release_lock(&t->guard);
	return false;
}

// status(S) for t, on q's heap. Call with t->guard held.

static cell *make_thread_status(query *q, thread *t)
{
	cell *tmp;

	if (!t->is_finished) {
		if (!(tmp = alloc_heap(q, 2))) return NULL;
		make_instr(tmp, new_atom(q->pl, "status"), NULL, 1, 1);
		make_atom(tmp+1, new_atom(q->pl, "running"));
	} else if (t->is_exception && t->ball) {
		if (!(tmp = alloc_heap(q, 2+t->ball->num_cells))) return NULL;
		make_instr(tmp, new_atom(q->pl, "status"), NULL, 1, 1+t->ball->num_cells);
		make_instr(tmp+1, new_atom(q->pl, "exception"), NULL, 1, t->ball->num_cells);
		dup_cells(tmp+2, t->ball, t->ball->num_cells);
	} else if (t->exit_code) {
		// thread_exit/1 stores its argument already wrapped as
		// exited/1, so hand that back whole - this reported a bare
		// `false` for it. And a goal that merely failed is is_failed,
		// not exit_code, which made it report `true`; thread_join/2 has
		// always read the right flag, so the two disagreed.
		if (!(tmp = alloc_heap(q, 1+t->exit_code->num_cells))) return NULL;
		make_instr(tmp, new_atom(q->pl, "status"), NULL, 1, t->exit_code->num_cells);
		dup_cells(tmp+1, t->exit_code, t->exit_code->num_cells);
	} else {
		if (!(tmp = alloc_heap(q, 2))) return NULL;
		make_instr(tmp, new_atom(q->pl, "status"), NULL, 1, 1);
		make_atom(tmp+1, t->is_failed?g_false_s:g_true_s);
	}

	return tmp;
}

// alias(A) for t, on q's heap, or NULL if it has none. Call with t->guard held.

static cell *make_thread_alias(query *q, thread *t)
{
	if (!t->alias)		// created without one: the property does not hold
		return NULL;

	cell *tmp = alloc_heap(q, 2);
	if (!tmp) return NULL;
	make_instr(tmp, new_atom(q->pl, "alias"), NULL, 1, 1);
	make_cstring(tmp+1, t->alias);
	return tmp;
}

// A named thread that has gone is an error; one met while enumerating is skipped.

static bool thread_gone(query *q, cell *p1, pl_ctx p1_ctx, bool enumerating)
{
	if (enumerating)
		return false;

	THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
	return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
}

static bool do_thread_property_pin_both(query *q, bool enumerating)
{
	GET_FIRST_ARG(p1,nonvar);
	GET_NEXT_ARG(p2,nonvar);
	int n = get_thread(q, p1);
	thread *t = n < 0 ? NULL : find_thread_by_id(q->pl, n);

	if (!t)
		return thread_gone(q, p1, p1_ctx, enumerating);

	if (get_arity(p2) != 1)
		return throw_error(q, p2, p2_ctx, "domain_error", "thread_property");

	cell *c = deref(q, p2, p2_ctx);
	pl_ctx c_ctx = q->latest_ctx;
	cell *tmp;

	if (!CMP_STRING_TO_CSTR(q, p2, "alias")) {
		if (!lock_live_thread(t, n))
			return thread_gone(q, p1, p1_ctx, enumerating);

		bool has_alias = t->alias;
		tmp = make_thread_alias(q, t);
		release_lock(&t->guard);

		if (!has_alias)
			return false;

		CHECKED(tmp);

		if (!unify(q, c, c_ctx, tmp, q->st.cur_ctx)) {
			unshare_cell(tmp+1);
			return false;
		}

		return true;
	} else if (!CMP_STRING_TO_CSTR(q, p2, "detached")) {
		tmp = alloc_heap(q, 2);
		CHECKED(tmp);
		make_instr(tmp, new_atom(q->pl, "detached"), NULL, 1, 1);
		make_atom(tmp+1, t->is_detached?g_true_s:g_false_s);
		return unify(q, c, c_ctx, tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p2, "status")) {
		if (!lock_live_thread(t, n))
			return thread_gone(q, p1, p1_ctx, enumerating);

		tmp = make_thread_status(q, t);
		release_lock(&t->guard);
		CHECKED(tmp);
		return unify(q, c, c_ctx, tmp, q->st.cur_ctx);
	} else
		return throw_error(q, p2, p2_ctx, "domain_error", "thread_property");

	return false;
}

static bool do_thread_property_pin_property(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,nonvar);
	int i = q->retry ? (int)q->st.v1 : -1;

	thread *t = next_of_kind(q->pl, i, TK_THREAD);

	if (!t)
		return false;

	q->st.v1 = t->chan;

	if (next_of_kind(q->pl, t->chan, TK_THREAD))
		CHECKED(push_choice(q));

	cell tmp;

	if (t->alias)
		make_atom(&tmp, new_atom(q->pl, t->alias));
	else {
		make_int(&tmp, q->st.v1);
		tmp.flags |= FLAG_INT_THREAD;
	}

	unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	return do_thread_property_pin_both(q, true);
}

static bool do_thread_property_pin_id(query *q, bool enumerating)
{
	GET_FIRST_ARG(p1,nonvar);
	GET_NEXT_ARG(p2,any);
	int n = get_thread(q, p1);
	thread *t = n < 0 ? NULL : find_thread_by_id(q->pl, n);

	if (!t)
		return thread_gone(q, p1, p1_ctx, enumerating);

	unsigned i = 0;

	if (q->retry)
		i = ++q->st.v2;
	else
		q->st.v2 = 0;

	cell *tmp = NULL;

	if (i == 0) {
		if (!lock_live_thread(t, n))
			return thread_gone(q, p1, p1_ctx, enumerating);

		bool has_alias = t->alias;
		tmp = make_thread_alias(q, t);
		release_lock(&t->guard);

		// An object created without an alias has no alias property at all,
		// rather than one carrying a null string - which is what
		// make_cstring() crashed on. Skip that slot, keeping v2 in step so
		// the next retry moves on instead of repeating this property.

		if (!has_alias)
			q->st.v2 = i = 1;
		else
			CHECKED(tmp);
	}

	if (i <= 1)
		CHECKED(push_choice(q));

	if (i == 0) {
		if (!unify(q, p2, p2_ctx, tmp, q->st.cur_ctx)) {
			unshare_cell(tmp+1);
			return false;
		}

		return true;
	}

	if (i == 1) {
		tmp = alloc_heap(q, 2);
		CHECKED(tmp);
		make_instr(tmp, new_atom(q->pl, "detached"), NULL, 1, 1);
		make_atom(tmp+1, t->is_detached?g_true_s:g_false_s);
		return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
	}

	if (!lock_live_thread(t, n))
		return thread_gone(q, p1, p1_ctx, enumerating);

	tmp = make_thread_status(q, t);
	release_lock(&t->guard);
	CHECKED(tmp);
	return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
}

static bool do_thread_property_wild(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,var);
	int i = q->retry ? (int)q->st.v1 : -1;

	if (!q->retry)
		q->st.v2 = -1;

	thread *t = next_of_kind(q->pl, i, TK_THREAD);

	if (!t)
		return false;

	q->st.v1 = t->chan;

	if (next_of_kind(q->pl, t->chan, TK_THREAD))
		CHECKED(push_choice(q));

	cell tmp;

	if (t->alias)
		make_atom(&tmp, new_atom(q->pl, t->alias));
	else {
		make_int(&tmp, q->st.v1);
		tmp.flags |= FLAG_INT_THREAD;
	}

	unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	return do_thread_property_pin_id(q, true);
}

static bool bif_thread_property_2(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);

	// Asked once: a thread retiring between two asks sent a handle to the enumerating cases.
	const bool is_thread = is_nonvar(p1) && check_thread(p1);

	if (is_nonvar(p1) && !is_thread)
		return false;

	bool ok = false;

	if (is_thread && !is_var(p2))
		ok = do_thread_property_pin_both(q, false);
	else if (is_thread)
		ok = do_thread_property_pin_id(q, false);
	else if (!is_var(p2))
		ok = do_thread_property_pin_property(q);
	else
		ok = do_thread_property_wild(q);

	THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
	return ok;
}

static bool bif_is_thread_1(query *q)
{
	GET_FIRST_ARG(p1,nonvar);
	return check_thread(p1);
}

// Validate a mutex/message-queue option list WITHOUT taking a slot.
//
// The whole bug class in this file came from allocating first and
// parsing options second, so every error exit had to unwind a slot and
// a registered alias - and none of them did. Validate first and there
// is nothing to unwind: the commit below cannot fail.
//
// Returns 1 if the options are good, 0 if an error has ALREADY been
// thrown. It cannot return throw_error()'s value directly, because
// throw_error() returns TRUE (it signals via q->did_throw), so a
// caller testing it as a success flag would read backwards.
//
// *alias_out is the alias(...) name cell, borrowed from the option
// list; it is only duplicated once a slot has been committed.
//
// domain and create name the caller's kind in its errors, as SWI-Prolog's
// do: queue_option and create,message_queue, say.

static int parse_thread_opts(query *q, cell *p2, pl_ctx p2_ctx, cell **alias_out, const char *domain, const char *create)
{
	*alias_out = NULL;
	PROLOG_LIST_HANDLER(p2);

	while (is_list(p2)) {
		cell *h = PROLOG_LIST_HEAD(p2);
		cell *c = deref(q, h, p2_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (is_var(c)) {
			throw_error(q, c, q->latest_ctx, "instantiation_error", "args_not_sufficiently_instantiated");
			return 0;
		}

		// Every option is name(Arg): anything else has no argument to read.

		if (!is_compound(c) || (get_arity(c) != 1)) {
			throw_error(q, c, c_ctx, "domain_error", domain);
			return 0;
		}

		cell *name = c + 1;
		name = deref(q, name, c_ctx);
		pl_ctx name_ctx = q->latest_ctx;

		if (!CMP_STRING_TO_CSTR(q, c, "alias")) {
			if (is_var(name)) {
				throw_error(q, name, name_ctx, "instantiation_error", domain);
				return 0;
			}

			if (!is_atom(name)) {
				throw_error(q, name, name_ctx, "type_error", "atom");
				return 0;
			}

			if (get_named_thread(q->pl, C_STR(q, name), C_STRLEN(q, name)) >= 0) {
				throw_error(q, name, name_ctx, "permission_error", create);
				return 0;
			}

			*alias_out = name;
		} else {
			throw_error(q, c, c_ctx, "domain_error", domain);
			return 0;
		}

		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;

		if (is_var(p2)) {
			throw_error(q, p2, p2_ctx, "instantiation_error", "args_not_sufficiently_instantiated");
			return 0;
		}
	}

	return 1;
}

static bool bif_message_queue_create_2(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,list_or_nil);

	// Options first - see parse_thread_opts().
	cell *alias = NULL;

	if (!parse_thread_opts(q, p2, p2_ctx, &alias, "queue_option", "create,message_queue"))
		return true;			// already thrown

	int n = new_thread(q->pl);

	if (n < 0)
		return throw_error(q, p1, p1_ctx, "resource_error", "too_many_threads");

	thread *t = find_thread_by_id(q->pl, n);
	t->is_queue_only = true;

	// Commit. Nothing below can fail in a way that needs unwinding.

	if (alias) {
		t->alias = DUP_STRING(q, alias);
		sl_app(q->pl->alias, t->alias, t);
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, C_STR(q, alias)));
		unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	} else {
		cell tmp;
		make_int(&tmp, n);
		tmp.flags |= FLAG_INT_THREAD;
		unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
	return true;
}

static bool bif_message_queue_destroy_1(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,any);
	int n = get_thread(q, p1);

	if (n < 0) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	thread *t = find_thread_by_id(q->pl, n);

	if (!t->is_queue_only)
		return throw_error(q, p1, p1_ctx, "permission_error", "destroy,not_queue");

	acquire_lock(&t->guard);
	msg *m;

	while ((m = list_pop_front(&t->queue)) != NULL) {
		unshare_cells(m->c, m->c->num_cells);
		TPL_free(m);
	}

	// Unlock before retiring - see the note in start_routine_thread_create().
	release_lock(&t->guard);
	retire_thread(q->pl, t);
	THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
	return true;
}

static bool do_message_queue_property_pin_both(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,nonvar);
	int n = get_thread(q, p1);

	if ((n < 0) || !is_queue(p1)) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	thread *t = find_thread_by_id(q->pl, n);

	if (get_arity(p2) != 1)
		return throw_error(q, p2, p2_ctx, "domain_error", "queue_property");

	cell *c = deref(q, p2, p2_ctx);
	pl_ctx c_ctx = q->latest_ctx;

	if (!CMP_STRING_TO_CSTR(q, p2, "alias")) {
		if (!t->alias)		// created without one: the property does not hold
			return false;

		cell *tmp = alloc_heap(q, 2);
		make_instr(tmp, new_atom(q->pl, "alias"), NULL, 1, 1);
		make_cstring(tmp+1, t->alias);

		if (!unify(q, c, c_ctx, tmp, q->st.cur_ctx)) {
			unshare_cell(tmp+1);
			return false;
		}

		return true;
	} else if (!CMP_STRING_TO_CSTR(q, p2, "size")) {
		cell *tmp = alloc_heap(q, 2);
		make_instr(tmp, new_atom(q->pl, "size"), NULL, 1, 1);
		make_int(tmp+1, queue_size(q->pl, n));

		if (!unify(q, c, c_ctx, tmp, q->st.cur_ctx))
			return false;

		unshare_cell(tmp+1);
		return true;
	} else
		return throw_error(q, p2, p2_ctx, "domain_error", "queue_property");

	return false;
}

static bool do_message_queue_property_pin_property(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,nonvar);
	int i = q->retry ? (int)q->st.v1 : -1;

	thread *t = next_of_kind(q->pl, i, TK_QUEUE);

	if (!t)
		return false;

	q->st.v1 = t->chan;

	if (next_of_kind(q->pl, t->chan, TK_QUEUE))
		CHECKED(push_choice(q));

	cell tmp;

	if (t->alias)
		make_atom(&tmp, new_atom(q->pl, t->alias));
	else {
		make_int(&tmp, q->st.v1);
		tmp.flags |= FLAG_INT_THREAD;
	}

	unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	return do_message_queue_property_pin_both(q);
}

static bool do_message_queue_property_pin_id(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	int n = get_thread(q, p1);

	if ((n < 0) || !is_queue(p1)) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	thread *t = find_thread_by_id(q->pl, n);
	unsigned i = 0;

	if (q->retry)
		i = ++q->st.v2;
	else
		q->st.v2 = 0;

	// An object created without an alias has no alias property at all,
	// rather than one carrying a null string - which is what
	// make_cstring() crashed on. Skip that slot, keeping v2 in step so
	// the next retry moves on instead of repeating this property.

	if ((i == 0) && !t->alias)
		q->st.v2 = i = 1;

	if (i == 0) {
		CHECKED(push_choice(q));
		cell *tmp = alloc_heap(q, 2);
		make_instr(tmp, new_atom(q->pl, "alias"), NULL, 1, 1);
		make_cstring(tmp+1, t->alias);

		if (!unify(q, p2, p2_ctx, tmp, q->st.cur_ctx)) {
			unshare_cell(tmp+1);
			return false;
		}

		return true;
	}

	cell *tmp = alloc_heap(q, 2);
	make_instr(tmp, new_atom(q->pl, "size"), NULL, 1, 1);
	make_int(tmp+1, queue_size(q->pl, n));
	return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
}

static bool do_message_queue_property_wild(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,var);
	int i = q->retry ? (int)q->st.v1 : -1;

	if (!q->retry)
		q->st.v2 = -1;

	thread *t = next_of_kind(q->pl, i, TK_QUEUE);

	if (!t)
		return false;

	q->st.v1 = t->chan;

	if (next_of_kind(q->pl, t->chan, TK_QUEUE))
		CHECKED(push_choice(q));

	cell tmp;

	if (t->alias)
		make_atom(&tmp, new_atom(q->pl, t->alias));
	else {
		make_int(&tmp, q->st.v1);
		tmp.flags |= FLAG_INT_THREAD;
	}

	unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	return do_message_queue_property_pin_id(q);
}

static bool bif_message_queue_property_2(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);

	if (is_nonvar(p1) && !check_queue(p1))
		return false;

	if (check_queue(p1) && !is_var(p2))
		return do_message_queue_property_pin_both(q);

	if (check_queue(p1))
		return do_message_queue_property_pin_id(q);

	if (!is_var(p2))
		return do_message_queue_property_pin_property(q);

	return do_message_queue_property_wild(q);
}


static bool bif_mutex_create_2(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,list_or_nil);

	// Options first - see parse_thread_opts().
	cell *alias = NULL;

	if (!parse_thread_opts(q, p2, p2_ctx, &alias, "mutex_option", "create,mutex"))
		return true;			// already thrown

	int n = new_thread(q->pl);

	if (n < 0)
		return throw_error(q, p1, p1_ctx, "resource_error", "too_many_threads");

	thread *t = find_thread_by_id(q->pl, n);
	t->is_mutex_only = true;

	// Commit. Nothing below can fail in a way that needs unwinding.

	if (alias) {
		t->alias = DUP_STRING(q, alias);
		sl_app(q->pl->alias, t->alias, t);
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, C_STR(q, alias)));
		unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	} else {
		cell tmp;
		make_int(&tmp, n);
		tmp.flags |= FLAG_INT_THREAD;
		unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
	return true;
}

static bool bif_mutex_destroy_1(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,any);
	int n = get_thread(q, p1);

	if (n < 0) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	thread *t = find_thread_by_id(q->pl, n);

	if (!t->is_mutex_only)
		return throw_error(q, p1, p1_ctx, "permission_error", "destroy,not_mutex");

	retire_thread(q->pl, t);
	THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
	return true;
}

static bool bif_mutex_trylock_1(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,any);
	int n = get_thread(q, p1);

	if ((n < 0) || !is_mutex(p1)) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	thread *t = find_thread_by_id(q->pl, n);

	if (!try_lock(&t->guard))
		return false;

	thread *me = get_self_query(q);

	if (!me) {	// FIX: guard NULL self
		release_lock(&t->guard);
		return false;
	}

	t->locked_by = me->chan;
	t->num_locks++;
	return true;
}

static bool bif_mutex_lock_1(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,any);
	int n = get_thread(q, p1);

	if ((n < 0) || !is_mutex(p1)) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	thread *t = find_thread_by_id(q->pl, n);
	thread *me = get_self_query(q);

	if (!me)	// FIX: guard NULL self
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");

	// A blocking acquire_lock() here would stall the whole scheduler
	// thread, not just this task - try_lock() (mutex_trylock/1's own,
	// non-blocking) and a poll retry instead.
	if (q->is_task) {
		if (!try_lock(&t->guard))
			return do_yield(q, MSG_TASK_POLL_MS);
	} else
		acquire_lock(&t->guard);

	t->locked_by = me->chan;
	t->num_locks++;
	THREAD_DEBUG DUMP_TERM(" -  ", q->st.instr, q->st.cur_ctx, 1);
	return true;
}

static bool bif_mutex_unlock_1(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,any);
	int n = get_thread(q, p1);

	if ((n < 0) || !is_mutex(p1)) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	thread *t = find_thread_by_id(q->pl, n);
	thread *me = get_self_query(q);

	if (!me)	// FIX: guard NULL self
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");

	if (t->locked_by != me->chan)
		return throw_error(q, p1, p1_ctx, "permission_error", "mutex_unlock,not_locked_by_me");

	if (--t->num_locks == 0)
		t->locked_by = -1;

	release_lock(&t->guard);
	THREAD_DEBUG DUMP_TERM(" -  ", q->st.instr, q->st.cur_ctx, 1);
	return true;
}

static bool bif_mutex_unlock_all_0(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	do_unlock_all(get_self_query(q));
	THREAD_DEBUG DUMP_TERM(" -  ", q->st.instr, q->st.cur_ctx, 1);
	return true;
}

static bool do_mutex_property_pin_both(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,nonvar);
	int n = get_thread(q, p1);

	if ((n < 0) || !is_mutex(p1)) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	thread *t = find_thread_by_id(q->pl, n);

	if (get_arity(p2) != 1)
		return throw_error(q, p2, p2_ctx, "domain_error", "mutex_property");

	cell *c = deref(q, p2, p2_ctx);
	pl_ctx c_ctx = q->latest_ctx;

	if (!CMP_STRING_TO_CSTR(q, p2, "alias")) {
		if (!t->alias)		// created without one: the property does not hold
			return false;

		cell *tmp = alloc_heap(q, 2);
		make_instr(tmp, new_atom(q->pl, "alias"), NULL, 1, 1);
		make_cstring(tmp+1, t->alias);

		if (!unify(q, c, c_ctx, tmp, q->st.cur_ctx)) {
			unshare_cell(tmp+1);
			return false;
		}

		return true;
	} else if (!CMP_STRING_TO_CSTR(q, p2, "status")) {
		if (t->num_locks == 0) {
			cell *tmp = alloc_heap(q, 2);
			make_instr(tmp, new_atom(q->pl, "status"), NULL, 1, 1);
			make_atom(tmp+1, new_atom(q->pl, "unlocked"));
			return unify(q, c, c_ctx, tmp, q->st.cur_ctx);
		}

		cell *tmp = alloc_heap(q, 4);
		make_instr(tmp, new_atom(q->pl, "status"), NULL, 1, 3);
		make_instr(tmp+1, new_atom(q->pl, "locked"), NULL, 2, 2);
		make_int(tmp+2, t->locked_by);
		tmp[2].flags |= FLAG_INT_THREAD;
		make_int(tmp+3, t->num_locks);
		return unify(q, c, c_ctx, tmp, q->st.cur_ctx);
	} else
		return throw_error(q, p2, p2_ctx, "domain_error", "mutex_property");

	return false;
}

static bool do_mutex_property_pin_property(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,nonvar);
	int i = q->retry ? (int)q->st.v1 : -1;

	thread *t = next_of_kind(q->pl, i, TK_MUTEX);

	if (!t)
		return false;

	q->st.v1 = t->chan;

	if (next_of_kind(q->pl, t->chan, TK_MUTEX))
		CHECKED(push_choice(q));

	cell tmp;

	if (t->alias)
		make_atom(&tmp, new_atom(q->pl, t->alias));
	else {
		make_int(&tmp, q->st.v1);
		tmp.flags |= FLAG_INT_THREAD;
	}

	unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	return do_mutex_property_pin_both(q);
}

static bool do_mutex_property_pin_id(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	int n = get_thread(q, p1);

	if ((n < 0) || !is_mutex(p1)) {
		THREAD_DEBUG DUMP_TERM(" - ", q->st.instr, q->st.cur_ctx, 1);
		return throw_error(q, p1, p1_ctx, "existence_error", "thread_object");
	}

	thread *t = find_thread_by_id(q->pl, n);
	unsigned i = 0;

	if (q->retry)
		i = ++q->st.v2;
	else
		q->st.v2 = 0;

	// An object created without an alias has no alias property at all,
	// rather than one carrying a null string - which is what
	// make_cstring() crashed on. Skip that slot, keeping v2 in step so
	// the next retry moves on instead of repeating this property.

	if ((i == 0) && !t->alias)
		q->st.v2 = i = 1;

	if (i == 0) {
		CHECKED(push_choice(q));
		cell *tmp = alloc_heap(q, 2);
		make_instr(tmp, new_atom(q->pl, "alias"), NULL, 1, 1);
		make_cstring(tmp+1, t->alias);

		if (!unify(q, p2, p2_ctx, tmp, q->st.cur_ctx)) {
			unshare_cell(tmp+1);
			return false;
		}

		return true;
	}

	cell *tmp;

	if (t->num_locks != 0) {
		tmp = alloc_heap(q, 4);
		make_instr(tmp, new_atom(q->pl, "status"), NULL, 1, 3);
		make_instr(tmp+1, new_atom(q->pl, "locked"), NULL, 2, 2);
		make_int(tmp+2, t->locked_by);
		tmp[2].flags |= FLAG_INT_THREAD;
		make_int(tmp+3, t->num_locks);
	} else {
		tmp = alloc_heap(q, 2);
		make_instr(tmp, new_atom(q->pl, "status"), NULL, 1, 1);
		make_atom(tmp+1, new_atom(q->pl, "unlocked"));
	}

	return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
}

static bool do_mutex_property_wild(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,var);
	int i = q->retry ? (int)q->st.v1 : -1;

	if (!q->retry)
		q->st.v2 = -1;

	thread *t = next_of_kind(q->pl, i, TK_MUTEX);

	if (!t)
		return false;

	q->st.v1 = t->chan;

	if (next_of_kind(q->pl, t->chan, TK_MUTEX))
		CHECKED(push_choice(q));

	cell tmp;

	if (t->alias)
		make_atom(&tmp, new_atom(q->pl, t->alias));
	else {
		make_int(&tmp, q->st.v1);
		tmp.flags |= FLAG_INT_THREAD;
	}

	unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	return do_mutex_property_pin_id(q);
}

static bool bif_mutex_property_2(query *q)
{
	THREAD_DEBUG DUMP_TERM("*** ", q->st.instr, q->st.cur_ctx, 1);
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);

	if (is_nonvar(p1) && !check_mutex(p1))
		return false;

	if (check_mutex(p1) && !is_var(p2))
		return do_mutex_property_pin_both(q);

	if (check_mutex(p1))
		return do_mutex_property_pin_id(q);

	if (!is_var(p2))
		return do_mutex_property_pin_property(q);

	return do_mutex_property_wild(q);
}

// How long shutdown gives a still running detached thread to finish and
// self-retire (the tail of start_routine_thread_create()) before giving up.

#define DETACHED_SHUTDOWN_WAIT_MS 2000

void thread_cancel_all(prolog *pl)
{
	msleep(10);

	uint64_t deadline = monotonic_time_in_usec() + ((uint64_t)DETACHED_SHUTDOWN_WAIT_MS * 1000);

	// Rescan from scratch each pass: a thread can spawn another (e.g. a
	// server's accept loop) after a pass has gone by it, and
	// threads_destroy() frees every struct unconditionally once this
	// returns. Joinable threads are cancelled and joined here (unlike
	// thread_cancel/1, which asks the thread to exit by signal and
	// leaves it to retire itself).
	// Detached threads can't be joined, so they just get a bounded wait
	// to self-retire.

	for (;;) {
		thread *found = NULL;
		bool detached_pending = false;
		prolog_lock(pl);					// a detached thread unlinks itself as it retires

		for_each_thread(pl, t) {
			if (t == pl->main_thread)
				continue;

			if (is_threaded(t) && !t->is_detached) {
				found = t;
				break;
			}

			if (t->is_detached && t->is_active)
				detached_pending = true;
		}

		prolog_unlock(pl);

		if (found) {
			if (!cancel_and_join(found->id, deadline))
				exit(pl->halt_code);

			retire_cancelled_thread(found);
			continue;
		}

		if (!detached_pending)
			return;

		if (monotonic_time_in_usec() >= deadline)
			break;

		msleep(5);
	}

	// Still running past the deadline: blocked with no timeout of its
	// own. Not safe to free anything under it - terminate now instead.

	exit(pl->halt_code);
}
#endif

builtins g_threads_bifs[] =
{
#if USE_THREADS

	// ISO standard...

	{"thread_create", 3, bif_thread_create_3, ":callable,--thread,+list", false, false, BLAH},
	{"thread_detach", 1, bif_thread_detach_1, "+thread", false, false, BLAH},
	{"thread_signal", 2, bif_thread_signal_2, "+thread,:callable", false, false, BLAH},
	{"$thread_join", 2, bif_thread_join_2, "+thread,-term", false, false, BLAH},
	{"thread_exit", 1, bif_thread_exit_1, "+term", false, false, BLAH},
	{"thread_self", 1, bif_thread_self_1, "-integer", false, false, BLAH},
	{"thread_sleep", 1, bif_thread_sleep_1, "+integer", false, false, BLAH},
	{"thread_yield", 0, bif_thread_yield_0, "", false, false, BLAH},
	{"thread_send_message", 2, bif_thread_send_message_2, "+queue,+term", false, false, BLAH},
	{"thread_get_message", 2, bif_thread_get_message_2, "+queue,?term", false, false, BLAH},
	{"thread_peek_message", 2, bif_thread_peek_message_2, "+queue,?term", false, false, BLAH},
	{"thread_property", 2, bif_thread_property_2, "?thread,?term", false, false, BLAH},

#if !defined(__ANDROID__)
	{"thread_cancel", 1, bif_thread_cancel_1, "+thread", false, false, BLAH},
#endif

	{"mutex_create", 2, bif_mutex_create_2, "-mutex,+list", false, false, BLAH},
	{"mutex_destroy", 1, bif_mutex_destroy_1, "+mutex", false, false, BLAH},
	{"mutex_trylock", 1, bif_mutex_trylock_1, "+mutex", false, false, BLAH},
	{"mutex_lock", 1, bif_mutex_lock_1, "+mutex", false, false, BLAH},
	{"mutex_unlock", 1, bif_mutex_unlock_1, "+mutex", false, false, BLAH},
	{"mutex_unlock_all", 0, bif_mutex_unlock_all_0, "", false, false, BLAH},
	{"mutex_property", 2, bif_mutex_property_2, "?mutex,?term", false, false, BLAH},

	{"message_queue_create", 2, bif_message_queue_create_2, "-queue,+list", false, false, BLAH},
	{"message_queue_destroy", 1, bif_message_queue_destroy_1, "+queue", false, false, BLAH},
	{"message_queue_property", 2, bif_message_queue_property_2, "?queue,?term", false, false, BLAH},

	// SWI-compatible...

	{"thread_get_message", 3, bif_thread_get_message_3, "+queue,?term,+list", false, false, BLAH},
	{"is_thread", 1, bif_is_thread_1, "+term", false, false, BLAH},

#endif

	{0}
};

