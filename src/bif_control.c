#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

#include "module.h"
#include "query.h"

bool bif_iso_true_0(query *q)
{
	return true;
}

bool bif_iso_fail_0(query *q)
{
	return false;
}

bool bif_iso_cut_0(query *q)
{
	cut(q);
	return true;
}

bool bif_sys_cut_1(query *q)
{
	GET_FIRST_ARG(p1,integer)
	choice *ch = GET_CHOICE(get_smalluint(p1));
	ch->reset = true;
	return true;
}

// goal , goal

bool bif_iso_conjunction_2(query *q)
{
	q->total_inferences--;
	q->retry = QUERY_NOOP;
	q->st.instr++;
	return true;
}

static bool bif_sys_cleanup_if_det_1(query *q)
{
	q->total_inferences--;
	GET_FIRST_RAW_ARG(p1,integer)
	choice *ch = GET_CURR_CHOICE();

	if ((q->st.cp-1) != get_smalluint(p1))
		return true;

	drop_choice(q);
	ch = GET_CURR_CHOICE();

	if (!ch->register_cleanup)
		return true;

	if (ch->fail_on_retry)
		return true;

	drop_choice(q);
	ch->fail_on_retry = true;
	cell *c = deref(q, ch->st.instr, ch->st.cur_ctx);
	pl_ctx c_ctx = q->latest_ctx;
	c = deref(q, FIRST_ARG(c), c_ctx);
	c_ctx = q->latest_ctx;
	do_cleanup(q, c, c_ctx);
	return true;
}

bool call_check(query *q, cell *p1, bool *status, bool calln)
{
	cell *save_p1 = p1;

	if (calln || !get_arity(p1)) {
		bool found = false;

		if ((p1->match = search_predicate(q->st.m, p1)) != NULL) {
			p1->flags &= ~FLAG_INTERNED_BUILTIN;
		} else if ((p1->bif_ptr = get_builtin_term(q->st.m, p1, &found, NULL)), found) {
			p1->flags |= FLAG_INTERNED_BUILTIN;

			if (calln && (get_arity(p1) <= 2)) {
				const char *functor = C_STR(q, p1);
				unsigned specifier;

				if (search_op(q->st.m, functor, &specifier, false))
					SET_OP(p1, specifier);
			}
		} else {
			p1->flags &= ~FLAG_INTERNED_BUILTIN;
		}
	}

	if ((get_arity(p1) == 2) && is_builtin(p1)
		&& ((p1->val_off == g_conjunction_s)
			|| (p1->val_off == g_disjunction_s)
			|| (p1->val_off == g_if_then_s)
			|| (p1->val_off == g_soft_cut_s)
			)
		&& (p1 = check_body_callable(p1)) != NULL) {
		*status = throw_error(q, save_p1, q->st.cur_ctx, "type_error", "callable");
		return false;
	}

	*status = true;
	return true;
}

bool bif_call_0(query *q, cell *p1, pl_ctx p1_ctx)
{
	if (!is_callable(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "callable");

	if (!p1->match) {
		bool status;

		if (!call_check(q, p1, &status, true))
			return status;
	}

	cell *tmp = prepare_call(q, CALL_SKIP, p1, p1_ctx, 3);
	CHECKED(tmp);
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_call(q, tmp+num_cells);
	CHECKED(push_fail_on_retry_with_barrier(q));
	q->st.instr = tmp;
	return true;
}

static bool bif_iso_call_n(query *q)
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

	cell *tmp = prepare_call(q, CALL_NOSKIP, tmp2, q->st.cur_ctx, 3);
	CHECKED(tmp);
	tmp->flags &= ~FLAG_INTERNED_TAIL_CALL;
	pl_idx num_cells = tmp2->num_cells;
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_call(q, tmp+num_cells);
	CHECKED(push_fail_on_retry_with_barrier(q));

	if (is_tail_call(q->st.instr))
		tmp->flags |= FLAG_INTERNED_TAIL_CALL;

	q->st.instr = tmp;
	return true;
}

bool bif_iso_call_1(query *q)
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

	if ((is_builtin(p1) && (get_arity(p1) == 2)) || !get_arity(p1)) {
		CHECKED(init_tmp_heap(q));
		p1 = clone_term_to_tmp(q, p1, p1_ctx);
		CHECKED(p1);
		bool status;
		p1_ctx = q->st.cur_ctx;

		if (!call_check(q, p1, &status, false))
			return status;
	}

	cell *tmp = prepare_call(q, CALL_NOSKIP, p1, p1_ctx, 3);
	CHECKED(tmp);
	tmp->flags &= ~FLAG_INTERNED_TAIL_CALL;
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_call(q, tmp+num_cells);
	CHECKED(push_fail_on_retry_with_barrier(q));

	if (is_tail_call(q->st.instr))
		tmp->flags |= FLAG_INTERNED_TAIL_CALL;

	q->st.instr = tmp;
	return true;
}

// goal, !

static bool bif_iso_once_1(query *q)
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

	if ((is_builtin(p1) && (get_arity(p1) == 2)) || !get_arity(p1)) {
		CHECKED(init_tmp_heap(q));
		p1 = clone_term_to_tmp(q, p1, p1_ctx);
		CHECKED(p1);
		bool status;
		p1_ctx = q->st.cur_ctx;

		if (!call_check(q, p1, &status, false))
			return status;
	}

	cell *tmp = prepare_call(q, CALL_NOSKIP, p1, p1_ctx, 4);
	CHECKED(tmp);
	tmp->flags &= ~FLAG_INTERNED_TAIL_CALL;
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_cut_s, bif_iso_cut_0, 0, 0);
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_call(q, tmp+num_cells);
	CHECKED(push_fail_on_retry_with_barrier(q));

	if (is_tail_call(q->st.instr))
		tmp->flags |= FLAG_INTERNED_TAIL_CALL;

	q->st.instr = tmp;
	return true;
}

// if -> ! ; true

static bool bif_ignore_1(query *q)
{
	GET_FIRST_ARG(p1,callable);

	if ((is_builtin(p1) && (get_arity(p1) == 2)) || !get_arity(p1)) {
		CHECKED(init_tmp_heap(q));
		p1 = clone_term_to_tmp(q, p1, p1_ctx);
		CHECKED(p1);
		p1_ctx = q->st.cur_ctx;
		bool status;

		if (!call_check(q, p1, &status, false))
			return status;
	}

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

	cell *tmp = prepare_call(q, CALL_NOSKIP, p1, q->st.cur_ctx, 4);
	CHECKED(tmp);
	tmp->flags &= ~FLAG_INTERNED_TAIL_CALL;
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_cut_s, bif_iso_cut_0, 0, 0);
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_call(q, tmp+num_cells);
	CHECKED(push_succeed_on_retry_with_barrier(q, 0));
	q->st.instr = tmp;
	return true;
}

// if -> then

bool bif_iso_if_then_2(query *q)
{
	GET_FIRST_ARG(p1,callable);
	GET_NEXT_ARG(p2,callable);
	cell *tmp = prepare_call(q, CALL_NOSKIP, p1, p1_ctx, 3+p2->num_cells+2);
	CHECKED(tmp);
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_cut_s, bif_iso_cut_0, 0, 0);
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	num_cells += dup_cells_by_ref(tmp+num_cells, p2, p2_ctx, p2->num_cells);
	make_instr(tmp+num_cells++, g_true_s, bif_iso_true_0, 0, 0); // Why???
	make_call(q, tmp+num_cells);
	CHECKED(push_fail_on_retry_with_barrier(q));
	q->st.instr = tmp;
	return true;
}

// if *-> then

bool bif_soft_if_then_2(query *q)
{
	GET_FIRST_ARG(p1,callable);
	GET_NEXT_ARG(p2,callable);
	cell *tmp = prepare_call(q, CALL_NOSKIP, p1, p1_ctx, 2+p2->num_cells+2);
	CHECKED(tmp);
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	num_cells += dup_cells_by_ref(tmp+num_cells, p2, p2_ctx, p2->num_cells);
	make_instr(tmp+num_cells++, g_true_s, bif_iso_true_0, 0, 0); // Why???
	make_call(q, tmp+num_cells);
	CHECKED(push_fail_on_retry_with_barrier(q));
	q->st.instr = tmp;
	return true;
}

// if -> then ; else

static bool do_if_then_else(query *q, cell *p1, cell *p2, cell *p3)
{
	// FIXME: memory waste, but see docs/norecov.md
	frame *f = GET_CURR_FRAME();
	f->no_recov = true;
	cell *tmp = prepare_call(q, CALL_NOSKIP, p1, q->st.cur_ctx, 3+p2->num_cells+2);
	CHECKED(tmp);
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_cut_s, bif_iso_cut_0, 0, 0);
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	num_cells += dup_cells_by_ref(tmp+num_cells, p2, q->st.cur_ctx, p2->num_cells);
	make_instr(tmp+num_cells++, g_true_s, bif_iso_true_0, 0, 0); // Why???
	make_call(q, tmp+num_cells);
	CHECKED(push_barrier(q));
	q->st.instr = tmp;
	return true;
}

// if *-> then ; else

static bool do_soft_if_then_else(query *q, cell *p1, cell *p2, cell *p3)
{
	// FIXME: memory waste, but see docs/norecov.md
	frame *f = GET_CURR_FRAME();
	f->no_recov = true;
	cell *tmp = prepare_call(q, CALL_NOSKIP, p1, q->st.cur_ctx, 4+p2->num_cells+2);
	CHECKED(tmp);
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_sys_cut_s, bif_sys_cut_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	num_cells += dup_cells_by_ref(tmp+num_cells, p2, q->st.cur_ctx, p2->num_cells);
	make_instr(tmp+num_cells++, g_true_s, bif_iso_true_0, 0, 0); // Why???
	make_call(q, tmp+num_cells);
	CHECKED(push_barrier(q));
	q->st.instr = tmp;
	return true;
}

// if(if,then,else)

static bool bif_if_3(query *q)
{
	cell *p1 = q->st.instr + 1;
	cell *p2 = p1 + p1->num_cells;
	cell *p3 = p2 + p2->num_cells;

	if (q->retry) {
		q->st.instr = p3;
		q->retry = QUERY_NOOP;
		return true;
	}

	return do_soft_if_then_else(q, p1, p2, p3);
}

// goal ; goal

static bool bif_iso_disjunction_2(query *q)
{
	if (q->retry) {
		q->st.instr++;
		return true;
	}

	cell *c = q->st.instr+1;

	if (is_callable(c) && c->bif_ptr) {
		if (c->bif_ptr->fn == bif_iso_if_then_2) {
			cell *p1 = c + 1;
			cell *p2 = p1 + p1->num_cells;
			cell *p3 = p2 + p2->num_cells;
			return do_if_then_else(q, p1, p2, p3);
		}

		if (c->bif_ptr->fn == bif_soft_if_then_2) {
			cell *p1 = c + 1;
			cell *p2 = p1 + p1->num_cells;
			cell *p3 = p2 + p2->num_cells;
			return do_soft_if_then_else(q, p1, p2, p3);
		}
	}

	GET_FIRST_ARG(p1,callable);
	cell *tmp = prepare_call(q, CALL_NOSKIP, p1, p1_ctx, 2);
	CHECKED(tmp);
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_true_s, bif_iso_true_0, 0, 0); // Why???
	make_call(q, tmp+num_cells);
	CHECKED(push_choice(q));
	q->st.instr = tmp;
	return true;
}

// if -> !, fail ; true

static bool bif_iso_negation_1(query *q)
{
	GET_FIRST_ARG(p1,callable);

	if ((is_builtin(p1) && (get_arity(p1) == 2)) || !get_arity(p1)) {
		CHECKED(init_tmp_heap(q));
		cell *p1_clone = clone_term_to_tmp(q, p1, p1_ctx);
		CHECKED(p1_clone);
		bool status;

		if (!call_check(q, p1_clone, &status, false))
			return status;
	}

	cell *tmp = prepare_call(q, CALL_NOSKIP, p1, p1_ctx, 5);
	CHECKED(tmp);
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_cut_s, bif_iso_cut_0, 0, 0);
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_instr(tmp+num_cells++, g_fail_s, bif_iso_fail_0, 0, 0);
	make_call(q, tmp+num_cells);
	CHECKED(push_succeed_on_retry_with_barrier(q, 0));
	q->st.instr = tmp;
	return true;
}

// Clears a stale choice index an untrailed binding left in a compiled control var.

static cell *get_control_var(query *q, pl_ctx *p1_ctx)
{
	cell *c = q->st.instr + 1;
	pl_ctx c_ctx = q->st.cur_ctx;

	if (is_ref(c))
		c_ctx = c->val_ctx;
	else if (!is_var(c))
		return NULL;

	slot *e = get_slot(q, GET_FRAME(c_ctx), c->var_num);
	cell *p1 = deref(q, c, c_ctx);
	*p1_ctx = q->latest_ctx;

	if (is_var(p1))
		return p1;

	if (!is_integer(p1))
		return NULL;

	unshare_cell(&e->c);
	memset(e, 0, sizeof(slot));
	*p1_ctx = c_ctx;
	return &e->c;
}

bool bif_sys_block_catcher_1(query *q)
{
	cell *p1 = get_first_raw_arg(q);

	// Compiled catch/3 passes a var bound to the index, catch/3 the index itself.

	if (is_var(p1))
		p1 = get_first_arg(q);

	pl_idx cp = get_smalluint(p1);
	choice *ch = GET_CHOICE(cp);

	if (!ch->catchme_retry)
		return false;

	if (q->retry) {
		ch->block_catcher = false;
		return false;
	}

	if (drop_barrier(q, cp))
		return true;

	ch->block_catcher = true;
	CHECKED(push_choice(q));
	return true;
}

static bool bif_sys_abort_0(query *q);

static bool bif_iso_catch_3(query *q)
{
	GET_FIRST_ARG(p1,any);

	if (q->retry && q->ball) {
		GET_NEXT_ARG(p2,any);

		if (unify(q, p2, p2_ctx, q->ball, q->ball_ctx))
			return true;

		if (is_var(p2) && (q->oom || q->error || q->in_throw)) {
			reset_var(q, p2, p2_ctx, q->ball, q->ball_ctx);
			return true;
		}
		return false;
	}

	// Second time through? Try the recover goal...

	if ((q->retry == QUERY_EXCEPTION) || (q->retry == QUERY_ABORT)) {
		unsigned is_abort = q->retry == QUERY_ABORT;
		GET_NEXT_ARG(p2,any);
		GET_NEXT_ARG(p3,any);
		q->retry = QUERY_OK;
		cell tmp2;
		make_instr(&tmp2, g_call_s, bif_iso_call_1, 1, 0);
		cell *tmp = prepare_call(q, CALL_NOSKIP, &tmp2, p3_ctx, p3->num_cells+3+ is_abort);
		CHECKED(tmp);
		tmp->num_cells += p3->num_cells;
		pl_idx num_cells = 1;
		num_cells += dup_cells_by_ref(tmp+num_cells, p3, p3_ctx, p3->num_cells);
		make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
		make_uint(tmp+num_cells++, q->st.cp);
		if (is_abort) make_instr(tmp+num_cells++, g_sys_abort_s, bif_sys_abort_0, 0, 0);
		make_call(q, tmp+num_cells);
		if (!push_catcher(q, QUERY_EXCEPTION))
			return false;

		// NOT q->oom: see throw_error(). An oom still set here was never
		// turned into a ball, so the allocation that failed is still
		// unaccounted for and execution must not continue over it.

		q->error = false;
		q->st.instr = tmp;
		return true;
	}

	if (q->retry)
		return false;

	// First time through? Try the primary goal...

	cell tmp2;
	make_instr(&tmp2, g_call_s, bif_iso_call_1, 1, 0);
	cell *tmp = prepare_call(q, CALL_NOSKIP, &tmp2, p1_ctx, p1->num_cells+3);
	CHECKED(tmp);
	tmp->num_cells += p1->num_cells;
	pl_idx num_cells = 1;
	num_cells += dup_cells_by_ref(tmp+num_cells, p1, p1_ctx, p1->num_cells);
	make_instr(tmp+num_cells++, g_sys_block_catcher_s, bif_sys_block_catcher_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_call(q, tmp+num_cells);
	CHECKED(push_catcher(q, QUERY_RETRY));
	q->st.instr = tmp;
	return true;
}

// Compiled catch/3: $catch(Var, Catcher, Skip), then Goal, then Skip cells on a landing before Recovery.

bool bif_sys_catch_3(query *q)
{
	if (q->retry && q->ball) {
		GET_FIRST_ARG(p1,any);
		GET_NEXT_ARG(p2,any);

		if (unify(q, p2, p2_ctx, q->ball, q->ball_ctx))
			return true;

		if (is_var(p2) && (q->oom || q->error || q->in_throw)) {
			reset_var(q, p2, p2_ctx, q->ball, q->ball_ctx);
			return true;
		}

		return false;
	}

	if ((q->retry == QUERY_EXCEPTION) || (q->retry == QUERY_ABORT)) {
		GET_FIRST_ARG(p1,any);
		GET_NEXT_ARG(p2,any);
		GET_NEXT_ARG(p3,integer);
		bool is_abort = q->retry == QUERY_ABORT;
		q->retry = QUERY_OK;
		p1 = get_control_var(q, &p1_ctx);
		CHECKED(p1);

		// $catch_exit reads a negative index as an unwind to rethrow.

		cell tmp;
		make_int(&tmp, is_abort ? -(pl_int)q->st.cp-1 : (pl_int)q->st.cp);
		CHECKED(push_catcher(q, QUERY_EXCEPTION));
		CHECKED(unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx));

		// NOT q->oom: see bif_iso_catch_3().

		q->error = false;

		// Not noskip: some callers run the instruction before stepping past it, so Recovery's first goal ran twice.

		q->st.instr += get_smalluint(p3);
		return true;
	}

	if (q->retry)
		return false;

	pl_ctx p1_ctx;
	cell *p1 = get_control_var(q, &p1_ctx);
	CHECKED(p1);
	cell tmp;
	make_uint(&tmp, (pl_uint)q->st.cp);
	CHECKED(push_catcher(q, QUERY_RETRY));
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

bool bif_sys_catch_exit_1(query *q)
{
	GET_FIRST_ARG(p1,integer)
	pl_int v = get_smallint(p1);
	pl_idx cp = v < 0 ? -v-1 : v;
	q->total_inferences--;

	drop_barrier(q, cp);
	return v < 0 ? bif_sys_abort_0(q) : true;
}

bool bif_sys_set_if_var_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);

	if (!is_var(p1))
		return true;

	return unify(q, p1, p1_ctx, p2, p2_ctx);
}

static bool bif_reset_3(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	GET_NEXT_ARG(p3,any);

	cell *tmp = prepare_call(q, CALL_NOSKIP, p1, p1_ctx, 3+p3->num_cells+2);
	CHECKED(tmp);
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_instr(tmp+num_cells++, g_sys_set_if_var_s, bif_sys_set_if_var_2, 2, p3->num_cells+1);
	num_cells += dup_cells_by_ref(tmp+num_cells, p3, p3_ctx, p3->num_cells);
	make_atom(tmp+num_cells++, g_none_s);
	make_call(q, tmp+num_cells);
	CHECKED(push_reset_handler(q));
	q->st.instr = tmp;
	return true;
}

static bool find_reset_handler(query *q)
{
	if (!q->st.cp)
		return false;

	for (pl_idx cp = q->st.cp; cp > 0; ) {
		choice *ch = GET_CHOICE(--cp);
		if (ch->reset) {
			ch->reset = false;
			q->st.instr = ch->st.instr;
			q->st.cur_ctx = ch->st.cur_ctx;
			q->st.m = ch->st.m;
			GET_FIRST_ARG0(p1, any, ch->st.instr);
			GET_NEXT_ARG(p2, any);
			GET_NEXT_ARG(p3, any);
			cell tmp;

			if (!q->ball) {
				make_atom(&tmp, g_none_s);
				return unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
			}

			if (!unify(q, p2, p2_ctx, q->ball, q->ball_ctx))
				return false;

			q->ball = NULL;

			if (!unify(q, p3, p3_ctx, q->cont, q->cont_ctx))
				return false;

			return true;
		}

	}

	return false;
}

// Collect every pending goal from the shift point up to (but excluding)
// the reset barrier, appending each as a (cell*, ctx) pair. Two passes:
// pass==0 just counts (goals + total goal cells), pass==1 fills the arrays.

// Scan a run of goals starting at c (in ctx cc), appending each until the
// clause end. Stops early (and sets *hit) at the reset's $drop_barrier
// instruction, which marks the boundary of the captured continuation.

static void scan_cont_segment(query *q, cell *c, pl_ctx cc, int pass,
	cell **goals, pl_ctx *ctxs, unsigned *pn, unsigned *ptotal, bool *hit,
	pl_idx reset_cp)
{
	while (c && !is_end(c)) {
		// $drop_barrier ends the captured continuation ONLY when it is
		// the barrier planted by our own reset/3: its operand is the
		// choice-point index of that reset. Barriers from call/1,
		// catch/3, findall/3 etc. sit BETWEEN the shift and the reset
		// (eg. a tabled call under call/1, as Logtalk's debug wrapper
		// does) and must be skipped, not treated as the delimiter -
		// otherwise the continuation is truncated and answers come back
		// with unbound variables.

		if (is_interned(c) && (c->val_off == g_sys_drop_barrier_s)) {
			if ((get_arity(c) == 1) && is_smallint(c+1)
				&& (get_smallint(c+1) == (pl_int)reset_cp)) {
				*hit = true;
				return;
			}

			c += c->num_cells;
			continue;
		}

		// Compiled if-then-else: a suspension inside a then-branch sees
		// the jump-over-the-else in its pending goals. Follow the jump
		// (its operand is the cell distance from the jump cell itself
		// to the landing cell) instead of capturing it as a goal.

		if (is_interned(c) && (c->val_off == g_sys_jump_s) && (get_arity(c) == 1)) {
			c += get_smallint(c+1);
			continue;
		}

		// The landing cell of an if-then-else is a bare true/0: harmless
		// to capture, skip it to keep continuations tidy.

		if (is_interned(c) && (c->val_off == g_true_s) && !get_arity(c) && c->bif_ptr) {
			c += c->num_cells;
			continue;
		}

		if (pass) { goals[*pn] = c; ctxs[*pn] = cc; }
		*ptotal += c->num_cells;
		(*pn)++;
		c += c->num_cells;
	}
}

// Collect every pending goal from the shift point up to (but excluding)
// the nearest reset barrier. The barrier is recognised by the $drop_barrier
// instruction that reset/3 installs after its goal. Two passes: pass==0
// counts, pass==1 fills the arrays.

static unsigned collect_cont_goals(query *q, int pass,
	cell **goals, pl_ctx *ctxs, unsigned *out_total_cells, pl_idx reset_cp)
{
	unsigned n = 0, total = 0;
	bool hit = false;

	// Segment 0: the rest of the shift's own clause body.
	scan_cont_segment(q, q->st.instr + q->st.instr->num_cells, q->st.cur_ctx,
		pass, goals, ctxs, &n, &total, &hit, reset_cp);

	// Walk up the frame chain; each frame contributes its caller's goals
	// *after* the call that entered it (f->instr points at the call site).
	pl_ctx f_ctx = q->st.cur_ctx;

	while (!hit) {
		const frame *f = GET_FRAME(f_ctx);

		if (f->prev == (pl_ctx)CTX_NUL)
			break;

		if (f->instr)
			scan_cont_segment(q, f->instr + f->instr->num_cells, f->prev,
				pass, goals, ctxs, &n, &total, &hit, reset_cp);

		if (f->prev == f_ctx)
			break;

		f_ctx = f->prev;
	}

	if (out_total_cells) *out_total_cells = total;
	return n;
}

static bool bif_shift_1(query *q)
{
	GET_FIRST_ARG(p1,nonvar);
	q->ball = p1;
	q->ball_ctx = p1_ctx;

	// Index of the nearest enclosing reset/3 choice point: reset planted
	// a $drop_barrier carrying exactly this value.

	pl_idx reset_cp = 0;
	bool have_barrier = false;

	if (q->st.cp) {
		for (pl_idx cp = q->st.cp; cp > 0; ) {
			choice *ch = GET_CHOICE(--cp);
			if (ch->reset) {
				reset_cp = cp;
				have_barrier = true;
				break;
			}
		}
	}

	if (have_barrier) {
		unsigned total_cells = 0;
		unsigned n = collect_cont_goals(q, 0, NULL, NULL, &total_cells, reset_cp);

		if (n == 0) {
			// Empty continuation. NB. must be an executable true/0
			// instruction (with its builtin resolved), not a bare atom:
			// fabricated cells bypass the compiler's builtin lookup.
			cell *tmp2 = alloc_heap(q, 2);
			make_instr(tmp2, g_cont_s, NULL, 1, 1);
			make_instr(tmp2+1, g_true_s, bif_iso_true_0, 0, 0);
			q->cont = tmp2;
			q->cont_ctx = q->st.cur_ctx;
			return find_reset_handler(q);
		}

		cell **goals = TPL_malloc(sizeof(cell*) * n);
		pl_ctx *ctxs = TPL_malloc(sizeof(pl_ctx) * n);
		CHECKED(goals); CHECKED(ctxs);
		collect_cont_goals(q, 1, goals, ctxs, NULL, reset_cp);

		// Right-nested conjunction: (n-1) comma cells + total goal cells,
		// wrapped in cont(...). If n==1 there are no commas.
		unsigned conj_cells = total_cells + (n - 1);
		cell *tmp2 = alloc_heap(q, 1 + conj_cells);

		if (!tmp2) { TPL_free(goals); TPL_free(ctxs); return false; }

		make_instr(tmp2, g_cont_s, NULL, 1, conj_cells);
		cell *dst = tmp2 + 1;

		for (unsigned i = 0; i < n; i++) {
			if (i < n - 1) {
				// remaining cells after this comma header
				unsigned rest = 0;
				for (unsigned j = i; j < n; j++) rest += goals[j]->num_cells;
				rest += (n - 1 - i);	// remaining comma cells
				make_instr(dst, g_conjunction_s, bif_iso_conjunction_2, 2, rest);
				SET_OP(dst, OP_XFY);
				dst++;
			}

			dst += copy_cells_by_ref(dst, goals[i], ctxs[i], goals[i]->num_cells);
		}

		TPL_free(goals); TPL_free(ctxs);
		q->cont = tmp2;
		q->cont_ctx = q->st.cur_ctx;
		return find_reset_handler(q);
	}

	// Fallback: original single-clause capture.
	cell *next = q->st.instr + q->st.instr->num_cells;
	cell *tmp2 = alloc_heap(q, 1+next->num_cells);
	make_instr(tmp2, g_cont_s, NULL, 1, next->num_cells);
	dup_cells_by_ref(tmp2+1, next, q->st.cur_ctx, next->num_cells);
	q->cont = tmp2;
	q->cont_ctx = q->st.cur_ctx;
	return find_reset_handler(q);
}

bool bif_sys_call_cleanup_3(query *q)
{
	GET_FIRST_ARG(p1,any);

	if (q->retry && q->ball) {
		GET_NEXT_ARG(p2,any);
		cell *tmp = clone_term_to_heap(q, q->ball, q->ball_ctx);
		if (!tmp) {
			if (is_var(p2)) {
				reset_var(q, p2, p2_ctx, q->ball, q->ball_ctx);
				return true;
			}
			return false;
		}
		if (unify(q, p2, p2_ctx, tmp, q->st.cur_ctx))
			return true;
		if (is_var(p2) && (q->oom || q->error || q->in_throw)) {
			reset_var(q, p2, p2_ctx, q->ball, q->ball_ctx);
			return true;
		}
		return false;
	}

	// Second time through? Try the recover goal...

	if (q->retry == QUERY_EXCEPTION) {
		GET_NEXT_ARG(p2,any);
		GET_NEXT_ARG(p3,callable);
		q->retry = QUERY_OK;
		cell tmp2;
		make_instr(&tmp2, g_call_s, bif_iso_call_1, 1, 0);
		cell *tmp = prepare_call(q, CALL_NOSKIP, &tmp2, p3_ctx, p3->num_cells+3);
		if (!tmp)
			return false;
		tmp->num_cells += p3->num_cells;
		pl_idx num_cells = 1;
		num_cells += dup_cells_by_ref(tmp+num_cells, p3, p3_ctx, p3->num_cells);
		make_instr(tmp+num_cells++, g_sys_cleanup_if_det_s, bif_sys_cleanup_if_det_1, 1, 1);
		make_uint(tmp+num_cells++, q->st.cp);
		make_call(q, tmp+num_cells);
		if (!push_catcher(q, QUERY_EXCEPTION))
			return false;

		// NOT q->oom: see throw_error(). An oom still set here was never
		// turned into a ball, so the allocation that failed is still
		// unaccounted for and execution must not continue over it.

		q->error = false;
		q->st.instr = tmp;
		return true;
	}

	if (q->retry)
		return false;

	// First time through? Try the primary goal...

	cell *tmp = prepare_call(q, CALL_NOSKIP, p1, p1_ctx, 3);
	CHECKED(tmp);
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_sys_cleanup_if_det_s, bif_sys_cleanup_if_det_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_call(q, tmp+num_cells);
	CHECKED(push_catcher(q, QUERY_RETRY));
	q->st.instr = tmp;
	return true;
}

bool bif_sys_call_check_1(query *q)
{
	GET_FIRST_ARG(p1,callable);

	if ((is_builtin(p1) && !is_evaluable(p1)
		&& ((p1->val_off == g_conjunction_s) || (p1->val_off == g_disjunction_s)
			|| (p1->val_off == g_if_then_s) || (p1->val_off == g_soft_cut_s))
		) || !get_arity(p1)) {
		CHECKED(init_tmp_heap(q));
		p1 = clone_term_to_tmp(q, p1, p1_ctx);
		CHECKED(p1);
		bool status;
		return call_check(q, p1, &status, false) ? true : status;
	}

	return true;
}

static bool bif_sys_register_cleanup_1(query *q)
{
	if (q->retry) {
		GET_FIRST_ARG(p1,callable);
		cell *tmp = prepare_call(q, CALL_NOSKIP, p1, p1_ctx, 3);
		pl_idx num_cells = p1->num_cells;
		make_instr(tmp+num_cells++, g_cut_s, bif_iso_cut_0, 0, 0);
		make_instr(tmp+num_cells++, g_fail_s, bif_iso_fail_0, 0, 0);
		make_call(q, tmp+num_cells);
		q->st.instr = tmp;
		return true;
	}

	CHECKED(push_choice(q));
	choice *ch = GET_CURR_CHOICE();
	ch->register_cleanup = true;
	return true;
}

bool bif_sys_get_level_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	cell tmp;
	make_int(&tmp, q->st.cp);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

bool bif_sys_drop_barrier_1(query *q)
{
	GET_FIRST_ARG(p1,integer)
	q->total_inferences--;
	drop_barrier(q, get_smalluint(p1));
	return true;
}

bool bif_sys_fail_on_retry_1(query *q)
{
	cell *c = q->st.instr + 1;
	pl_ctx c_ctx = q->st.cur_ctx;
	unsigned var_num;

	if (is_ref(c)) {
		c_ctx = c->val_ctx;
		var_num = c->var_num;
	} else if (is_var(c)) {
		var_num = c->var_num;
	} else {
		return throw_error(q, c, c_ctx, "type_error", "var");
	}

	slot *e = get_slot(q, GET_FRAME(c_ctx), var_num);
	cell *p1 = deref(q, c, c_ctx);
	pl_ctx p1_ctx = q->latest_ctx;

	if (!is_var(p1)) {
		if (!is_integer(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "var");
		unshare_cell(&e->c);
		memset(e, 0, sizeof(slot));
		p1 = &e->c;
		p1_ctx = c_ctx;
	}

	cell tmp;
	make_uint(&tmp, (pl_uint)q->st.cp);
	CHECKED(push_fail_on_retry_with_barrier(q));
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

bool bif_sys_succeed_on_retry_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	CHECKED(push_succeed_on_retry(q, get_smalluint(p1)));
	return true;
}

bool bif_sys_succeed_on_retry_2(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,integer);
	cell tmp;
	make_uint(&tmp, (pl_uint)q->st.cp);
	CHECKED(push_succeed_on_retry_with_barrier(q, get_smalluint(p2)));
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}


static pl_idx restore_streams_walk(query *q, const cell *src, cell *dst)
{
	if (is_compound(src) && (get_arity(src) == 1) && !strcmp(C_STR(q, src), "$stream")) {
		const cell *arg = src + 1;

		if (is_smallint(arg)) {
			make_int(dst, get_smallint(arg));
			dst->flags |= FLAG_INT_STREAM;
			return 1;
		}
	}

	if (is_compound(src)) {
		*dst = *src;
		pl_idx cnt = 1;
		const cell *p = src + 1;

		for (unsigned i = 0; i < get_arity(src); i++) {
			cnt += restore_streams_walk(q, p, dst + cnt);
			p += p->num_cells;
		}

		dst->num_cells = cnt;
		return cnt;
	}

	pl_idx nc = src->num_cells;

	for (pl_idx i = 0; i < nc; i++)
		dst[i] = src[i];

	return nc;
}

static void restore_streams(query *q, cell *ball)
{
	if (!ball || !is_compound(ball))
		return;

	cell *buf = TPL_malloc(sizeof(cell) * ball->num_cells);
	if (!buf) return;
	pl_idx n = restore_streams_walk(q, ball, buf);
	memcpy(ball, buf, sizeof(cell) * n);
	TPL_free(buf);
}

static cell *parse_to_heap(query *q, const char *src)
{
	SB(s);
	SB_sprintf(s, "%s.", src);
	parser *p2 = parser_create(q->st.m);
	check_error(p2);
	frame *f = GET_CURR_FRAME();
	p2->read_term_slots = f->actual_slots;
	p2->skip = true;
	p2->srcptr = SB_cstr(s);
	tokenize(p2, false, false);
	process_clause(p2->m, p2->cl, NULL);
	p2->read_term_slots = 0;
	SB_free(s);

	if (p2->num_vars) {
		if (create_vars(q, p2->num_vars) < 0) {
			parser_destroy(p2);
			return NULL;
		}
	}


	cell *tmp = alloc_heap(q, p2->cl->cells->num_cells);
	if (!tmp) return NULL;
	dup_cells(tmp, p2->cl->cells, p2->cl->cells->num_cells);
	check_error(tmp, parser_destroy(p2));
	parser_destroy(p2);
	restore_streams(q, tmp);
	return tmp;
}

// Non-static so a bif can raise a ball it built itself, rather than one
// throw_error3() composes from a type/expected pair. bif_dcgs.c needs
// this to reproduce library/dcgs.pl's [culprit-Term] error context,
// which is not a Name/Arity context and so cannot come from
// throw_error3(). Ownership of `ball` stays with the caller.

// Sentinel control-throws ($abort, unwind/1) are always shaped by
// throw_error3() as error(Sentinel, Context) - never bare. A ball from
// plain throw/1 (e.g. throw(bar)) has no such guarantee: peeking at
// ball+1 without first confirming that shape reads whatever heap cell
// happens to follow a 1-cell ball, which is how issue #1152 turned a
// deeply nested throw into a bogus "$abort"/"unwind" match.

static bool is_sentinel_control(query *q, cell *ball, const char *name)
{
	return is_compound(ball) && (get_arity(ball) == 2)
		&& !CMP_STRING_TO_CSTR(q, ball, "error")
		&& !strcmp(C_STR(q, ball+1), name);
}

bool find_exception_handler(query *q, char *ball)
{
	errno = 0;

	while (retry_choice(q)) {
		const choice *ch = GET_CHOICE(q->st.cp);

		if (ch->block_catcher)
			continue;

		if (!ch->catchme_retry)
			continue;

		q->ball = parse_to_heap(q, ball);

		// Reparsing the ball needs memory, and the throw that got us
		// here may itself be the memory error. Failing silently would
		// turn the exception into a quiet failure, so flag it and let
		// the toplevel report a memory error, re issue #801.

		if (!q->ball) {
			q->oom = q->error = true;
			return false;
		}

		q->ball_ctx = q->st.cur_ctx;

		if (is_sentinel_control(q, q->ball, "$abort")) {
			break;
		} else if (is_sentinel_control(q, q->ball, "unwind")) {
			q->retry = QUERY_ABORT;
		} else {
			q->retry = QUERY_EXCEPTION;
		}

		// Use the catcher's own builtin (catch/3 or $call_cleanup/3).
		// Hardcoding bif_iso_catch_3 breaks $call_cleanup's rethrow path.

		builtins *bif = q->st.instr->bif_ptr;
		bool (*fn)(query *) = (bif && bif->fn) ? bif->fn : bif_iso_catch_3;

		if (!fn(q)) {
			q->ball = NULL;
			continue;
		}

		q->ball = NULL;

		// Phase 2: arm the recover goal. A nested throw during phase 1
		// may already have done this (retry cleared to QUERY_OK).

		if ((q->retry == QUERY_EXCEPTION) || (q->retry == QUERY_ABORT)) {
			if (!fn(q))
				continue;
		}

		return true;
	}

	cell *e = parse_to_heap(q, ball);
	pl_ctx e_ctx = q->st.cur_ctx;
	q->did_unhandled_exception = true;

	if (!e) {
		q->oom = q->error = true;
		return false;
	}

	if (is_sentinel_control(q, e, "unwind") || is_sentinel_control(q, e, "$abort")) {
		if (!q->is_thread && !q->is_task)
			fprintf(stdout, "%% Execution aborted\n");

		q->pl->did_dump_vars = true;
		q->ball = NULL;
		q->abort = true;
		return false;
	} else {
		q->ball = clone_term_to_heap(q, e, e_ctx);
		if (!q->ball)
			return false;
		q->ball_ctx = q->st.cur_ctx;
		rebase_term(q, q->ball, 0, false);
	}

	if (!q->thread_ptr) {
		prolog_lock(q->pl);

		if (!q->is_redo)
			fprintf(stdout, "   ");
		else
			fprintf(stdout, "  ");

		if (q->run_init && !q->error)
			fprintf(stdout, "\rWarning: Initialization goal exception: ");

		if (!q->run_init/*!is_interned(e)*/ && strcmp(C_STR(q, e), "error"))
			fprintf(stdout, "throw(");

		q->quoted = 1;
		print_term(q, stdout, e, e_ctx, 1);

		if (!q->run_init/*!is_interned(e)*/ && strcmp(C_STR(q, e), "error"))
			fprintf(stdout, ")");

		if (!is_empty(e)) fprintf(stdout, ".\n");
		q->quoted = 0;
		prolog_unlock(q->pl);
	}

	q->pl->did_dump_vars = true;
	q->error = true;
	q->abort = true;
	return false;
}

static bool bif_iso_throw_1(query *q)
{
	GET_FIRST_ARG(p1,any);

	if (is_var(p1))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "var");

	// This may be a saved memory error rethrown by call_cleanup/2. More
	// generally, an explicit throw can itself happen under memory pressure.

	release_oom_reserve(q);
	q->fullstop = q->nl = false;
	q->parens = q->numbervars = true;
	//q->is_dump_vars = true;
	q->quoted = true;
	char *ball = print_term_to_strbuf(q, p1, p1_ctx, 1);
	CHECKED(ball);
	clear_write_options(q);

	if (!find_exception_handler(q, ball)) {
		TPL_free(ball);
		return false;
	}

	TPL_free(ball);
	return true;
}

bool throw_error3(query *q, cell *c, pl_ctx c_ctx, const char *err_type, const char *expected, cell *goal)
{
	if (/*g_tpl_interrupt ||*/ q->halt || q->pl->halt)
		return false;

	if (q->in_throw) {
		q->oom = q->error = true;
		return false;
	}

	q->in_throw = true;
	q->did_throw = true;
	q->quoted = 0;

	if (!strncmp(expected, "iso_", 4))
		expected += 4;

	char tmpbuf[1024];
	snprintf(tmpbuf, sizeof(tmpbuf), "%s", expected);
	tmpbuf[sizeof(tmpbuf)-1] = '\0';
	char *ptr;

	if (!strcmp(err_type, "type_error")
		&& ((ptr = strstr(tmpbuf, "_or")) != NULL))
		*ptr = '\0';

	if (!strcmp(err_type, "type_error") && (!strcmp(expected, "stream") || !strcmp(expected, "stream_or_alias"))) {
		if (is_atom(c)) {
			err_type = "existence_error";
			expected = tmpbuf;
		} else if (is_smallint(c) && (c->flags & FLAG_INT_STREAM)) {
			err_type = "existence_error";
			expected = tmpbuf;
		} else
			err_type = "domain_error";
	} else
		expected = tmpbuf;

	char functor[1024];
	functor[0] = '\0';

	if (!strcmp(expected, "smallint"))
		expected = "integer";

	cell tmpc = {0}, *instr = q->st.instr ? q->st.instr : &tmpc;
	if (!c) c = &tmpc;
	if (!goal) goal = &tmpc;

	// A goal naming nothing took its name from atom index zero, printing dummy/0, re issue #801.

	const bool no_context = is_end(goal) || is_empty(goal);

	if (!is_var(c) || q->cycle_error) {
		char *tmpbuf = DUP_STRING(q, goal);
		snprintf(functor, sizeof(functor), "%s", tmpbuf);
		functor[sizeof(functor)-1] = '\0';
		TPL_free(tmpbuf);
	}

	int extra = 0;
	const char *eptr = expected;

	while ((eptr = strchr(eptr, ',')) != NULL) {
		extra += 1;
		eptr++;
	}

	bool is_abolish = false;

	if (!strcmp(C_STR(q, instr), "abolish"))
		is_abolish = true;

	if (!strcmp(C_STR(q, instr), "$call_check"))
		strcpy(functor, "call");

	bool is_builtin = false, evaluable = false;
	get_builtin_term(q->st.m, c, &is_builtin, &evaluable);
	bool is_op = search_op(q->st.m, C_STR(q, c), NULL, true) > 0;

	cell *tmp;

	if (is_var(c) && !q->cycle_error) {
		err_type = "instantiation_error";
		//printf("error(%s,%s).\n", err_type, expected);
		tmp = alloc_heap(q, 3);
		CHECKED(tmp);
		pl_idx num_cells = 0;
		make_instr(tmp+num_cells++, g_error_s, NULL, 2, 2);
		make_cstring(tmp+num_cells++, err_type);
		make_cstring(tmp+num_cells, expected);
	} else if (!strcmp(err_type, "unwind")) {
		//printf("error(%s(%s),(%s)/%u).\n", err_type, C_STR(q, c), functor, get_arity(goal));
		tmp = alloc_heap(q, 6+(c->num_cells-1));
		CHECKED(tmp);
		pl_idx num_cells = 0;
		make_instr(tmp+num_cells++, g_error_s, NULL, 2, 5+(c->num_cells-1));
		make_instr(tmp+num_cells++, new_atom(q->pl, err_type), NULL, 1, 1+(c->num_cells-1));
		dup_cells_by_ref(tmp+num_cells, c, c_ctx, c->num_cells);
		num_cells += c->num_cells;
		make_instr(tmp+num_cells, g_slash_s, NULL, 2, 2);
		SET_OP(tmp+num_cells, OP_YFX); num_cells++;
		make_atom(tmp+num_cells++, new_atom(q->pl, functor));
		make_int(tmp+num_cells, !is_string(goal)?get_arity(goal):0);
	} else if (!strcmp(err_type, "type_error") && !strcmp(expected, "var")) {
		err_type = "uninstantiation_error";
		//printf("error(%s(%s),(%s)/%u).\n", err_type, C_STR(q, c), functor, get_arity(goal));
		tmp = alloc_heap(q, 6+(c->num_cells-1));
		CHECKED(tmp);
		pl_idx num_cells = 0;
		make_instr(tmp+num_cells++, g_error_s, NULL, 2, 5+(c->num_cells-1));
		make_instr(tmp+num_cells++, new_atom(q->pl, err_type), NULL, 1, 1+(c->num_cells-1));
		dup_cells_by_ref(tmp+num_cells, c, c_ctx, c->num_cells);
		num_cells += c->num_cells;
		make_instr(tmp+num_cells, g_slash_s, NULL, 2, 2);
		SET_OP(tmp+num_cells, OP_YFX); num_cells++;
		make_atom(tmp+num_cells++, new_atom(q->pl, functor));
		make_int(tmp+num_cells, !is_string(goal)?get_arity(goal):0);
	} else if (!strcmp(err_type, "type_error") && !strcmp(expected, "evaluable")) {
		//printf("error(%s(%s,(%s)/%u),(%s)/%u).\n", err_type, expected, C_STR(q, c), get_arity(c), functor, get_arity(goal));
		tmp = alloc_heap(q, 9);
		CHECKED(tmp);
		pl_idx num_cells = 0;
		make_instr(tmp+num_cells++, g_error_s, NULL, 2, 8);
		make_instr(tmp+num_cells++, new_atom(q->pl, err_type), NULL, 2, 4);
		make_cstring(tmp+num_cells++, expected);
		make_instr(tmp+num_cells, g_slash_s, NULL, 2, 2);
		SET_OP(tmp+num_cells, OP_YFX); num_cells++;
		tmp[num_cells] = *c;
		share_cell(c);
		if (is_callable(c)) { tmp[num_cells].num_cells = 1; CLR_OP(tmp+num_cells); set_arity(&tmp[num_cells], 0); }
		num_cells++;
		make_int(tmp+num_cells++, get_arity(c));
		make_instr(tmp+num_cells, g_slash_s, NULL, 2, 2);
		SET_OP(tmp+num_cells, OP_YFX); num_cells++;
		make_atom(tmp+num_cells++, new_atom(q->pl, functor));
		make_int(tmp+num_cells, !is_string(goal)?get_arity(goal):0);
	} else if (!strcmp(err_type, "permission_error") && is_compound(c) && CMP_STRING_TO_CSTR(q, c, "/") && is_var(FIRST_ARG(c))) {
		//printf("error(%s(%s,(%s)/%u),(%s)/%u).\n", err_type, expected, tmpbuf, get_arity(c), functor, get_arity(goal));
		tmp = alloc_heap(q, 9+extra);
		CHECKED(tmp);
		pl_idx num_cells = 0;
		make_instr(tmp+num_cells++, g_error_s, NULL, 2, 8+extra);
		make_instr(tmp+num_cells++, new_atom(q->pl, err_type), NULL, 2+extra, 4+extra);

		if (!extra)
			make_cstring(tmp+num_cells++, expected);
		else {
			char tmpbuf[1024*8];
			strcpy(tmpbuf, expected);
			char *ptr = tmpbuf;
			char *ptr2 = strchr(ptr, ',');
			if (*ptr2) *ptr2++ = '\0';

			while (ptr2) {
				make_cstring(tmp+num_cells++, ptr);
				ptr = ptr2;
				ptr2 = strchr(ptr, ',');
			}

			make_cstring(tmp+num_cells++, ptr);
		}

		make_instr(tmp+num_cells, g_slash_s, NULL, 2, 2);
		SET_OP(tmp+num_cells, OP_YFX); num_cells++;
		tmp[num_cells] = *c;
		share_cell(c);
		if (is_callable(c)) { tmp[num_cells].num_cells = 1; CLR_OP(tmp+num_cells); set_arity(&tmp[num_cells], 0); }
		num_cells++;
		make_int(tmp+num_cells++, get_arity(c));
		make_instr(tmp+num_cells, g_slash_s, NULL, 2, 2);
		SET_OP(tmp+num_cells, OP_YFX); num_cells++;
		make_atom(tmp+num_cells++, new_atom(q->pl, functor));
		make_int(tmp+num_cells, !is_string(goal)?get_arity(goal):0);
	} else if (!strcmp(err_type, "permission_error") && (is_builtin || (is_op && get_arity(c))) && !is_abolish) {
		//printf("error(%s(%s,(%s)/%u),(%s)/%u).\n", err_type, expected, tmpbuf, get_arity(c), functor, get_arity(goal));
		tmp = alloc_heap(q, 9+extra);
		CHECKED(tmp);
		pl_idx num_cells = 0;
		make_instr(tmp+num_cells++, g_error_s, NULL, 2, 8+extra);
		make_instr(tmp+num_cells++, new_atom(q->pl, err_type), NULL, 2+extra, 4+extra);

		if (!extra)
			make_cstring(tmp+num_cells++, expected);
		else {
			char tmpbuf[1024*8];
			strcpy(tmpbuf, expected);
			char *ptr = tmpbuf;
			char *ptr2 = strchr(ptr, ',');
			if (*ptr2) *ptr2++ = '\0';

			while (ptr2) {
				make_cstring(tmp+num_cells++, ptr);
				ptr = ptr2;
				ptr2 = strchr(ptr, ',');
			}

			make_cstring(tmp+num_cells++, ptr);
		}

		make_instr(tmp+num_cells, g_slash_s, NULL, 2, 2);
		SET_OP(tmp+num_cells, OP_YFX); num_cells++;
		tmp[num_cells] = *c;
		share_cell(c);
		if (is_callable(c)) { tmp[num_cells].num_cells = 1; CLR_OP(tmp+num_cells); set_arity(&tmp[num_cells], 0); }
		num_cells++;
		make_int(tmp+num_cells++, get_arity(c));
		make_instr(tmp+num_cells, g_slash_s, NULL, 2, 2);
		SET_OP(tmp+num_cells, OP_YFX); num_cells++;
		make_atom(tmp+num_cells++, new_atom(q->pl, functor));
		make_int(tmp+num_cells, !is_string(goal)?get_arity(goal):0);
	} else if (!strcmp(err_type, "instantiation_error")) {
		//printf("error(%s,(%s)/%u).\n", err_type, functor, get_arity(goal));
		tmp = alloc_heap(q, 5);
		CHECKED(tmp);
		pl_idx num_cells = 0;
		make_instr(tmp+num_cells++, g_error_s, NULL, 2, 4);
		make_cstring(tmp+num_cells++,  err_type);
		make_instr(tmp+num_cells, g_slash_s, NULL, 2, 2);
		SET_OP(tmp+num_cells, OP_YFX); num_cells++;
		make_atom(tmp+num_cells++, new_atom(q->pl, functor));
		make_int(tmp+num_cells, !is_string(goal)?get_arity(goal):0);
	} else if (!strcmp(err_type, "existence_error") && !strcmp(expected, "procedure") && is_callable(c)) {
		//printf("error(%s(%s,(%s)/%u),(%s)/%u).\n", err_type, expected, tmpbuf, get_arity(c), functor, get_arity(goal));
		tmp = alloc_heap(q, 9);
		CHECKED(tmp);
		pl_idx num_cells = 0;
		make_instr(tmp+num_cells++, g_error_s, NULL, 2, 8);
		make_instr(tmp+num_cells++, new_atom(q->pl, err_type), NULL, 2, 4);
		make_cstring(tmp+num_cells++, expected);
		make_instr(tmp+num_cells, g_slash_s, NULL, 2, 2);
		SET_OP(tmp+num_cells, OP_YFX); num_cells++;
		make_atom(tmp+num_cells++, new_atom(q->pl, C_STR(q, c)));
		make_int(tmp+num_cells++, get_arity(c));
		make_instr(tmp+num_cells, g_slash_s, NULL, 2, 2);
		SET_OP(tmp+num_cells, OP_YFX); num_cells++;
		make_atom(tmp+num_cells++, new_atom(q->pl, functor));
		make_int(tmp+num_cells, !is_string(goal)?get_arity(goal):0);
	} else if (!strcmp(err_type, "representation_error")
		|| !strcmp(err_type, "evaluation_error")
		|| !strcmp(err_type, "syntax_error")
		|| !strcmp(err_type, "resource_error")) {
		//printf("error(%s(%s),(%s)/%u).\n", err_type, expected, functor, get_arity(goal));
		tmp = alloc_heap(q, 6);
		CHECKED(tmp);
		pl_idx num_cells = 0;
		make_instr(tmp+num_cells++, g_error_s, NULL, 2, 5);
		make_instr(tmp+num_cells++, new_atom(q->pl, err_type), NULL, 1, 1);
		make_cstring(tmp+num_cells++, expected);
		make_instr(tmp+num_cells, g_slash_s, NULL, 2, 2);
		SET_OP(tmp+num_cells, OP_YFX); num_cells++;
		make_atom(tmp+num_cells++, new_atom(q->pl, functor));
		make_int(tmp+num_cells, !is_string(goal)?get_arity(goal):0);
	} else {
		//printf("error(%s(%s,(%s)),(%s)/%u).\n", err_type, expected, C_STR(q, c), functor, get_arity(goal));
		tmp = alloc_heap(q, 7+(c->num_cells-1)+extra);
		CHECKED(tmp);
		pl_idx num_cells = 0;
		make_instr(tmp+num_cells++, g_error_s, NULL, 2, 6+(c->num_cells-1)+extra);
		make_instr(tmp+num_cells++, new_atom(q->pl, err_type), NULL, 2+extra, 2+(c->num_cells-1)+extra);

		if (!extra) {
			make_cstring(tmp+num_cells++, expected);
		} else {
			char tmpbuf[1024*8];
			strcpy(tmpbuf, expected);
			char *ptr = tmpbuf;
			char *ptr2 = strchr(ptr, ',');
			if (*ptr2) *ptr2++ = '\0';

			while (ptr2) {
				make_cstring(tmp+num_cells++, ptr);
				ptr = ptr2;
				ptr2 = strchr(ptr, ',');
			}

			make_cstring(tmp+num_cells++, ptr);
		}

		num_cells += dup_cells_by_ref(tmp+num_cells, c, c_ctx, c->num_cells);
		make_instr(tmp+num_cells, g_slash_s, NULL, 2, 2);
		SET_OP(tmp+num_cells, OP_YFX); num_cells++;
		make_atom(tmp+num_cells++, new_atom(q->pl, functor));
		make_int(tmp+num_cells, !is_string(goal)?get_arity(goal):0);
	}

	// Every branch that names a culprit ends with the '/' instr and its two args; swap them for a var.

	if (no_context && (tmp->num_cells > 2)) {
		cell *culprit = tmp + tmp->num_cells - 3;

		if (is_compound(culprit) && (culprit->val_off == g_slash_s)
			&& (get_arity(culprit) == 2)) {
			// A fresh slot: var zero would print the frame's first binding, and failing must not corrupt the term.

			int var_num = create_vars(q, 1);

			if (var_num >= 0) {
				make_ref(culprit, var_num, q->st.cur_ctx);
				tmp->num_cells -= 2;
			}
		}
	}

	// See bif_iso_throw_1: clear leftover portray_clause fullstop/nl so the
	// ball is a single term, not "error(...).\n." (#948).
	q->fullstop = q->nl = false;
	q->parens = q->numbervars = true;
	q->quoted = true;
	char *ball = print_term_to_strbuf(q, tmp, c_ctx, 1);
	clear_write_options(q);

	if (find_exception_handler(q, ball)) {
		TPL_free(ball);
		q->in_throw = false;
		return true;
	}

	TPL_free(ball);
	q->in_throw = false;
	return false;
}

bool throw_error2(query *q, cell *c, pl_ctx c_ctx, const char *err_type, const char *expected, cell *goal)
{
	cell tmp;
	tmp = goal[1];
	set_arity(&tmp, get_smallint(&goal[2]));
	return throw_error3(q, c, c_ctx, err_type, expected, &tmp);
}

bool throw_error(query *q, cell *c, pl_ctx c_ctx, const char *err_type, const char *expected)
{
	if ((q->st.m->flags.syntax_error == UNK_FAIL) && !strcmp(err_type, "syntax_error"))
		return false;

	bool is_memory_error = !strcmp(err_type, "resource_error")
		&& expected && !strcmp(expected, "memory");

	// Exception handling prints and reparses the ball after unwinding to
	// the catcher. Release the query's emergency reserve before doing
	// any of that work; push_catcher() rearms it after recovery is ready.

	if (is_memory_error)
		release_oom_reserve(q);

	// An allocation failure becomes a catchable ball HERE, and from here
	// on the ordinary exception machinery owns it - so drop q->oom now.
	//
	// The catchers used to clear it instead, unconditionally. That let
	// catch/3 recover (issue #1094) but it also cleared an oom that had
	// NOT been turned into a ball: the failed alloc_grow() returned 0, so
	// the array never grew, and execution simply carried on over an
	// undersized stack. Clearing at the throw keeps #1094 working while
	// leaving a genuinely unhandled OOM set, so the query loop still
	// hard-stops on it.
	//
	// If throw_error3()'s own allocations fail below, oom is set again -
	// which is right: that one really is unrecoverable.

	if (is_memory_error)
		q->oom = false;

	q->max_depth = 10;
	return throw_error3(q, c, c_ctx, err_type, expected, q->st.instr);
}

static bool bif_abort_0(query *q)
{
	return throw_error(q, q->st.instr, q->st.cur_ctx, "unwind", "abort");
}

static bool bif_sys_abort_0(query *q)
{
	return throw_error(q, q->st.instr, q->st.cur_ctx, "$abort", "abort");
}

builtins g_control_bifs[] =
{
	{"true", 0, bif_iso_true_0, NULL, true, false, BLAH},
	{"fail", 0, bif_iso_fail_0, NULL, true, false, BLAH},
	{"false", 0, bif_iso_fail_0, NULL, true, false, BLAH},
	{"!", 0, bif_iso_cut_0, NULL, true, false, BLAH},
	{",", 2, bif_iso_conjunction_2, ":callable,:callable", true, false, BLAH},
	{";", 2, bif_iso_disjunction_2, ":callable,:callable", true, false, BLAH},
	{"\\+", 1, bif_iso_negation_1, ":callable", true, false, BLAH},
	{"->", 2, bif_iso_if_then_2, ":callable,:callable", true, false, BLAH},
	{"call", 1, bif_iso_call_1, ":callable", true, false, BLAH},
	{"call", 2, bif_iso_call_n, ":callable,?term", true, false, BLAH},
	{"call", 3, bif_iso_call_n, ":callable,?term,term", true, false, BLAH},
	{"call", 4, bif_iso_call_n, ":callable,?term,?term,?term", true, false, BLAH},
	{"call", 5, bif_iso_call_n, ":callable,?term,?term,?term,?term", true, false, BLAH},
	{"call", 6, bif_iso_call_n, ":callable,?term,?term,?term,?term,?term", true, false, BLAH},
	{"call", 7, bif_iso_call_n, ":callable,?term,?term,?term,?term,?term,?term", true, false, BLAH},
	{"call", 8, bif_iso_call_n, ":callable,?term,?term,?term,?term,?term,?term,?term", true, false, BLAH},
	{"once", 1, bif_iso_once_1, ":callable", true, false, BLAH},
	{"throw", 1, bif_iso_throw_1, "+term", true, false, BLAH},
	{"catch", 3, bif_iso_catch_3, ":callable,?term,:callable", true, false, BLAH},

	{"*->", 2, bif_soft_if_then_2, ":callable,:callable", false, false, BLAH},
	{"if", 3, bif_if_3, ":callable,:callable,:callable", false, false, BLAH},
	{"ignore", 1, bif_ignore_1, ":callable", false, false, BLAH},
	{"reset", 3, bif_reset_3, ":callable,?term,-term", false, false, BLAH},
	{"shift", 1, bif_shift_1, "+term", false, false, BLAH},
	{"abort", 0, bif_abort_0, NULL, false, false, BLAH},

	{"$cut", 1, bif_sys_cut_1, "+integer", false, false, BLAH},
	{"$block_catcher", 1, bif_sys_block_catcher_1, NULL, false, false, BLAH},
	{"$catch", 3, bif_sys_catch_3, NULL, false, false, BLAH},
	{"$catch_exit", 1, bif_sys_catch_exit_1, NULL, false, false, BLAH},
	{"$set_if_var", 2, bif_sys_set_if_var_2, "?term,+term", false, false, BLAH},
	{"$cleanup_if_det", 1, bif_sys_cleanup_if_det_1, NULL, false, false, BLAH},
	{"$call_check", 1, bif_sys_call_check_1, "+callable", false, false, BLAH},
	{"$drop_barrier", 1, bif_sys_drop_barrier_1, "+integer", false, false, BLAH},
	{"$get_level", 1, bif_sys_get_level_1, "?integer", false, false, BLAH},
	{"$register_cleanup", 1, bif_sys_register_cleanup_1, NULL, false, false, BLAH},
	{"$call_cleanup", 3, bif_sys_call_cleanup_3, NULL, false, false, BLAH},
	{"$fail_on_retry", 1, bif_sys_fail_on_retry_1, "-integer", false, false, BLAH},
	{"$succeed_on_retry", 1, bif_sys_succeed_on_retry_1, "+integer", false, false, BLAH},
	{"$succeed_on_retry", 2, bif_sys_succeed_on_retry_2, "-integer,+integer", false, false, BLAH},
	{"$abort", 0, bif_sys_abort_0, NULL, false, false, BLAH},

	{0}
};
