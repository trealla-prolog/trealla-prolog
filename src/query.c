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
	uint64_t calls, linear, idx1, idx2, idx3, candidates;
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

		fprintf(stderr, "INDEX_PROFILE %s/%u calls=%llu linear=%llu idx1=%llu idx2=%llu idx3=%llu candidates=%llu avg=%.1f\n",
			best->name, best->arity,
			(unsigned long long)best->calls, (unsigned long long)best->linear,
			(unsigned long long)best->idx1,
			(unsigned long long)best->idx2, (unsigned long long)best->idx3,
			(unsigned long long)best->candidates,
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

#define YIELD_INTERVAL 100000	// Goal interval between yield and pressure checks
#define REDUCE_PRESSURE 1
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
		q->st.cur_ctx, q->st.fp, q->st.cp, slot_index(q, q->st.sp),
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

static void free_slot_pages(slot_page *a)
{
	while (a) {
		slot_page *save = a;
		a = a->next;
		TPL_free(save->slots);
		TPL_free(save);
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

		for (unsigned i = 0; i < FRAME_PAGE_SIZE; i++) {
			frames[i].idx = (page_idx << FRAME_PAGE_SHIFT) + i;
			frames[i].slots = frames[i].ovf = q->slot_pages->slots;
		}

		q->frame_pages[page_idx] = frames;
	}

	frame *f = GET_NEW_FRAME();
	f->max_vars = max_vars;
	f->slots = q->st.sp;
	return true;
}

// A too-small next page stays in the list past the new one, as a stale frame above fp can still point into it.

static bool next_slot_page(query *q, unsigned cnt)
{
	slot_page *a = q->st.sp_page, *b = a->next;

	if (!b || ((size_t)(b->end - b->slots) <= cnt)) {
		slot *slots = NULL;
		size_t n = alloc_grow(q, (void**)&slots, sizeof(slot), (size_t)cnt + 1, (size_t)(a->end - a->slots) * 2);
		b = n ? TPL_calloc(1, sizeof(slot_page)) : NULL;

		if (!b) {
			TPL_free(slots);
			q->oom = q->error = true;
			return false;
		}

		b->slots = slots;
		b->end = slots + n;
		b->prev = a;
		b->next = a->next;

		if (b->next)
			b->next->prev = b;

		a->next = b;
	}

	a->used = q->st.sp - a->slots;
	b->base = a->base + a->used;
	q->st.sp_page = b;
	q->st.sp = b->slots;
	return true;
}

// Make room for a run of cnt slots at sp, which moves to the start of the next page if its own is short.

bool check_slot(query *q, unsigned cnt)
{
	if (cnt > UINT32_MAX - 2) {
		q->oom = q->error = true;
		return false;
	}

	cnt += 2;	// Allow some extra

	if ((size_t)(q->st.sp_page->end - q->st.sp) > cnt)
		return true;

	return next_slot_page(q, cnt);
}

static inline void check_slots_highwater(query *q)
{
	pl_idx n = q->st.sp_page->base + (pl_idx)(q->st.sp - q->st.sp_page->slots);

	if (n > q->hw_slots)
		q->hw_slots = n;
}

// A slot's index as if the live slots were one array, skipped page tails left out, which is what names a variable when printing.

pl_idx slot_index(const query *q, const slot *e)
{
	pl_idx n = 0;

	for (const slot_page *a = q->slot_pages; a; a = a->next) {
		if ((e >= a->slots) && (e <= a->end))
			return n + (pl_idx)(e - a->slots);

		n += a->used;
	}

	return n;
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

// An item goes on the current choicepoint, or on the query when there is
// none, and a cut promotes it outwards rather than dropping it.

static undo_item *push_undo_item(query *q)
{
	undo_item *u = TPL_calloc(1, sizeof(undo_item));
	if (!u) return NULL;
	u->m = q->st.m;

	list *undo;

	if (q->st.cp) {
		choice *ch = GET_CURR_CHOICE();
		undo = &ch->undo;
	} else
		undo = &q->undo;

	list_push_back(undo, u);
	return u;
}

bool undo_on_backtrack(query *q, void *v, enum undo_item type)
{
	undo_item *u = push_undo_item(q);
	if (!u) return false;
	u->c = v;

	if (type == UNDO_BBOARD)
		u->is_bboard = true;
	else if (type == UNDO_RULE)
		u ->is_rule = true;
	else
		u->is_cells = true;

	return true;
}

// close/1 does not unmap: a slice carries no refcount, so the mapping has to
// outlive the stream for a term that holds one to stay good. Backtracking over
// the open/4 undoes every binding made since, so nothing can reach it then.

bool undo_mmap_on_backtrack(query *q, void *addr, size_t len)
{
	undo_item *u = push_undo_item(q);
	if (!u) return false;
	u->addr = addr;
	u->mmap_len = len;
	u->is_mmap = true;
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
	const bool no_ovf = f->ovf == q->slot_pages->slots;

	// A choicepoint made since this frame restores only its own frame's layout, yet rewinds sp under a run moved up here.

	if (q->st.cp && (GET_CURR_CHOICE()->st.fp > q->st.cur_ctx)
		&& !add_trail(q, q->st.cur_ctx, TRAIL_FRAME_LAYOUT | f->actual_slots, no_ovf ? NULL : (cell*)f->ovf))
		return -1;

	if (no_ovf && ((f->slots + f->initial_slots) == q->st.sp)) {
		f->initial_slots += cnt;
	} else if (no_ovf) {
		f->ovf = q->st.sp;
	} else if ((f->ovf + (f->actual_slots - f->initial_slots)) == q->st.sp) {
	} else {
		const slot *save_overflow = f->ovf;
		pl_idx cnt2 = f->actual_slots - f->initial_slots;

		// The overflow moves up to sp whole, so the room has to hold it and the new slots together.

		if (!check_slot(q, cnt2 + cnt))
			return -1;

		f->ovf = q->st.sp;
		memmove(f->ovf, save_overflow, sizeof(slot)*cnt2);
		q->st.sp += cnt2;
	}

	slot *e = get_slot(q, f, f->actual_slots);
	memset(e, 0, sizeof(slot)*cnt);
	q->st.sp += cnt;
	f->actual_slots += cnt;
	check_slots_highwater(q);
	return var_num;
}

static void enter_predicate(query *q, predicate *pr)
{
	frame *f = GET_FRAME(q->st.cur_ctx);
	q->st.pr = pr;

	// Incremental tabling (item 3). Once per CALL, not once per clause
	// tried, and the bit is false for everything unless declared, so
	// the null case is a test on a struct already in cache.

	if (pr->is_incremental)
		tbl_note_predicate_dep(q, pr);

	if (!pr->is_dynamic) {
		f->dbgen = q->pl->dbgen;
		return;
	}

	// Under the lock a leave counts under, so a drain's count of the readers present is exact.

	const bool mt = q->pl->is_multithreaded;

	if (mt)
		prolog_lock_mod(pr->m->pl, pr->m);

	f->dbgen = q->pl->dbgen;
	q->st.pr_dbgen = f->dbgen;
	pr->refcnt++;

	if (mt)
		prolog_unlock_mod(pr->m->pl, pr->m);
}

void leave_predicate_and_drop(query *q, predicate *pr, bool is_final)
{
	leave_predicate(q, pr, q->st.pr_dbgen, is_final);
	drop_choice(q);
}

// Retracted clauses leave the chain while readers are still about, as far as the logical update view
// allows. A reader that entered before a clause was retracted may still see it (can_view()), so the
// clause can come out only once every such reader has gone. A drain bumps the generation to G and
// counts the readers present, all of which entered before G; when the last of them leaves, no reader
// can see a clause retracted at or before G, and those come out of the chain. Nothing is freed here:
// that still waits for the count to reach zero, because a query goes on running a clause body, and
// holding terms out of a clause, after it has left the predicate (tests/misc/db_purge_window.pl).

static void drain_complete(predicate *pr)
{
	rule *r;

	while ((r = list_front(&pr->dirty)) != NULL) {
		if (r->dbgen_retracted > pr->drain_gen)
			break;

		list_pop_front(&pr->dirty);
		predicate_delink(pr, r);
		list_push_back(&pr->delinked, r);
	}

	pr->drain_gen = 0;
	pr->drain_old = 0;
}

static void reclaim_rule(query *q, predicate *pr, rule *r)
{
	if (pr->cnt)
		index_remove_clause(pr, r);

	// Leave the undo path alone. Rules on it are freed with clear_clause() only, while
	// everything on q->dirty pays index_remove_clause() at teardown - an sl_rem() per rule
	// against an index that can hold hundreds of thousands of entries with many sharing a
	// key. Moving rules from the first to the second cost giso's full tester 80 seconds.

	if (q->in_retract && !r->cl.num_vars && q->pl->opt) {
		undo_on_backtrack(q, r, UNDO_RULE);
		q->dirty_cnt++;
	} else {
		r->cl.is_deleted = true;
		list_push_back(&q->dirty, r);
		q->dirty_cnt++;
	}
}

void leave_predicate(query *q, predicate *pr, uint64_t dbgen, bool is_final)
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

	if (pr->drain_gen && (dbgen < pr->drain_gen) && (pr->drain_old > 0))
		pr->drain_old--;

	if (--pr->refcnt != 0) {
		if (pr->drain_gen && !pr->drain_old)
			drain_complete(pr);

		if (!pr->drain_gen && list_count(&pr->dirty) && !pr->is_abolished) {
			pr->drain_gen = ++pr->m->pl->dbgen;
			pr->drain_old = pr->refcnt;
		}

		if (mt) prolog_unlock_mod(pr->m->pl, pr->m);
		return;
	}

	pr->drain_gen = 0;
	pr->drain_old = 0;

	if ((!list_count(&pr->dirty) && !list_count(&pr->delinked)) || pr->is_abolished) {
		if (mt) prolog_unlock_mod(pr->m->pl, pr->m);
		return;
	}

	// Predicate is no longer being used

	//printf("*** leave %u, %s/%u, in_retractall=%d, is_final=%d, retry=%d\n",
	//	(unsigned)list_count(&pr->dirty), C_STR(q, &pr->key), get_arity(&pr->key), q->in_retractall, is_final, q->retry);

	rule *r;

	while ((r = list_pop_front(&pr->delinked)) != NULL)
		reclaim_rule(q, pr, r);

	while ((r = list_pop_front(&pr->dirty)) != NULL) {
		predicate_delink(pr, r);
		reclaim_rule(q, pr, r);
	}

	if (pr->idx1 && !pr->cnt) {
		sl_destroy(pr->idx2);
		sl_destroy(pr->idx1);
		sl_destroy(pr->idx3);
		pr->idx1 = pr->idx2 = pr->idx3 = NULL;
		pr->needs_index = false;
		pr->no_idx3 = false;
		pr->idx3_want = 0;
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

// Free what is on q->dirty and can be proved unreachable now, rather than waiting for the query
// to end. Without this a deterministic loop that retracts accumulates every clause it retracts:
// nothing drains q->dirty until teardown, and only an actual retry drains the undo list.
//
// A clause reaches here already delinked and with no reader inside its predicate, so iteration
// cannot reach it. is_purgeable rules out a binding pointing into its cells. What is left is code
// running out of them, which is this scan. Another thread's query is not scanned, so this only
// runs single-threaded; the concurrent case still waits, as before.

#define PURGE_DIRTY_AT 512
#define PURGE_MAX_STACK 64

static bool clause_holds_instr(const query *q, const clause *cl, const rule *r)
{
	const cell *lo = cl->cells, *hi = cl->cells + cl->cidx;

	if ((q->st.instr >= lo) && (q->st.instr < hi))
		return true;

	for (pl_idx i = 0; i < q->st.fp; i++) {
		const frame *f = GET_FRAME(i);

		if (f->instr && (f->instr >= lo) && (f->instr < hi))
			return true;
	}

	for (pl_idx i = 0; i < q->st.cp; i++) {
		const choice *ch = GET_CHOICE(i);

		if (ch->st.instr && (ch->st.instr >= lo) && (ch->st.instr < hi))
			return true;

		if (ch->st.dbe == r)
			return true;
	}

	return false;
}

// A retracted rule on an undo list is freed when backtracking undoes past it, which a
// deterministic loop never does. The undo item's whole action is that free, so releasing it
// early is the same work done sooner - the same two roots have to be ruled out as for
// q->dirty, and index entries are already gone either way, because reclaim_rule() runs
// before leave_predicate() destroys the index.

static void purge_undo_rules(query *q, list *l)
{
	undo_item *u = list_front(l);

	while (u) {
		undo_item *next = list_next(u);

		if (u->is_rule && u->r->cl.is_purgeable && !clause_holds_instr(q, &u->r->cl, u->r)) {
			list_remove(l, u);
			clear_clause(&u->r->cl);
			TPL_free(u->r);
			TPL_free(u);
			q->dirty_cnt--;
		}

		u = next;
	}
}

static void purge_reclaimed(query *q)
{
	// A deep stack makes the scan above dear, and a program with one is not the retract loop
	// this is for. Leave those to teardown rather than pay per clause.

	// Whatever this pass cannot free stays on the list, so the next attempt waits for another
	// PURGE_DIRTY_AT arrivals. Without that, a list of clauses that are all held would have
	// every following goal walking it again.

	q->purge_at = q->dirty_cnt + PURGE_DIRTY_AT;

	if ((q->st.fp > PURGE_MAX_STACK) || (q->st.cp > PURGE_MAX_STACK))
		return;

	const bool mt = q->pl->is_multithreaded;

	purge_undo_rules(q, &q->undo);

	for (pl_idx i = 0; i < q->st.cp; i++)
		purge_undo_rules(q, &GET_CHOICE(i)->undo);

	rule *r = list_front(&q->dirty);

	while (r) {
		rule *next = list_next(r);

		if (r->cl.is_purgeable && !clause_holds_instr(q, &r->cl, r)) {
			// reclaim_rule() only drops index entries while the predicate still has clauses,
			// so an index can still name this rule. query_purge_dirty_list() does the same
			// before freeing; skipping it leaves the skiplist pointing into freed cells.

			if (mt) prolog_lock_mod(q->pl, r->owner->m);
			index_remove_clause(r->owner, r);
			if (mt) prolog_unlock_mod(q->pl, r->owner->m);

			list_remove(&q->dirty, r);
			clear_clause(&r->cl);
			TPL_free(r);
			q->dirty_cnt--;
		}

		r = next;
	}

	q->purge_at = q->dirty_cnt + PURGE_DIRTY_AT;
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

static void trim_trail(query *q, bool reused, bool moved)
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

		if ((tr->val_ctx != q->st.cur_ctx) || is_frame_layout(tr))
			break;

		if (!reused) {
			const frame *f = GET_FRAME(tr->val_ctx);

			if (f->no_recov) {
				const slot *e = get_slot(q, f, tr->var_num);

				if (is_managed(&e->c))
					break;
			}
		} else if (moved && q->st.cp) {
			const frame *f = GET_FRAME(tr->val_ctx);

			// These now release the values reuse_frame() moved in, should we backtrack past the frame.

			if ((tr->var_num < f->actual_slots) && is_managed(&get_slot(q, f, tr->var_num)->c))
				break;
		}

		pop_trail(q);
	}
}

// Nothing from a run on an earlier page onwards is live once sp falls back to it.

static void rewind_slots(query *q, slot *run)
{
	slot_page *a = q->st.sp_page;

	while ((run < a->slots) || (run > a->end))
		a = a->prev;

	q->st.sp_page = a;
	q->st.sp = run;
}

static void trim_frame(query *q, const frame *f)
{
	for (unsigned i = 0; i < f->actual_slots; i++) {
		slot *e = get_slot(q, f, i);
		cell *c = &e->c;
		unshare_cell(c);
		memset(e, 0, sizeof(slot));
	}

	if ((size_t)(q->st.sp - q->st.sp_page->slots) >= f->actual_slots)
		q->st.sp -= f->actual_slots;
	else
		rewind_slots(q, f->slots);

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

	if (q->st.tp > q->hw_trails)
		q->hw_trails = q->st.tp;

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

		if (is_frame_layout(tr)) {
			frame *fl = GET_FRAME(tr->val_ctx);
			fl->actual_slots = tr->var_num & ~TRAIL_FRAME_LAYOUT;
			fl->ovf = tr->attrs ? (slot*)tr->attrs : q->slot_pages->slots;

			// Without an overflow run a frame's slots are all initial ones.

			if (!tr->attrs)
				fl->initial_slots = fl->actual_slots;

			continue;
		}

		const frame *f = GET_FRAME(tr->val_ctx);
		slot *e;

		// An entry can name a slot its frame no longer has, when a smaller frame took the index: that slot was
		// released already and backtracking discards the frame, so skip it rather than reach into the overflow area.

		if (tr->var_num < f->initial_slots)
			e = f->slots + tr->var_num;
		else if (tr->var_num < f->actual_slots)
			e = f->ovf + (tr->var_num - f->initial_slots);
		else
			continue;

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
	f->heap_pinned = false;
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

	f_new->ovf = q->slot_pages->slots;
	f_new->no_recov = q->no_recov;
	f_new->chgen = ++q->chgen;
	f_new->hp = q->st.hp;
	f_new->hp_num = q->st.hp_num;
	q->st.sp += f_new->actual_slots;
	q->st.cur_ctx = q->st.fp;
	q->st.fp++;

	if (q->st.fp > q->hw_frames)
		q->hw_frames = q->st.fp;

	check_slots_highwater(q);
}

// A reused frame whose run is on a page before sp's restarts it at that page's start: nothing live lies past the old run.

static void restart_run(query *q, frame *f)
{
	slot_page *a = q->st.sp_page->prev;

	while ((f->slots < a->slots) || (f->slots > a->end)) {
		a->used = 0;
		a = a->prev;
	}

	a->used = f->slots - a->slots;
	q->st.sp_page->base = a->base + a->used;
	f->slots = q->st.sp_page->slots;
}

// Note: TCO's clause might not be the caller clause, nor even the caller's
// predicate... hence passing num_vars.

static bool reuse_frame(query *q, unsigned num_vars)
{
	cell *c_next = q->st.instr + q->st.instr->num_cells;

	// This is if the last call was actually call/n

	if (c_next->val_off == g_sys_drop_barrier_s)
		drop_choice(q);

	// Release all the current frame's slots before copying: the new frame's can overlap them...

	const frame *f_new = GET_NEW_FRAME();
	frame *f_cur = GET_CURR_FRAME();

	for (unsigned i = 0; i < f_cur->actual_slots; i++)
		unshare_cell(&get_slot(q, f_cur, i)->c);

	f_cur->initial_slots = f_cur->actual_slots = num_vars;
	f_cur->no_recov = false;
	f_cur->heap_pinned = f_new->heap_pinned;

	if ((f_cur->slots < q->st.sp_page->slots) || (f_cur->slots > f_new->slots))
		restart_run(q, f_cur);

	slot *to = f_cur->slots;
	const slot *from = f_new->slots;

	for (unsigned i = 0; i < num_vars; i++)
		to[i] = from[i];

	// Head unification trailed the reference-counted values it bound in the new frame, and this frame's own entries
	// named the values just released: point the one here and drop the other, so backtracking releases each once.
	// Attribute hooks hold on to trail positions, so leave the trail alone while attributes are about.

	bool moved = false;

	if (!q->attrs_used && (q->st.cp > 1)) {
		for (pl_idx i = GET_CURR_CHOICE()->st.tp; !moved && (i < q->st.tp); i++) {
			const trail *tr = get_trail(q, i);
			moved = !is_frame_layout(tr) && (tr->val_ctx == q->st.fp);
		}
	}

	if (moved) {
		pl_idx w = GET_CHOICE(q->st.cp - 2)->st.tp;

		for (pl_idx r = w, end = q->st.tp; r < end; r++) {
			trail *tr = get_trail(q, r);

			if (!is_frame_layout(tr)) {
				if (tr->val_ctx == q->st.cur_ctx)
					continue;

				if (tr->val_ctx == q->st.fp)
					tr->val_ctx = q->st.cur_ctx;
			}

			if (w != r)
				*get_trail(q, w) = *tr;

			w++;
		}

		while (q->st.tp > w)
			pop_trail(q);
	}

	q->st.sp = f_cur->slots + f_cur->actual_slots;
	check_slots_highwater(q);
	q->st.dbe->tcos++;
	q->total_tcos++;
	q->st.hp = f_cur->hp;
	q->st.hp_num = f_cur->hp_num;
	trim_heap(q);
	return moved;
}

// Does a slot of the new frame refer to a term that reusing this frame would trim from the heap? A term
// built here can carry an older context (=../2 does that), which set_var() cannot tell from the context.

static bool refs_trimmed_heap(const query *q, const frame *f, unsigned num_vars)
{
	const page *a = q->heap_pages;

	if (!a || ((a->num <= f->hp_num) && (a->idx <= f->hp)))
		return false;

	const frame *f_new = GET_NEW_FRAME();

	for (unsigned i = 0; i < num_vars; i++) {
		const cell *c = &get_slot(q, f_new, i)->c;

		if (is_indirect(c) && is_heap_since(q, f, c->val_ptr))
			return true;
	}

	return false;
}

// Did head unification trail a slot of the new frame? Only reference-counted values are, and reuse_frame()
// moves them without their trail entries, so backtracking to an older choicepoint would release them twice.

static bool head_trailed_new_frame(query *q)
{
	const choice *ch = GET_CURR_CHOICE();

	for (pl_idx i = ch->st.tp; i < q->st.tp; i++) {
		const trail *tr = get_trail(q, i);

		if (!is_frame_layout(tr) && (tr->val_ctx == q->st.fp))
			return true;
	}

	return false;
}

static bool commit_any_choices(const query *q, unsigned skip)
{
	if (q->st.cp <= skip)
		return false;

	const choice *ch = GET_CHOICE(q->st.cp - 1 - skip);

	// The clause's own alternatives carry its frame's generation: reusing the frame would put them in reach of the callee's cut.

	return (ch->st.fp >= q->st.fp) || (ch->gen >= GET_CURR_FRAME()->chgen);
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

	if (last_match
		&& is_tail_call(q->st.instr)
		&& !q->no_recov
		&& !f->heap_pinned
		&& (q->st.fp == (q->st.cur_ctx + 1))
		) {
		bool barrier = false;

		tco = is_last_call(q, &barrier)
			&& !commit_any_choices(q, barrier ? 2 : 1)
			&& !refs_trimmed_heap(q, f, cl->num_vars)
			&& !((q->st.cp > (barrier ? 2u : 1u)) && q->attrs_used && head_trailed_new_frame(q));

#if 0
		cell *head = get_head(cl->cells);

		fprintf(stderr,
			"*** %s/%u tco=%d,q->no_recov=%d,last_match=%d,is_det=%d,"
			"cl->num_vars=%u,f->initial_slots=%u/%u\n",
			C_STR(q, head), get_arity(head),
			tco, q->no_recov, last_match, is_det,
			cl->num_vars, f->initial_slots, f->actual_slots);
#endif
	}

	if (!q->st.dbe->owner->is_builtin)
		q->st.m = q->st.dbe->owner->m;

	const bool reused = tco && q->pl->opt;

	// No EXIT port for a reused frame: the calls sharing it get one, from resume_frame(), when they are done.

	bool moved = false;

	if (reused)
		moved = reuse_frame(q, cl->num_vars);
	else
		push_frame(q);

	// Read what we still need out of cl BEFORE giving up the reference.
	// leave_predicate() may take the refcount to zero and reclaim, and a
	// concurrent purge can free this very clause the moment it does -
	// leaving the continuation to be read out of freed memory.

	cell *next_instr = cl->alt ? cl->alt : get_body(cl->cells);
	if (!next_instr) next_instr = cl->cells + (cl->cidx-1);

	if (last_match) {
		leave_predicate_and_drop(q, q->st.pr, false);
		trim_trail(q, reused, moved);


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
		else if (u->is_mmap)
			stream_unmap(u->addr, u->mmap_len);
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
		f->ovf = ch->ovf;
		f->slots = ch->slots;

		if (ch->reset)
			continue;

		if (ch->catchme_exception || ch->fail_on_retry) {
			// Choice abandoned without drop_choice(); free its prefetch.
			release_prefetch(q, ch, cp);
			leave_predicate(q, ch->st.pr, ch->st.pr_dbgen, true);
			continue;
		}

		if (!ch->register_cleanup && q->noretry) {
			release_prefetch(q, ch, cp);
			leave_predicate(q, ch->st.pr, ch->st.pr_dbgen, true);
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

	if (q->st.cp > q->hw_choices)
		q->hw_choices = q->st.cp;

	list_init(&ch->undo);
	ch->dbgen = f->dbgen;
	ch->chgen = ch->gen = f->chgen;
	ch->initial_slots = f->initial_slots;
	ch->actual_slots = f->actual_slots;
	ch->ovf = f->ovf;
	ch->slots = f->slots;

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

		leave_predicate(q, ch->st.pr, ch->st.pr_dbgen, false);
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

			leave_predicate(q, ch->st.pr, ch->st.pr_dbgen, false);
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

	// When every bound argument is atomic and among the first three, the per-argument tests in
	// has_next_key() already decide a match and its whole-head compare adds nothing.

	const unsigned arity = get_arity(q->st.key);
	bool checked = (q->st.karg1_is_atomic || !q->st.karg1_is_ground)
		&& ((arity < 2) || q->st.karg2_is_atomic || !q->st.karg2_is_ground)
		&& ((arity < 3) || q->st.karg3_is_atomic || !q->st.karg3_is_ground);

	if (checked && (arity > 3)) {
		cell *arg = NEXT_ARG(NEXT_ARG(save_arg2));

		for (unsigned i = 3; checked && (i < arity); i++, arg += arg->num_cells)
			checked = is_var(deref(q, arg, q->st.key_ctx));
	}

	q->st.key_args_checked = checked;
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

		if (q->st.key_args_checked || (index_cmpkey(q->st.key, dkey, q->st.m, NULL) == 0))
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
	const rule **got, unsigned num_got, int idx_arg, skiplist *idx, bool composite)
{
	const uint64_t dbgen = q->pl->dbgen;
	unsigned missing = 0;

	g_index_check_lookups++;

	for (const rule *c = pr->head; c; c = c->next) {
		if (!can_view(q, dbgen, c))
			continue;

		cell *ch = get_head(((rule*)c)->cl.cells);
		cell *ck = ch;

		if (!composite && get_arity(ch))
			ck = get_nth_arg(ch, idx_arg);

		if (composite) {
			if (index_cmpkey2(ck, key, pr, NULL) != 0)
				continue;
		} else if (index_cmpkey(ck, key, q->st.m, NULL) != 0)
			continue;

		if (in_candidates(got, num_got, c))
			continue;

		if (!missing) {
			fprintf(stderr, "\n*** index-check FAILED for %s/%u (%s)\n",
				C_STR(q, &pr->key), get_arity(&pr->key),
				composite ? "composite" : "argument");
			fprintf(stderr, "***   goal   ");
			DUMP_TERM("", goal, q->st.cur_ctx, 1);
		}

		fprintf(stderr, "***   MISSING db_id=%llu  ",
			(unsigned long long)c->db_id);
		DUMP_TERM("", ch, q->st.cur_ctx, 1);

		sliter *probe = sl_find_key(idx, ck);
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
		fprintf(stderr, "***   predicate has %u clauses, idx1=%s idx2(arg%u)=%s idx3=%s\n",
			(unsigned)pr->cnt, pr->idx1 ? "yes" : "no", pr->idx2_arg + 1,
			pr->idx2 ? "yes" : "no", pr->idx3 ? "yes" : "no");

		g_index_check_bad++;
	}
}

// A goal with both key arguments bound gets every clause sharing the first, and the candidate
// filter rejects the rest one at a time. Once enough of those go by, build the index that
// answers them directly. Counting is approximate under threads on purpose: it only decides when
// to build, and the build itself takes the lock and re-checks.

#define COMPOSITE_INDEX_THRESHOLD 100

static void composite_index_wanted(query *q, predicate *pr)
{
	if (++pr->idx3_want < COMPOSITE_INDEX_THRESHOLD)
		return;

	const bool mt = q->pl->is_multithreaded;

	if (mt)
		prolog_lock_mod(pr->m->pl, pr->m);

	build_predicate_composite_index(pr);

	if (mt)
		prolog_unlock_mod(pr->m->pl, pr->m);
}

static bool find_key(query *q, predicate *pr, cell *key, pl_ctx key_ctx)
{
	q->st.iter = NULL;
	q->st.iter_single = false;
	q->st.karg1_is_ground = q->st.karg2_is_ground = q->st.karg3_is_ground = false;
	q->st.karg1_is_atomic = q->st.karg2_is_atomic = q->st.karg3_is_atomic = false;
	q->st.key_args_checked = false;
	q->st.key = key;
	q->st.key_ctx = key_ctx;

	if (!pr->idx1 && pr->needs_index) {
		// First lookup since the predicate outgrew the threshold: build the index now.

		const bool mt = q->pl->is_multithreaded;

		if (mt)
			prolog_lock_mod(pr->m->pl, pr->m);

		build_predicate_index(pr);

		if (mt)
			prolog_unlock_mod(pr->m->pl, pr->m);
	}

	if (!pr->idx1) {
		q->st.dbe = pr->head;

		if (get_arity(key)) {
			if (pr->is_meta_predicate) {
				if (!expand_meta_predicate(q, pr))
					return false;
			}

			// has_next_key() is the only reader of what setup_key() works out, and it
			// answers from the chain alone when there is no clause after this one.

			if (q->st.dbe && q->st.dbe->next)
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
	int idx_arg = 0, idx_arg2 = -1;
	bool composite = false;

	if (arg1 && (is_var(arg1) || pr->is_var_in_first_arg)) {
		if (!pr->idx2 || pr->is_var_in_idx2_arg) {
			INDEX_PROFILE_MODE(ip, linear);
			INDEX_PROFILE_CANDIDATES(ip, pr->cnt);
			q->st.dbe = pr->head;

			// A chain walk after all, so has_next_key() needs to know which arguments are bound.

			if (q->st.dbe && q->st.dbe->next)
				setup_key(q);

			return true;
		}

		cell *arg2 = get_nth_arg(key, pr->idx2_arg);

		if (is_var(arg2)) {
			INDEX_PROFILE_MODE(ip, linear);
			INDEX_PROFILE_CANDIDATES(ip, pr->cnt);
			q->st.dbe = pr->head;

			if (q->st.dbe && q->st.dbe->next)
				setup_key(q);

			return true;
		}

		key = arg2;
		idx = pr->idx2;
		idx_arg = pr->idx2_arg;
		INDEX_PROFILE_MODE(ip, idx2);
	} else if (arg1) {
		cell *argn = pr->idx2_arg ? get_nth_arg(key, pr->idx2_arg) : NULL;
		const bool both_bound = argn && !is_var(argn) && !pr->is_var_in_first_arg
			&& !pr->is_var_in_idx2_arg;

		if (both_bound && !pr->idx3 && !pr->no_idx3)
			composite_index_wanted(q, pr);

		if (both_bound && pr->idx3) {
			// Keyed on both, so the whole goal is the key (see index_cmpkey2).
			idx = pr->idx3;
			idx_arg2 = pr->idx2_arg;
			composite = true;
			INDEX_PROFILE_MODE(ip, idx3);
		} else {
			// idx1 is keyed on Arg1 only (see assert_commit).
			key = arg1;
			INDEX_PROFILE_MODE(ip, idx1);
		}
	}

	if (!arg1) {
		INDEX_PROFILE_MODE(ip, idx1);
	}

	q->st.dbe = NULL;
	sliter *iter;

	if (!(iter = sl_find_key(idx, key))) {
		if (g_index_check)
			index_check(q, pr, goal, key, NULL, 0, idx_arg, idx, composite);

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
	const unsigned key_arity = get_arity(goal);

	while (sl_next_key(iter, (void*)&r)) {
		INDEX_PROFILE_CANDIDATES(ip, 1);
		if (g_index_check) {
			if (num_got == max_got) {
				max_got = max_got ? max_got * 2 : 32;
				got = TPL_realloc(got, max_got * sizeof(*got));
			}

			got[num_got++] = r;
		}

		// The index keys on one argument, so candidates sharing that key can
		// still differ in another: a clause whose atomic argument differs from
		// the call's cannot unify, and is dropped here rather than prefetched
		// and unified. Only atomic against atomic, so no walk of a compound.

		if (key_arity) {
			cell *ch = get_head(((rule*)r)->cl.cells);
			cell *ka = FIRST_ARG(goal), *ca = FIRST_ARG(ch);
			bool may_match = true;

			for (unsigned n = 0; n < key_arity; n++) {
				if (((int)n != idx_arg) && ((int)n != idx_arg2)
					&& is_atomic(ka) && is_atomic(ca)
					&& index_cmpkey(ka, ca, q->st.m, NULL)) {
					may_match = false;
					break;
				}

				ka = NEXT_ARG(ka);
				ca = NEXT_ARG(ca);
			}

			if (!may_match)
				continue;
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
		index_check(q, pr, goal, key, got, num_got, idx_arg, idx, composite);
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
		leave_predicate(q, q->st.pr, q->st.pr_dbgen, true);
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
			if (is_retract == DO_CLAUSE)
				return throw_error(q, p1, p1_ctx, "permission_error", "access,private_procedure");

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
		leave_predicate(q, q->st.pr, q->st.pr_dbgen, true);
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

		// import_term() detaches a copy because unifying against the clause can leave the
		// caller pointing into cells that retract is about to take away, and it keeps that
		// copy until backtracking undoes past it - which a deterministic loop never does,
		// so a retract loop kept one copy and one undo item per call. A fact whose arguments
		// are all atomic cannot be pointed into: unify() copies such values into the caller's
		// slots. Match against the clause itself and allocate nothing. clause/2 still gets a
		// copy, since it hands the body back to its caller.

		cell *tmp;

		if ((is_retract != DO_CLAUSE) && cl->is_purgeable) {
			tmp = c;
		} else {
			tmp = import_term(q, c, q->st.cur_ctx);
			CHECKED(tmp);
		}

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

	leave_predicate(q, q->st.pr, q->st.pr_dbgen, true);
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
		leave_predicate(q, q->st.pr, q->st.pr_dbgen, true);
		return false;
	}

	CHECKED(check_frame(q, q->st.pr->max_vars));
	CHECKED(push_choice(q));
	const frame *f = GET_CURR_FRAME();

	// Nothing under the index threshold has an index, so a call walks the chain and unifies
	// every head until one takes. Most of those fail: 73% of attempts in chess. Summarise the
	// goal's first argument once and throw out the clauses that cannot match it, which costs
	// a compare instead of try_me()'s memset, a unify() and an undo_me().

	uint64_t goal_sig[3] = {0};

	// Only where nothing else has narrowed the field: an index hit was filtered by find_key(), and a
	// lone clause is tried whatever its head looks like. An indexed predicate whose goal fell back to
	// the chain (neither indexed argument bound) walks it unfiltered too, so it gets the same test.

	if ((!q->st.pr->idx1 || (!q->st.iter && !q->st.iter_single)) && q->st.dbe->next && get_arity(q->st.key)) {
		const uint32_t arity = get_arity(q->st.key);
		cell *ga = FIRST_ARG(q->st.key);

		for (unsigned i = 0; (i < 3) && (i < arity); i++) {
			cell *d = deref(q, ga, q->st.key_ctx);
			goal_sig[i] = cell_signature(d);
			ga += ga->num_cells;
		}
	}

	for (; q->st.dbe; next_key(q)) {
		if (!can_view(q, f->dbgen, q->st.dbe))
			continue;

		clause *cl = &q->st.dbe->cl;

		if ((goal_sig[0] && cl->arg_sig[0] && (cl->arg_sig[0] != goal_sig[0]))
			|| (goal_sig[1] && cl->arg_sig[1] && (cl->arg_sig[1] != goal_sig[1]))
			|| (goal_sig[2] && cl->arg_sig[2] && (cl->arg_sig[2] != goal_sig[2])))
			continue;
		cell *head = get_head(cl->cells);

		if (cl->num_vars > q->st.pr->max_vars) {
			CHECKED(check_slot(q, q->st.pr->max_vars=cl->num_vars));
			GET_NEW_FRAME()->slots = q->st.sp;
		}

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
		if ((q->dirty_cnt >= q->purge_at) && !q->pl->is_multithreaded)
			purge_reclaimed(q);

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
				check_pressure(q);

				if (q->yield_at && !q->run_hook) {
					uint64_t now = wall_time_in_usec() / 1000;

					// Only tasks, and WASI top-level queries, can yield; anything else carries on.

					if ((now > q->yield_at) && !do_yield_then(q, status))
						break;
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

			// A task runs its goal once, with nobody to ask for more.

			if (q->top && !q->run_init && !q->is_task && any_outstanding_choices(q)) {
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

// Frame 0 is one run from the first page's start, which grows to fit only while nothing else can point into it.

static bool layout_frame0(query *q, unsigned num_vars)
{
	slot_page *a = q->slot_pages;
	size_t size = a->end - a->slots;

	if (num_vars > size) {
		const uintptr_t lo = (uintptr_t)a->slots, hi = (uintptr_t)a->end;
		size_t n = alloc_grow(q, (void**)&a->slots, sizeof(slot), num_vars, (size_t)num_vars * 5 / 4);

		if (!n) {
			q->error = true;
			return false;
		}

		memset(a->slots + size, 0, sizeof(slot) * (n - size));
		a->end = a->slots + n;

		// Frames still pointing at the first page, unused ones included, follow it.

		for (pl_idx i = 0; i < q->frame_pages_size; i++) {
			for (unsigned j = 0; q->frame_pages[i] && (j < FRAME_PAGE_SIZE); j++) {
				frame *f = q->frame_pages[i] + j;

				if (((uintptr_t)f->slots >= lo) && ((uintptr_t)f->slots <= hi))
					f->slots = a->slots + ((uintptr_t)f->slots - lo) / sizeof(slot);

				if (((uintptr_t)f->ovf >= lo) && ((uintptr_t)f->ovf <= hi))
					f->ovf = a->slots + ((uintptr_t)f->ovf - lo) / sizeof(slot);
			}
		}
	}

	frame *f = get_frame(q, 0);
	f->slots = f->ovf = a->slots;
	f->initial_slots = f->actual_slots = num_vars;
	q->st.sp_page = a;
	q->st.sp = a->slots + num_vars;
	return true;
}

bool execute(query *q, cell *cells, unsigned num_vars)
{
	q->retry = q->halt = q->error = q->abort = false;
	q->pl->did_dump_vars = false;
	q->st.instr = cells;
	q->is_redo = false;

	// There is an initial frame (fp=0), so this
	// to the next available frame...

	q->st.fp = 1;

	frame *f = GET_FRAME(0);

	// engine_create() can leave frame 0 with an overflow run already, and sp above it.

	if (((f->ovf == q->slot_pages->slots) || (f->actual_slots != num_vars)) && !layout_frame0(q, num_vars))
		return false;

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

	// Pages before sp's hold their used count of live slots, pages after it none.

	for (slot_page *a = q->slot_pages; a; a = a->next) {
		const slot *end = a == q->st.sp_page ? q->st.sp : a->slots + a->used;

		for (slot *e = a->slots; e < end; e++)
			unshare_cell(&e->c);

		if (a == q->st.sp_page)
			break;
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
	free_slot_pages(q->slot_pages);
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
	TPL_free(q->engine_ball);

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

	if (q)
		q->purge_at = PURGE_DIRTY_AT;

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

	q->slot_pages = TPL_calloc(1, sizeof(slot_page));
	slot *slots = TPL_calloc(INITIAL_NBR_SLOTS, sizeof(slot));

	if (!q->slot_pages || !slots) {
		TPL_free(q->slot_pages);
		TPL_free(slots);
		BAIL_OUT();
	}

	#undef BAIL_OUT

	q->slot_pages->slots = q->st.sp = slots;
	q->slot_pages->end = slots + INITIAL_NBR_SLOTS;
	q->st.sp_page = q->slot_pages;

	for (unsigned i = 0; i < FRAME_PAGE_SIZE; i++)
		q->frame_pages[0][i].slots = q->frame_pages[0][i].ovf = slots;

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

	if (!layout_frame0(subq, fsrc->actual_slots)) {
		query_destroy(subq);
		return NULL;
	}

	fdst->dbgen = ++q->pl->dbgen;
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

	if (!layout_frame0(subq, num_vars)) {
		query_destroy(subq);
		return NULL;
	}

	get_frame(subq, 0)->dbgen = ++q->pl->dbgen;
	return subq;
}
