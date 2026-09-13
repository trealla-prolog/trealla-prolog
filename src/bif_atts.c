#include <ctype.h>
#include <stdlib.h>

#include "module.h"
#include "query.h"

static const char *find_attribute(query *q, cell *attr, unsigned arity, bool *found)
{
	for (module *m = list_front(&q->pl->modules);
		m; m = list_next(m)) {
		if ((arity == m->arity) && !CMP_STRING_TO_CSTR(q, attr, m->name)) {
			*found = true;
			return m->orig->name;
		}
	}

	*found = false;
	return q->st.m->name;
}

static bool bif_attribute_3(query *q)
{
	GET_FIRST_ARG(p1,atom_or_var);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,integer);
	bool found;
	const char *m_name = find_attribute(q, p2, get_smalluint(p3), &found);
	if (!found) return false;
	cell tmp;
	make_atom(&tmp, new_atom(q->pl, m_name));
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool do_put_atts(query *q, cell *attr, pl_ctx attr_ctx, bool is_minus)
{
	GET_FIRST_ARG(p1,var);
	const frame *f = GET_FRAME(p1_ctx);
	slot *e = get_slot(q, f, p1->var_num);
	cell *c = deref(q, &e->c, e->c.val_ctx);
	pl_ctx c_ctx = q->latest_ctx;

	if (!c->val_attrs && is_minus)
		return true;

	if (((attr->val_off == g_minus_s) || (attr->val_off == g_plus_s)) && (get_arity(attr) == 1))
		attr++;

	CHECKED(add_trail(q, p1_ctx, p1->var_num, c->val_attrs));
	unsigned a_arity = get_arity(attr);
	bool found;
	const char *m_name = find_attribute(q, attr, a_arity, &found);
	if (!found) return false;
	CHECKED(init_tmp_heap(q));

	// Add this attribute value...

	if (!is_minus) {
		cell *tmp = alloc_tmp(q, 1+1);
		CHECKED(tmp);
		make_atom(tmp, g_dot_s);
		set_arity(tmp, 2);
		tmp->num_cells += 1;
		make_atom(tmp+1, new_atom(q->pl, m_name));
		set_arity(&tmp[1], 1);
		cell *tmp2 = clone_term_to_tmp(q, attr, attr_ctx);
		CHECKED(tmp2);
		cell *tmp3 = get_tmp_heap(q, 1);
		tmp3->num_cells += tmp2->num_cells;
		tmp = get_tmp_heap(q, 0);
		tmp->num_cells += tmp2->num_cells;
	}

	// If existing attributes drop old value...

	if (c->val_attrs) {
		cell *l = c->val_attrs;
		pl_ctx l_ctx = c_ctx;
		PROLOG_LIST_HANDLER(l);

		while (is_iso_list(l)) {
			cell *h = PROLOG_LIST_HEAD(l);
			h = deref(q, h, l_ctx);
			pl_ctx h_ctx = q->latest_ctx;
			cell *h1 = deref(q, h+1, h_ctx);
			pl_ctx h1_ctx = q->latest_ctx;

			if (CMP_STRING_TO_CSTR(q, h, m_name)
				|| CMP_STRING_TO_STRING(q, h1, attr)
				|| (get_arity(h1) != a_arity)) {
				append_list(q, h);
			} else if (is_minus) {
				if (!unify(q, attr, attr_ctx, h1, h1_ctx))
					return false;
			}

			l = PROLOG_LIST_TAIL(l);
			l = deref(q, l, l_ctx);
			l_ctx = q->latest_ctx;
		}
	}

	pl_idx hp = q->st.hp, hp_num = q->st.hp_num;
	cell *l = end_list(q);
	CHECKED(l);

	if (is_nil(l)) {
		memset(e, 0, sizeof(slot));
		return true;
	}

	cell *tmp = TPL_malloc(sizeof(cell)*l->num_cells);
	CHECKED(tmp);
	dup_cells(tmp, l, l->num_cells);
	l = tmp;
	q->st.hp = hp;
	q->st.hp_num = hp_num;
	trim_heap(q);
	CHECKED(undo_on_backtrack(q, l, UNDO_CELLS));
	e->c.val_attrs = l;
	q->attrs_used = true;
	return true;
}

static bool bif_put_atts_2(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,callable);
	bool is_minus = (p2->val_off == g_minus_s) && (get_arity(p2) == 1);

	if (is_iso_list(p2)) {
		PROLOG_LIST_HANDLER(p2);

		while (is_iso_list(p2)) {
			cell *attr = PROLOG_LIST_HEAD(p2);
			attr = deref(q, attr, p2_ctx);
			pl_ctx attr_ctx = q->latest_ctx;

			if (!do_put_atts(q, attr, attr_ctx, is_minus))
				return false;

			p2 = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, p2, p2_ctx);
			p2_ctx = q->latest_ctx;
		}

		if (!is_nil(p2))
			return throw_error(q, p2, p2_ctx, "type_error", "list");

		return true;
	}

	return do_put_atts(q, p2, p2_ctx, is_minus);
}

static bool bif_get_atts_2(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,callable_or_var);
	const frame *f = GET_FRAME(p1_ctx);
	slot *e = get_slot(q, f, p1->var_num);
	cell *c = deref(q, &e->c, e->c.val_ctx);
	pl_ctx c_ctx = q->latest_ctx;
	bool is_minus = !is_var(p2) && (p2->val_off == g_minus_s) && (get_arity(p2) == 1);

	if (!c->val_attrs)
		return is_minus ? true : false;

	if (is_var(p2)) {
		cell *l = c->val_attrs;
		pl_ctx l_ctx = c_ctx;
		init_tmp_heap(q);
		PROLOG_LIST_HANDLER(l);

		while (is_iso_list(l)) {
			cell *h = PROLOG_LIST_HEAD(l);
			h = deref(q, h, l_ctx);
			cell *h1 = deref(q, h+1, q->latest_ctx);

			if (!is_nil(h1))
				append_list(q, h1);

			l = PROLOG_LIST_TAIL(l);
			l = deref(q, l, l_ctx);
			l_ctx = q->latest_ctx;
		}

		l = end_list(q);
		CHECKED(l);

		if (is_nil(l))
			return false;

		return unify(q, p2, p2_ctx, l, q->st.cur_ctx);
	}

	cell *attr = p2;

	if (((p2->val_off == g_minus_s) || (p2->val_off == g_plus_s)) && (get_arity(p2) == 1))
		attr++;

	unsigned a_arity = get_arity(attr);
	bool found;
	const char *m_name = find_attribute(q, attr, a_arity, &found);
	if (!found) return false;
	cell *l = c->val_attrs;
	pl_ctx l_ctx = c_ctx;
	PROLOG_LIST_HANDLER(l);

	while (is_iso_list(l)) {
		cell *h = PROLOG_LIST_HEAD(l);
		h = deref(q, h, l_ctx);
		pl_ctx h_ctx = q->latest_ctx;
		cell *h1 = deref(q, h+1, h_ctx);
		pl_ctx h1_ctx = q->latest_ctx;

		if (!CMP_STRING_TO_CSTR(q, h, m_name)
			&& !CMP_STRING_TO_STRING(q, h1, attr)
			&& (get_arity(h1) == a_arity)) {
			if (is_minus)
				return false;

			return unify(q, attr, p2_ctx, h1, h1_ctx);
		}

		l = PROLOG_LIST_TAIL(l);
		l = deref(q, l, l_ctx);
		l_ctx = q->latest_ctx;
	}

	return is_minus ? true : false;
}

bool any_attributed(query *q)
{
	for (unsigned j = 0; j < q->st.fp; j++) {
		const frame *f = GET_FRAME(j);

		for (unsigned i = 0; i < f->actual_slots; i++) {
			slot *e = get_slot(q, f, i);
			cell *c = deref(q, &e->c, e->c.val_ctx);

			if (!is_empty(c) || !c->val_attrs)
				continue;

			cell *v = c->val_attrs;
			bool any = false;

			while (is_iso_list(v)) {
				cell *h = v + 1;

				//  Ignore \== created by dif/2, but what are they?

				if (!is_op(h))
					any = true;

				v = v + 1;
				v += v->num_cells;
			}

			if (!any)
				continue;

			return true;
		}
	}

	return false;
}

static bool bif_sys_list_attributed_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,var);
	CHECKED(init_tmp_heap(q));
	pl_idx mark = get_smalluint(p1);

	for (unsigned j = mark; j < q->st.tp; j++) {
		const trail *tr = get_trail(q, j);
		const frame *f = GET_FRAME(tr->val_ctx);
		slot *e = get_slot(q, f, tr->var_num);
		cell *c = deref(q, &e->c, e->c.val_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (!is_empty(c) || !c->val_attrs)
			continue;

		//if (!c_ctx)
		//	continue;

		cell tmp;
		make_ref(&tmp, tr->var_num, tr->val_ctx);
		append_list(q, &tmp);

		if (!is_compound(c->val_attrs))
			continue;

		collect_vars(q, c->val_attrs, c_ctx);

		for (unsigned k = 0; k < q->tab_idx; k++) {
			const frame *f = GET_FRAME(q->tabs[k].ctx);
			slot *e = get_slot(q, f, q->tabs[k].var_num);
			cell *c = deref(q, &e->c, e->c.val_ctx);

			if (!c->val_attrs)
				continue;

			//if (!q->tabs[k].ctx)
			//	continue;

			cell tmp;
			make_ref(&tmp, q->tabs[k].var_num, q->tabs[k].ctx);
			append_list(q, &tmp);
		}
	}

	cell *l = end_list(q);
	CHECKED(l);
	return unify(q, p2, p2_ctx, l, q->st.cur_ctx);
}

static bool bif_sys_attributed_var_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	const frame *f = GET_FRAME(p1_ctx);
	slot *e = get_slot(q, f, p1->var_num);
	cell *c = deref(q, &e->c, e->c.val_ctx);
	pl_ctx c_ctx = q->latest_ctx;

	if (!c->val_attrs)
		return false;

	cell *l = c->val_attrs;
	pl_ctx l_ctx = c_ctx;
	bool any = false;
	PROLOG_LIST_HANDLER(l);

	while (is_iso_list(l)) {
		cell *h = PROLOG_LIST_HEAD(l);
		h = deref(q, h, l_ctx);
		cell *h1 = deref(q, h+1, l_ctx);

		if (!is_nil(h1)) {
			any = true;
			break;
		}

		l = PROLOG_LIST_TAIL(l);
		l = deref(q, l, l_ctx);
		l_ctx = q->latest_ctx;
	}

	return any;
}

static bool bif_sys_unattributed_var_1(query *q)
{
	return !bif_sys_attributed_var_1(q);
}

typedef struct {
	blob b;
	pl_idx lo_tp, hi_tp;
	slot e[];
} bind_state;

// Mark a cyclically-bound variable's occurrences inside its own value,
// so term_variables/2 in verify_attributes/3 skips the variable being
// bound. The mark holds only for the hook: left on the heap cells it
// hides that variable from every later term_variables/2, and a dif/2
// that cannot see it posts no constraint at all (issue #1127).

static void mark_occurs(unsigned var_num, pl_ctx val_ctx, cell *c, pl_ctx c_ctx, bool set)
{
	for (int num_cells = c->num_cells; num_cells--; c++) {
		if (!is_var(c))
			continue;

		pl_ctx ctx = c_ctx;

		if (is_ref(c))
			ctx = c->val_ctx;

		if (var_num != c->var_num)
			continue;

		if (val_ctx != ctx)
			continue;

		if (set)
			c->flags |= FLAG_VAR_CYCLIC;
		else
			c->flags &= ~FLAG_VAR_CYCLIC;
	}
}

static bool bif_sys_undo_trail_2(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,var);

	assert(q->undo_hi_tp > q->undo_lo_tp);
	pl_idx slots = q->undo_hi_tp - q->undo_lo_tp;
	bind_state *save = TPL_malloc(sizeof(bind_state)+(sizeof(slot)*slots));
	CHECKED(save);
	save->b.ptr = save->b.ptr2 = NULL;
	save->lo_tp = q->undo_lo_tp;
	save->hi_tp = q->undo_hi_tp;
	CHECKED(init_tmp_heap(q), TPL_free(save));

	for (pl_idx i = q->undo_lo_tp, j = 0; i < q->undo_hi_tp; i++, j++) {
		const trail *tr = get_trail(q, i);
		const frame *f = GET_FRAME(tr->val_ctx);
		slot *e = get_slot(q, f, tr->var_num);
		save->e[j].c = e->c;
		cell *c = deref(q, &e->c, e->c.val_ctx);
		pl_ctx c_ctx = q->latest_ctx;
		mark_occurs(tr->var_num, tr->val_ctx, c, c_ctx, true);
		cell lhs, rhs;
		make_ref(&lhs, tr->var_num, tr->val_ctx);

		if (is_compound(c))
			make_indirect(&rhs, c, c_ctx);
		else
			rhs = *c;

		cell tmp[3];
		make_instr(tmp, g_minus_s, NULL, 2, 2);
		SET_OP(tmp, OP_YFX);
		tmp[1] = lhs;
		tmp[2] = rhs;
		append_list(q, tmp);
		memset(e, 0, sizeof(slot));
		e->c.val_attrs = tr->attrs;
	}

	cell *tmp = end_list(q);
	CHECKED(tmp, TPL_free(save));
	unify(q, p1, p1_ctx, tmp, q->st.cur_ctx);
	cell tmp2;
	make_blob(&tmp2, &save->b);
	unify(q, p2, p2_ctx, &tmp2, q->st.cur_ctx);
	return true;
}

static bool bif_sys_redo_trail_1(query * q)
{
	GET_FIRST_ARG(p1,blob);
	const bind_state *save = (bind_state*)p1->val_blob;

	for (pl_idx i = save->lo_tp, j = 0; i < save->hi_tp; i++, j++) {
		const trail *tr = get_trail(q, i);
		const frame *f = GET_FRAME(tr->val_ctx);
		slot *e = get_slot(q, f, tr->var_num);
		e->c = save->e[j].c;

		// Close the bracket '$undo_trail'/2 opened: the binding is back,
		// so the occurs marks it set have done their job (issue #1127).

		cell *c = deref(q, &e->c, e->c.val_ctx);
		mark_occurs(tr->var_num, tr->val_ctx, c, q->latest_ctx, false);
	}

	return true;
}

static bool bif_sys_mark_start_1(query * q)
{
	GET_FIRST_ARG(p1,var);
	cell mark;
	make_uint(&mark, q->st.tp);
	unify(q, p1, p1_ctx, &mark, q->st.cur_ctx);
	return true;
}

bool do_post_unify_hook(query *q, bool is_builtin)
{
	q->run_hook = false;
	q->undo_lo_tp = q->before_hook_tp;
	q->undo_hi_tp = q->st.tp;
	//printf("*** post_unify_hook lo=%u, hi=%u\n", q->undo_lo_tp, q->undo_hi_tp);
	cell *tmp = alloc_heap(q, 3);
	CHECKED(tmp);
	make_instr(tmp+0, g_true_s, bif_iso_true_0, 0, 0);
	make_instr(tmp+1, g_post_unify_hook_s, NULL, 0, 0);
	is_builtin ? make_call(q, tmp+2) : make_call_redo(q, tmp+2);
	q->st.instr = tmp;
	return true;
}

builtins g_atts_bifs[] =
{
	{"attribute", 3, bif_attribute_3, "?atom,+atom,+integer", false, false, BLAH},
	{"put_atts", 2, bif_put_atts_2, "@variable,+term", false, false, BLAH},
	{"get_atts", 2, bif_get_atts_2, "@variable,-term", false, false, BLAH},

	{"$list_attributed", 2, bif_sys_list_attributed_2, "+integer,-list", false, false, BLAH},
	{"$unattributed_var", 1, bif_sys_unattributed_var_1, "@variable", false, false, BLAH},
	{"$attributed_var", 1, bif_sys_attributed_var_1, "@variable", false, false, BLAH},
	{"$undo_trail", 2, bif_sys_undo_trail_2, "-list,-blob", false, false, BLAH},
	{"$redo_trail", 1, bif_sys_redo_trail_1, "+blob", false, false, BLAH},
	{"$mark_start", 1, bif_sys_mark_start_1, "-integer", false, false, BLAH},

	{0}
};
