#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "module.h"
#include "network.h"
#include "parser.h"
#include "prolog.h"
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
#else
static void msleep(int ms)
{
	struct timespec tv = {0};
	tv.tv_sec = (ms) / 1000;
	tv.tv_nsec = ((ms) % 1000) * 1000 * 1000;
	nanosleep(&tv, &tv);
}
#endif

#define Trace(p1,p2,p3,p4) if (q->trace /*&& !consulting*/) trace_call(p1,p2,p3,p4)

#define DEBUG_MATCH if (0)

#ifdef INDEX_PROFILE

// Deliberately process-global and opt-in: this is diagnostic accounting for
// one workload, not query state. It reports which dynamic predicate lookups
// lose selectivity after an indexing change.

#define INDEX_PROFILE_ROWS 1024

typedef struct {
	const predicate *pr;
	char name[64];
	unsigned arity;
	uint64_t calls, linear, idx0, idx1, idx2, candidates;
} index_profile_row;

static index_profile_row g_index_profile[INDEX_PROFILE_ROWS];
static bool g_index_profile_registered;

static index_profile_row *index_profile_get(const predicate *pr)
{
	unsigned i = ((size_t)pr >> 4) % INDEX_PROFILE_ROWS;

	for (unsigned probes = 0; probes < INDEX_PROFILE_ROWS; probes++) {
		index_profile_row *r = &g_index_profile[i];

		if (!r->pr) {
			r->pr = pr;
			r->arity = get_arity(&pr->key);
			snprintf(r->name, sizeof(r->name), "%s", C_STR(pr->m, &pr->key));
			return r;
		}

		if (r->pr == pr)
			return r;

		i = (i + 1) % INDEX_PROFILE_ROWS;
	}

	return NULL;
}

static void index_profile_report(void)
{
	for (unsigned rank = 0; rank < 20; rank++) {
		index_profile_row *best = NULL;

		for (unsigned i = 0; i < INDEX_PROFILE_ROWS; i++) {
			index_profile_row *r = &g_index_profile[i];
			if (r->pr && (!best || (r->candidates > best->candidates)))
				best = r;
		}

		if (!best || !best->candidates)
			break;

		fprintf(stderr, "INDEX_PROFILE %s/%u calls=%llu linear=%llu idx0=%llu idx1=%llu idx2=%llu candidates=%llu avg=%.1f\n",
			best->name, best->arity,
			(unsigned long long)best->calls, (unsigned long long)best->linear,
			(unsigned long long)best->idx0, (unsigned long long)best->idx1,
			(unsigned long long)best->idx2, (unsigned long long)best->candidates,
			best->calls ? (double)best->candidates / best->calls : 0.0);

		best->candidates = 0;
	}
}

#define INDEX_PROFILE_START(pr) index_profile_row *ip = index_profile_get(pr); if (ip) ip->calls++
#define INDEX_PROFILE_MODE(ip, n) if (ip) (ip)->n++
#define INDEX_PROFILE_CANDIDATES(ip, n) if (ip) ((ip)->candidates += (n))

#else

#define INDEX_PROFILE_START(pr)
#define INDEX_PROFILE_MODE(ip, n)
#define INDEX_PROFILE_CANDIDATES(ip, n)

#endif

static const unsigned INITIAL_NBR_QUEUE_CELLS = 100;

// The query's scratch parser, built on first use.
//
// Only number_codes/2 and number_chars/2 ever ask for one, but a parser is
// 39KB - a 16000-byte name pool and six MAX_VARS arrays - and one was built
// for every query ever created, which made it much the largest thing a query
// allocated. Most queries never parse anything at all: a task, a findall, a
// goal expansion, a format/3 sub-query.
//
// Built against the module the query was created in rather than the one in
// force when it is first needed, because parser_create() fixes p->m and
// p->flags and parser_reset() never revisits them. So a scratch parse means
// the same thing wherever execution has got to, exactly as before.

parser *query_parser(query *q)
{
	if (q->p)
		return q->p;

	q->p = parser_create(q->parser_m);

	if (q->p)
		q->p->q = q;

	return q->p;
}

// Depths are entered one at a time and nearly always just the one, so this
// grows to fit rather than doubling. A new depth starts with no buffer and
// the initial size as its hint; alloc_queuen() does the rest.

bool ensure_queuen(query *q, unsigned qnum)
{
	if (qnum < q->queues_alloc)
		return true;

	if (qnum >= MAX_QUEUES)
		return false;

	unsigned wanted = qnum + 1;
	qbuf *queues = TPL_realloc(q->queues, wanted * sizeof(qbuf));

	if (!queues)
		return false;

	for (unsigned i = q->queues_alloc; i < wanted; i++) {
		queues[i].queue = NULL;
		queues[i].qp = queues[i].qcnt = 0;
		queues[i].q_size = INITIAL_NBR_QUEUE_CELLS;
	}

	q->queues = queues;
	q->queues_alloc = wanted;
	return true;
}
static const unsigned INITIAL_NBR_HEAP_CELLS = 100;
static const unsigned INITIAL_NBR_SLOTS = 1000;
static const unsigned INITIAL_NBR_TRAILS = 1000;
static const unsigned INITIAL_NBR_CHOICES = 100;
static const unsigned INITIAL_NBR_FRAMES = 100;
static const unsigned INITIAL_NBR_CELLS = 100;

int g_tpl_interrupt = 0;

typedef enum { CALL, EXIT, REDO, NEXT, FAIL } box_t;

#define YIELD_INTERVAL 100000	// Goal interval between yield checks
#define REDUCE_PRESSURE 1
#define PRESSURE_FACTOR 4
#define TRACE_MEM 0
#define OOM_RESERVE_SIZE (1024U * 1024U)

static void rearm_oom_reserve(query *q)
{
	if (!q->oom_reserve)
		q->oom_reserve = TPL_malloc(OOM_RESERVE_SIZE);
}

void release_oom_reserve(query *q)
{
	TPL_free(q->oom_reserve);
	q->oom_reserve = NULL;
}

void dump_term(query *q, const char *s, const cell *c)
{
	unsigned num_cells = c->num_cells;
	printf("*** %s\n", s);

	for (unsigned i = 0; i < num_cells; i++, c++) {
		printf("    ");
		printf("[%u] tag=%u ", i, c->tag);

		if (is_atom(c))
			printf("%s ", C_STR(q, c));
		else if (is_var(c))
			printf("_%u ", c->var_num);
		else if (is_compound(c))
			printf("%s/%u ", C_STR(q, c), get_arity(c));

		printf("\n");
	}
}

static void trace_call(query *q, cell *c, pl_ctx c_ctx, box_t box)
{
	if (!c || is_empty(c))
		return;

	if (is_builtin(c) && c->bif_ptr && !c->bif_ptr->fn)
		return;

#ifndef DEBUG
	if (c->val_off == g_sys_succeed_on_retry_s)
		return;

	if (c->val_off == g_sys_fail_on_retry_s)
		return;

	if (c->val_off == g_sys_jump_s)
		return;

	if (c->val_off == g_sys_drop_barrier_s)
		return;

	if (c->val_off == g_sys_block_catcher_s)
		return;

	if (c->val_off == g_sys_catch_s)
		return;

	if (c->val_off == g_sys_catch_exit_s)
		return;

	if (c->val_off == g_conjunction_s)
		return;

	if (c->val_off == g_disjunction_s)
		return;
#endif

	if (box == CALL)
		box = q->retry?REDO:CALL;

	const char *src = C_STR(q, c);
	frame *f = GET_CURR_FRAME();
	q->step++;
	SB(pr);

	SB_sprintf(pr, "[%u:%s:%"PRIu64":f%u:fp%u:cp%u:sp%u:tp%u:hp%u/%u:nr%d] ",
		q->my_chan,
		q->st.m->name,
		q->step,
		q->st.cur_ctx, q->st.fp, q->st.cp, q->st.sp,
		q->st.tp,
		q->st.hp, q->st.hp_num,
		f->no_recov
		);

	SB_sprintf(pr, "%s ",
		box == CALL ? "CALL" :
		box == EXIT ? "EXIT" :
		box == REDO ? "REDO" :
		box == NEXT ? "NEXT" :
		box == FAIL ? "FAIL":
		"????");

	q->quoted = true;
	q->double_quotes = true;
	char *dst = print_term_to_strbuf(q, c, c_ctx, -1);
	SB_strcat(pr, dst);
	TPL_free(dst);
	q->quoted = false;
	q->double_quotes = false;
	SB_sprintf(pr, "%s", "\n");
	src = SB_cstr(pr);
	size_t srclen = srclen = SB_strlen(pr);
	int n = q->pl->current_error;
	stream *str = &q->pl->streams[n];
	tpl_write(src, srclen, str);
	SB_free(pr);
	if (++q->vgen == 0) q->vgen = 1;

	if (q->creep) {
		msleep(250);
	}
}

void check_pressure(query *q)
{
#if REDUCE_PRESSURE
	if (q->tmp_heap && (q->tmph_size > 4000)) {
		TPL_free(q->tmp_heap);
		q->tmp_heap = NULL;
		q->tmph_size = 1000;
	}

#if TRACE_MEM
	printf("*** q->st.sp=%u, q->slots_size=%u\n", (unsigned)q->st.sp, (unsigned)q->slots_size);
#endif
	// A live choicepoint can restore a frame based well above the current sp, so shrinking to fit only sp corrupts it on retry, re issue #1151.
	if (!q->st.cp && (q->st.sp < (q->slots_size / 2))) {
		unsigned new_size = q->st.sp < INITIAL_NBR_SLOTS ? INITIAL_NBR_SLOTS : q->st.sp + 1;
		q->slots_size = alloc_grow(q, (void**)&q->slots, sizeof(slot), new_size, new_size*5/4);
	}
#endif
}

static bool check_choice(query *q)
{
	choice_page *a = q->choice_current;

	if (a && (q->choice_next < (a->entries + a->page_size)))
		return true;

	if (a && a->next) {
		q->choice_current = a = a->next;
		q->choice_next = a->entries;
		return true;
	}

	a = TPL_calloc(1, sizeof(choice_page));
	if (!a) {
		q->oom = q->error = true;
		return false;
	}

	a->page_size = q->choice_current ? q->choice_current->page_size * 2 : INITIAL_NBR_CHOICES;
	a->entries = TPL_calloc(a->page_size, sizeof(choice));

	if (!a->entries) {
		TPL_free(a);
		q->oom = q->error = true;
		return false;
	}

	a->base = q->st.cp;
	a->prev = q->choice_current;

	if (a->prev)
		a->prev->next = a;
	else
		q->choice_pages = a;

	q->choice_current = a;
	q->choice_next = a->entries;
	return true;
}

bool check_frame(query *q, unsigned max_vars)
{
	CHECKED(check_slot(q, max_vars));
	pl_idx page_idx = q->st.fp >> FRAME_PAGE_SHIFT;

	if (page_idx >= q->frame_pages_size) {
		pl_idx pages = alloc_grow(q, (void**)&q->frame_pages, sizeof(frame *),
			page_idx + 1, (page_idx + 1) * 2);

		if (!pages) {
			q->oom = q->error = true;
			return false;
		}

		memset(q->frame_pages + q->frame_pages_size, 0,
			(pages - q->frame_pages_size) * sizeof(frame *));
		q->frame_pages_size = pages;
	}

	if (!q->frame_pages[page_idx]) {
		frame *frames = TPL_calloc(FRAME_PAGE_SIZE, sizeof(frame));
		if (!frames) {
			q->oom = q->error = true;
			return false;
		}

		for (unsigned i = 0; i < FRAME_PAGE_SIZE; i++)
			frames[i].idx = (page_idx << FRAME_PAGE_SHIFT) + i;

		q->frame_pages[page_idx] = frames;
	}

	frame *f = GET_NEW_FRAME();
	f->max_vars = max_vars;
	f->base = q->st.sp;
	return true;
}

bool check_slot(query *q, unsigned cnt)
{
	if (cnt > UINT32_MAX - 2) {
		q->oom = q->error = true;
		return false;
	}

	cnt += 2;	// Allow some extra

	if (q->st.sp > UINT32_MAX - cnt) {
		q->oom = q->error = true;
		return false;
	}

	pl_idx num = q->st.sp + cnt;

	if (num < q->slots_size)
		return true;

	pl_idx new_slotssize = alloc_grow(q, (void**)&q->slots, sizeof(slot), num+1, num * 2);

	if (!new_slotssize) {
		q->oom = q->error = true;
		return false;
	}

	q->slots_size = new_slotssize;
	return true;
}

bool check_trail(query *q)
{
	trail_page *a = q->trail_current;

	if (a && (q->trail_next < (a->entries + a->page_size)))
		return true;

	if (a && a->next) {
		q->trail_current = a = a->next;
		q->trail_next = a->entries;
		return true;
	}

	a = TPL_calloc(1, sizeof(trail_page));
	if (!a) {
		q->oom = q->error = true;
		return false;
	}

	a->page_size = q->trail_current ? q->trail_current->page_size * 2 : INITIAL_NBR_TRAILS;
	a->entries = TPL_calloc(a->page_size, sizeof(trail));

	if (!a->entries) {
		TPL_free(a);
		q->oom = q->error = true;
		return false;
	}

	a->base = q->st.tp;
	a->prev = q->trail_current;

	if (a->prev)
		a->prev->next = a;
	else
		q->trail_pages = a;

	q->trail_current = a;
	q->trail_next = a->entries;
	return true;
}

trail *get_trail(query *q, pl_idx idx)
{
	trail_page *a = q->trail_current;

	while (a && (idx < a->base))
		a = a->prev;

	while (a && (idx >= (a->base + a->page_size)))
		a = a->next;

	assert(a);
	return a->entries + (idx - a->base);
}

bool undo_on_backtrack(query *q, void *v, enum undo_item type)
{
	undo_item *u = TPL_calloc(1, sizeof(undo_item));
	if (!u) return false;
	u->m = q->st.m;
	u->c = v;

	if (type == UNDO_BBOARD)
		u->is_bboard = true;
	else if (type == UNDO_RULE)
		u ->is_rule = true;
	else
		u->is_cells = true;

	list *undo;

	if (q->st.cp) {
		choice *ch = GET_CURR_CHOICE();
		undo = &ch->undo;
	} else
		undo = &q->undo;

	list_push_back(undo, u);
	return true;
}

void make_call_engine(query *q, cell *tmp, cell *c)
{
	make_end(tmp);
	const frame *f = GET_CURR_FRAME();
	tmp->ret_instr = c + c->num_cells;	// save next as the return instruction
	tmp->chgen = f->chgen;				// ... choice-generation
	tmp->mid = q->st.m->id;				// ... current-module
}

void make_call(query *q, cell *tmp)
{
	make_end(tmp);
	const frame *f = GET_CURR_FRAME();
	cell *c = q->st.instr;
	tmp->ret_instr = c + c->num_cells;	// save next as the return instruction
	tmp->chgen = f->chgen;				// ... choice-generation
	tmp->mid = q->st.m->id;				// ... current-module
}

void make_call_redo(query *q, cell *tmp)
{
	make_end(tmp);
	const frame *f = GET_CURR_FRAME();
	tmp->ret_instr = q->st.instr;		// save the return instruction
	tmp->chgen = f->chgen;				// ... choice-generation
	tmp->mid = q->st.m->id;				// ... current-module
}

cell *prepare_call(query *q, bool noskip, cell *p1, pl_ctx p1_ctx, unsigned extras)
{
	unsigned num_cells = p1->num_cells + extras;
	cell *tmp = alloc_heap(q, num_cells);
	if (!tmp) return NULL;
	q->noskip = noskip;
	dup_cells_by_ref(tmp, p1, p1_ctx, p1->num_cells);
	return tmp;
}

const char *dump_id(const void *k, const void *v, const void *p)
{
	uint64_t id = (uint64_t)(size_t)k;
	static char tmpbuf[1024];
	snprintf(tmpbuf, sizeof(tmpbuf), "%"PRIu64"", id);
	return tmpbuf;
}

static size_t scan_is_chars_list_internal(query *q, cell *l, pl_ctx l_ctx, bool allow_codes, bool *has_var, bool *is_partial, cell **cptr)
{
	*is_partial = *has_var = false;
	size_t is_chars_list = 0;
	cell *save_l = l;
	pl_ctx save_l_ctx = l_ctx;
	bool any1 = false, any2 = false;
	PROLOG_LIST_HANDLER(l);

	while (is_list(l) && (q->st.m->flags.double_quote_chars || allow_codes)) {
		cell *h = PROLOG_LIST_HEAD(l);
		pl_ctx h_ctx = l_ctx;
		slot *e = NULL;
		uint32_t save_vgen = 0;
		int both = 0;
		DEREF_VAR(any1, both, save_vgen, e, e->vgen, h, h_ctx, q->vgen);
		q->suspect = h;

		if (is_var(h)) {
			*has_var = true;
			return 0;
		}

		if (!is_integer(h) && !is_iso_atom(h))
			return 0;

		if (is_integer(h) && !allow_codes)
			return 0;

		if (is_integer(h)) {
			int ch = get_smallint(h);
			char tmp[MAX_BYTES_PER_CODEPOINT+1];
			put_char_utf8(tmp, ch);
			size_t len = len_char_utf8(tmp);
			is_chars_list += len;
		} else {
			const char *src = C_STR(q, h);
			size_t len = len_char_utf8(src);

			if (len != C_STRLEN(q, h))
				return 0;

			is_chars_list += len;
		}

		if (e) e->vgen = save_vgen;
		l = PROLOG_LIST_TAIL(l);
		cell *lsave = l;

		both = 0;
		DEREF_VAR(any2, both, save_vgen, e, e->vgen, l, l_ctx, q->vgen);

		if (both) {
			*is_partial = true;
			save_l = lsave;
			break;
		}
	}

	if (any2 && !*is_partial) {
		cell *l2 = save_l;
		pl_ctx l2_ctx = save_l_ctx;
		PROLOG_LIST_HANDLER(l2);

		while (is_list(l2) && (q->st.m->flags.double_quote_chars || allow_codes)) {
			PROLOG_LIST_HEAD(l2);
			l2 = PROLOG_LIST_TAIL(l2);
			RESTORE_VAR(l2, l2_ctx, l2, l2_ctx, q->vgen);
		}
	}

	if (is_var(l)) {
		*has_var = *is_partial = true;
		if (cptr) *cptr = l;
	} else if ((is_interned(l) || is_string(l) || is_number(l)) && !is_nil(l)) {
		*is_partial = true;
		if (cptr) *cptr = save_l;
	} else if (!is_interned(l) || !is_nil(l))
		is_chars_list = 0;

	return is_chars_list;
}

size_t scan_is_chars_list2(query *q, cell *l, pl_ctx l_ctx, bool allow_codes, bool *has_var, bool *is_partial, cell **cptr)
{
	if (++q->vgen == 0) q->vgen = 1;
	return scan_is_chars_list_internal(q, l, l_ctx, allow_codes, has_var, is_partial, cptr);
}

size_t scan_is_chars_list(query *q, cell *l, pl_ctx l_ctx, bool allow_codes)
{
	bool has_var, is_partial;
	return scan_is_chars_list2(q, l, l_ctx, allow_codes, &has_var, &is_partial, NULL);
}

bool make_slice(query *q, cell *d, const cell *orig, size_t off, size_t n)
{
	if (!n) {
		make_atom(d, g_empty_s);
		return true;
	}

	if (is_slice(orig)) {
		*d = *orig;
		d->val_str += off;
		d->str_len = n;
		return true;
	}

	const char *s = C_STR(q, orig);

	if (is_string(orig))
		return make_stringn(d, s+off, n);

	return make_cstringn(d, s+off, n);
}

#define MAX_LOCAL_VARS (1L<<30)

int create_vars(query *q, unsigned cnt)
{
	frame *f = GET_CURR_FRAME();

	if (!cnt)
		return f->actual_slots;

	// Fail soft: callers use CHECKED() to throw resource_error(memory).
	// Setting oom/error here would make start() abort the query even when
	// catch/3 handles the throw (issue #1094).
	if ((f->actual_slots > MAX_LOCAL_VARS) || (cnt > (MAX_LOCAL_VARS - f->actual_slots)))
		return -1;

	if (!check_slot(q, cnt))
		return -1;

	unsigned var_num = f->actual_slots;

	if (!f->op && ((f->base + f->initial_slots) == q->st.sp)) {
		f->initial_slots += cnt;
	} else if (!f->op) {
		f->op = q->st.sp;
	} else if ((f->op + (f->actual_slots - f->initial_slots)) == q->st.sp) {
	} else {
		pl_idx save_overflow = f->op;
		f->op = q->st.sp;
		pl_idx cnt2 = f->actual_slots - f->initial_slots;

		if (!check_slot(q, cnt2))
			return -1;

		memmove(q->slots+f->op, q->slots+save_overflow, sizeof(slot)*cnt2);
		q->st.sp += cnt2;
	}

	slot *e = get_slot(q, f, f->actual_slots);
	memset(e, 0, sizeof(slot)*cnt);
	q->st.sp += cnt;
	f->actual_slots += cnt;
	return var_num;
}

static void enter_predicate(query *q, predicate *pr)
{
	frame *f = GET_FRAME(q->st.cur_ctx);
	f->dbgen = q->pl->dbgen;
	q->st.pr = pr;

	// Incremental tabling (item 3). Once per CALL, not once per clause
	// tried, and the bit is false for everything unless declared, so
	// the null case is a test on a struct already in cache.

	if (pr->is_incremental)
		tbl_note_predicate_dep(q, pr);

	if (pr->is_dynamic)
		pr->refcnt++;
}

void leave_predicate_and_drop(query *q, predicate *pr, bool is_final)
{
	leave_predicate(q, pr, is_final);
	drop_choice(q);
}

void leave_predicate(query *q, predicate *pr, bool is_final)
{
	if (!pr)
		return;

	q->st.iter = NULL;

	if (!pr->is_dynamic || !pr->refcnt)
		return;

	// Must span the decrement, not just the purge: shrinking it lets a
	// thread run a clause that has been reclaimed. See
	// tests/misc/db_purge_window.pl.

	const bool mt = pr->m->pl->is_multithreaded;

	if (mt)
		prolog_lock_mod(pr->m->pl, pr->m);

	if (--pr->refcnt != 0) {
		if (mt) prolog_unlock_mod(pr->m->pl, pr->m);
		return;
	}

	if (!list_count(&pr->dirty) || pr->is_abolished) {
		if (mt) prolog_unlock_mod(pr->m->pl, pr->m);
		return;
	}

	// Predicate is no longer being used

	//printf("*** leave %u, %s/%u, in_retractall=%d, is_final=%d, retry=%d\n",
	//	(unsigned)list_count(&pr->dirty), C_STR(q, &pr->key), get_arity(&pr->key), q->in_retractall, is_final, q->retry);

	rule *r;
	const frame *f = GET_CURR_FRAME();

	while ((r = list_pop_front(&pr->dirty)) != NULL) {
		predicate_delink(pr, r);

		if (pr->cnt)
			index_remove_clause(pr, r);

		if (q->in_retract && !r->cl.num_vars && q->pl->opt) {
			undo_on_backtrack(q, r, UNDO_RULE);
		} else {
			r->cl.is_deleted = true;
			list_push_back(&q->dirty, r);
		}
	}

	if (pr->idx1 && !pr->cnt) {
		sl_destroy(pr->idx0);
		sl_destroy(pr->idx2);
		sl_destroy(pr->idx1);
		pr->idx0 = pr->idx1 = pr->idx2 = NULL;
		pr->is_var_in_head = false;
		pr->is_var_in_first_arg = false;
		pr->is_var_in_idx2_arg = false;
		pr->idx2_arg = 0;
	} else if (pr->is_var_in_head || pr->is_var_in_first_arg || pr->is_var_in_idx2_arg) {
		// Clauses just left the chain. If the last var-headed one was
		// among them the flags are now stale, and being stale here is
		// one-way: they are only ever set by assert_commit(). Safe to
		// walk - refcnt is 0, so no query is iterating this predicate.

		recheck_var_in_indexed_args(pr);
	}

	if (mt)
		prolog_unlock_mod(pr->m->pl, pr->m);
}

static void query_purge_dirty_list(query *q)
{
	unsigned cnt = 0;
	rule *r;
	const bool mt = q->pl->is_multithreaded;

	// q->dirty can mix rules from different predicates/modules, unlike
	// leave_predicate()'s single pr->m - lock per rule's own owner
	// rather than once for the whole pass.

	for (r = list_front(&q->dirty); r; r = list_next(r)) {
		if (mt) prolog_lock_mod(q->pl, r->owner->m);
		index_remove_clause(r->owner, r);
		if (mt) prolog_unlock_mod(q->pl, r->owner->m);
	}

	while ((r = list_pop_front(&q->dirty)) != NULL) {
		clear_clause(&r->cl);
		TPL_free(r);
		cnt++;
	}

	if (cnt && 0)
		printf("*** query_purge_dirty_list %u\n", cnt);
}

static void trim_trail(query *q, bool reused)
{
	if (q->undo_hi_tp)
		return;

	pl_idx tp;

	if (q->st.cp)  {
		const choice *ch = GET_CURR_CHOICE();
		tp = ch->st.tp;
	} else
		tp = 0;

	while (q->st.tp > tp) {
		const trail *tr = get_trail(q, q->st.tp - 1);

		if (tr->val_ctx != q->st.cur_ctx)
			break;

		if (!reused) {
			const frame *f = GET_FRAME(tr->val_ctx);

			if (f->no_recov) {
				const slot *e = get_slot(q, f, tr->var_num);

				if (is_managed(&e->c))
					break;
			}
		}

		pop_trail(q);
	}
}

static void trim_frame(query *q, const frame *f)
{
	for (unsigned i = 0; i < f->actual_slots; i++) {
		slot *e = get_slot(q, f, i);
		cell *c = &e->c;
		unshare_cell(c);
		memset(e, 0, sizeof(slot));
	}

	q->st.sp -= f->actual_slots;
	q->st.fp = q->st.cur_ctx;
}

// A stale indirect landed on a variable: resolve from the variable's own
// context, since pairing that cell with the indirect's context would have
// set_var() bind the slot the context names rather than the one the
// variable does. Bounded, in case the stale cells point at each other.

cell *deref_stale_indirect(query *q, cell *c, pl_ctx c_ctx)
{
	for (unsigned hops = 0; hops < 64; hops++) {
		if (is_ref(c))
			c_ctx = c->val_ctx;

		slot *e = get_slot(q, GET_FRAME(c_ctx), c->var_num);

		while (is_var(&e->c)) {
			c_ctx = e->c.val_ctx;
			c = &e->c;

			if (is_ref(c))
				c_ctx = c->val_ctx;

			slot *e2 = get_slot(q, GET_FRAME(c_ctx), c->var_num);

			if (e == e2)
				break;

			e = e2;
		}

		if (!is_indirect(&e->c)) {
			q->latest_ctx = c_ctx;
			return is_empty(&e->c) ? c : &e->c;
		}

		if (!is_var(e->c.val_ptr)) {
			q->latest_ctx = e->c.val_ctx;
			return e->c.val_ptr;
		}

		c_ctx = e->c.val_ctx;
		c = e->c.val_ptr;
	}

	q->latest_ctx = c_ctx;
	return c;
}

bool add_trail(query *q, pl_ctx c_ctx, unsigned c_var_nbr, cell *attrs)
{
	if (!check_trail(q))
		return false;

	trail *tr = q->trail_next++;
	q->st.tp++;
	tr->val_ctx = c_ctx;
	tr->var_num = c_var_nbr;
	tr->attrs = attrs;
	return true;
}

void undo_me(query *q)
{
	q->total_retries++;
	const choice *ch = GET_CURR_CHOICE();

	while (q->st.tp > ch->st.tp) {
		const trail *tr = pop_trail(q);
		const frame *f = GET_FRAME(tr->val_ctx);
		slot *e = get_slot(q, f, tr->var_num);
		cell *c = &e->c;
		unshare_cell(c);
		memset(e, 0, sizeof(slot));
		c->val_attrs = tr->attrs;
	}
}

static void try_me(query *q, unsigned num_vars)
{
	frame *f = GET_NEW_FRAME();
	f->initial_slots = f->actual_slots = num_vars;
	q->total_matches++;

	for (unsigned i = 0; i < num_vars; i++) {
		slot *e = get_slot(q, f, i);
		memset(e, 0, sizeof(slot));
	}
}

// Skip the branch join points compile_term() emits on the way to the
// clause end: a bare `true` landing, or a forward `$jump` to one. Both
// are no-ops for machine state (bif_sys_jump_1 only moves q->st.instr),
// so a goal followed by nothing but these is followed by nothing.
// is_end() on the result means the clause is over.

static const cell *skip_landings(const cell *c)
{
	while (!is_end(c)) {
		if (!is_interned(c))
			break;

		if ((c->val_off == g_true_s) && !get_arity(c))
			c += c->num_cells;						// landing
		else if ((c->val_off == g_sys_jump_s) && (get_arity(c) == 1)
			&& is_smallint(c+1) && (get_smallint(c+1) > 0))
			c += get_smallint(c+1);					// jump to a landing
		else
			break;
	}

	return c;
}

static void push_frame(query *q)
{
	const frame *f_cur = GET_CURR_FRAME();
	frame *f_new = GET_NEW_FRAME();
	const cell *next_cell = skip_landings(q->st.instr + q->st.instr->num_cells);

	// Avoid long chains of useless returns...

	if (q->pl->opt && is_end(next_cell) && !next_cell->ret_instr) {
		f_new->prev = f_cur->prev;
		f_new->instr = f_cur->instr;
	} else {
		f_new->prev = q->st.cur_ctx;
		f_new->instr = q->st.instr;
	}

	f_new->op = 0;
	f_new->no_recov = q->no_recov;
	f_new->heap_pinned = false;
	f_new->chgen = ++q->chgen;
	f_new->hp = q->st.hp;
	f_new->hp_num = q->st.hp_num;
	q->st.sp += f_new->actual_slots;
	q->st.cur_ctx = q->st.fp;
	q->st.fp++;
}

// Note: TCO's clause might not be the caller clause... hence passing
// num_vars. Currently restricted to the same predicate though (still?).

static void reuse_frame(query *q, unsigned num_vars)
{
	cell *c_next = q->st.instr + q->st.instr->num_cells;

	// This is if the last call was actually call/n

	if (c_next->val_off == g_sys_drop_barrier_s)
		drop_choice(q);

	// Copy slots from the new frame to the current frame...

	const frame *f_new = GET_NEW_FRAME();
	frame *f_cur = GET_CURR_FRAME();
	f_cur->initial_slots = f_cur->actual_slots = num_vars;
	f_cur->no_recov = false;
	f_cur->heap_pinned = false;

	for (pl_idx i = 0; i < num_vars; i++) {
		const slot *from = get_slot(q, f_new, i);
		slot *to = get_slot(q, f_cur, i);
		unshare_cell(&to->c);
		*to = *from;
	}

	q->st.sp = f_cur->base + f_cur->actual_slots;
	q->st.dbe->tcos++;
	q->total_tcos++;
	q->st.hp = f_cur->hp;
	q->st.hp_num = f_cur->hp_num;
	trim_heap(q);
}

static bool commit_any_choices(const query *q, unsigned skip)
{
	if (q->st.cp <= skip)
		return false;

	const choice *ch = GET_CHOICE(q->st.cp - 1 - skip);
	return ch->st.fp >= q->st.fp;
}

static bool is_last_call(const query *q, bool *has_barrier)
{
	const cell *c = q->st.instr + q->st.instr->num_cells;
	bool barrier = false;

	// call/N plants nothing after the goal but a $drop_barrier, which is
	// bookkeeping that reuse_frame() performs directly.

	if (is_interned(c) && (c->val_off == g_sys_drop_barrier_s)) {
		c += c->num_cells;
		barrier = true;
	}

	if (has_barrier)
		*has_barrier = barrier;

	// Past that, only the branch join points that compile_term() emits
	// on the way to the clause end may be skipped: they do nothing.

	c = skip_landings(c);

	if (!is_end(c))
		return false;

	return barrier || !c->ret_instr;
}

static void commit_frame(query *q, bool head_has_vars)
{
	q->st.dbe->matched++;
	q->total_matched++;

	clause *cl = &q->st.dbe->cl;
	frame *f = GET_CURR_FRAME();
	f->m = q->st.m;

	rule *save_dbe = q->st.dbe;

	bool is_det = !head_has_vars && cl->is_unique
		&& !q->st.pr->is_var_in_head && !q->st.pr->is_var_in_first_arg
		&& !q->st.pr->is_var_in_idx2_arg;
	bool last_match = is_det || cl->is_first_cut || !has_next_key(q)
		|| (is_next_cut(q->st.instr) && cl->is_fact);
	bool tco = false;

#if 0
	if (last_match) {
		fprintf(stderr, "*** q->no_recov=%d, last_match=%d %s/%u, q->st.cur_ctx=%u,q->st.fp=%u\n",
			q->no_recov, last_match,
			C_STR(q, q->st.key), get_arity(q->st.key),
			q->st.cur_ctx, q->st.fp
			);
	}
#endif

	// A frame whose heap another frame now points into cannot be
	// reused: reuse_frame() winds hp back to this frame's own base and
	// trims, which would take those cells with it. q->no_recov cannot
	// carry that on its own - the next unify() clears it, and head
	// unification of this very call is one - so set_var() pins the
	// frame and the pin is what is read here. f->no_recov is not, it
	// being set by every if-then-else, \+, ignore/1 and \= as well
	// (docs/tco-then-branch-report.md, 3), which would cost those
	// their TCO.

	if (!q->no_recov
		&& !f->heap_pinned
		&& last_match
		&& (q->st.fp == (q->st.cur_ctx + 1))
		) {
		bool barrier = false;
		bool tail_recursive = is_recursive_call(q->st.instr) && is_last_call(q, &barrier);
		bool slots_ok = f->initial_slots <= cl->num_vars;
		bool choices = commit_any_choices(q, barrier ? 2 : 1);
		tco = slots_ok && tail_recursive && !choices;

#if 0
		cell *head = get_head(cl->cells);

		fprintf(stderr,
			"*** %s/%u tco=%d,q->no_recov=%d,last_match=%d,is_det=%d,"
			"tail_recursive=%d,slots_ok=%d,choices=%d,"
			"cl->num_vars=%u,f->initial_slots=%u/%u\n",
			C_STR(q, head), get_arity(head),
			tco, q->no_recov, last_match, is_det,
			tail_recursive, slots_ok, choices,
			cl->num_vars, f->initial_slots, f->actual_slots);
#endif
	}

	if (!q->st.dbe->owner->is_builtin)
		q->st.m = q->st.dbe->owner->m;

	const bool reused = tco && q->pl->opt;

	if (reused) {
		Trace(q, get_head(save_dbe->cl.cells), q->st.cur_ctx, EXIT);
		reuse_frame(q, cl->num_vars);
	} else {
		push_frame(q);
	}

	// Read what we still need out of cl BEFORE giving up the reference.
	// leave_predicate() may take the refcount to zero and reclaim, and a
	// concurrent purge can free this very clause the moment it does -
	// leaving the continuation to be read out of freed memory.

	cell *next_instr = cl->alt ? cl->alt : get_body(cl->cells);
	if (!next_instr) next_instr = cl->cells + (cl->cidx-1);

	if (last_match) {
		leave_predicate_and_drop(q, q->st.pr, false);
		trim_trail(q, reused);


	} else {
		choice *ch = GET_CURR_CHOICE();
		ch->st.dbe = q->st.dbe;
		ch->gen = q->chgen;
	}

	q->st.instr = next_instr;
	q->st.iter = NULL;
}

static void undo_list_drain(list *l)
{
	undo_item *u;

	while ((u = list_pop_back(l)) != NULL) {
		if (u->is_bboard)
			sl_del(u->m->keyval, u->key);
		else if (u->is_rule) {
			clear_clause(&u->r->cl);
			TPL_free(u->r);
		} else {
			unshare_cells(u->c, u->c->num_cells);
			TPL_free(u->c);
		}

		TPL_free(u);
	}
}

// Release the prefetch a choicepoint owns.
//
// run_state is snapshotted whole into every choice raised after
// find_key(), so the handle is aliased by all of them and only the
// choice it was built for may free it. iter_owner names that slot, and a
// choice's slot is simply its own index - which is why this takes the
// choice rather than a caller-computed cp. The three call sites had
// spelled that index two different ways (q->st.cp in retry_choice, where
// the decrement comes after; q->st.cp - 1 in drop_choice, where it comes
// before), which looked like a discrepancy and was not.

static void release_prefetch(query *q, choice *ch, pl_idx cp)
{
	if (!ch->st.iter || (ch->st.iter_owner != cp))
		return;

	// q->st may still alias it - defuse before the free.

	if (q->st.iter == ch->st.iter)
		q->st.iter = NULL;

	sl_done(ch->st.iter);
	ch->st.iter = NULL;
}

int retry_choice(query *q)
{
	while (q->st.cp) {
		undo_me(q);
		pl_idx cp = q->st.cp - 1;
		choice *ch = GET_CURR_CHOICE();
		pop_choice(q);
		undo_list_drain(&ch->undo);

		q->st = ch->st;

		frame *f = GET_CURR_FRAME();
		f->dbgen = ch->dbgen;
		f->chgen = ch->chgen;
		f->initial_slots = ch->initial_slots;
		f->actual_slots = ch->actual_slots;
		f->op = ch->op;
		f->base = ch->base;

		if (ch->reset)
			continue;

		if (ch->catchme_exception || ch->fail_on_retry) {
			// Choice abandoned without drop_choice(); free its prefetch.
			release_prefetch(q, ch, cp);
			leave_predicate(q, ch->st.pr, true);
			continue;
		}

		if (!ch->register_cleanup && q->noretry) {
			release_prefetch(q, ch, cp);
			leave_predicate(q, ch->st.pr, true);
			continue;
		}

		if (ch->register_cleanup && q->noretry)
			q->noretry = false;

		trim_heap(q);

		if (ch->succeed_on_retry) {
			q->st.instr += ch->skip;
			return ch->skip ? -2 : -1;
		}

		return 1;
	}

	trim_heap(q);
	return 0;
}

void drop_choice(query *q)
{
	if (!q->st.cp)
		return;

	pl_idx cp = q->st.cp - 1;
	choice *ch = GET_CHOICE(cp);

	release_prefetch(q, ch, cp);

	list *undo;

	if (q->st.cp > 1) {
		choice *ch_prev = GET_PREV_CHOICE();
		undo = &ch_prev->undo;
	} else
		undo = &q->undo;

	undo_item *u;

	while ((u = list_pop_front(&ch->undo)) != NULL)
		list_push_back(undo, u);

	pop_choice(q);
}

bool push_choice(query *q)
{
	CHECKED(check_choice(q));
	const frame *f = GET_CURR_FRAME();
	choice *ch = q->choice_next++;
	ch->skip = 0;
	ch->st = q->st;
	q->st.cp++;

	list_init(&ch->undo);
	ch->dbgen = f->dbgen;
	ch->chgen = ch->gen = f->chgen;
	ch->initial_slots = f->initial_slots;
	ch->actual_slots = f->actual_slots;
	ch->op = f->op;
	ch->base = f->base;

	ch->catchme_retry =
		ch->catchme_exception = ch->barrier = ch->register_cleanup =
		ch->block_catcher = ch->fail_on_retry =
		ch->succeed_on_retry = ch->reset = false;

	return true;
}

bool push_succeed_on_retry(query *q, pl_idx skip)
{
	CHECKED(push_choice(q));
	choice *ch = GET_CURR_CHOICE();
	ch->succeed_on_retry = true;
	ch->skip = skip;
	return true;
}

// A barrier is used when making a call, it sets a new
// choice generation so that normal cuts are contained.

bool push_barrier(query *q)
{
	CHECKED(push_choice(q));
	choice *ch = GET_CURR_CHOICE();
	frame *f = GET_CURR_FRAME();
	ch->gen = f->chgen = ++q->chgen;
	ch->barrier = true;
	return true;
}

bool push_succeed_on_retry_with_barrier(query *q, pl_idx skip)
{
	// FIXME: memory waste, but see docs/norecov.md
	frame *f = GET_CURR_FRAME();
	f->no_recov = true;
	CHECKED(push_barrier(q));
	choice *ch = GET_CURR_CHOICE();
	ch->succeed_on_retry = true;
	ch->skip = skip;
	return true;
}

bool push_fail_on_retry_with_barrier(query *q)
{
	CHECKED(push_barrier(q));
	choice *ch = GET_CURR_CHOICE();
	ch->fail_on_retry = true;
	return true;
}

bool push_reset_handler(query *q)
{
	CHECKED(push_fail_on_retry_with_barrier(q));
	choice *ch = GET_CURR_CHOICE();
	ch->reset = true;
	return true;
}

bool push_catcher(query *q, enum q_retry retry)
{
	CHECKED(push_barrier(q));
	choice *ch = GET_CURR_CHOICE();

	if (retry == QUERY_RETRY)
		ch->catchme_retry = true;
	else if (retry == QUERY_EXCEPTION)
		ch->catchme_exception = true;

	rearm_oom_reserve(q);

	return true;
}

// If the call is det then the barrier can be dropped...

bool drop_barrier(query *q, pl_idx cp)
{
	if ((q->st.cp-1) != cp) {
		// The call's choices still need the barrier, but a cut after the call must reach the caller's.

		if (cp < q->st.cp) {
			frame *f = GET_CURR_FRAME();
			f->chgen = GET_CHOICE(cp)->chgen;
		}

		return false;
	}

	const choice *ch = GET_CURR_CHOICE();
	frame *f = GET_CURR_FRAME();
	f->chgen = ch->chgen;
	drop_choice(q);
	return true;
}

void cut(query *q)
{
	const frame *f = GET_CURR_FRAME();

	while (q->st.cp) {
		choice *ch = GET_CURR_CHOICE();

		// A normal cut can't break out of a barrier...

		if (ch->barrier) {
			if (ch->gen <= f->chgen)
				break;
		} else {
			if (ch->gen < f->chgen)
				break;
		}

		// Done...

		leave_predicate(q, ch->st.pr, false);
		drop_choice(q);

		if (ch->register_cleanup && !ch->fail_on_retry) {
			cell *c = FIRST_ARG(ch->st.instr);
			pl_ctx c_ctx = ch->st.cur_ctx;
			c = deref(q, c, c_ctx);
			c_ctx = q->latest_ctx;
			do_cleanup(q, c, c_ctx);
			break;
		}
	}
}

static bool resume_any_choices(const query *q, const frame *f)
{
	if (!q->st.cp)
		return false;

	const choice *ch = GET_CURR_CHOICE();
	return ch->gen >= f->chgen;
}

// Resume at next goal in previous clause...

static bool resume_frame(query *q)
{
	const frame *f = GET_CURR_FRAME();

	if (f->prev == CTX_NUL)
		return false;

#if 0
	printf("*** q->st.cur_ctx=%d, f->no_recov=%d, any_choices=%d\n",
		(unsigned)q->st.cur_ctx,
		(unsigned)f->no_recov, (unsigned)resume_any_choices(q, f));
#endif
	Trace(q, get_head(f->instr), f->prev, EXIT);

	// Call is followed by !: drop callee-internal choices the cut will
	// kill so trim_frame can run. Stop at barriers (cut handles those,
	// including setup_call_cleanup) and at the parent clause choice
	// (gen < f->chgen) - that stays until the real cut.

	if (f->instr && is_next_cut(f->instr)) {
		while (q->st.cp) {
			choice *ch = GET_CURR_CHOICE();

			if (ch->barrier || (ch->gen < f->chgen))
				break;

			leave_predicate(q, ch->st.pr, false);
			drop_choice(q);
		}
	}

	if (q->pl->opt
		&& !f->no_recov
		&& (q->st.fp == (q->st.cur_ctx + 1))
		&& !resume_any_choices(q, f)
		) {
		q->total_recovs++;
		q->st.hp = f->hp;
		q->st.hp_num = f->hp_num;
		trim_frame(q, f);
	}

	q->st.instr = f->instr;
	q->st.cur_ctx = f->prev;
	f = GET_CURR_FRAME();
	q->st.m = f->m;
	return true;
}

// Proceed to next goal in current clause...

static void proceed(query *q)
{
	if (!q->noskip)
		q->st.instr += q->st.instr->num_cells;

	q->noskip = false;

	if (!is_end(q->st.instr))
		return;

	if (q->st.instr->ret_instr) {
		frame *f = GET_CURR_FRAME();
		f->chgen = q->st.instr->chgen;
		q->st.m = module_by_id(q->pl, q->st.instr->mid);
	}

	q->st.instr = q->st.instr->ret_instr;
}

static bool can_view(query *q, uint64_t dbgen, const rule *r)
{
	if (r->cl.is_deleted)
		return false;

	if (r->dbgen_created > dbgen)
		return false;

	if (r->dbgen_retracted && (r->dbgen_retracted <= dbgen))
		return false;

	return true;
}

static void setup_key(query *q)
{
	cell *save_arg1 = FIRST_ARG(q->st.key), *save_arg2 = NULL;
	cell *arg1 = deref(q, save_arg1, q->st.key_ctx);

	q->st.karg1_is_ground = !is_var(arg1);
	q->st.karg1_is_atomic = is_atomic(arg1);

	if (get_arity(q->st.key) > 1) {
		cell *arg2 = deref(q, save_arg2 = NEXT_ARG(save_arg1), q->st.key_ctx);
		q->st.karg2_is_ground = arg2 && !is_var(arg2);
		q->st.karg2_is_atomic = arg2 && is_atomic(arg2);
	}

	if (get_arity(q->st.key) > 2) {
		cell *arg3 = deref(q, NEXT_ARG(save_arg2), q->st.key_ctx);
		q->st.karg3_is_ground = arg3 && !is_var(arg3);
		q->st.karg3_is_atomic = arg3 && is_atomic(arg3);
	}
}

static void next_key(query *q)
{
	if (q->st.iter_single) {
		q->st.iter_single = false;
		q->st.dbe = NULL;
		return;
	}

	if (!q->st.iter) {
		q->st.dbe = q->st.dbe->next;
		return;
	}

	if (!sl_next(q->st.iter, (void*)&q->st.dbe)) {
		q->st.dbe = NULL;
		q->st.iter = NULL;
	}
}

bool has_next_key(query *q)
{
	if (q->st.iter_single)
		return false;

	if (q->st.iter)
		return sl_has_next(q->st.iter, NULL);

	if (!q->st.dbe->next)
		return false;

	if (!get_arity(q->st.key))
		return true;

	if (q->st.dbe->cl.is_unique) {
		if ((get_arity(q->st.key) == 1) && q->st.karg1_is_atomic)
			return false;

		if ((get_arity(q->st.key) == 2) && q->st.karg1_is_atomic && q->st.karg2_is_atomic)
			return false;

		if ((get_arity(q->st.key) == 3) && q->st.karg1_is_atomic && q->st.karg2_is_atomic && q->st.karg3_is_atomic)
			return false;
	}

	cell *karg1 = FIRST_ARG(q->st.key), *karg2 = NULL, *karg3 = NULL;
	cell *save_arg1 = karg1;

	if (q->st.karg1_is_ground)
		karg1 = deref(q, save_arg1, q->st.key_ctx);

	if (q->st.karg2_is_ground)
		karg2 = deref(q, NEXT_ARG(save_arg1), q->st.key_ctx);

	if (q->st.karg3_is_ground)
		karg3 = deref(q, NEXT_ARG(NEXT_ARG(save_arg1)), q->st.key_ctx);

	//DUMP_TERM("key ", q->st.key, q->st.key_ctx, 1);

	for (rule *next = q->st.dbe->next; next; next = next->next) {
		cell *dkey = next->cl.cells;

		if ((dkey->val_off == g_neck_s) && (get_arity(dkey) == 2))
			dkey++;

		//DUMP_TERM("next", dkey, q->st.cur_ctx, 0);

		if (karg1) {
			if (index_cmpkey(karg1, FIRST_ARG(dkey), q->st.m, NULL) != 0)
				continue;
		}

		if (karg2) {
			if (index_cmpkey(karg2, NEXT_ARG(FIRST_ARG(dkey)), q->st.m, NULL) != 0)
				continue;
		}

		if (karg3) {
			if (index_cmpkey(karg3, NEXT_ARG(NEXT_ARG(FIRST_ARG(dkey))), q->st.m, NULL) != 0)
				continue;
		}

		if (index_cmpkey(q->st.key, dkey, q->st.m, NULL) == 0)
			return true;
	}

	return false;
}

static bool expand_meta_predicate(query *q, predicate *pr)
{
	uint32_t arity = get_arity(q->st.key);
	cell *tmp = alloc_heap(q, q->st.key->num_cells*3);	// allocate max possible
	CHECKED(tmp);
	cell *save_tmp = tmp;
	tmp += copy_cells(tmp, q->st.key, 1);

	// Expand module-sensitive args...

	for (cell *k = q->st.key+1, *m = pr->meta_args+1; arity--; k += k->num_cells, m += m->num_cells) {
		cell *k0 = deref(q, k, q->st.key_ctx);

		if ((get_arity(k0) == 2) && (k0->val_off == g_colon_s) && is_atom(FIRST_ARG(k0)))
			;
		else if (!is_interned(k0) || is_iso_list(k0))
			;
		else if (is_interned(k0) && ((k0->val_off == g_call_s) || (k0->val_off == g_once_s) || (k0->val_off == g_ignore_s)))
			;
		else if (is_interned(m) && (m->val_off == g_colon_s)) {
			make_instr(tmp, g_colon_s, bif_iso_qualify_2, 2, 1+k->num_cells);
			SET_OP(tmp, OP_XFY); tmp++;
			make_atom(tmp++, new_atom(q->pl, q->st.m->name));
		} else if (is_smallint(m) && is_positive(m) && (get_smallint(m) <= 9)) {
			make_instr(tmp, g_colon_s, bif_iso_qualify_2, 2, 1+k->num_cells);
			SET_OP(tmp, OP_XFY); tmp++;
			make_atom(tmp++, new_atom(q->pl, q->st.m->name));
		}

		tmp += dup_cells_by_ref(tmp, k, q->st.key_ctx, k->num_cells);
	}

	save_tmp->num_cells = tmp - save_tmp;
	q->st.key = save_tmp;
	return true;
}

int g_index_check = 0;
unsigned long g_index_check_lookups = 0, g_index_check_bad = 0;

static bool in_candidates(const rule **got, unsigned num_got, const rule *c)
{
	for (unsigned i = 0; i < num_got; i++) {
		if (got[i] == c)
			return true;
	}

	return false;
}

static void index_check(query *q, predicate *pr, cell *goal, cell *key,
	const rule **got, unsigned num_got, int idx_arg)
{
	const uint64_t dbgen = q->pl->dbgen;
	unsigned missing = 0;

	g_index_check_lookups++;

	for (const rule *c = pr->head; c; c = c->next) {
		if (!can_view(q, dbgen, c))
			continue;

		cell *ch = get_head(((rule*)c)->cl.cells);
		cell *ck = ch;

		if (idx_arg >= 0 && get_arity(ch))
			ck = get_nth_arg(ch, idx_arg);

		if (index_cmpkey(ck, key, q->st.m, NULL) != 0)
			continue;

		if (in_candidates(got, num_got, c))
			continue;

		if (!missing) {
			fprintf(stderr, "\n*** index-check FAILED for %s/%u (%s)\n",
				C_STR(q, &pr->key), get_arity(&pr->key),
				idx_arg < 0 ? "head" : "argument");
			fprintf(stderr, "***   goal   ");
			DUMP_TERM("", goal, q->st.cur_ctx, 1);
		}

		fprintf(stderr, "***   MISSING db_id=%llu  ",
			(unsigned long long)c->db_id);
		DUMP_TERM("", ch, q->st.cur_ctx, 1);

		sliter *probe = sl_find_key(idx_arg < 0 ? pr->idx0 : idx_arg ? pr->idx2 : pr->idx1, ck);
		const rule *probe_r;
		bool self = false;

		while (probe && sl_next_key(probe, (void*)&probe_r)) {
			if (probe_r == c) {
				self = true;
				break;
			}
		}

		if (probe)
			sl_done(probe);

		fprintf(stderr, "***     reachable by its own key: %s\n",
			self ? "YES (ordering ok, query descent went astray)"
			     : "NO (mis-filed on insert, or lost on removal)");
		fprintf(stderr, "***     cmp(clause,goal)=%d\n",
			index_cmpkey(ck, key, q->st.m, NULL));
		missing++;
	}

	if (missing) {
		fprintf(stderr, "***   indexed set had %u entr%s, %u missing\n",
			num_got, num_got == 1 ? "y" : "ies", missing);
		fprintf(stderr, "***   predicate has %u clauses, head=%s idx1=%s idx2(arg%u)=%s\n",
			(unsigned)pr->cnt, pr->idx0 ? "yes" : "no", pr->idx1 ? "yes" : "no", pr->idx2_arg + 1,
			pr->idx2 ? "yes" : "no");

		g_index_check_bad++;
	}
}

static bool find_key(query *q, predicate *pr, cell *key, pl_ctx key_ctx)
{
	q->st.iter = NULL;
	q->st.iter_single = false;
	q->st.karg1_is_ground = q->st.karg2_is_ground = q->st.karg3_is_ground = false;
	q->st.karg1_is_atomic = q->st.karg2_is_atomic = q->st.karg3_is_atomic = false;
	q->st.key = key;
	q->st.key_ctx = key_ctx;

	if (!pr->idx1) {
		q->st.dbe = pr->head;

		if (get_arity(key)) {
			if (pr->is_meta_predicate) {
				if (!expand_meta_predicate(q, pr))
					return false;
			}

			setup_key(q);
		}

		return true;
	}

	INDEX_PROFILE_START(pr);

	if (pr->is_meta_predicate) {
		if (!expand_meta_predicate(q, pr))
			return false;

		key = q->st.key;
		key_ctx = q->st.cur_ctx;
	} else {
		CHECKED(init_tmp_heap(q));
		key = clone_term_to_tmp(q, key, key_ctx);
		key_ctx = q->st.cur_ctx;
	}

	cell *arg1 = get_arity(key) ? FIRST_ARG(key) : NULL;
	skiplist *idx = pr->idx1;
	cell *goal = key;
	int idx_arg = 0;

	if (pr->idx0 && !pr->is_var_in_head && is_ground(key)) {
		idx = pr->idx0;
		idx_arg = -1;
		INDEX_PROFILE_MODE(ip, idx0);
	} else if (pr->idx2 && (pr->idx2_arg == 1) && !pr->is_var_in_idx2_arg
			&& is_interned(&pr->key) && !strcmp(C_STR(q, &pr->key), "$predicate_property")) {
		cell *arg2 = get_nth_arg(key, pr->idx2_arg);

		if (!is_var(arg2)) {
			key = arg2;
			idx = pr->idx2;
			idx_arg = pr->idx2_arg;
			INDEX_PROFILE_MODE(ip, idx2);
		} else if (arg1 && (is_var(arg1) || pr->is_var_in_first_arg)) {
			INDEX_PROFILE_MODE(ip, linear);
			INDEX_PROFILE_CANDIDATES(ip, pr->cnt);
			q->st.dbe = pr->head;
			return true;
		} else if (arg1) {
			key = arg1;
			INDEX_PROFILE_MODE(ip, idx1);
		}
	} else if (arg1 && (is_var(arg1) || pr->is_var_in_first_arg)) {
		if (!pr->idx2 || pr->is_var_in_idx2_arg) {
			INDEX_PROFILE_MODE(ip, linear);
			INDEX_PROFILE_CANDIDATES(ip, pr->cnt);
			q->st.dbe = pr->head;
			return true;
		}

		cell *arg2 = get_nth_arg(key, pr->idx2_arg);

		if (is_var(arg2)) {
			INDEX_PROFILE_MODE(ip, linear);
			INDEX_PROFILE_CANDIDATES(ip, pr->cnt);
			q->st.dbe = pr->head;
			return true;
		}

		key = arg2;
		idx = pr->idx2;
		idx_arg = pr->idx2_arg;
		INDEX_PROFILE_MODE(ip, idx2);
	} else if (arg1) {
		// idx1 is keyed on Arg1 only (see assert_commit).
		key = arg1;
		INDEX_PROFILE_MODE(ip, idx1);
	}

	if (!arg1) {
		INDEX_PROFILE_MODE(ip, idx1);
	}

	q->st.dbe = NULL;
	sliter *iter;

	if (!(iter = sl_find_key(idx, key))) {
		if (g_index_check)
			index_check(q, pr, goal, key, NULL, 0, idx_arg);

		return false;
	}

	// If the index search has found just one (definite) solution
	// then we can use it with no problems. If more than one then
	// results must be returned in database order, so prefetch all
	// the results and return them sorted as an iterator...

	skiplist *tmp_idx = NULL;
	const rule *first = NULL;
	const rule *r;
	const rule **got = NULL;
	unsigned num_got = 0, max_got = 0;

	while (sl_next_key(iter, (void*)&r)) {
		INDEX_PROFILE_CANDIDATES(ip, 1);
		if (g_index_check) {
			if (num_got == max_got) {
				max_got = max_got ? max_got * 2 : 32;
				got = TPL_realloc(got, max_got * sizeof(*got));
			}

			got[num_got++] = r;
		}

		if (!first) {
			first = r;
			continue;
		}

		if (!tmp_idx) {
			tmp_idx = sl_create(NULL, NULL, NULL);
			sl_set_tmp(tmp_idx);
			sl_app(tmp_idx, (void*)(size_t)first->db_id, (void*)first);
		}

		sl_app(tmp_idx, (void*)(size_t)r->db_id, (void*)r);
	}

	sl_done(iter);

	if (g_index_check) {
		index_check(q, pr, goal, key, got, num_got, idx_arg);
		TPL_free(got);
	}

	if (!first)
		return false;

	if (!tmp_idx) {
		q->st.dbe = (rule*)first;
		q->st.iter = NULL;
		q->st.iter_single = true;
		return true;
	}

	// More than one: results must come back in database order, so the
	// prefetch stands.

	iter = sl_first(tmp_idx);

	if (!sl_next(iter, (void*)&q->st.dbe)) {
		sl_done(iter);
		return false;
	}

	q->st.iter = iter;
	q->st.iter_owner = q->st.cp;
	return true;
}

// Match HEAD :- BODY.

bool match_rule(query *q, cell *p1, pl_ctx p1_ctx, enum clause_type is_retract)
{
	if (!q->retry) {
		cell *c = deref(q, get_head(p1), p1_ctx);
		pl_ctx c_ctx = q->latest_ctx;
		predicate *pr = NULL;

		if (is_interned(c))
			pr = c->match;
		else if (is_cstring(c))
			convert_to_literal(q->st.m, c);

		if (pr && pr->is_abolished)
			pr = search_predicate(q->st.m, c);

		if (!pr || is_evaluable(c) || is_builtin(c)) {
			pr = search_predicate(q->st.m, c);

			if (pr)
				c->match = pr;
		}

		if (!pr) {
			bool found = false;

			if (get_builtin_term(q->st.m, c, &found, NULL), found)
				return throw_error(q, c, c_ctx, "permission_error", "modify,static_procedure");

			q->st.dbe = NULL;
			return false;
		}

		if (pr->alias) {
			c->val_off = pr->alias->key.val_off;
			pr = pr->alias;
		}

		if (!pr->is_dynamic)
			return throw_error(q, c, c_ctx, "permission_error", "modify,static_procedure");

		// Enter before finding: find_key() reads pr->head and parks a
		// rule pointer in q->st.dbe, and it is the refcount taken by
		// enter_predicate() that stops leave_predicate() reclaiming
		// what it parked.
		enter_predicate(q, pr);
		find_key(q, pr, c, c_ctx);
	} else {
		next_key(q);
	}

	if (!q->st.dbe) {
		leave_predicate(q, q->st.pr, true);
		return false;
	}

	const frame *f = GET_CURR_FRAME();
	cell *p1_body = deref(q, get_logical_body(p1), p1_ctx);
	cell *orig_p1 = p1;

	for (; q->st.dbe; q->st.dbe = q->st.dbe->next) {
		if (!can_view(q, f->dbgen, q->st.dbe))
			continue;

		CHECKED(push_choice(q));
		clause *cl = &q->st.dbe->cl;
		cell *c = cl->cells;
		bool needs_true = false;
		p1 = orig_p1;

		cell *tmp = import_term(q, c, q->st.cur_ctx);
		CHECKED(tmp);
		c = tmp;
		cell *head = get_head(c);
		const cell *c_body = get_logical_body(c);

		if (p1_body && is_var(p1_body) && !c_body) {
			p1 = deref(q, get_head(p1), p1_ctx);
			c = get_head(tmp);
			needs_true = true;
		}

		if (unify(q, p1, p1_ctx, c, q->st.cur_ctx)) {
			if (q->did_throw)
				return true;

			int ok;

			if (needs_true) {
				p1_body = deref(q, p1_body, p1_ctx);
				pl_ctx p1_body_ctx = q->latest_ctx;
				cell tmp;
				make_instr(&tmp, g_true_s, bif_iso_true_0, 0, 0);
				ok = unify(q, p1_body, p1_body_ctx, &tmp, q->st.cur_ctx);
				if (q->did_throw)
					return true;
			} else
				ok = true;

			return ok;
		}

		retry_choice(q);
	}

	leave_predicate_and_drop(q, q->st.pr, true);
	return false;
}

// Match HEAD.
// Match HEAD :- true.

bool match_clause(query *q, cell *p1, pl_ctx p1_ctx, cell **ret_body, enum clause_type is_retract)
{
	if (!q->retry) {
		cell *c = p1;
		pl_ctx c_ctx = p1_ctx;
		predicate *pr = NULL;

		if (is_interned(c))
			pr = c->match;
		else if (is_cstring(c))
			convert_to_literal(q->st.m, c);

		if (pr && pr->is_abolished)
			pr = search_predicate(q->st.m, c);

		if (!pr || is_evaluable(c) || is_builtin(c)) {
			pr = search_predicate(q->st.m, c);

			if (pr)
				c->match = pr;
		}

		if (!pr) {
			bool found = false;

			if (get_builtin_term(q->st.m, p1, &found, NULL), found) {
				if (is_retract != DO_CLAUSE)
					return throw_error(q, p1, p1_ctx, "permission_error", "modify,static_procedure");
				else
					return throw_error(q, p1, p1_ctx, "permission_error", "access,private_procedure");
			}

			q->st.dbe = NULL;
			return false;
		}

		if (pr->alias) {
			c->val_off = pr->alias->key.val_off;
			pr = pr->alias;
		}

		if (!pr->is_dynamic) {
			if (is_retract == DO_CLAUSE) {
				if (!q->access_private)
					return throw_error(q, p1, p1_ctx, "permission_error", "access,private_procedure");
			} else
				return throw_error(q, p1, p1_ctx, "permission_error", "modify,static_procedure");
		}

		// Enter before finding: find_key() reads pr->head and parks a
		// rule pointer in q->st.dbe, and it is the refcount taken by
		// enter_predicate() that stops leave_predicate() reclaiming
		// what it parked.
		enter_predicate(q, pr);
		find_key(q, pr, c, c_ctx);
	} else {
		next_key(q);
	}

	if (!q->st.dbe) {
		leave_predicate(q, q->st.pr, true);
		return false;
	}

	const frame *f = GET_CURR_FRAME();

	for (; q->st.dbe; q->st.dbe = q->st.dbe->next) {
		if (!can_view(q, f->dbgen, q->st.dbe))
			continue;

		clause *cl = &q->st.dbe->cl;
		cell *c = cl->cells;
		cell *body = get_logical_body(c);

		// retract(HEAD) should ignore rules (and directives)

		if ((is_retract == DO_RETRACT) && body)
			continue;

		CHECKED(push_choice(q));
		cell *tmp = import_term(q, c, q->st.cur_ctx);
		CHECKED(tmp);
		cell *head = get_head(tmp);
		body = get_body(tmp);

		if (unify(q, p1, p1_ctx, head, q->st.cur_ctx)) {
			if (q->did_throw)
				return true;

			if (ret_body)
				*ret_body = body;

			return true;
		}

		retry_choice(q);
	}

	leave_predicate(q, q->st.pr, true);
	return false;
}

bool match_head(query *q)
{
	if (!q->retry) {
		cell *c = q->st.instr;
		pl_ctx c_ctx = q->st.cur_ctx;
		predicate *pr = NULL;

		if (is_interned(c))
			pr = c->match;
		else if (is_cstring(c)) {
			convert_to_literal(q->st.m, c);
		}

		if (pr && pr->is_abolished)
			pr = search_predicate(q->st.m, c);

		if (!pr || is_evaluable(c) || is_builtin(c)) {
			pr = search_predicate(q->st.m, c);

			if (pr) {
				c->match = pr;
				// Keep NEXT_CUT / TCO hints; only drop builtin tags.
				c->flags &= ~(FLAG_INTERNED_BUILTIN | FLAG_INTERNED_EVALUABLE);
			}
		}

		if (!pr) {
			if (!is_end(c) && !(is_interned(c) && !strcmp(C_STR(q, c), "initialization"))) {
				if (q->st.m->flags.unknown == UNK_ERROR)
					return throw_error(q, c, c_ctx, "existence_error", "procedure");
				return false;
			} else
				q->error = true;

			return false;
		}

		if (pr->alias) {
			c->val_off = pr->alias->key.val_off;
			pr = pr->alias;
		}

		// A predicate that exists in the module but has no clauses and is
		// neither dynamic nor multifile (e.g. a static predicate left empty
		// after a file reconsult removed its last clause) must be treated as
		// an undefined procedure and honor the `unknown` flag, rather than
		// silently failing.
		if (!pr->head && !pr->is_dynamic && !pr->is_multifile && !pr->is_discontiguous && !pr->is_builtin) {
			if (!is_end(c) && !(is_interned(c) && !strcmp(C_STR(q, c), "initialization"))) {
				if (q->st.m->flags.unknown == UNK_ERROR)
					return throw_error(q, c, c_ctx, "existence_error", "procedure");
				return false;
			} else {
				q->error = true;
				return false;
			}
		}

		// Enter before finding: find_key() reads pr->head and parks a
		// rule pointer in q->st.dbe, and it is the refcount taken by
		// enter_predicate() that stops leave_predicate() reclaiming
		// what it parked.
		enter_predicate(q, pr);
		find_key(q, pr, c, c_ctx);
	} else
		next_key(q);

	if (!q->st.dbe) {
		leave_predicate(q, q->st.pr, true);
		return false;
	}

	CHECKED(check_frame(q, q->st.pr->max_vars));
	CHECKED(push_choice(q));
	const frame *f = GET_CURR_FRAME();

	for (; q->st.dbe; next_key(q)) {
		if (!can_view(q, f->dbgen, q->st.dbe))
			continue;

		clause *cl = &q->st.dbe->cl;
		cell *head = get_head(cl->cells);

		if (cl->num_vars > q->st.pr->max_vars)
			CHECKED(check_slot(q, q->st.pr->max_vars=cl->num_vars));

		try_me(q, cl->num_vars);
		q->st.dbe->attempted++;

		if (unify(q, q->st.key, q->st.key_ctx, head, q->st.fp)) {
			if (q->did_throw)
				return true;

			const bool head_has_vars = q->has_vars;

			if (q->error)
				break;

			commit_frame(q, head_has_vars);
			return true;
		}

		undo_me(q);
	}

	leave_predicate_and_drop(q, q->st.pr, true);
	return false;
}

static bool any_outstanding_choices(query *q)
{
	while (q->st.cp) {
		const choice *ch = GET_CURR_CHOICE();

		if (!ch->barrier)
			break;

		pop_choice(q);
	}

	return q->st.cp > 0;
}

void do_cleanup(query *q, cell *c, pl_ctx c_ctx)
{
	cell *tmp = prepare_call(q, CALL_NOSKIP, c, c_ctx, 4);
	ENSURE(tmp);
	pl_idx num_cells = c->num_cells;
	make_instr(tmp+num_cells++, g_cut_s, bif_iso_cut_0, 0, 0);
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_call(q, tmp+num_cells);
	q->st.instr = tmp;
}

static bool consultall(query *q, cell *l, pl_ctx l_ctx)
{
	if (is_cyclic_term(q, l, l_ctx))
		return throw_error(q, l, l_ctx, "type_error", "callable");

	PROLOG_LIST_HANDLER(l);

	while (is_list(l)) {
		cell *h = PROLOG_LIST_HEAD(l);
		h = deref(q, h, l_ctx);
		pl_ctx h_ctx = q->latest_ctx;

		if (is_list(h)) {
			if (consultall(q, h, h_ctx) != true)
				return false;
		} else {
			do_load_file(q, h, h_ctx);
		}

		l = PROLOG_LIST_TAIL(l);
		l = deref(q, l, l_ctx);
		l_ctx = q->latest_ctx;
	}

	return true;
}

bool start(query *q)
{
	q->yielded = false;
	bool done = false;

	while (!done && !q->error) {
		if (interrupt_pending(q)) {
			switch (check_interrupt(q)) {
				case 1: return true;
				case -1: q->retry = true;
				default: continue;
			}
		}

#if USE_THREADS
		if (q->thread_ptr) {
			thread *t = q->thread_ptr;

			if (list_count(&t->signals)) {
				do_signal(q, t);
				proceed(q);
			}
		}
#endif

		if (q->retry) {
			switch (retry_choice(q)) {
				case 0: done = true; continue;
				case -1: proceed(q); goto MORE;
				case -2: q->retry = false; break;
			}
		}

		if (!is_callable(q->st.instr)
			&& (q->run_init || !is_list(q->st.instr))) {
			cell *p1 = deref(q, q->st.instr, q->st.cur_ctx);
			pl_ctx p1_ctx = q->latest_ctx;

			if (!bif_call_0(q, p1, p1_ctx)) {
				if (is_var(p1))
					break;

				continue;
			}
		}

		Trace(q, q->st.instr, q->st.cur_ctx, CALL);
		cell *save_cell = q->st.instr;
		pl_ctx save_ctx = q->st.cur_ctx;
		q->cycle_error = q->did_throw = false;
		q->total_goals++;

		if (is_builtin(q->st.instr)) {
			q->total_inferences++;
			bool status;

#if USE_FFI
			if (q->st.instr->bif_ptr->ffi) {
				if (q->st.instr->bif_ptr->evaluable)
					status = wrap_ffi_function(q, q->st.instr->bif_ptr);
				else
					status = wrap_ffi_predicate(q, q->st.instr->bif_ptr);
			} else
#endif
				status = q->st.instr->bif_ptr->fn(q);

			if (q->retry == QUERY_NOOP) {
				q->retry = QUERY_OK;
				continue;
			}

			// An unhandled throw has already drained every choicepoint,
			// so there is no goal left to advance past. In an engine the
			// bottom barrier then restores the instruction pointer it
			// captured before the goal was installed - a NULL one, which
			// proceed() dereferenced. MORE handles a NULL instr already.

			if (q->did_throw) {
				// An abort leaves q->error clear, so stop here rather than resume wherever unwinding left off.

				if (q->abort && !q->error)
					break;

				if (q->st.instr)
					proceed(q);

				goto MORE;
			}

			if (!(q->total_goals % YIELD_INTERVAL)) {
				q->s_cnt = 0;

				if (!(q->s_cnt++ % 10000))
					check_pressure(q);

				if (q->yield_at && !q->run_hook) {
					uint64_t now = wall_time_in_usec() / 1000;

					if (now > q->yield_at)  {
						do_yield_then(q, status);
						break;
					}
				}
			}

			if (!status || q->abort) {
				Trace(q, q->st.instr, q->st.cur_ctx, FAIL);
				q->retry = QUERY_RETRY;

				if (q->yielded)
					break;

				q->total_backtracks++;
				continue;
			}

			if (q->run_hook)
				do_post_unify_hook(q, true);

			Trace(q, save_cell, save_ctx, EXIT);
			proceed(q);
		} else if (!q->run_init && is_list(q->st.instr)) {
			if (!consultall(q, q->st.instr, q->st.cur_ctx)) {
				Trace(q, q->st.instr, q->st.cur_ctx, FAIL);
				q->retry = QUERY_RETRY;
				q->total_backtracks++;
				continue;
			}

			Trace(q, save_cell, save_ctx, EXIT);
			proceed(q);
		} else {
			q->total_inferences++;

			if (!match_head(q)) {
				Trace(q, q->st.instr, q->st.cur_ctx, FAIL);
				q->retry = QUERY_RETRY;
				q->total_backtracks++;
				continue;
			}

			if (q->did_throw) {
				if (q->abort && !q->error)
					break;

				if (q->st.instr)
					proceed(q);

				goto MORE;
			}

			if (q->run_hook)
				do_post_unify_hook(q, false);
		}

		MORE:

		q->retry = QUERY_OK;

		while (!q->st.instr || is_end(q->st.instr)) {
			if (resume_frame(q)) {
				proceed(q);
				continue;
			}

			if (q->top && !q->run_init && any_outstanding_choices(q)) {
				if (!check_redo(q))
					break;

				q->status = true;
				return true;
			}

			done = q->status = true;
			break;
		}

		if (q->oom) {
			q->error = true;
			printf("\nresource_error(memory). %%query terminated\n");
			break;
		}
	}

	if (q->halt)
		q->error = false;
	else if (q->do_dump_vars && !q->abort && q->status && !q->error)
		dump_vars(q, false);

	return true;
}

bool execute(query *q, cell *cells, unsigned num_vars)
{
	q->retry = q->halt = q->error = q->abort = false;
	q->pl->did_dump_vars = false;
	q->st.instr = cells;
	q->st.sp = num_vars;
	q->is_redo = false;

	// There is an initial frame (fp=0), so this
	// to the next available frame...

	q->st.fp = 1;

	frame *f = GET_FRAME(0);
	f->initial_slots = f->actual_slots = num_vars;
	f->dbgen = ++q->pl->dbgen;
	return start(q);
}

void query_destroy(query *q)
{
	if (!q)
		return;

	q->done = true;

	// Off the registry before anything else, so a lookup from another
	// thread can never resolve to a query that is mid-teardown. Safe to
	// call unconditionally: unregister_task()/drain_mailbox() are no-ops
	// for a query that never registered (transient sub-queries, most
	// queries in a single-threaded program), and cheap ones - not worth
	// gating behind is_task when the callee already returns at once for
	// anything that never called task_self/1.

	unregister_task(q);
	drain_mailbox(q);

	for (page *a = q->heap_pages; a;) {
		cell *c = a->cells;

		for (pl_idx i = 0; i < a->idx; i++, c++)
			unshare_cell(c);

		page *save = a;
		a = a->next;
		TPL_free(save->cells);
		TPL_free(save);
	}

	slot *e = q->slots;

	for (pl_idx i = 0; i < q->st.sp; i++, e++) {
		cell *c = &e->c;
		unshare_cell(c);
	}

	for (unsigned i = 0; i < q->queues_alloc; i++) {
		cell *c = q->queues[i].queue;
		for (pl_idx j = 0; j < q->queues[i].qp; j++, c++)
			unshare_cell(c);

		TPL_free(q->queues[i].queue);
	}

	TPL_free(q->queues);

	// Unlink first, destroy second: the queues are shared now, so a
	// task still sitting in one would be left dangling by the free
	// below. query_destroy() recurses, and each level unlinks its own.

	sched_release(q);

	while (q->tasks) {
		query *task = q->tasks->next;
		query_destroy(q->tasks);
		q->tasks = task;
	}

	// Choicepoints still live at teardown hold undo items of their own.
	// Draining q->undo alone left them behind, so a query that halted -
	// or simply succeeded - with choicepoints outstanding leaked
	// whatever they were holding. Deepest first, the order backtracking
	// would have taken.

	for (pl_idx i = q->st.cp; i > 0; i--)
		undo_list_drain(&GET_CHOICE(i - 1)->undo);

	undo_list_drain(&q->undo);

	TPL_free(q->tab1);
	TPL_free(q->tab2);
	TPL_free(q->ignores);
	mp_int_clear(&q->tmp_ival);
	mp_rat_clear(&q->tmp_irat);
	query_purge_dirty_list(q);
	parser_destroy(q->p);
	for (trail_page *a = q->trail_pages; a;) {
		trail_page *save = a;
		a = a->next;
		TPL_free(save->entries);
		TPL_free(save);
	}
	for (choice_page *a = q->choice_pages; a;) {
		choice_page *save = a;
		a = a->next;
		TPL_free(save->entries);
		TPL_free(save);
	}
	TPL_free(q->slots);
	for (pl_idx i = 0; i < q->frame_pages_size; i++)
		TPL_free(q->frame_pages[i]);
	TPL_free(q->frame_pages);
	TPL_free(q->tmp_heap);
	TPL_free(q->tabs);
	TPL_free(q->unify_seen);
	release_oom_reserve(q);

	if (q->owns_top) {
		parser_destroy(q->top);
		q->top = NULL;
	}

	release_pl_terms(q);			// the embedding API's term handles
	TPL_free(q->terms);

	q->pl->q_cnt--;
	TPL_free(q);
}

static query *query_create_(module *m, bool is_toplevel)
{
	static pl_atomic uint64_t g_query_id = 0;

#ifdef INDEX_PROFILE
	if (!g_index_profile_registered) {
		g_index_profile_registered = true;
		atexit(index_profile_report);
	}
#endif

	query *q = TPL_calloc(1, sizeof(query));

	if (!q)
		return NULL;

	q->parser_m = m;

	const bool is_main_root = !g_query_id;
	q->qid = g_query_id++;
	q->pl = m->pl;
	q->pl->q_cnt++;

	if (is_main_root)
		m->pl->main_thread->q = q;

	q->st.m = m;
	q->trace = m->pl->trace;
	q->flags = m->flags;
	q->get_started = wall_time_in_usec();
	q->cpu_time = q->time_cpu_last_started = q->st.cpu_time = cpu_time_in_usec();
	q->ops_dirty = true;
	q->max_depth = m->pl->def_max_depth;
	q->vgen = 1;
	q->dump_var_num = -1;
	q->dump_var_ctx = -1;
	q->double_quotes = false;

#ifndef __wasi__
	q->rand_seed = getpid() + g_query_id;
#else
	q->rand_seed = clock() + g_query_id;
#endif

	//if (is_threaded) q->trace = 1;

	mp_int_init(&q->tmp_ival);
	mp_rat_init(&q->tmp_irat);

	// Allocate these now...

	q->slots_size = INITIAL_NBR_SLOTS;

	q->frame_pages_size = 1;

	// Undo what the setup above touched in the prolog state, or a failed
	// query would leave a dangling main_thread->q and a bumped q_cnt.

	#define BAIL_OUT() {									\
		if (is_main_root) m->pl->main_thread->q = NULL;		\
		q->pl->q_cnt--;										\
		mp_int_clear(&q->tmp_ival);							\
		mp_rat_clear(&q->tmp_irat);							\
		if (q->frame_pages) TPL_free(q->frame_pages[0]);	\
		TPL_free(q->frame_pages);							\
		TPL_free(q);										\
		return NULL;										\
	}

	q->frame_pages = TPL_calloc(q->frame_pages_size, sizeof(frame *));

	if (!q->frame_pages)
		BAIL_OUT();

	q->frame_pages[0] = TPL_calloc(FRAME_PAGE_SIZE, sizeof(frame));

	if (!q->frame_pages[0])
		BAIL_OUT();

	for (unsigned i = 0; i < FRAME_PAGE_SIZE; i++)
		q->frame_pages[0][i].idx = i;

	q->slots = TPL_calloc(q->slots_size, sizeof(slot));

	if (!q->slots)
		BAIL_OUT();

	#undef BAIL_OUT

	// Allocate these later as needed...

	q->heap_size = INITIAL_NBR_HEAP_CELLS;
	q->tmph_size = INITIAL_NBR_CELLS;


	frame *f = GET_CURR_FRAME();
	f->prev = CTX_NUL;

	rearm_oom_reserve(q);
	clear_write_options(q);
	return q;
}

query *query_create(module *m)
{
	return query_create_(m, true);
}

query *query_create_threaded(module *m)
{
	query *t = query_create_(m, false);

	if (!t)
		return NULL;

	t->is_thread = true;
	return t;
}

query *query_create_subquery(query *q, cell *instr)
{
	query *subq = query_create_(q->st.m, false);
	if (!subq) return NULL;
	subq->parent = q;
	subq->thread_ptr = q->thread_ptr;
	subq->st.fp = 1;
	subq->top = q->top;

	cell *tmp = prepare_call(subq, false, instr, q->st.cur_ctx, 1);
	pl_idx num_cells = tmp->num_cells;
	make_end(tmp+num_cells);
	subq->st.instr = tmp;

	frame *fsrc = GET_FRAME(q->st.cur_ctx);
	frame *fdst = get_frame(subq, 0);
	fdst->initial_slots = fdst->actual_slots = fsrc->actual_slots;
	fdst->dbgen = ++q->pl->dbgen;
	subq->st.sp = fdst->actual_slots;
	return subq;
}

query *query_create_task(query *q, cell *instr)
{
	query *t = query_create_subquery(q, instr);
	if (!t) return NULL;
	t->is_task = true;
	return t;
}

// For a goal that has already been cloned and rebased into a numbering
// of its own. query_create_subquery() copies by reference against the
// caller's context, and a context is just a frame index - meaningless in
// a query with its own frames, which is why a caller's bindings never
// reached the task. Here the cells are taken as they stand and the
// frame is sized from the goal itself, the way execute() does it for a
// thread.

query *query_create_task_rebased(query *q, cell *instr, unsigned num_vars)
{
	query *subq = query_create_(q->st.m, false);
	if (!subq) return NULL;
	subq->parent = q;

	// Inherit the thread: a task belongs to the run queue of whichever
	// thread object spawned it, not to the main thread's.

	subq->thread_ptr = q->thread_ptr;
	subq->st.fp = 1;
	subq->top = q->top;
	subq->is_task = true;

	pl_idx num_cells = instr->num_cells;
	cell *tmp = alloc_heap(subq, num_cells+1);

	if (!tmp) {
		query_destroy(subq);
		return NULL;
	}

	dup_cells(tmp, instr, num_cells);
	make_end(tmp+num_cells);
	subq->st.instr = tmp;

	frame *fdst = get_frame(subq, 0);
	fdst->initial_slots = fdst->actual_slots = num_vars;
	fdst->dbgen = ++q->pl->dbgen;
	subq->st.sp = num_vars;
	return subq;
}
