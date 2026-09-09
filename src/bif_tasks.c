#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "tpl_features.h"
#include "module.h"
#include "query.h"

#if TPL_FREESTANDING
#include "platform/platform.h"
static void msleep(int ms)
{
	uint64_t until = tpl_platform_monotonic_usec() + (uint64_t)ms * 1000u;

	while (tpl_platform_monotonic_usec() < until)
		;
}
#elif defined(_WIN32)
#include <windows.h>
#define msleep Sleep
#define localtime_r(p1,p2) localtime(p1)
#else
static void msleep(int ms)
{
	struct timespec tv = {0};
	tv.tv_sec = (ms) / 1000;
	tv.tv_nsec = ((ms) % 1000) * 1000 * 1000;
	nanosleep(&tv, &tv);
}
#endif

// Park a task on a descriptor only where tpl_set_nonblocking() is real,
// otherwise the read would have blocked rather than yielded and we'd
// never get here anyway. Keep this condition identical to that one.

#if TPL_FEATURE_NETWORK && !defined(_WIN32) && !defined(__wasi__)
#define USE_POLL 1
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#endif

// Longest we sleep in one go.
//
// Where a signal breaks the sleep this only bounds how long an interrupt
// can go unnoticed, and can afford to be generous: SIGINT and the
// SIGALRM behind an expiring alarm both cut poll() and nanosleep() short
// with EINTR, so neither waits this out.
//
// Timeouts are polled rather than signalled, so nothing cuts this sleep
// short on their behalf. That used to force the cap down to 5ms, which
// made every idle scheduler wake two hundred times a second just in
// case. sched_wait() now asks next_alarm_delay() and sleeps exactly
// until the nearest deadline instead, so the cap is back to bounding
// interrupt latency and nothing else.

#define SCHED_MAX_SLEEP_MS 250

// A task parked on a descriptor also carries a deadline, so that a
// wakeup we somehow miss costs latency rather than a hang.

#define SCHED_IO_BACKSTOP_MS 1000

bool do_yield(query *q, int msecs)
{
#ifdef __wasi__
	if (!q->is_task && !q->pl->is_query)
#else
	if (!q->is_task)
#endif
		return true;

	q->yield_at = 0;
	q->yielded = true;
	q->tmo_msecs = wall_time_in_usec() / 1000;
	q->tmo_msecs += msecs > 0 ? msecs : 1;
	CHECKED(push_choice(q));
	return false;
}

bool do_yield_then(query *q, bool status)
{
#ifdef __wasi__
	if (!q->is_task && !q->pl->is_query)
#else
	if (!q->is_task)
#endif
		return true;

	q->yield_at = 0;
	q->yielded = true;
	q->tmo_msecs = wall_time_in_usec() / 1000 + 1;
	// Push a choice point with the same result as the goal we hijacked
	// With that we can continue as if the yield didn't happen
	CHECKED(push_choice(q));
	choice *ch = GET_CURR_CHOICE();

	if (status)
		ch->succeed_on_retry = true;
	else
		ch->fail_on_retry = true;

	return false;
}

// yield/0 means "let the others have a turn", not "sleep for a bit", so
// it asks for no deadline at all and the scheduler puts the task back on
// the ready queue as it stands.
//
// That zero deadline is the whole trick: sched_park() only reaches the
// timer heap for a deadline still in the future, so a zero goes
// straight back on the ready queue.

bool do_yield_now(query *q)
{
#ifdef __wasi__
	if (!q->is_task && !q->pl->is_query)
#else
	if (!q->is_task)
#endif
		return true;

	q->yield_at = 0;
	q->yielded = true;
	q->tmo_msecs = 0;
	CHECKED(push_choice(q));
	return false;
}

void do_yield_at(query *q, unsigned int time_in_ms)
{
	q->yield_at = wall_time_in_usec() / 1000;
	q->yield_at += time_in_ms > 0 ? time_in_ms : 1;
}

// Yield knowing what we are actually waiting for, so the scheduler can
// poll() the descriptor instead of retrying us on a timer.
//
// Two conditions have to hold for the descriptor to mean anything. The
// kernel must have said EAGAIN, which tells us the read really did
// reach it - so the stdio buffer above it is empty and poll() is not
// answering about the wrong buffer. And the stream must not be TLS,
// which keeps decrypted bytes of its own that poll() cannot see. Any
// other reason for stopping - a reset peer says ECONNRESET, and poll()
// then reports nothing readable at all - goes back on the retry timer,
// where a doomed read costs a millisecond rather than the backstop.

bool do_yield_on_stream(query *q, stream *str, bool is_write)
{
#if USE_POLL
	bool would_block = (errno == EAGAIN) || (errno == EWOULDBLOCK);

	if (would_block && q->is_task && str->is_socket && !str->ssl && !str->is_memory) {
		FILE *fp = is_write ? str->fp_out : str->fp_in;
		int fd = fp ? fileno(fp) : -1;

		if (fd >= 0) {
			q->wait_fd = fd;
			q->wait_events = is_write ? POLLOUT : POLLIN;
			q->waiting_io = true;
			return do_yield(q, SCHED_IO_BACKSTOP_MS);
		}
	}
#else
	(void)str;
	(void)is_write;
#endif

	return do_yield(q, 1);
}

// The scheduler holds every task in exactly one of three places: the
// ready FIFO (runnable now), the timer heap (waiting on a deadline), or
// the io list (parked on a descriptor). q->tasks stays the registry of
// all of them, and remains what query_destroy() tears down.

enum {
	SCHED_READY = 0,					// a calloc'd query starts out here
	SCHED_TIMER,
	SCHED_IO
};

struct scheduler_ {
	query *ready_head, *ready_tail;
	query *io_head;
	query **timers;						// min-heap keyed on tmo_msecs
	unsigned timers_used, timers_size;
	uint64_t last_poll;					// when we last looked at the descriptors
#if USE_POLL
	struct pollfd *pfds;				// scratch for one poll() call
	query **pfd_owners;					// ... and who each entry belongs to
	unsigned pfds_size;

	// Self-pipe, so a thread that is not the owner can cut short a
	// sleep it is in - having just promoted one of its parked tasks.
	// The read end sits in every poll() set.

	int wake_fd[2];						// [0] read, [1] write; -1 if unavailable
	pl_atomic bool wake_pending;		// a byte is already in flight
#endif

	// Only the owning thread ever runs the scheduler, so the pollfd
	// scratch above stays single-writer. What other threads do is
	// *promote*: move one of these queues' tasks to ready when a message
	// it was parked on arrives. This guards exactly that - the ready
	// list, the timer heap, the io list, and the sched_where/heap_idx
	// each task carries saying which one it is on.
	//
	// Never held across start() or poll(). The first would serialise
	// what the pool exists to parallelise; the second would block.

	lock guard;
	bool guard_init;
};

static void sched_lock(scheduler *s)
{
	if (s->guard_init)
		acquire_lock(&s->guard);
}

static void sched_unlock(scheduler *s)
{
	if (s->guard_init)
		release_lock(&s->guard);
}

static void pop_task(query *q, query *task);

// There is one scheduler per prolog instance, not one per query that
// happened to spawn something. Ownership is tracked separately: a task
// stays on its spawner's q->tasks registry, and every query on the
// chain above it counts it in num_subtasks. So the scheduler answers
// "what can run", and the registry answers "whose is it" - which is
// what lets wait/0 mean "until my own work is done" while the queues
// themselves are shared.

// Which scheduler a query's tasks belong to: the one on its thread
// object. Tasks inherit thread_ptr from whoever spawned them, so a task
// of a thread's query lands on that thread's queues rather than the
// main thread's.

// Originally only ever called by a thread lazily creating its own
// scheduler (push_task(), below - single-writer, no race possible).
// send/2 broke that invariant: it needs the *target's* scheduler from
// whatever thread happens to be sending, which may be the first thing
// to touch that target's scheduler at all.
//
// That used to be settled by taking prolog_lock() for the whole
// function, which put a process-wide mutex on the hottest path there
// is: every task create, send, recv, wait and destroy calls this, so
// samples/skynet_mixed.pl was paying six or seven acquisitions of one
// global lock per task, and four threads spent 27% more instructions
// than one to finish in the same wall time. The lock was only ever
// guarding the lazy allocation below, which happens once per thread
// and never again, so it is double-checked instead: an atomic read of
// an already-created scheduler on the fast path, the lock only on the
// one call per thread that finds none.

static scheduler *sched_get(query *q)
{
	thread *t = get_self_query(q);
	scheduler *s = t->sched;

	if (s)
		return s;

	const bool mt = q->pl->is_multithreaded;

	if (mt)
		prolog_lock(q->pl);

	// Re-check under the lock: two threads can arrive here at once,
	// and only the first may allocate.

	// Built complete in a local and published last, not assigned to
	// t->sched and filled in afterwards: the fast path above reads that
	// pointer with no lock, so a half-initialised scheduler - a guard
	// not yet init_lock()'d - must never be reachable through it.

	if (!t->sched) {
		scheduler *ns = TPL_calloc(1, sizeof(scheduler));

		if (ns) {
			init_lock(&ns->guard);
			ns->guard_init = true;
#if USE_POLL
			if (pipe(ns->wake_fd) == 0) {
				fcntl(ns->wake_fd[0], F_SETFL, fcntl(ns->wake_fd[0], F_GETFL, 0) | O_NONBLOCK);
				fcntl(ns->wake_fd[1], F_SETFL, fcntl(ns->wake_fd[1], F_GETFL, 0) | O_NONBLOCK);
			} else
				ns->wake_fd[0] = ns->wake_fd[1] = -1;
#endif
			pl_publish_barrier();
			t->sched = ns;
		}
	}

	s = t->sched;

	if (mt)
		prolog_unlock(q->pl);

	return s;
}

// Cut short a sleep the owning thread is in. Safe from any thread, and
// cheap when it is not sleeping: one byte, at most one in flight.

void sched_wake(thread *t)
{
#if USE_POLL
	scheduler *s = t->sched;

	if (!s || (s->wake_fd[1] < 0) || s->wake_pending)
		return;

	s->wake_pending = true;
	const char c = 1;

	if (write(s->wake_fd[1], &c, 1) != 1)
		s->wake_pending = false;
#else
	(void)t;
#endif
}

void sched_destroy(thread *t)
{
	if (!t->sched)
		return;

	TPL_free(t->sched->timers);
#if USE_POLL
	TPL_free(t->sched->pfds);
	TPL_free(t->sched->pfd_owners);

	if (t->sched->wake_fd[0] >= 0) close(t->sched->wake_fd[0]);
	if (t->sched->wake_fd[1] >= 0) close(t->sched->wake_fd[1]);
#endif

	if (t->sched->guard_init)
		deinit_lock(&t->sched->guard);

	TPL_free(t->sched);
	t->sched = NULL;
}

static void heap_swap(scheduler *s, unsigned a, unsigned b)
{
	query *tmp = s->timers[a];
	s->timers[a] = s->timers[b];
	s->timers[b] = tmp;
	s->timers[a]->heap_idx = a;
	s->timers[b]->heap_idx = b;
}

static void heap_up(scheduler *s, unsigned i)
{
	while (i && (s->timers[(i-1)/2]->tmo_msecs > s->timers[i]->tmo_msecs)) {
		heap_swap(s, i, (i-1)/2);
		i = (i-1) / 2;
	}
}

static void heap_down(scheduler *s, unsigned i)
{
	while (true) {
		unsigned l = (2*i) + 1, r = l + 1, min = i;

		if ((l < s->timers_used) && (s->timers[l]->tmo_msecs < s->timers[min]->tmo_msecs))
			min = l;

		if ((r < s->timers_used) && (s->timers[r]->tmo_msecs < s->timers[min]->tmo_msecs))
			min = r;

		if (min == i)
			break;

		heap_swap(s, i, min);
		i = min;
	}
}

static bool heap_push(scheduler *s, query *task)
{
	if (s->timers_used == s->timers_size) {
		unsigned size = s->timers_size ? s->timers_size * 2 : 8;
		query **timers = TPL_realloc(s->timers, size * sizeof(query*));

		if (!timers)
			return false;

		s->timers = timers;
		s->timers_size = size;
	}

	s->timers[s->timers_used] = task;
	task->heap_idx = s->timers_used++;
	task->sched_where = SCHED_TIMER;
	heap_up(s, task->heap_idx);
	return true;
}

static void heap_remove(scheduler *s, unsigned i)
{
	s->timers_used--;

	if (i != s->timers_used) {
		s->timers[i] = s->timers[s->timers_used];
		s->timers[i]->heap_idx = i;
		heap_down(s, i);
		heap_up(s, i);
	}
}

static void sched_ready_push(scheduler *s, query *task)
{
	task->sched_where = SCHED_READY;
	task->sched_next = NULL;
	task->waiting_io = false;

	if (s->ready_tail)
		s->ready_tail->sched_next = task;
	else
		s->ready_head = task;

	s->ready_tail = task;
}

static query *sched_ready_pop(scheduler *s)
{
	query *task = s->ready_head;

	if (!task)
		return NULL;

	s->ready_head = task->sched_next;

	if (!s->ready_head)
		s->ready_tail = NULL;

	task->sched_next = NULL;
	return task;
}

// Take a task off whichever queue it is currently on. Only needed for
// cancellation, so the linear walks are not on any hot path.

static void sched_unlink(scheduler *s, query *task)
{
	if (task->sched_where == SCHED_TIMER) {
		heap_remove(s, task->heap_idx);
		return;
	}

	query **head = task->sched_where == SCHED_IO ? &s->io_head : &s->ready_head;
	query *prev = NULL;

	for (query *t = *head; t; prev = t, t = t->sched_next) {
		if (t != task)
			continue;

		if (prev)
			prev->sched_next = task->sched_next;
		else
			*head = task->sched_next;

		if ((head == &s->ready_head) && (s->ready_tail == task))
			s->ready_tail = prev;

		break;
	}

	task->sched_next = NULL;
}

// Move a task that was parked waiting for a message onto its own
// scheduler's ready queue, and wake the thread that owns it. Called
// from whichever thread did the send, which is why the scheduler needs
// a lock at all.

void sched_promote(query *task)
{
	thread *owner = task->thread_ptr ? task->thread_ptr : task->pl->main_thread;
	scheduler *s = owner->sched;

	if (!s)
		return;

	sched_lock(s);

	if (task->sched_where != SCHED_READY) {
		sched_unlink(s, task);
		task->tmo_msecs = 0;
		sched_ready_push(s, task);
	}

	sched_unlock(s);
	sched_wake(owner);
}

// Where a task goes after it has had its turn. A task that errored is
// put straight back on the ready queue so it gets reaped next pass,
// rather than sitting out whatever it asked to wait for.

static void sched_park(scheduler *s, query *task, uint64_t now)
{
	if (!task->error) {
		if (task->waiting_io) {
			task->sched_where = SCHED_IO;
			task->sched_next = s->io_head;
			s->io_head = task;
			return;
		}

		if ((task->tmo_msecs >= now) && heap_push(s, task))
			return;
	}

	sched_ready_push(s, task);
}

static void sched_expire_timers(scheduler *s, uint64_t now)
{
	while (s->timers_used && (s->timers[0]->tmo_msecs < now)) {
		query *task = s->timers[0];
		heap_remove(s, 0);
		sched_ready_push(s, task);
	}
}

// Look at the descriptors tasks are parked on, promoting whatever woke
// or ran out of backstop. With `block` set - nothing else is runnable -
// this sleeps until one of them comes up or until the nearest deadline,
// whichever is sooner, the cap only bounding how long an interrupt can
// go unnoticed. Without it this is a check that returns immediately.

static void sched_wait(query *q, scheduler *s, uint64_t now, bool block)
{
	unsigned n = 0;
	sched_lock(s);
	uint64_t deadline = s->timers_used ? s->timers[0]->tmo_msecs + 1 : 0;

#if USE_POLL
	// Slot 0 is always the wake pipe, so any sleep below can be ended by
	// another thread that has just promoted one of our tasks.

	unsigned wake_slot = (unsigned)-1;

	if (s->wake_fd[0] >= 0) {
		if (!s->pfds_size) {
			struct pollfd *pfds = TPL_realloc(s->pfds, 8 * sizeof(struct pollfd));
			query **owners = pfds ? TPL_realloc(s->pfd_owners, 8 * sizeof(query*)) : NULL;

			if (owners) {
				s->pfds = pfds;
				s->pfd_owners = owners;
				s->pfds_size = 8;
			} else if (pfds)
				s->pfds = pfds;
		}

		if (s->pfds_size) {
			wake_slot = n;
			s->pfds[n].fd = s->wake_fd[0];
			s->pfds[n].events = POLLIN;
			s->pfds[n].revents = 0;
			s->pfd_owners[n] = NULL;
			n++;
		}
	}

	for (query *task = s->io_head; task; task = task->sched_next) {
		if (n == s->pfds_size) {
			unsigned size = s->pfds_size ? s->pfds_size * 2 : 8;
			struct pollfd *pfds = TPL_realloc(s->pfds, size * sizeof(struct pollfd));
			query **owners = pfds ? TPL_realloc(s->pfd_owners, size * sizeof(query*)) : NULL;

			if (!owners) {
				if (pfds) s->pfds = pfds;
				break;
			}

			s->pfds = pfds;
			s->pfd_owners = owners;
			s->pfds_size = size;
		}

		s->pfds[n].fd = task->wait_fd;
		s->pfds[n].events = task->wait_events;
		s->pfds[n].revents = 0;
		s->pfd_owners[n] = task;
		n++;

		if (task->tmo_msecs && (!deadline || (task->tmo_msecs + 1 < deadline)))
			deadline = task->tmo_msecs + 1;
	}
#endif

	sched_unlock(s);

	// Clamp before narrowing: a long enough sleep/1 would otherwise
	// overflow the int and could turn into poll()'s "block forever".

	uint64_t delta = deadline && (deadline > now) ? deadline - now : 0;
	int tmo = delta > SCHED_MAX_SLEEP_MS ? SCHED_MAX_SLEEP_MS : (int)delta;

	if (!deadline)
		tmo = SCHED_MAX_SLEEP_MS;

	// A pending call_with_time_limit/2 is a deadline too. Without this
	// the sleep would run past it and the timeout would fire late by
	// however much of the cap was left.

	unsigned alarm_ms = 0;

	if (next_alarm_delay(q, &alarm_ms) && ((int)alarm_ms < tmo))
		tmo = (int)alarm_ms;

	if (!block)
		tmo = 0;

	if (!n) {
		if (block)
			msleep(tmo > 0 ? tmo : 1);

		return;
	}

#if USE_POLL
	int ready = poll(s->pfds, n, tmo);
	now = wall_time_in_usec() / 1000;
	sched_lock(s);

	// Drain the wake pipe if that is what ended the sleep. It carries no
	// information beyond "look again", so the bytes are discarded.

	if ((wake_slot != (unsigned)-1) && (ready > 0) && s->pfds[wake_slot].revents) {
		char buf[64];

		while (read(s->wake_fd[0], buf, sizeof(buf)) > 0)
			;

		s->wake_pending = false;
	}

	// Promote by owner rather than by walking the io list in lockstep
	// with the descriptors. The lock was released across poll(), so
	// another thread may have promoted one of these already - a lockstep
	// walk assumes the list is exactly as it was when the set was built,
	// and would put the wrong task on the wrong descriptor if it is not.
	// sched_where says whether an entry is still parked on io at all.

	for (unsigned i = 0; i < n; i++) {
		query *task = s->pfd_owners[i];

		if (!task)					// the wake pipe owns no task
			continue;

		bool woken = (ready > 0) && s->pfds[i].revents;

		if (!woken && task->tmo_msecs && (task->tmo_msecs >= now))
			continue;

		if (task->sched_where != SCHED_IO)
			continue;

		sched_unlink(s, task);
		sched_ready_push(s, task);
	}

	sched_unlock(s);
#endif
}

// Run until the caller's own subtree is done. There used to be a second
// mode that stopped as soon as one task "signalled" - yielded without
// asking for a deadline - which is how await/0 heard about send/1. All
// three went together, so what is left is the drain.

static void sched_run(query *q)
{
	scheduler *s = sched_get(q);

	while (q->num_subtasks && !q->end_wait) {
		CHECK_INTERRUPT();
		uint64_t now = wall_time_in_usec() / 1000;

		sched_lock(s);
		sched_expire_timers(s, now);
		query *task = sched_ready_pop(s);
		bool idle = !task && !s->timers_used && !s->io_head;

		// A yield goes straight back on the ready queue, so a task that
		// yields in a loop would keep this queue occupied and we would
		// never reach the blocking wait below - leaving anyone parked on
		// a descriptor unheard. Look at them anyway, without blocking,
		// and at most once a millisecond so this stays off the hot path.

		bool peek_io = task && s->io_head && (now > s->last_poll);

		if (peek_io)
			s->last_poll = now;

		sched_unlock(s);

		if (!task) {
			if (idle)
				break;					// nothing left to wake us

			sched_wait(q, s, now, true);
			continue;
		}

		if (peek_io)
			sched_wait(q, s, now, false);

		task->tmo_msecs = 0;
		task->waiting_io = false;

		// The one place task_cancel/1's request actually takes effect:
		// single-threaded here, relative to this task, so folding it
		// into `error` is safe in a way writing `error` directly from
		// whatever foreign thread called task_cancel/1 would not be.

		if (task->cancel_requested)
			task->error = true;

		if (!task->yielded || !task->st.instr || task->error) {
			pop_task(task->parent, task);
			query_destroy(task);
			continue;
		}

		// Deliberately outside the lock: this runs arbitrary Prolog and
		// can re-enter the scheduler.

		start(task);

		sched_lock(s);
		sched_park(s, task, now);
		sched_unlock(s);
	}

	q->end_wait = false;
}

// A task counts against everyone above it, not just its spawner, so
// that a wait/0 high up knows there is still work underneath it even
// when its own direct children have all finished.

static void count_subtask(query *q, int delta)
{
	for (; q; q = q->parent)
		q->num_subtasks += delta;
}

static bool push_task(query *q, query *task)
{
	scheduler *s = sched_get(q);

	if (!s)
		return false;

	task->next = q->tasks;

	if (q->tasks)
		q->tasks->prev = task;

	q->tasks = task;
	count_subtask(q, 1);
	sched_lock(s);
	sched_ready_push(s, task);
	sched_unlock(s);
	return true;
}

static void pop_task(query *q, query *task)
{
	if (task->prev)
		task->prev->next = task->next;

	if (task->next)
		task->next->prev = task->prev;

	if (task == q->tasks)
		q->tasks = task->next;

	count_subtask(q, -1);
}

// Take a query's tasks off the shared queues before it is torn down.
// With a scheduler of its own this was free - the whole thing was
// freed with the query - but the queues outlive any one query now, so
// anything still sitting in them has to be unlinked or it dangles.

void sched_release(query *q)
{
	scheduler *s = sched_get(q);

	if (!s)
		return;

	sched_lock(s);

	for (query *task = q->tasks; task; task = task->next)
		sched_unlink(s, task);

	sched_unlock(s);
}

// Registry for addressing an arbitrary task by id, from any thread.
//
// One skiplist per *thread*, not per prolog instance. A task is created
// by its owning thread, destroyed by its owning thread, and - in every
// workload that spawns tasks in bulk - addressed by its owning thread,
// so all three take a lock nobody else wants. What they used to take
// was prolog_lock(), one process-wide mutex, three times per task:
// samples/skynet_mixed.pl spends its whole run creating, sending to and
// destroying 1.11M tasks, and four threads doing that could not go
// faster than one.
//
// An id carries its owner, which is what makes the per-thread split
// work at all: without it a sender holding nothing but an integer would
// have to search every thread. The layout is
//
//     id = (owning thread's chan << TASK_ID_SEQ_BITS) | seq
//
// with seq counting up per thread. chan is monotonic and never reused,
// so an id whose thread has since been retired resolves to a struct
// that no longer holds that seq - a lookup miss, existence_error, the
// same answer the global registry gave. The seq keys are small, which
// also retires the note this code used to carry about a process-wide
// uint64_t qid truncating when a skiplist key is a 32-bit uintptr_t.
//
// The bits: 24 for chan, 40 for seq. A process would have to create
// 16.7M threads, or one thread 1.1T tasks, to wrap either; both wrap to
// an id that fails to resolve rather than one that resolves wrongly,
// because seq keys are only ever compared within one thread's own
// registry.

#define TASK_ID_SEQ_BITS 40
#define TASK_ID_SEQ_MASK ((UINT64_C(1) << TASK_ID_SEQ_BITS) - 1)

bool register_task(query *q)
{
	thread *t = get_self_query(q);
	acquire_lock(&t->tasks_guard);

	if (!t->tasks)
		t->tasks = sl_create(NULL, NULL, NULL);

	bool ok = false;

	if (t->tasks) {
		uint64_t seq = ++t->next_task_seq;
		ok = sl_set(t->tasks, (const void*)(uintptr_t)seq, q);

		if (ok) {
			q->task_id = ((uint64_t)t->chan << TASK_ID_SEQ_BITS) | seq;
			q->task_owner = t;
		}
	}

	release_lock(&t->tasks_guard);
	return ok;
}

// Deliberately keyed off task_owner rather than get_self_query(): a
// query is torn down by the thread that owns it today, but nothing in
// the type system says so, and removing an entry from a registry other
// than the one it went into would leave a dangling pointer behind.

void unregister_task(query *q)
{
	// Nothing to delete unless this query ever registered itself, and
	// most never do - query_destroy() calls this for every transient
	// sub-query (findall, format's ~@, goal expansion) as well.

	if (!q->is_registered)
		return;

	thread *t = q->task_owner;

	if (!t)
		return;

	acquire_lock(&t->tasks_guard);

	if (t->tasks)
		sl_del(t->tasks, (const void*)(uintptr_t)(q->task_id & TASK_ID_SEQ_MASK));

	release_lock(&t->tasks_guard);
	q->is_registered = false;
}

// Which thread's registry an id belongs in. The common case answers
// itself: a task addressing a task of its own thread reads its own
// chan back out of the id and never touches anything shared. Only a
// genuine cross-thread send pays find_thread_by_id()'s prolog_lock().

thread *find_task_owner(query *q, uint64_t task_id)
{
	int chan = (int)(task_id >> TASK_ID_SEQ_BITS);
	thread *self = get_self_query(q);

	if (chan == self->chan)
		return self;

	return find_thread_by_id(q->pl, chan);
}

// Callers that go on to *use* what they find must hold the owner's
// tasks_guard across both, not call this - see bif_send_2(). Dropping
// the lock before the pointer is dereferenced is the exact bug class
// fixed on the thread side: the owning thread can destroy the task in
// the window between.

query *find_task_by_id(query *q, uint64_t task_id)
{
	thread *t = find_task_owner(q, task_id);

	if (!t)
		return NULL;

	acquire_lock(&t->tasks_guard);
	const void *v = NULL;
	bool found = t->tasks
		&& sl_get(t->tasks, (const void*)(uintptr_t)(task_id & TASK_ID_SEQ_MASK), &v);
	release_lock(&t->tasks_guard);
	return found ? (query*)v : NULL;
}

// Called from threads_destroy(), beside sched_destroy(), and from
// retire_thread() - a retired struct's ids stop meaning anything the
// moment it goes on the free list, and its registry has to stop
// answering for them before the struct is handed out under a new chan.

void tasks_destroy(thread *t)
{
	acquire_lock(&t->tasks_guard);

	if (t->tasks) {
		sl_destroy(t->tasks);
		t->tasks = NULL;
	}

	release_lock(&t->tasks_guard);
}

static bool bif_end_wait_0(query *q)
{
	if (q->parent)
		q->parent->end_wait = true;

	return true;
}

static bool bif_wait_0(query *q)
{
	if (sched_get(q))
		sched_run(q);

	q->end_wait = false;
	return true;
}

static bool bif_yield_0(query *q)
{
	if (q->retry)
		return true;

	return do_yield_now(q);
}

static bool bif_call_task_n(query *q)
{
	GET_FIRST_ARG(p1,callable);

	if ((p1->val_off == g_colon_s) && (get_arity(p1) == 2)) {
		cell *cm = p1 + 1;
		cm = deref(q, cm, p1_ctx);

		if (!is_atom(cm) && !is_var(cm))
			return throw_error(q, cm, p1_ctx, "type_error", "callable");

		if (!is_var(cm)) {
			module *m = find_module(q->pl, C_STR(q, cm));
			if (m) q->st.m = m;
		}

		p1 += 2;
		p1 = deref(q, p1, p1_ctx);
		p1_ctx = q->latest_ctx;

		if (!is_callable(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "callable");
	}

	uint32_t arity = get_arity(p1), args = 1, xarity = get_arity(q->st.instr);
	CHECKED(init_tmp_heap(q));
	CHECKED(append_to_tmp(q, p1, p1_ctx));

	while (args++ < xarity) {
		GET_NEXT_ARG(p2,any);
		CHECKED(append_to_tmp(q, p2, p2_ctx));
		arity++;
	}

	cell *tmp2 = get_tmp_heap(q, 0);
	tmp2->num_cells = tmp_heap_used(q);

	if (is_cstring(tmp2)) {
		share_cell(tmp2);
		convert_to_literal(q->st.m, tmp2);
	}

	set_arity(tmp2, arity);

	tmp2->match = NULL;
	bool status;

	if (!call_check(q, tmp2, &status, true))
		return status;

	// A task is a query of its own, so the goal has to reach it as a
	// term rather than as references into our frames - a context is a
	// frame index, and means nothing over there. So: clone the assembled
	// goal, which resolves what we have bound; then rebase it into a
	// numbering of its own, which is also what turns the references
	// prepare_call() just made back into plain variables.
	//
	// The clone has to read from somewhere other than the heap it writes
	// to, hence staging the goal first. And it has to happen once, over
	// the whole goal: clone_term_to_tmp() starts a new generation on each
	// call, so cloning argument by argument renumbers each in isolation
	// and lets unrelated variables collide.

	pl_idx num_cells = tmp2->num_cells;
	cell *staged = TPL_malloc(num_cells * sizeof(cell));
	CHECKED(staged);
	copy_cells(staged, tmp2, num_cells);

	if (!init_tmp_heap(q)) {
		TPL_free(staged);
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");
	}

	cell *goal = clone_term_to_tmp(q, staged, q->st.cur_ctx);
	TPL_free(staged);
	CHECKED(goal);

	cell *tmp = prepare_call(q, CALL_SKIP, goal, q->st.cur_ctx, 0);
	CHECKED(tmp);
	unsigned num_vars = rebase_term(q, tmp, 0, false);
	query *task = query_create_task_rebased(q, tmp, num_vars);
	CHECKED(task);
	task->yielded = task->spawned = true;
	CHECKED(push_task(q, task));
	return true;
}

// call_task/N with the new task's address handed back immediately,
// mirroring thread_create/2 - unlike task_self/1's lazy registration,
// this registers eagerly, before the task has run a single instruction,
// because the whole point is a caller holding Qid and being able to
// send/2 to it right away. No variadic call_task(Goal,A1,...,Qid) form:
// Qid is always the second and only other argument, and a caller
// wanting extra goal arguments just builds the compound itself, e.g.
// task_create(worker(A,B,C), Qid).

static bool bif_task_create_2(query *q)
{
	GET_FIRST_ARG(p1,callable);
	GET_NEXT_ARG(p2,var);

	if ((p1->val_off == g_colon_s) && (get_arity(p1) == 2)) {
		cell *cm = p1 + 1;
		cm = deref(q, cm, p1_ctx);

		if (!is_atom(cm) && !is_var(cm))
			return throw_error(q, cm, p1_ctx, "type_error", "callable");

		if (!is_var(cm)) {
			module *m = find_module(q->pl, C_STR(q, cm));
			if (m) q->st.m = m;
		}

		p1 += 2;
		p1 = deref(q, p1, p1_ctx);
		p1_ctx = q->latest_ctx;

		if (!is_callable(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "callable");
	}

	CHECKED(init_tmp_heap(q));
	CHECKED(append_to_tmp(q, p1, p1_ctx));

	uint32_t arity = get_arity(p1);
	cell *tmp2 = get_tmp_heap(q, 0);
	tmp2->num_cells = tmp_heap_used(q);

	if (is_cstring(tmp2)) {
		share_cell(tmp2);
		convert_to_literal(q->st.m, tmp2);
	}

	set_arity(tmp2, arity);

	tmp2->match = NULL;
	bool status;

	if (!call_check(q, tmp2, &status, true))
		return status;

	// See bif_call_task_n just above for why this stages through a
	// malloc'd copy rather than cloning tmp2 directly.

	pl_idx num_cells = tmp2->num_cells;
	cell *staged = TPL_malloc(num_cells * sizeof(cell));
	CHECKED(staged);
	copy_cells(staged, tmp2, num_cells);

	if (!init_tmp_heap(q)) {
		TPL_free(staged);
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");
	}

	cell *goal = clone_term_to_tmp(q, staged, q->st.cur_ctx);
	TPL_free(staged);
	CHECKED(goal);

	cell *tmp = prepare_call(q, CALL_SKIP, goal, q->st.cur_ctx, 0);
	CHECKED(tmp);
	unsigned num_vars = rebase_term(q, tmp, 0, false);
	query *task = query_create_task_rebased(q, tmp, num_vars);
	CHECKED(task);
	task->yielded = task->spawned = true;
	CHECKED(push_task(q, task));

	if (!register_task(task))
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

	task->is_registered = true;
	cell tmp_qid;
	make_int(&tmp_qid, (pl_int)task->task_id);
	return unify(q, p2, p2_ctx, &tmp_qid, q->st.cur_ctx);
}

static bool bif_fork_0(query *q)
{
	cell *instr = q->st.instr + q->st.instr->num_cells;
	query *task = query_create_task(q, instr);
	task->yielded = true;
	CHECKED(push_task(q, task));
	return false;
}

static bool bif_sys_cancel_future_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	uint64_t future = get_smalluint(p1);

	for (query *task = q->tasks; task; task = task->next) {
		if (task->future == future) {
			task->error = true;

			// Take it off whatever it was waiting for, so the cancel
			// lands on the next pass rather than whenever its
			// descriptor or deadline happens to come up.

			scheduler *s = sched_get(q);

			if (s && (task->sched_where != SCHED_READY)) {
				sched_lock(s);

				if (task->sched_where != SCHED_READY) {
					sched_unlink(s, task);
					sched_ready_push(s, task);
				}

				sched_unlock(s);
			}

			break;
		}
	}

	return true;
}

static bool bif_sys_set_future_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	q->future = get_smalluint(p1);
	return true;
}

// Cross-thread task cancellation, addressed by qid instead of the
// future-id $cancel_future/1 uses (and restricted to the caller's own
// children, via q->tasks - a qid found through the registry can belong
// to any thread). Only cancel_requested gets written here; see its
// comment in internal.h and the check in sched_run() for why the
// `error = true` that actually stops the task has to happen on the
// task's own owning thread, not this one. sched_promote() is the same
// unconditionally-safe call send/2 already makes - a no-op if the task
// is already ready or running, which just means the cancellation is
// picked up next time it parks rather than instantly: there is no
// preemption for a cooperative task, only the next checkpoint.

static bool bif_task_cancel_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	uint64_t task_id = (uint64_t)get_smallint(p1);
	thread *owner = find_task_owner(q, task_id);

	if (!owner)
		return throw_error(q, p1, p1_ctx, "existence_error", "task");

	// Lookup and use under one hold of the owner's lock: the owning
	// thread destroys its own tasks, so a pointer resolved and then
	// used with the lock dropped in between can be freed underneath.

	acquire_lock(&owner->tasks_guard);
	const void *v = NULL;
	bool found = owner->tasks
		&& sl_get(owner->tasks, (const void*)(uintptr_t)(task_id & TASK_ID_SEQ_MASK), &v);
	query *target = found ? (query*)v : NULL;

	if (!target || !target->is_task) {
		release_lock(&owner->tasks_guard);
		return throw_error(q, p1, p1_ctx, "existence_error", "task");
	}

	target->cancel_requested = true;
	sched_promote(target);
	release_lock(&owner->tasks_guard);
	return true;
}

// A task's own mailbox. Minimal on purpose - no from_chan-shaped extra
// baggage beyond what a reply-to address needs (from_qid), unlike
// thread.c's msg, which this deliberately does not share: the two
// mailboxes are locked differently (a task's under its owning thread's
// scheduler->guard, not a thread's own guard) and qid does not fit
// where from_chan does (uint64_t vs int).

typedef struct task_msg_ {
	lnode hdr;						// must be first
	uint64_t from_qid;
	cell c[];
} task_msg;

// A task destroyed with unread mail still owns those cells' references -
// query_destroy() calls this before it starts unsharing the heap, same
// as thread teardown draining its own queue.

void drain_mailbox(query *q)
{
	task_msg *m;

	while ((m = list_pop_front(&q->mailbox))) {
		unshare_cells(m->c, m->c->num_cells);
		TPL_free(m);
	}
}

static bool bif_task_self_1(query *q)
{
	GET_FIRST_ARG(p1,var);

	// Registered lazily, here, rather than at query construction: the
	// only way anyone else ever learns this qid is by this query telling
	// them (there's no other way to enumerate qids), so a query that
	// never calls task_self/1 is unreachable and not worth a registry
	// slot. This covers a plain top-level directive's query exactly the
	// same as a task's or a thread's - none of them are otherwise
	// distinguishable from the countless transient queries (format's
	// ~@, with_output_to, goal/term expansion) that come and go by the
	// thousand and must never touch the registry.

	if (!q->is_registered) {
		CHECKED(register_task(q));
		q->is_registered = true;
	}

	cell tmp;
	make_int(&tmp, (pl_int)q->task_id);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_send_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,nonvar);
	uint64_t task_id = (uint64_t)get_smallint(p1);
	thread *owner = find_task_owner(q, task_id);

	if (!owner)
		return throw_error(q, p1, p1_ctx, "existence_error", "task");

	// Built before the lock, not under it: cloning the term and
	// allocating the message are the expensive half of a send, and the
	// owner's registry is exactly what a busy thread's own tasks are
	// contending for.

	CHECKED(init_tmp_heap(q));
	cell *c = clone_term_to_tmp(q, p2, p2_ctx);
	CHECKED(c);

	for (pl_idx i = 0; i < c->num_cells; i++)
		share_cell(c + i);

	task_msg *m = TPL_malloc(sizeof(task_msg) + (sizeof(cell)*c->num_cells));

	if (!m) {
		for (pl_idx i = 0; i < c->num_cells; i++)
			unshare_cell(c + i);

		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");
	}

	m->from_qid = q->task_id;
	dup_cells(m->c, c, c->num_cells);

	// Resolve and deliver under one hold of the owner's lock. Dropping
	// it in between is what let a target be destroyed by its own thread
	// between being found and being written to.

	acquire_lock(&owner->tasks_guard);
	const void *v = NULL;
	bool found = owner->tasks
		&& sl_get(owner->tasks, (const void*)(uintptr_t)(task_id & TASK_ID_SEQ_MASK), &v);
	query *target = found ? (query*)v : NULL;

	if (!target) {
		release_lock(&owner->tasks_guard);

		for (pl_idx i = 0; i < c->num_cells; i++)
			unshare_cell(c + i);

		TPL_free(m);
		return throw_error(q, p1, p1_ctx, "existence_error", "task");
	}

	scheduler *s = sched_get(target);

	if (s) sched_lock(s);
	list_push_back(&target->mailbox, m);
	if (s) sched_unlock(s);

	// Safe unconditionally, not just when target happens to be parked:
	// sched_where reads SCHED_READY both while genuinely on the ready
	// queue and while actively running (sched_ready_pop() does not
	// change it, only sched_park() does, after start() returns), and
	// promoting either case is a correct no-op. No separate waiter
	// list needed - unlike a thread's mailbox, which several tasks
	// could be parked on at once, a task's own mailbox has at most one
	// possible waiter: itself.

	sched_promote(target);
	release_lock(&owner->tasks_guard);
	return true;
}

static bool bif_recv_1(query *q)
{
	GET_FIRST_ARG(p1,any);

	// Under our own scheduler's lock: another thread's send/2 can be
	// pushing into this mailbox concurrently right now.

	scheduler *s = sched_get(q);
	if (s) sched_lock(s);

	task_msg *m = list_front(&q->mailbox);
	const frame *f = GET_CURR_FRAME();

	while (m) {
		CHECKED(push_choice(q), if (s) sched_unlock(s));
		cell *tmp = import_term(q, m->c, q->st.cur_ctx);
		CHECKED(tmp, if (s) sched_unlock(s));

		if (unify(q, p1, p1_ctx, tmp, q->st.cur_ctx)) {
			q->cur_task_qid = m->from_qid;
			list_remove(&q->mailbox, m);
			if (s) sched_unlock(s);
			unshare_cells(m->c, m->c->num_cells);
			TPL_free(m);
			drop_choice(q);
			return true;
		}

		retry_choice(q);
		m = list_next(m);
	}

	if (s) sched_unlock(s);
	return false;
}

// Backstop nap for recv/2's park, mirroring MSG_TASK_POLL_MS
// (bif_threads.c): send/2 already promotes this task on every send
// (sched_promote() is a correct no-op if we are not actually parked
// yet, per its own comment), so this is not the wakeup path in the
// ordinary case - it only bounds how long a missed promote (this task
// not yet parked at the moment a concurrent send() calls it) costs.

#define TASK_RECV_POLL_MS 5

// Blocking counterpart to recv/1, with the same in-place selective
// scan. Opts takes timeout(Seconds) (int or float); with no timeout,
// blocks indefinitely. See do_match_message_()'s do_wait_message() in
// bif_threads.c for the dual-path pattern this mirrors, and why it has
// to be dual: do_yield() only parks a *task* (it is a correct no-op -
// an immediate `true` - for anything else, per its own q->is_task
// guard), but recv/2 can just as well be called from a plain top-level
// directive or a thread's own root query, which are not tasks. Get
// this wrong and a non-task caller's do_yield() silently no-ops,
// making the surrounding recv/2 vacuously "succeed" without binding or
// checking anything at all - caught by a test where a message that was
// never sent appeared to have been received instantly.
//
// So: q->is_task parks via do_yield() and returns to the scheduler,
// same as recv/1's plain wait would if it had one; otherwise this
// blocks the real OS thread directly, sleeping and rescanning in a
// plain C loop, the same as do_wait_message()'s non-task branch calls
// suspend_thread() instead of parking. The deadline still has to live
// on the query (q->msg_deadline), not a local, because a parked task
// is retried from the top of this function on its next entry, and a
// deadline recomputed there would reset the clock every time; q->retry
// - already true on that re-entry - is what stops that.

static bool bif_recv_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,list_or_nil);
	PROLOG_LIST_HANDLER(p2);
	cell *p2_orig = p2;
	pl_ctx p2_orig_ctx = p2_ctx;
	double timeout = -1.0;

	while (is_iso_list(p2)) {
		cell *h = PROLOG_LIST_HEAD(p2);
		h = deref(q, h, p2_ctx);
		pl_ctx h_ctx = q->latest_ctx;

		if (!is_interned(h) || !is_compound(h))
			return throw_error(q, h, h_ctx, "domain_error", "read_option");

		if (!CMP_STRING_TO_CSTR(q, h, "timeout")) {
			cell *c1 = deref(q, FIRST_ARG(h), h_ctx);

			if (!is_number(c1))
				return throw_error(q, c1, h_ctx, "type_error", "read_option");

			timeout = is_float(c1) ? get_float(c1) : get_smallint(c1);
		} else
			return throw_error(q, h, h_ctx, "domain_error", "read_option");

		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_orig_ctx);
		p2_ctx = q->latest_ctx;
	}

	if (is_var(p2))
		return throw_error(q, p2_orig, p2_orig_ctx, "instantiation_error", "get_option");

	if (!is_nil(p2))
		return throw_error(q, p2_orig, p2_orig_ctx, "type_error", "list");

	if (!q->retry) {
		pl_int tmo_ms = timeout >= 0 ? (pl_int)(timeout * 1000) : -1;
		q->msg_deadline = (tmo_ms >= 0) ? (wall_time_in_usec() / 1000) + tmo_ms : 0;
	}

	const uint64_t deadline = q->msg_deadline;

	while (!q->halt && !q->abort) {
		scheduler *s = sched_get(q);
		if (s) sched_lock(s);

		task_msg *m = list_front(&q->mailbox);
		const frame *f = GET_CURR_FRAME();

		while (m) {
			CHECKED(push_choice(q), if (s) sched_unlock(s));
			cell *tmp = import_term(q, m->c, q->st.cur_ctx);
			CHECKED(tmp, if (s) sched_unlock(s));

			if (unify(q, p1, p1_ctx, tmp, q->st.cur_ctx)) {
				q->cur_task_qid = m->from_qid;
				list_remove(&q->mailbox, m);
				if (s) sched_unlock(s);
				unshare_cells(m->c, m->c->num_cells);
				TPL_free(m);
				drop_choice(q);
				return true;
			}

			retry_choice(q);
			m = list_next(m);
		}

		if (s) sched_unlock(s);

		uint64_t now = wall_time_in_usec() / 1000;

		if (deadline && (now >= deadline))
			return false;

		uint64_t left = deadline ? deadline - now : 0;
		uint64_t nap = TASK_RECV_POLL_MS;

		if (deadline && (nap > left))
			nap = left;

		if (nap < 1)
			nap = 1;

		if (q->is_task)
			return do_yield(q, (int)nap);

		msleep((int)nap);
	}

	return false;
}

builtins g_tasks_bifs[] =
{
	{"task_self", 1, bif_task_self_1, "-integer", false, false, BLAH},
	{"send", 2, bif_send_2, "+integer,+term", false, false, BLAH},
	{"recv", 1, bif_recv_1, "?term", false, false, BLAH},
	{"recv", 2, bif_recv_2, "?term,+list", false, false, BLAH},
	{"task_create", 2, bif_task_create_2, ":callable,-integer", false, false, BLAH},
	{"task_cancel", 1, bif_task_cancel_1, "+integer", false, false, BLAH},

	{"call_task", 1, bif_call_task_n, ":callable", false, false, BLAH},
	{"call_task", 2, bif_call_task_n, ":callable,?term", false, false, BLAH},
	{"call_task", 3, bif_call_task_n, ":callable,?term,?term", false, false, BLAH},
	{"call_task", 4, bif_call_task_n, ":callable,?term,?term,?term", false, false, BLAH},
	{"call_task", 5, bif_call_task_n, ":callable,?term,?term,?term,?term", false, false, BLAH},
	{"call_task", 6, bif_call_task_n, ":callable,?term,?term,?term,?term,?term", false, false, BLAH},
	{"call_task", 7, bif_call_task_n, ":callable,?term,?term,?term,?term,?term,?term", false, false, BLAH},
	{"call_task", 8, bif_call_task_n, ":callable,?term,?term,?term,?term,?term,?term,?term", false, false, BLAH},

	{"end_wait", 0, bif_end_wait_0, NULL, false, false, BLAH},
	{"wait", 0, bif_wait_0, NULL, false, false, BLAH},
	{"yield", 0, bif_yield_0, NULL, false, false, BLAH},
	{"fork", 0, bif_fork_0, NULL, false, false, BLAH},

	{"$cancel_future", 1, bif_sys_cancel_future_1, "+integer", false, false, BLAH},
	{"$set_future", 1, bif_sys_set_future_1, "+integer", false, false, BLAH},

	{0}
};
