#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "internal.h"
#include "heap.h"

#if USE_FFI
bool wrap_ffi_function(query *q, builtins *bif_ptr);
bool wrap_ffi_predicate(query *q, builtins *bif_ptr);
#endif

#define is_callable_or_var(c) (is_callable(c) || is_var(c))
#define is_list_or_nil(c) (is_list(c) || is_nil(c))
#define is_list_or_atom_or_var(c) (is_list(c) || is_atom(c) || is_var(c))
#define is_list_or_nil_or_var(c) (is_list_or_nil(c) || is_var(c))
#define is_list_or_var(c) (is_list(c) || is_var(c))
#define is_nil_or_var(c) (is_nil(c) || is_var(c))
#define is_compound_or_atom(c) (is_compound(c) || is_atom(c))
#define is_compound_or_var(c) (is_compound(c) || is_var(c))
#define is_atomic_or_var(c) (is_atomic(c) || is_var(c))
#define is_atom_or_var(c) (is_atom(c) || is_var(c))
#define is_atom_or_integer(c) (is_atom(c) || is_integer(c))
#define is_atom_or_list_or_nil(c) (is_atom(c) || is_list_or_nil(c))
#define is_atom_or_compound(c) (is_atom(c) || is_compound(c))
#define is_number_or_var(c) (is_number(c) || is_var(c))
#define is_float_or_var(c) (is_float(c) || is_var(c))
#define is_integer_or_var(c) (is_integer(c) || is_var(c))
#define is_integer_or_atom(c) (is_integer(c) || is_atom(c))
#define is_smallint_or_var(c) (is_smallint(c) || is_var(c))
#define is_smallint_or_atom(c) (is_smallint(c) || is_atom(c))
#define is_stream(c) (get_stream(q,c) >= 0)
#define is_stream_or_alias(c) (get_stream(q,c) >= 0)
#define is_stream_or_var(c) (is_stream(c) || is_var(c))
#define is_stream_or_compound(c) (is_stream(c) || is_compound(c))
#define is_list_or_atom(c) (is_atom(c) || is_iso_list(c))
#define is_atom_or_list(c) (is_atom(c) || is_iso_list(c))
#define is_atom_or_list_or_var(c) (is_atom(c) || is_iso_list(c) || is_var(c))
#define is_character(c) (is_iso_atom(c) && ((strlen_utf8(C_STR(q, c)) <= 1) || !CMP_STRING_TO_CSTR(q, c, "end_of_file")))
#define is_character_or_var(c) (is_in_character(c) || is_var(c))
#define is_in_character(c) (is_atom(c) && ((strlen_utf8(C_STR(q, c)) <= 1) || !CMP_STRING_TO_CSTR(q, c, "end_of_file")))
#define is_in_character_or_var(c) (is_in_character(c) || is_var(c))
#define is_in_byte(c) (is_integer(c) && (get_smallint(c) >= -1) && (get_smallint(c) < 256))
#define is_in_byte_or_var(c) (is_in_byte(c) || is_var(c))
#define is_byte(c) (is_integer(c) && (get_smallint(c) >= 0) && (get_smallint(c) < 256))
#define is_chars(q,c,ctx) (is_nil(c) || is_string(c) || scan_is_chars_list(q, c, ctx, false) || (is_cstring(c) && !CMP_STRING_TO_CSTR(q, c, "[]")))
#define is_sregex(c) (is_blob(c) && ((c)->flags & FLAG_BLOB_SREGEX))
#define is_string_or_var(c) (is_string(c) || is_var(c))
#define is_source_sink(c) (is_atom(c) || is_compound(c))
#define is_any(c) true

#define is_iso_list_or_nil(c) (is_iso_list(c) || is_nil(c))
#define is_iso_list_or_nil_or_var(c) (is_iso_list_or_nil(c) || is_var(c))
#define is_iso_list_or_var(c) (is_iso_list(c) || is_var(c))
#define is_iso_atom_or_var(c) (is_iso_atom(c) || is_var(c))
#define is_iso_atomic_or_var(c) (is_iso_atom(c) || is_number(c) || is_var(c))

#define GET_SOURCE_SINK(p1, p1_ctx, filename) { \
	if (is_iso_list(p1)) { \
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true); \
		if (!len) \
			return throw_error(q, p1, p1_ctx, "type_error", "source_sink"); \
		filename = chars_list_to_string(q, p1, p1_ctx); \
	} else \
		filename = DUP_STRING(q, p1); \
}

bool call_builtin(query *q, cell *c, pl_ctx c_ctx);
bool call_userfun(query *q, cell *c, pl_ctx c_ctx);

#define eval(q,c)														\
	is_evaluable(c) || is_builtin(c) ? (call_builtin(q,c,c##_ctx), q->accum) : \
	is_callable(c) ? (call_userfun(q, c, c##_ctx), q->accum) : *c;		\
	q->accum.flags = 0;													\
	q->accum.num_cells = 1;											\
	if (q->did_throw)													\
		return true; 												\
	if (is_var(c))													\
		return throw_error(q, c, q->st.cur_ctx, "instantiation_error", "number"); \
	if (is_builtin(c) && c->bif_ptr && (c->bif_ptr->fn != bif_iso_float_1) && (c->bif_ptr->fn != bif_iso_integer_1)) \
		return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");

bool bif_iso_add_2(query *q);
bool bif_iso_float_1(query *q);
bool bif_iso_integer_1(query *q);

bool do_yield(query *q, int msecs);
bool do_yield_on_stream(query *q, stream *str, bool is_write);
bool do_yield_now(query *q);
bool do_yield_then(query *q, bool status);
void do_yield_at(query *q, unsigned int time_in_ms);

// Makes sure a queue depth exists, growing the array if it does not.
bool ensure_queuen(query *q, unsigned qnum);

inline static void init_queuen(query *q)
{
	qbuf *b = &q->queues[q->st.qnum];
	TPL_free(b->queue);
	b->queue = NULL;
	b->qp = 0;
	b->qcnt = 0;
}

// Returns false when the nesting limit is reached or the array cannot grow;
// the caller raises resource_error(max_queues) either way. It used to step
// q->st.qnum first and let the caller notice afterwards, which wrote one
// past the end of every one of those arrays before the check was reached.

inline static bool grab_queuen(query *q)
{
	if (((q->st.qnum + 1) >= MAX_QUEUES) || !ensure_queuen(q, q->st.qnum + 1))
		return false;

	q->st.qnum++;
	init_queuen(q);
	return true;
}

inline static void drop_queuen(query *q)
{
	init_queuen(q);
	q->st.qnum--;
}

inline static pl_idx queuen_used(const query *q)
{
	return q->queues[q->st.qnum].qp;
}

inline static cell *get_queuen(query *q)
{
	return q->queues[q->st.qnum].queue;
}

// findall/3 and findnsols/4 rebuild their result list from the queue
// with end_list(), which dup_cells() - i.e. takes its OWN reference to
// every managed blob. The queue's references (alloc_queuen ->
// dup_cells) are then redundant and must be released, or the blob
// outlives the query. Order matters: unshare only AFTER end_list() has
// taken its refs, or the last one drops to zero and the list gets
// built over freed strbufs.
//
// NB. not every queue works this way: bif_sys_list_1() converts with an
// explicitly unsafe copy that TRANSFERS ownership to the new list, so
// that path must keep freeing the buffer raw via drop_queuen().

inline static void free_solns(cell *solns, pl_idx num_cells)
{
	for (cell *c = solns; num_cells--; c++)
		unshare_cell(c);

	TPL_free(solns);
}

inline static cell *take_queuen(query *q)
{
	qbuf *b = &q->queues[q->st.qnum];
	cell *save = b->queue;
	b->queue = NULL;
	b->qp = 0;
	return save;
}

static inline choice *get_choice(const query *q, pl_idx idx)
{
	choice_page *a = q->choice_current;

	while (a && (idx < a->base))
		a = a->prev;

	while (a && (idx >= (a->base + a->page_size)))
		a = a->next;

	assert(a);
	return a->entries + (idx - a->base);
}
#define GET_CHOICE(i) get_choice(q, (i))
#define GET_CURR_CHOICE() (q->choice_next - 1)
#define GET_PREV_CHOICE() GET_CHOICE(q->st.cp-2)

static inline void pop_choice(query *q)
{
	assert(q->st.cp);
	q->st.cp--;

	// choice_current owns the top live choice.  At a page boundary the
	// decremented cp is the next free slot, so its predecessor is already
	// on the previous page.
	q->choice_next--;

	if (q->choice_next == q->choice_current->entries && q->st.cp)
	{
		q->choice_current = q->choice_current->prev;
		q->choice_next = q->choice_current->entries + q->choice_current->page_size;
	}
}

static inline frame *get_frame(const query *q, pl_idx idx)
{
	return q->frame_pages[idx >> FRAME_PAGE_SHIFT] + (idx & (FRAME_PAGE_SIZE - 1));
}
#define GET_FRAME(i) get_frame(q, (i))
#define GET_CURR_FRAME() GET_FRAME(q->st.cur_ctx)
#define GET_NEW_FRAME() GET_FRAME(q->st.fp)

inline static slot *get_slot(const query *q, const frame *f, unsigned var_num)
{
	return var_num < f->initial_slots
		? q->slots + f->base + var_num
		: q->slots + f->op + (var_num - f->initial_slots);
}

// The caller has already resolved c_ctx and the slot for c. This is used by
// DEREF_VAR(), which needs the initial slot for cycle bookkeeping as well.
// make_indirect() is only ever handed a compound, but a stale indirect can
// land on a cell the heap has since reused as a variable. Out of line: it
// never happens while the heap is sound, and inlining it here cost the
// deref hot path its inlining everywhere else.

cell *deref_stale_indirect(query *q, cell *c, pl_ctx c_ctx);

__attribute__((always_inline))
inline static cell *deref_from_slot(query *q, cell *c, pl_ctx c_ctx, slot *e)
{
	const frame *f;

	while (is_var(&e->c)) {
		c_ctx = e->c.val_ctx;
		c = &e->c;

		if (is_ref(c))
			c_ctx = c->val_ctx;

		f = GET_FRAME(c_ctx);
		slot *e2 = get_slot(q, f, c->var_num);

		if (e == e2)
			break;

		e = e2;
	}

	if (is_indirect(&e->c)) {
		if (is_var(e->c.val_ptr))
			return deref_stale_indirect(q, e->c.val_ptr, e->c.val_ctx);

		q->latest_ctx = e->c.val_ctx;
		return e->c.val_ptr;
	}

	q->latest_ctx = c_ctx;

	if (is_empty(&e->c))
		return c;

	return &e->c;
}

__attribute__((always_inline))
inline static cell *deref(query *q, cell *c, pl_ctx c_ctx)
{
	if (!is_var(c)) {
		if (is_indirect(c)) {
			if (is_var(c->val_ptr))
				return deref_stale_indirect(q, c->val_ptr, c->val_ctx);

			q->latest_ctx = c->val_ctx;
			return c->val_ptr;
		}

		q->latest_ctx = c_ctx;
		return c;
	}

	if (is_ref(c))
		c_ctx = c->val_ctx;

	const frame *f = GET_FRAME(c_ctx);
	slot *e = get_slot(q, f, c->var_num);
	return deref_from_slot(q, c, c_ctx, e);
}

#define FIRST_ARG(c) ((c)+1)
#define NEXT_ARG(c) ((c)+(c)->num_cells)

inline static cell *get_nth_arg(cell *c, unsigned n)
{
	c = FIRST_ARG(c);

	while (n--)
		c = NEXT_ARG(c);

	return c;
}

#define GET_RAW_ARG(n,p) \
	cell *p = get_raw_arg(q,n); \
	pl_ctx p##_ctx = q->latest_ctx

#define GET_FIRST_ARG(p,vt) \
	cell *p = get_first_arg(q); \
	pl_ctx p##_ctx = q->latest_ctx; \
	if (!is_##vt(p)) { return q->did_throw ? false : throw_error(q, p, p##_ctx, "type_error", #vt); }

#define GET_FIRST_ARG0(p,vt,p0) \
	cell *p = get_first_arg0(q,p0); \
	pl_ctx p##_ctx = q->latest_ctx; \
	if (!is_##vt(p)) { return q->did_throw ? false : throw_error(q, p, p##_ctx, "type_error", #vt); }

#define GET_FIRST_RAW_ARG(p,vt) \
	cell *p = get_first_raw_arg(q); \
	pl_ctx p##_ctx = q->latest_ctx; \
	if (!is_##vt(p)) { return q->did_throw ? false : throw_error(q, p, p##_ctx, "type_error", #vt); }

#define GET_FIRST_RAW_ARG0(p,vt,p0) \
	cell *p = get_first_raw_arg0(q,p0); \
	pl_ctx p##_ctx = q->latest_ctx; \
	if (!is_##vt(p)) { return q->did_throw ? false : throw_error(q, p, p##_ctx, "type_error", #vt); }

#define GET_NEXT_ARG(p,vt) \
	cell *p = get_next_arg(q); \
	pl_ctx p##_ctx = q->latest_ctx; \
	if (!is_##vt(p)) { return q->did_throw ? false : throw_error(q, p, p##_ctx, "type_error", #vt); }

#define GET_NEXT_RAW_ARG(p,vt) \
	cell *p = get_next_raw_arg(q); \
	pl_ctx p##_ctx = q->latest_ctx; \
	if (!is_##vt(p)) { return q->did_throw ? false : throw_error(q, p, p##_ctx, "type_error", #vt); }

inline static cell *get_first_arg(query *q)
{
	q->last_arg = q->st.instr + 1;
	return deref(q, q->last_arg, q->st.cur_ctx);
}

inline static cell *get_first_arg0(query *q, cell *p0)
{
	q->last_arg = p0 + 1;
	return deref(q, q->last_arg, q->st.cur_ctx);
}

inline static cell *get_first_raw_arg(query *q)
{
	q->last_arg = q->st.instr + 1;

	if (is_ref(q->last_arg))
		q->latest_ctx = q->last_arg->val_ctx;
	else
		q->latest_ctx = q->st.cur_ctx;

	return q->last_arg;
}

inline static cell *get_first_raw_arg0(query *q, cell *p0)
{
	q->last_arg = p0 + 1;

	if (is_ref(q->last_arg))
		q->latest_ctx = q->last_arg->val_ctx;
	else
		q->latest_ctx = q->st.cur_ctx;

	return q->last_arg;
}

inline static cell *get_next_arg(query *q)
{
	q->last_arg += q->last_arg->num_cells;
	return deref(q, q->last_arg, q->st.cur_ctx);
}

inline static cell *get_next_raw_arg(query *q)
{
	q->last_arg += q->last_arg->num_cells;

	if (is_ref(q->last_arg))
		q->latest_ctx = q->last_arg->val_ctx;
	else
		q->latest_ctx = q->st.cur_ctx;

	return q->last_arg;
}

inline static cell *get_raw_arg(query *q, int n)
{
	cell *c = q->st.instr + 1;

	for (int i = 1; i < n; i++)
		c += c->num_cells;

	if (is_ref(c))
		q->latest_ctx = c->val_ctx;
	else
		q->latest_ctx = q->st.cur_ctx;

	return c;
}

#define CHECKED(expr, ...) \
	CHECK_SENTINEL(expr, 0, __VA_ARGS__; \
	return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory"))

// This one leaves original state if a cycle detected...

#define DEREF_CHECKED(any, both, svg, e, evgen, c, c_ctx, qvgen)	\
	if (is_var(c)) {												\
		pl_ctx tmp_c_ctx = c_ctx;									\
		any = true;													\
																	\
		if (is_ref(c))												\
			tmp_c_ctx = c->val_ctx;									\
																	\
		const frame *f = GET_FRAME(tmp_c_ctx);						\
		e = get_slot(q, f, c->var_num);								\
		svg = evgen;												\
																	\
		if (evgen == qvgen) {										\
			both++;													\
		} else {													\
			c = deref(q, c, tmp_c_ctx);								\
			c_ctx = q->latest_ctx;									\
			evgen = qvgen;											\
		}															\
	}

// This one always derefs...

#define DEREF_VAR(any, both, svg, e, evgen, c, c_ctx, qvgen)		\
	if (is_var(c)) {												\
		pl_ctx tmp_c_ctx = c_ctx;									\
		any = true;													\
																	\
		if (is_ref(c))												\
			tmp_c_ctx = c->val_ctx;									\
																	\
		const frame *f = GET_FRAME(tmp_c_ctx);						\
		e = get_slot(q, f, c->var_num);								\
		svg = evgen;												\
																	\
		if (evgen == qvgen) {										\
			both++;													\
		} else {													\
			evgen = qvgen;											\
		}															\
																	\
		c = deref_from_slot(q, c, tmp_c_ctx, e);						\
		c_ctx = q->latest_ctx;										\
	}

#define RESTORE_VAR(c, c_ctx, p, p_ctx, qvgen)						\
	if (is_var(c)) {												\
		if (is_ref(c))												\
			c_ctx = c->val_ctx;										\
																	\
		const frame *f = GET_FRAME(c_ctx);							\
		slot *e = get_slot(q, f, c->var_num);						\
		e->vgen = 0;												\
		p = deref(q, c, c_ctx);										\
		p_ctx = q->latest_ctx;										\
	}

#define START_FUNCTION(q) { \
	errno = 0; \
	if (!q->eval) { \
		if (!q->st.m->flags.unknown) \
			return throw_error(q, q->st.instr, q->st.cur_ctx, "existence_error", "procedure"); \
		else \
			return false; \
	} \
}

bool bif_iso_close_1(query *q);
bool bif_iso_true_0(query *q);
bool bif_iso_fail_0(query *q);
bool bif_iso_call_1(query *q);
bool bif_iso_conjunction_2(query *q);
bool bif_iso_qualify_2(query *q);
bool bif_iso_cut_0(query *q);
bool bif_iso_unify_2(query *q);
bool bif_iso_if_then_2(query *q);
bool bif_soft_if_then_2(query *q);
bool bif_sys_call_check_1(query *q);
bool bif_sys_succeed_on_retry_1(query *q);
bool bif_sys_succeed_on_retry_2(query *q);
bool bif_sys_fail_on_retry_1(query *q);
bool bif_sys_drop_barrier_1(query *q);
bool bif_sys_reset_handler_1(query *q);
bool bif_sys_call_cleanup_3(query *q);
bool bif_sys_queue_1(query *q);
bool bif_sys_get_level_1(query *q);
bool bif_sys_set_if_var_2(query *q);
bool bif_sys_jump_1(query *q);
bool bif_sys_jump_if_nil_2(query *q);
bool bif_sys_cut_1(query *q);
bool bif_parse_csv_file_2(query *q);
bool bif_parse_csv_line_3(query *q);
bool bif_parse_csv_line_2(query *q);
bool bif_sre_compile_2(query *q);
bool bif_sre_matchp_4(query *q);
bool bif_sre_match_4(query *q);
bool bif_sre_substp_4(query *q);
bool bif_sre_subst_4(query *q);
bool bif_call_0(query *q, cell *p1, pl_ctx p1_ctx);
bool bif_statistics_0(query *q);
bool bif_sys_module_1(query *q);
bool bif_sys_create_var_1(query *q);
