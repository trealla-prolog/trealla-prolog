#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "module.h"
#include "query.h"

static bool module_context(query *q, cell **p1, pl_ctx p1_ctx)
{
	if (!is_var(*p1)) {
		if ((*p1)->val_off == g_colon_s) {
			*p1 = *p1 + 1;
			cell *cm = deref(q, *p1, p1_ctx);
			module *m = find_module(q->pl, C_STR(q, cm));

			if (!m) {
				return throw_error(q, cm, p1_ctx, "existence_error", "module");
			}

			*p1 += (*p1)->num_cells;
		}
	}

	return true;
}

static bool bif_clause_3(query *q)
{
	GET_FIRST_ARG(p1,callable_or_var);
	GET_NEXT_ARG(p2,callable_or_var);
	GET_NEXT_ARG(p3,atom_or_var);

	if (!module_context(q, &p1, p1_ctx))
		return false;

	if (is_var(p1) && is_var(p2) && is_var(p3))
		return throw_error(q, p3, p3_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

	const frame *f = GET_CURR_FRAME();

	for (;;) {
		clause *cl;

		if (!is_var(p3)) {
			uuid u;
			uuid_from_buf(C_STR(q, p3), &u);
			rule *r = find_in_db(q->st.m, &u);

			if (!r || (!u.u1 && !u.u2))
				break;

			CHECKED(push_choice(q));
			q->st.dbe = r;
			cl = &r->cl;
			cell *c = cl->cells;
			cell *head = get_head(c);

			if (!unify(q, p1, p1_ctx, head, q->st.cur_ctx)) {
				drop_choice(q);
				break;
			}
		} else {
			if (match_clause(q, p1, p1_ctx, NULL, DO_CLAUSE) != true)
				break;

			char tmpbuf[128];
			uuid_to_buf(&q->st.dbe->u, tmpbuf, sizeof(tmpbuf));
			cell tmp;
			make_cstring(&tmp, tmpbuf);
			unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
			unshare_cell(&tmp);
			cl = &q->st.dbe->cl;
		}

		cell *c = cl->cells;
		cell *body = get_body(c);
		bool ok;

		if (body)
			ok = unify(q, p2, p2_ctx, body, q->st.cur_ctx);
		else {
			cell tmp;
			make_instr(&tmp, g_true_s, bif_iso_true_0, 0, 0);
			ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
		}

		if (ok) {
			bool last_match;

			if (is_var(p3)) {
				last_match = !has_next_key(q);
			} else {
				last_match = true;
			}

			if (last_match) {
				leave_predicate_and_drop(q, q->st.pr, true);
			}

			return true;
		}

		if (!is_var(p3))
			break;

		q->retry = QUERY_RETRY;
		q->total_backtracks++;
		retry_choice(q);
	}

	return false;
}

static void db_log(query *q, rule *r, enum log_type l)
{
	FILE *fp = q->pl->logfp;

	if (!fp)
		return;

	char tmpbuf[256];
	char *dst;
	q->quoted = 2;

	switch(l) {
	case LOG_ASSERTA:
		dst = print_term_to_strbuf(q, r->cl.cells, q->st.cur_ctx, 1);
		uuid_to_buf(&r->u, tmpbuf, sizeof(tmpbuf));
		fprintf(fp, "%s:'$a_'((%s),'%s').\n", q->st.m->name, dst, tmpbuf);
		TPL_free(dst);
		break;
	case LOG_ASSERTZ:
		dst = print_term_to_strbuf(q, r->cl.cells, q->st.cur_ctx, 1);
		uuid_to_buf(&r->u, tmpbuf, sizeof(tmpbuf));
		fprintf(fp, "%s:'$z_'((%s),'%s').\n", q->st.m->name, dst, tmpbuf);
		TPL_free(dst);
		break;
	case LOG_ERASE:
		uuid_to_buf(&r->u, tmpbuf, sizeof(tmpbuf));
		fprintf(fp, "%s:'$e_'('%s').\n", q->st.m->name, tmpbuf);
		break;
	}

	q->quoted = 0;
}

static bool bif_iso_clause_2(query *q)
{
	GET_FIRST_ARG(p1,callable);
	GET_NEXT_ARG(p2,callable_or_var);

	if (!module_context(q, &p1, p1_ctx))
		return false;

	cell *body;

	while (match_clause(q, p1, p1_ctx, &body, DO_CLAUSE)) {
		if (q->did_throw) return true;
		bool ok;

		if (body) {
			ok = unify(q, p2, p2_ctx, body, q->st.cur_ctx);
		} else {
			cell tmp;
			make_instr(&tmp, g_true_s, bif_iso_true_0, 0, 0);
			ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
		}

		if (ok) {
			bool last_match = !has_next_key(q);

			if (last_match) {
				leave_predicate_and_drop(q, q->st.pr, true);
			}

			return true;
		}

		q->retry = QUERY_RETRY;
		q->total_backtracks++;
		retry_choice(q);
	}

	return false;
}

static void predicate_purge_dirty_list(predicate *pr)
{
	unsigned cnt = 0;
	rule *r;

	while ((r = list_pop_front(&pr->dirty)) != NULL) {
		predicate_delink(pr, r);
		clear_clause(&r->cl);
		TPL_free(r);
		cnt++;
	}

	if (cnt && (pr->is_var_in_head || pr->is_var_in_first_arg || pr->is_var_in_idx2_arg))
		recheck_var_in_indexed_args(pr);

#if 0
	if (cnt)
		printf("*** predicate_purge_dirty_list %u\n", cnt);
#endif
}

bool do_retract(query *q, cell *p1, pl_ctx p1_ctx, enum clause_type is_retract)
{
	if (!q->retry) {
		cell *head = deref(q, get_head(p1), p1_ctx);

		if (is_var(head))
			return throw_error(q, head, q->latest_ctx, "instantiation_error", "not_sufficiently_instantiated");

		if (!is_callable(head))
			return throw_error(q, head, q->latest_ctx, "type_error", "callable");
	}

	bool match;

	if (is_a_rule(p1) && get_logical_body(p1)) {
		match = match_rule(q, p1, p1_ctx, is_retract);
	} else {
		cell *head = get_head(p1);
		match = match_clause(q, head, p1_ctx, NULL, is_retract);
	}

	if (!match || q->did_throw)
		return match;

	rule *r = q->st.dbe;
	db_log(q, r, LOG_ERASE);
	retract_from_db(r->owner->m, r);
	bool last_match = (is_retract == DO_RETRACT) && !has_next_key(q);

	if (last_match) {
		q->in_retract = true;
		leave_predicate_and_drop(q, q->st.pr, true);
		q->in_retract = false;
	}

	return true;
}

static bool bif_iso_retract_1(query *q)
{
	GET_FIRST_ARG(p1,callable);

	if (!module_context(q, &p1, p1_ctx))
		return false;

	prolog_lock_mod(q->pl, q->st.m);
	bool ok = do_retract(q, p1, p1_ctx, DO_RETRACT);
	prolog_unlock_mod(q->pl, q->st.m);
	return ok;
}

static bool bif_iso_retractall_1(query *q)
{
	GET_FIRST_ARG(p1,callable);

	if (!module_context(q, &p1, p1_ctx))
		return false;

	cell *head = deref(q, get_head(p1), p1_ctx);
	predicate *pr = search_predicate(q->st.m, head);

	if (!pr) {
		bool found = false;

		if (get_builtin_term(q->st.m, head, &found, NULL), found)
			return throw_error(q, head, q->latest_ctx, "permission_error", "modify,static_procedure");

		return true;
	}

	if (!pr->cnt)
		return true;

	prolog_lock_mod(q->pl, q->st.m);
	q->in_retract = true;

	while (do_retract(q, p1, p1_ctx, DO_RETRACTALL)) {
		if (q->did_throw) {
			q->in_retract = false;
			prolog_unlock_mod(q->pl, q->st.m);
			return true;
		}

		q->retry = QUERY_RETRY;
		q->total_backtracks++;
		retry_choice(q);
	}

	q->in_retract = false;
	pr->is_processed = false;
	prolog_unlock_mod(q->pl, q->st.m);
	return true;
}

bool do_abolish(query *q, cell *c_orig, cell *c_pi, bool hard)
{
	predicate *pr = search_predicate(q->st.m, c_pi);
	if (!pr) return true;

	if (!pr->is_dynamic)
		return throw_error(q, c_orig, q->st.cur_ctx, "permission_error", "modify,static_procedure");

	for (rule *r = pr->head; r; r = r->next)
		retract_from_db(r->owner->m, r);

	if (!pr->refcnt) {
		predicate_purge_dirty_list(pr);
	} else {
		rule *r;

		while ((r = list_pop_front(&pr->dirty)) != NULL)
			list_push_back(&q->dirty, r);
	}

	sl_destroy(pr->idx0);
	sl_destroy(pr->idx2);
	sl_destroy(pr->idx1);
	pr->idx0 = pr->idx1 = pr->idx2 = NULL;
	pr->is_var_in_head = false;
	pr->is_var_in_first_arg = false;
	pr->is_var_in_idx2_arg = false;
	pr->idx2_arg = 0;
	pr->is_processed = false;
	pr->head = pr->tail = NULL;
	pr->cnt = 0;

	if (hard)
		pr->is_abolished = true;

	return true;
}

static bool bif_iso_abolish_1(query *q)
{
	GET_FIRST_ARG(p1,callable);

	if (!is_var(p1)) {
		if (p1->val_off == g_colon_s) {
			p1 = p1 + 1;
			p1 += p1->num_cells;
		}
	}

	if (get_arity(p1) != 2)
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	if (CMP_STRING_TO_CSTR(q, p1, "/") && CMP_STRING_TO_CSTR(q, p1, "//"))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *p1_name = p1 + 1;
	p1_name = deref(q, p1_name, p1_ctx);

	if (!is_atom(p1_name))
		return throw_error(q, p1_name, p1_ctx, "type_error", "atom");

	cell *p1_arity = p1 + 2;
	p1_arity = deref(q, p1_arity, p1_ctx);

	if (!CMP_STRING_TO_CSTR(q, p1, "//"))
		p1_arity += 2;

	if (!is_integer(p1_arity))
		return throw_error(q, p1_arity, p1_ctx, "type_error", "integer");

	if (is_negative(p1_arity))
		return throw_error(q, p1_arity, p1_ctx, "domain_error", "not_less_than_zero");

	if (get_smallint(p1_arity) > MAX_PROCEDURE_ARITY)
		return throw_error(q, p1_arity, p1_ctx, "representation_error", "max_procedure_arity");

	bool found = false;

	if (get_builtin(q->pl, C_STR(q, p1_name), C_STRLEN(q, p1_name), get_smallint(p1_arity), &found, NULL), found) {
		return throw_error(q, p1, p1_ctx, "permission_error", "modify,static_procedure");
	}

	cell tmp;
	tmp = *p1_name;
	CLR_OP(&tmp);
	set_arity(&tmp, get_smallint(p1_arity));
	prolog_lock_mod(q->pl, q->st.m);
	bool ok = do_abolish(q, p1, &tmp, true);
	prolog_unlock_mod(q->pl, q->st.m);
	return ok;
}

static cell *get_db_head(cell *head)
{
	if (is_interned(head) && (head->val_off == g_colon_s) && (get_arity(head) == 2)) {
		cell *module_name = FIRST_ARG(head);
		return NEXT_ARG(module_name);
	}

	return head;
}

static bool bif_iso_asserta_1(query *q)
{
	GET_FIRST_ARG(p1,callable);
	CHECKED(init_tmp_heap(q));
	cell *tmp = copy_term_to_tmp(q, p1, p1_ctx, false);
	CHECKED(tmp);
	cell *head = get_head(tmp);

	if (is_var(head))
		return throw_error(q, head, q->st.cur_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

	if (!is_interned(head) && !is_cstring(head))
		return throw_error(q, head, q->st.cur_ctx, "type_error", "callable");

	cell *db_head = get_db_head(head);

	if (is_callable(db_head) && (get_arity(db_head) > MAX_PROCEDURE_ARITY))
		return throw_error(q, db_head, q->st.cur_ctx, "representation_error", "max_procedure_arity");

	bool found = false;

	if (get_builtin_term(q->st.m, head, &found, NULL), found) {
		if (!GET_OP(head)) {
			return throw_error(q, head, q->st.cur_ctx, "permission_error", "modify,static_procedure");
		}
	}

	cell *tmp2, *body = get_body(tmp);

	if (body && ((tmp2 = check_body_callable(body)) != NULL)) {
		return throw_error(q, tmp2, q->st.cur_ctx, "type_error", "callable");
	}

	pl_idx num_cells = tmp->num_cells;
	parser *p = parser_create(q->st.m);
	CHECKED(p);

	if (num_cells > p->cl->num_allocated_cells) {
		p->cl = TPL_realloc(p->cl, sizeof(clause)+(sizeof(cell)*(num_cells+1)));
		CHECKED(p->cl);
		p->cl->num_allocated_cells = num_cells;
	}

	p->cl->cidx = dup_cells(p->cl->cells, tmp, num_cells);
	assign_vars(p, 0, true);
	term_to_body(p);
	cell *h = get_head(p->cl->cells);

	prolog_lock_mod(q->pl, q->st.m);
	rule *r = asserta_to_db(q->st.m, p->cl->num_vars, p->cl->cells, false);
	prolog_unlock_mod(q->pl, q->st.m);

	p->cl->cidx = 0;

	if (!r) {
		// h points into p->cl, so copy it out BEFORE parser_destroy()
		// frees that. Only the static-procedure error path reads h,
		// which is why this stayed latent.

		h = copy_term_to_heap(q, h, q->st.cur_ctx, false);
		parser_destroy(p);
		CHECKED(h);
		return throw_error(q, h, q->st.cur_ctx, "permission_error", "modify,static_procedure");
	}

	parser_destroy(p);

	db_log(q, r, LOG_ASSERTA);
	return true;
}

static bool do_assertz_1(query *q, bool consulting)
{
	GET_FIRST_ARG(p1,callable);
	CHECKED(init_tmp_heap(q));
	cell *tmp = copy_term_to_tmp(q, p1, p1_ctx, false);
	CHECKED(tmp);
	cell *head = get_head(tmp);

	if (is_var(head))
		return throw_error(q, head, q->st.cur_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

	if (!is_interned(head) && !is_cstring(head))
		return throw_error(q, head, q->st.cur_ctx, "type_error", "callable");

	cell *db_head = get_db_head(head);

	if (is_callable(db_head) && (get_arity(db_head) > MAX_PROCEDURE_ARITY))
		return throw_error(q, db_head, q->st.cur_ctx, "representation_error", "max_procedure_arity");

	bool found = false, evaluable = false;

	if (get_builtin_term(q->st.m, head, &found, &evaluable), found && !evaluable) {
		if (!GET_OP(head)) {
			return throw_error(q, head, q->st.cur_ctx, "permission_error", "modify,static_procedure");
		}
	}

	cell *tmp2, *body = get_body(tmp);

	if (body && ((tmp2 = check_body_callable(body)) != NULL)) {
		return throw_error(q, tmp2, q->st.cur_ctx, "type_error", "callable");
	}

	pl_idx num_cells = tmp->num_cells;
	parser *p = parser_create(q->st.m);
	CHECKED(p);

	if (num_cells > p->cl->num_allocated_cells) {
		p->cl = TPL_realloc(p->cl, sizeof(clause)+(sizeof(cell)*(num_cells+1)));
		CHECKED(p->cl);
		p->cl->num_allocated_cells = num_cells;
	}

	p->cl->cidx = dup_cells(p->cl->cells, tmp, num_cells);
	assign_vars(p, 0, true);
	term_to_body(p);
	cell *h = get_head(p->cl->cells);

	prolog_lock_mod(q->pl, q->st.m);
	rule *r = assertz_to_db(q->st.m, p->cl->num_vars, p->cl->cells, consulting);
	prolog_unlock_mod(q->pl, q->st.m);

	p->cl->cidx = 0;

	if (!r) {
		// h points into p->cl, so copy it out BEFORE parser_destroy()
		// frees that. Only the static-procedure error path reads h,
		// which is why this stayed latent.

		h = copy_term_to_heap(q, h, q->st.cur_ctx, false);
		parser_destroy(p);
		CHECKED(h);
		return throw_error(q, h, q->st.cur_ctx, "permission_error", "modify,static_procedure");
	}

	parser_destroy(p);

	db_log(q, r, LOG_ASSERTZ);
	return true;
}

static bool bif_iso_assertz_1(query *q)
{
	return do_assertz_1(q, false);
}

static bool bif_sys_assertz_static_1(query *q)
{
	return do_assertz_1(q, true);
}

static bool do_asserta_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	cell *head = deref(q, get_head(p1), p1_ctx);

	if (is_var(head))
		return throw_error(q, head, q->latest_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

	cell *db_head = get_db_head(head);

	if (is_callable(db_head) && (get_arity(db_head) > MAX_PROCEDURE_ARITY))
		return throw_error(q, db_head, q->latest_ctx, "representation_error", "max_procedure_arity");

	bool found = false;

	if (get_builtin_term(q->st.m, head, &found, NULL), found) {
		if (!GET_OP(head)) {
			return throw_error(q, head, q->latest_ctx, "permission_error", "modify,static_procedure");
		}
	}

	cell *body = get_body(p1);

	if (body)
		body = deref(q, body, p1_ctx);

	if (body && !is_callable(body)) {
		return throw_error(q, body, q->latest_ctx, "type_error", "callable");
	}

	cell *tmp2;

	if (body && ((tmp2 = check_body_callable(body)) != NULL)) {
		return throw_error(q, tmp2, q->latest_ctx, "type_error", "callable");
	}

	GET_NEXT_ARG(p2,atom_or_var);
	CHECKED(init_tmp_heap(q));
	cell *tmp = copy_term_to_tmp(q, p1, p1_ctx, false);
	CHECKED(tmp);

	pl_idx num_cells = tmp->num_cells;
	parser *p = parser_create(q->st.m);
	CHECKED(p);

	if (num_cells > p->cl->num_allocated_cells) {
		p->cl = TPL_realloc(p->cl, sizeof(clause)+(sizeof(cell)*(num_cells+1)));
		CHECKED(p->cl);
		p->cl->num_allocated_cells = num_cells;
	}

	p->cl->cidx = dup_cells(p->cl->cells, tmp, num_cells);
	assign_vars(p, 0, true);
	term_to_body(p);
	cell *h = get_head(p->cl->cells);

	prolog_lock_mod(q->pl, q->st.m);
	rule *r = asserta_to_db(q->st.m, p->cl->num_vars, p->cl->cells, false);
	prolog_unlock_mod(q->pl, q->st.m);

	p->cl->cidx = 0;

	if (!r) {
		// h points into p->cl, so copy it out BEFORE parser_destroy()
		// frees that. Only the static-procedure error path reads h,
		// which is why this stayed latent.

		h = copy_term_to_heap(q, h, q->st.cur_ctx, false);
		parser_destroy(p);
		CHECKED(h);
		return throw_error(q, h, q->st.cur_ctx, "permission_error", "modify,static_procedure");
	}

	parser_destroy(p);

	if (!is_var(p2)) {
		uuid u;
		uuid_from_buf(C_STR(q, p2), &u);
		r->u = u;
	} else {
		uuid_gen(q->pl, &r->u);
		char tmpbuf[128];
		uuid_to_buf(&r->u, tmpbuf, sizeof(tmpbuf));
		cell tmp2;
		make_cstring(&tmp2, tmpbuf);
		unify(q, p2, p2_ctx, &tmp2, q->st.cur_ctx);
		unshare_cell(&tmp2);
	}

	db_log(q, r, LOG_ASSERTA);
	return true;
}

static bool bif_asserta_2(query *q)
{
	GET_FIRST_ARG(p1,nonvar);
	GET_NEXT_ARG(p2,var);
	return do_asserta_2(q);
}

static bool bif_sys_asserta_2(query *q)
{
	GET_FIRST_ARG(p1,nonvar);
	GET_NEXT_ARG(p2,atom);
	return do_asserta_2(q);
}

static bool do_assertz_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	cell *head = deref(q, get_head(p1), p1_ctx);

	if (is_var(head))
		return throw_error(q, head, q->latest_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

	cell *db_head = get_db_head(head);

	if (is_callable(db_head) && (get_arity(db_head) > MAX_PROCEDURE_ARITY))
		return throw_error(q, db_head, q->latest_ctx, "representation_error", "max_procedure_arity");

	bool found = false;

	if (get_builtin_term(q->st.m, head, &found, NULL), found) {
		if (!GET_OP(head)) {
			return throw_error(q, head, q->latest_ctx, "permission_error", "modify,static_procedure");
		}
	}

	cell *body = get_body(p1);

	if (body)
		body = deref(q, body, p1_ctx);

	if (body && !is_callable(body)) {
		return throw_error(q, body, q->latest_ctx, "type_error", "callable");
	}

	cell *tmp2;

	if (body && ((tmp2 = check_body_callable(body)) != NULL)) {
		return throw_error(q, tmp2, q->latest_ctx, "type_error", "callable");
	}

	GET_NEXT_ARG(p2,atom_or_var);
	CHECKED(init_tmp_heap(q));
	cell *tmp = copy_term_to_tmp(q, p1, p1_ctx, false);
	CHECKED(tmp);

	pl_idx num_cells = tmp->num_cells;
	parser *p = parser_create(q->st.m);
	CHECKED(p);

	if (num_cells > p->cl->num_allocated_cells) {
		p->cl = TPL_realloc(p->cl, sizeof(clause)+(sizeof(cell)*(num_cells+1)));
		CHECKED(p->cl);
		p->cl->num_allocated_cells = num_cells;
	}

	p->cl->cidx = dup_cells(p->cl->cells, tmp, num_cells);
	assign_vars(p, 0, true);
	term_to_body(p);
	cell *h = get_head(p->cl->cells);

	prolog_lock_mod(q->pl, q->st.m);
	rule *r = assertz_to_db(q->st.m, p->cl->num_vars, p->cl->cells, false);
	prolog_unlock_mod(q->pl, q->st.m);

	p->cl->cidx = 0;

	if (!r) {
		// h points into p->cl, so copy it out BEFORE parser_destroy()
		// frees that. Only the static-procedure error path reads h,
		// which is why this stayed latent.

		h = copy_term_to_heap(q, h, q->st.cur_ctx, false);
		parser_destroy(p);
		CHECKED(h);
		return throw_error(q, h, q->st.cur_ctx, "permission_error", "modify,static_procedure");
	}

	parser_destroy(p);

	if (!is_var(p2)) {
		uuid u;
		uuid_from_buf(C_STR(q, p2), &u);
		r->u = u;
	} else {
		uuid_gen(q->pl, &r->u);
		char tmpbuf[128];
		uuid_to_buf(&r->u, tmpbuf, sizeof(tmpbuf));
		cell tmp2;
		make_cstring(&tmp2, tmpbuf);
		unify(q, p2, p2_ctx, &tmp2, q->st.cur_ctx);
		unshare_cell(&tmp2);
	}

	db_log(q, r, LOG_ASSERTZ);
	return true;
}

static bool bif_assertz_2(query *q)
{
	GET_FIRST_ARG(p1,nonvar);
	GET_NEXT_ARG(p2,var);
	return do_assertz_2(q);
}

static bool bif_sys_assertz_2(query *q)
{
	GET_FIRST_ARG(p1,nonvar);
	GET_NEXT_ARG(p2,atom);
	return do_assertz_2(q);
}

void save_db(FILE *fp, query *q, int logging)
{
	q->listing = true;
	q->double_quotes = true;

	for (predicate *pr = list_front(&q->st.m->predicates);
		pr; pr = list_next(pr)) {
		if (pr->is_builtin)
			continue;

		const char *src = C_STR(q, &pr->key);

		if (src[0] == '$')
			continue;

		for (rule *r = pr->head; r; r = r->next) {
			if (r->dbgen_retracted)
				continue;

			if (logging)
				fprintf(fp, "'$z_'(");

			clear_write_options(q);
			print_term(q, fp, r->cl.cells, 0, 0);

			if (logging) {
				char tmpbuf[256];
				uuid_to_buf(&r->u, tmpbuf, sizeof(tmpbuf));
				fprintf(fp, ",'%s')", tmpbuf);
			}

			if (q->last_thing == WAS_SYMBOL)
				fprintf(fp, " ");

			fprintf(fp, ".\n");
		}
	}

	q->double_quotes = false;
	q->listing = false;
}

static bool bif_abolish_2(query *q)
{
	GET_FIRST_ARG(p1,callable);
	GET_NEXT_ARG(p2,iso_list_or_nil);

	if (!is_var(p1)) {
		if (p1->val_off == g_colon_s) {
			p1 = p1 + 1;
			p1 += p1->num_cells;
		}
	}

	if (get_arity(p1) != 2)
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	if (CMP_STRING_TO_CSTR(q, p1, "/") && CMP_STRING_TO_CSTR(q, p1, "//"))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *p1_name = p1 + 1;
	p1_name = deref(q, p1_name, p1_ctx);

	if (!is_atom(p1_name))
		return throw_error(q, p1_name, p1_ctx, "type_error", "atom");

	cell *p1_arity = p1 + 2;
	p1_arity = deref(q, p1_arity, p1_ctx);

	if (!CMP_STRING_TO_CSTR(q, p1, "//"))
		p1_arity += 2;

	if (!is_integer(p1_arity))
		return throw_error(q, p1_arity, p1_ctx, "type_error", "integer");

	if (is_negative(p1_arity))
		return throw_error(q, p1_arity, p1_ctx, "domain_error", "not_less_than_zero");

	if (get_smallint(p1_arity) > MAX_PROCEDURE_ARITY)
		return throw_error(q, p1_arity, p1_ctx, "representation_error", "max_procedure_arity");

	bool force = false, tree = false;
	PROLOG_LIST_HANDLER(p2);

	while (is_list(p2)) {
		cell *h = PROLOG_LIST_HEAD(p2);
		cell *c = deref(q, h, p2_ctx);

		if (is_var(c))
			return throw_error(q, c, q->latest_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

		if (is_compound(c) && (get_arity(c) == 1)) {
			cell *name = c + 1;
			name = deref(q, name, q->latest_ctx);

			if (!CMP_STRING_TO_CSTR(q, c, "force")) {
				if (is_atom(name) && !CMP_STRING_TO_CSTR(q, name, "true")) {
					force = true;
				} else if (is_atom(name) && !CMP_STRING_TO_CSTR(q, name, "false")) {
					force = false;
				}
			} else if (!CMP_STRING_TO_CSTR(q, c, "tree") && false) {
				if (is_atom(name) && !CMP_STRING_TO_CSTR(q, name, "true")) {
					tree = force;
				} else if (is_atom(name) && !CMP_STRING_TO_CSTR(q, name, "false")) {
					tree = false;
				}
			} else
				return throw_error(q, c, q->latest_ctx, "domain_error", "stream_option");
		} else
			return throw_error(q, c, q->latest_ctx, "domain_error", "stream_option");

		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;

		if (is_var(p2))
			return throw_error(q, p2, p2_ctx, "instantiation_error", "args_not_sufficiently_instantiated");
	}

	if (!force) {
		bool found = false;

		if (get_builtin(q->pl, C_STR(q, p1_name), C_STRLEN(q, p1_name), get_smallint(p1_arity), &found, NULL), found) {
			return throw_error(q, p1, p1_ctx, "permission_error", "modify,static_procedure");
		}
	}

	cell tmp;
	tmp = *p1_name;
	CLR_OP(&tmp);
	set_arity(&tmp, get_smallint(p1_arity));

	bool ok = do_abolish(q, p1, &tmp, true);
	return ok;
}

bool do_erase(module* m, const char *str)
{
	uuid u;
	uuid_from_buf(str, &u);
	erase_from_db(m, &u);
	return true;
}

static bool bif_erase_1(query *q)
{
	GET_FIRST_ARG(p1,atom);
	return do_erase(q->st.m, C_STR(q, p1));
}

static bool bif_instance_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,any);
	uuid u;
	uuid_from_buf(C_STR(q, p1), &u);
	rule *r = find_in_db(q->st.m, &u);
	CHECKED(r);
	return unify(q, p2, p2_ctx, r->cl.cells, q->st.cur_ctx);
}

static bool bif_sys_clause_2(query *q)
{
	q->access_private = true;
	bool ok = bif_iso_clause_2(q);
	q->access_private = false;
	return ok;
}

static bool bif_sys_clause_3(query *q)
{
	q->access_private = true;
	bool ok = bif_clause_3(q);
	q->access_private = false;
	return ok;
}

static bool do_dump_term(query *q, cell *p1x, pl_ctx p1x_ctx, cell *p1, pl_ctx p1_ctx, bool deref, int depth)
{
	if (!depth) {
		const frame *f = GET_CURR_FRAME();
		printf("f=%u, f->initial_slots=%u, f->actual_slots=%u\n", q->st.cur_ctx, f->initial_slots, f->actual_slots);
	}

	cell *tmp = p1;

	if (depth > 1)
		return true;

	for (unsigned i = 0; i < p1->num_cells; i++, tmp++) {
		if (depth) printf("  ");

		printf("[%02u] tag=%10s, num_cells=%u, arity=%u",
			i,
			(
				(tmp->tag == TAG_VAR && is_ref(tmp))? "var_ref" :
				tmp->tag == TAG_VAR ? "var" :
				tmp->tag == TAG_INTERNED ? "interned" :
				tmp->tag == TAG_CSTR ? "cstr" :
				tmp->tag == TAG_INT ? "integer" :
				tmp->tag == TAG_FLOAT ? "float" :
				tmp->tag == TAG_RATIONAL ? "rational" :
				tmp->tag == TAG_INDIRECT ? "indirect" :
				tmp->tag == TAG_BLOB ? "blob" :
				"other"
			),
			tmp->num_cells, get_arity(tmp));

		if ((tmp->tag == TAG_INT) && !is_managed(tmp))
			printf(", %lld", (long long)tmp->val_int);

		if (get_arity(tmp) && (tmp->tag == TAG_INTERNED))
			printf(", ground=%u", is_ground(tmp)?1:0);

		if (tmp->tag == TAG_INTERNED)
			printf(", '%s'", C_STR(q, tmp));

		if (is_var(tmp))
			printf(", global=%d, void=%d, local=%d, temp=%d, anon=%d",
				is_global(tmp)?1:0, is_void(tmp)?1:0,
				is_local(tmp)?1:0, is_temporary(tmp)?1:0,
				is_anon(tmp)?1:0);

		if (is_ref(tmp))
			printf(", slot=%u, ctx=%u", tmp->var_num, tmp->val_ctx);
		else if (is_var(tmp))
			printf(", slot=%u, %s", tmp->var_num, C_STR(q, tmp));

		if (is_var(tmp) && deref) {
			const frame *f = GET_FRAME(is_ref(tmp)?tmp->val_ctx:p1_ctx);
			slot *e = get_slot(q, f, tmp->var_num);

			if (e->c.val_attrs && 0) {
				printf("\n");
				do_dump_term(q, p1x, p1x_ctx, e->c.val_attrs, q->st.cur_ctx, deref, depth+1);
				continue;
			}
		}

		printf("\n");
	}

	if (!depth) printf("no_recov=%d\n", q->no_recov?1:0);
	return true;
}

static bool bif_listing_0(query *q)
{
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];
	save_db(str->fp, q, 0);
	return true;
}

static bool save_name(FILE *fp, query *q, pl_idx name, pl_int arity, bool alt, bool dump)
{
	module *m = q->st.dbe ? q->st.dbe->owner->m : q->st.m;
	q->listing = true;
	bool any = false;

	for (predicate *pr = list_front(&m->predicates);
		pr; pr = list_next(pr)) {
		if (pr->is_builtin && (arity < 0))
			continue;

		if (name != pr->key.val_off)
			continue;

		if ((arity >= 0) && ((uint32_t)arity != get_arity(&pr->key)))
			continue;

		any = true;

		for (rule *r = pr->head; r; r = r->next) {
			if (r->dbgen_retracted)
				continue;

			clear_write_options(q);

			if (alt) {
				print_term(q, fp, get_head(r->cl.cells), 0, 0);
				printf(":-\n");

				if (r->cl.alt) {
					cell *c = r->cl.alt;

					while (!is_end(c)) {
						printf("  ");
						print_term(q, fp, c, 0, 0);
						c += c->num_cells;
						printf("\n");
					}
				} else
					fprintf(fp, "  true\n");
			} else if (dump) {
				do_dump_term(q, r->cl.cells, 0, r->cl.cells, 0, 0, 0);
			} else {
				print_term(q, fp, r->cl.cells, 0, 0);
				fprintf(fp, ".\n");
			}
		}
	}

	q->listing = false;
	return any;
}

static bool bif_listing_1(query *q)
{
	GET_FIRST_ARG(p1,callable);
	pl_idx name = p1->val_off;
	pl_int arity = -1;

	if (p1->val_off == g_colon_s) {
		p1 = p1 + 1;
		cell *cm = deref(q, p1, p1_ctx);
		module *m = find_module(q->pl, C_STR(q, cm));

		if (!m)
			return throw_error(q, cm, p1_ctx, "existence_error", "module");

		p1 += p1->num_cells;
	}

	if (get_arity(p1)) {
		if (CMP_STRING_TO_CSTR(q, p1, "/") && CMP_STRING_TO_CSTR(q, p1, "//"))
			return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

		cell *p2 = p1 + 1;

		if (!is_atom(p2))
			return throw_error(q, p2, p1_ctx, "type_error", "atom");

		cell *p3 = p2 + p2->num_cells;

		if (!is_integer(p3))
			return throw_error(q, p3, p1_ctx, "type_error", "integer");

		name = new_atom(q->pl, C_STR(q, p2));
		arity = get_smallint(p3);

		if (!CMP_STRING_TO_CSTR(q, p1, "//"))
			arity += 2;
	}

	if (arity >= 0) {
		cell tmp;
		make_atom(&tmp, name);
		set_arity(&tmp, arity);
		bool found;

		if (get_builtin_term(q->st.m, &tmp, &found, NULL), found)
			return throw_error(q, &tmp, p1_ctx, "permission_error", "access,private_procedure");
	}

	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];
	return save_name(str->fp, q, name, arity, false, false);
}

static bool bif_sys_xlisting_1(query *q)
{
	GET_FIRST_ARG(p1,callable);
	pl_idx name = p1->val_off;
	pl_int arity = -1;

	if (p1->val_off == g_colon_s) {
		p1 = p1 + 1;
		cell *cm = deref(q, p1, p1_ctx);
		module *m = find_module(q->pl, C_STR(q, cm));

		if (!m)
			return throw_error(q, cm, p1_ctx, "existence_error", "module");

		p1 += p1->num_cells;
	}

	if (get_arity(p1)) {
		if (CMP_STRING_TO_CSTR(q, p1, "/") && CMP_STRING_TO_CSTR(q, p1, "//"))
			return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

		cell *p2 = p1 + 1;

		if (!is_atom(p2))
			return throw_error(q, p2, p1_ctx, "type_error", "atom");

		cell *p3 = p2 + p2->num_cells;

		if (!is_integer(p3))
			return throw_error(q, p3, p1_ctx, "type_error", "integer");

		name = new_atom(q->pl, C_STR(q, p2));
		arity = get_smallint(p3);

		if (!CMP_STRING_TO_CSTR(q, p1, "//"))
			arity += 2;
	}

	if (arity >= 0) {
		cell tmp;
		make_atom(&tmp, name);
		set_arity(&tmp, arity);
		bool found;

		if (get_builtin_term(q->st.m, &tmp, &found, NULL), found)
			return throw_error(q, &tmp, p1_ctx, "permission_error", "access,private_procedure");
	}

	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];
	save_name(str->fp, q, name, arity, true, false);
	fprintf(str->fp, "\n");
	return true;
}

static bool bif_sys_dump_term_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,atom);
	GET_FIRST_RAW_ARG(p1x,any);
	bool deref = p2->val_off == g_true_s;
	p1 = deref ? p1 : p1x;
	return do_dump_term(q, p1x, p1x_ctx, p1, p1_ctx, deref, 0);
}

static bool bif_sys_dlisting_1(query *q)
{
	GET_FIRST_ARG(p1,callable);
	pl_idx name = p1->val_off;
	pl_int arity = -1;

	if (p1->val_off == g_colon_s) {
		p1 = p1 + 1;
		cell *cm = deref(q, p1, p1_ctx);
		module *m = find_module(q->pl, C_STR(q, cm));

		if (!m)
			return throw_error(q, cm, p1_ctx, "existence_error", "module");

		p1 += p1->num_cells;
	}

	if (get_arity(p1)) {
		if (CMP_STRING_TO_CSTR(q, p1, "/") && CMP_STRING_TO_CSTR(q, p1, "//"))
			return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

		cell *p2 = p1 + 1;

		if (!is_atom(p2))
			return throw_error(q, p2, p1_ctx, "type_error", "atom");

		cell *p3 = p2 + p2->num_cells;

		if (!is_integer(p3))
			return throw_error(q, p3, p1_ctx, "type_error", "integer");

		name = new_atom(q->pl, C_STR(q, p2));
		arity = get_smallint(p3);

		if (!CMP_STRING_TO_CSTR(q, p1, "//"))
			arity += 2;
	}

	if (arity >= 0) {
		cell tmp;
		make_atom(&tmp, name);
		set_arity(&tmp, arity);
		bool found;

		if (get_builtin_term(q->st.m, &tmp, &found, NULL), found)
			return throw_error(q, &tmp, p1_ctx, "permission_error", "access,private_procedure");
	}

	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];
	save_name(str->fp, q, name, arity, false, true);
	fprintf(str->fp, "\n");
	return true;
}

builtins g_database_bifs[] =
{
	{"abolish", 1, bif_iso_abolish_1, "+predicate_indicator", true, false, BLAH},
	{"asserta", 1, bif_iso_asserta_1, "+term", true, false, BLAH},
	{"assertz", 1, bif_iso_assertz_1, "+term", true, false, BLAH},
	{"retract", 1, bif_iso_retract_1, "+term", true, false, BLAH},
	{"retractall", 1, bif_iso_retractall_1, "+term", true, false, BLAH},
	{"clause", 2, bif_iso_clause_2, "+term,?term", true, false, BLAH},

	{"asserta", 2, bif_asserta_2, "+term,-string", false, false, BLAH},
	{"assertz", 2, bif_assertz_2, "+term,-string", false, false, BLAH},
	{"erase", 1, bif_erase_1, "+string", false, false, BLAH},
	{"clause", 3, bif_clause_3, "?term,?term,-string", false, false, BLAH},
	{"abolish", 2, bif_abolish_2, "+term,+list", false, false, BLAH},
	{"instance", 2, bif_instance_2, "+string,?term", false, false, BLAH},

	{"listing", 0, bif_listing_0, NULL, false, false, BLAH},
	{"listing", 1, bif_listing_1, "+predicate_indicator", false, false, BLAH},

	{"$xlisting", 1, bif_sys_xlisting_1, "+predicate_indicator", false, false, BLAH},
	{"$dlisting", 1, bif_sys_dlisting_1, "+predicate_indicator", false, false, BLAH},
	{"$dump_term", 2, bif_sys_dump_term_2, "+term,+bool", false, false, BLAH},
	{"$clause", 2, bif_sys_clause_2, "?term,?term", false, false, BLAH},
	{"$clause", 3, bif_sys_clause_3, "?term,?term,-string", false, false, BLAH},
	{"$assertz_static", 1, bif_sys_assertz_static_1, "+term", true, false, BLAH},

	{"$a_", 2, bif_sys_asserta_2, "+term,+atom", true, false, BLAH},
	{"$z_", 2, bif_sys_assertz_2, "+term,+atom", true, false, BLAH},
	{"$e_", 1, bif_erase_1, "+atom", true, false, BLAH},

	{0}
};
