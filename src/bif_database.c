#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "module.h"
#include "query.h"

// Strip M: from *p1, setting *mp to M, or to NULL when unqualified. False
// means stop and return *status: a throw that found a catcher returns true.
// A module that doesn't exist has no clauses, so as in SWI and Scryer the
// call just gives if_missing.

static void abolish_predicate(query *q, predicate *pr, bool hard);

static bool module_context(query *q, cell **p1, pl_ctx *p1_ctx, module **mp, bool *status, bool if_missing)
{
	*mp = NULL;

	if (!is_var(*p1)) {
		if (((*p1)->val_off == g_colon_s) && (get_arity(*p1) == 2)) {
			cell *cm = deref(q, *p1 + 1, *p1_ctx);

			if (is_var(cm)) {
				*status = throw_error(q, cm, q->latest_ctx, "instantiation_error", "module");
				return false;
			}

			if (!is_atom(cm)) {
				*status = throw_error(q, cm, q->latest_ctx, "type_error", "module");
				return false;
			}

			module *m = find_module(q->pl, C_STR(q, cm));

			if (!m) {
				*status = if_missing;
				return false;
			}

			cell *h = *p1 + 1;
			h += h->num_cells;
			*p1 = deref(q, h, *p1_ctx);
			*p1_ctx = q->latest_ctx;
			*mp = m;
		}
	}

	return true;
}

// A clause reference is that clause's creation generation, printed as an opaque hex handle:
// dbgen is a global atomic counter bumped once per assert, so the stamp is unique and monotonic.
// Anything that is not one of these handles parses as 0, which matches no clause.

static char *clause_ref_to_buf(uint64_t ref, char *buf, size_t buflen)
{
	snprintf(buf, buflen, "%016"PRIx64"", ref);
	return buf;
}

static uint64_t clause_ref_from_buf(const char *s)
{
	if (!s || !*s)
		return 0;

	uint64_t ref = 0;

	for (; *s; s++) {
		if (!isxdigit((unsigned char)*s))
			return 0;

		int d = isdigit((unsigned char)*s) ? *s - '0' : (tolower((unsigned char)*s) - 'a') + 10;
		ref = (ref * 16) + (uint64_t)d;
	}

	return ref;
}

static bool bif_clause_3(query *q)
{
	GET_FIRST_ARG(p1,callable_or_var);
	GET_NEXT_ARG(p2,callable_or_var);
	GET_NEXT_ARG(p3,atom_or_var);

	module *m;
	bool status;

	if (!module_context(q, &p1, &p1_ctx, &m, &status, false))
		return status;

	if (is_var(p1) && is_var(p2) && is_var(p3))
		return throw_error(q, p3, p3_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

	const frame *f = GET_CURR_FRAME();

	for (;;) {
		cell *body;

		if (!is_var(p3)) {
			rule *r = find_in_db(m ? m : q->st.m, clause_ref_from_buf(C_STR(q, p3)));

			if (!r)
				break;

			CHECKED(push_choice(q));
			q->st.dbe = r;

			// A renamed copy, as match_clause() makes, so the clause's variables are not the caller's slots.

			cell *tmp = import_term(q, r->cl.cells, q->st.cur_ctx);
			CHECKED(tmp);
			body = get_body(tmp);

			if (!unify(q, p1, p1_ctx, get_head(tmp), q->st.cur_ctx)) {
				drop_choice(q);
				break;
			}

			if (q->did_throw) return true;
		} else {
			if (match_clause(q, m, p1, p1_ctx, &body, DO_CLAUSE) != true)
				break;

			if (q->did_throw) return true;	// q->st.dbe and body are unset when unifying the head threw

			char tmpbuf[128];
			clause_ref_to_buf(q->st.dbe->dbgen_created, tmpbuf, sizeof(tmpbuf));
			cell tmp;
			make_cstring(&tmp, tmpbuf);
			unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
			unshare_cell(&tmp);
		}

		bool ok;

		if (body)
			ok = unify(q, p2, p2_ctx, body, q->st.cur_ctx);
		else {
			cell tmp;
			make_instr(&tmp, g_true_s, bif_iso_true_0, 0, 0);
			ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
		}

		if (q->did_throw) return true;

		if (ok) {
			if (!is_var(p3))
				drop_choice(q);		// by reference enters no predicate, so has none to leave
			else if (!has_next_key(q))
				leave_predicate_and_drop(q, q->st.pr, true);

			return true;
		}

		if (!is_var(p3)) {
			drop_choice(q);
			break;
		}

		q->retry = QUERY_RETRY;
		q->total_backtracks++;
		retry_choice(q);
	}

	return false;
}

static bool do_clause_2(query *q, enum clause_type type)
{
	GET_FIRST_ARG(p1,callable);
	GET_NEXT_ARG(p2,callable_or_var);

	module *m;
	bool status;

	if (!module_context(q, &p1, &p1_ctx, &m, &status, false))
		return status;

	cell *body;

	while (match_clause(q, m, p1, p1_ctx, &body, type)) {
		if (q->did_throw) return true;
		bool ok;

		if (body) {
			ok = unify(q, p2, p2_ctx, body, q->st.cur_ctx);
		} else {
			cell tmp;
			make_instr(&tmp, g_true_s, bif_iso_true_0, 0, 0);
			ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
		}

		if (q->did_throw) return true;

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

static bool bif_iso_clause_2(query *q)
{
	return do_clause_2(q, DO_CLAUSE);
}

// '$clause'(:Head, ?Body): clause/2 that reads static predicates too, for
// code analysis such as library(sandbox) does.

static bool bif_sys_clause_2(query *q)
{
	return do_clause_2(q, DO_CLAUSE_STATIC);
}

bool do_retract(query *q, module *m, cell *p1, pl_ctx p1_ctx, enum clause_type is_retract)
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
		match = match_rule(q, m, p1, p1_ctx, is_retract);
	} else {
		cell *head = get_head(p1);
		match = match_clause(q, m, head, p1_ctx, NULL, is_retract);
	}

	if (!match || q->did_throw)
		return match;

	rule *r = q->st.dbe;
	retract_from_db(r->owner->m, r);
	bool last_match = (is_retract == DO_RETRACT) && !has_next_key(q);

	if (last_match)
		leave_predicate_and_drop(q, q->st.pr, true);

	return true;
}

// For retract((M:H :- B)): rebuild it as (H :- B), setting *mp to M.

static bool rule_head_context(query *q, cell **p1, pl_ctx *p1_ctx, module **mp, bool *status)
{
	if (*mp || ((*p1)->val_off != g_neck_s) || (get_arity(*p1) != 2))
		return true;

	cell *h = deref(q, *p1 + 1, *p1_ctx);
	pl_ctx h_ctx = q->latest_ctx;

	if ((h->val_off != g_colon_s) || (get_arity(h) != 2))
		return true;

	cell *cm = deref(q, h + 1, h_ctx);

	if (is_var(cm)) {
		*status = throw_error(q, cm, q->latest_ctx, "instantiation_error", "module");
		return false;
	}

	if (!is_atom(cm)) {
		*status = throw_error(q, cm, q->latest_ctx, "type_error", "module");
		return false;
	}

	if (!(*mp = find_module(q->pl, C_STR(q, cm)))) {
		*status = false;
		return false;
	}

	cell *hh = h + 1;
	hh += hh->num_cells;
	hh = deref(q, hh, h_ctx);
	pl_ctx hh_ctx = q->latest_ctx;
	cell *b = *p1 + 1;
	b += b->num_cells;
	pl_idx n = 1 + hh->num_cells + b->num_cells;
	cell *tmp = alloc_heap(q, n);

	if (!tmp) {
		*status = throw_error(q, *p1, *p1_ctx, "resource_error", "memory");
		return false;
	}

	make_instr(tmp, g_neck_s, NULL, 2, n-1);
	SET_OP(tmp, OP_XFX);
	dup_cells_by_ref(tmp+1, hh, hh_ctx, hh->num_cells);
	dup_cells_by_ref(tmp+1+hh->num_cells, b, *p1_ctx, b->num_cells);
	*p1 = tmp;
	*p1_ctx = q->st.cur_ctx;
	return true;
}

// '$destroy_module'(+Name): emptied rather than freed. Code can hold a
// cached pointer to one of its predicates, so their headers stay till
// exit, abolished (see module_bury_predicates()). Its clauses, indexes,
// operators, blackboard and imports go, and so does its name, for a new
// module to take, and the module itself goes on pl->dead_modules, for
// module_create() to take again.

static bool bif_sys_destroy_module_1(query *q)
{
	GET_FIRST_ARG(p1,atom);
	module *m = find_module(q->pl, C_STR(q, p1));

	if (!m)
		return true;

	if ((m == q->pl->user_m) || (m == q->pl->system_m) || m->prebuilt || m->orig || m->from_file)
		return throw_error(q, p1, p1_ctx, "permission_error", "destroy,module");

	prolog_lock_mod(q->pl, m);

	for (predicate *pr = list_front(&m->predicates); pr; pr = list_next(pr)) {
		if (pr->head || pr->cnt)
			abolish_predicate(q, pr, true);
		else {
			pr->is_abolished = true;
			q->pl->pred_gen++;
		}
	}

	module_clear_tables(m);
	m->idx_used = 0;
	prolog_unlock_mod(q->pl, m);

	prolog_lock(q->pl);

	for (module *tmp = list_front(&q->pl->modules); tmp; tmp = list_next(tmp)) {
		unsigned j = 0;

		for (unsigned i = 0; i < tmp->idx_used; i++) {
			if (tmp->used[i] != m)
				tmp->used[j++] = tmp->used[i];
		}

		tmp->idx_used = j;
	}

	list_remove(&q->pl->modules, m);
	m->hdr.prev = m->hdr.next = NULL;
	module_bury_predicates(m);
	m->dead_next = q->pl->dead_modules;
	q->pl->dead_modules = m;
	prolog_unlock(q->pl);
	return true;
}

// '$create_module'(+Name): an empty module, for in_temporary_module/3,
// which destroys it after. One that exists already is only taken if it
// is still empty, as a call to M:G makes one: no predicates, not even
// declared ones, and no imports.

static bool bif_sys_create_module_1(query *q)
{
	GET_FIRST_ARG(p1,atom);
	module *m = find_module(q->pl, C_STR(q, p1));

	if (m) {
		bool in_use = (m == q->pl->user_m) || (m == q->pl->system_m)
			|| m->prebuilt || m->orig || m->from_file || m->idx_used;

		for (predicate *pr = list_front(&m->predicates); pr && !in_use; pr = list_next(pr)) {
			if (!pr->is_abolished && strcmp(C_STR(q, &pr->key), "$predicate_property"))
				in_use = true;
		}

		if (in_use)
			return throw_error(q, p1, p1_ctx, "permission_error", "create,module");

		return true;
	}

	CHECKED(module_create(q->pl, C_STR(q, p1)));
	return true;
}

// thread_local(+Spec), the directive and the predicate: Name/Arity, a list
// or conjunction of them, or M:Spec. Each becomes dynamic, with the clauses
// each thread asserts kept to itself (see thread_local_shadow()).

static bool do_thread_local(query *q, cell *c, pl_ctx c_ctx, module *m)
{
	// A list or conjunction's tail is looped rather than recursed, so a long one can't exhaust the C stack

	for (;;) {
		c = deref(q, c, c_ctx);
		c_ctx = q->latest_ctx;

		if (is_var(c))
			return throw_error(q, c, c_ctx, "instantiation_error", "predicate_indicator");

		if (!is_iso_list(c) && !(is_interned(c) && (get_arity(c) == 2) && (c->val_off == g_conjunction_s)))
			break;

		cell *h = c + 1, *t = h + h->num_cells;

		if (!do_thread_local(q, h, c_ctx, m))
			return false;

		c = t;
	}

	if (is_nil(c))
		return true;

	if (is_interned(c) && (get_arity(c) == 2) && (c->val_off == g_colon_s)) {
		cell *cm = deref(q, c + 1, c_ctx);

		if (!is_atom(cm))
			return throw_error(q, cm, q->latest_ctx, "type_error", "module");

		module *m2 = find_module(q->pl, C_STR(q, cm));

		if (!m2 && !(m2 = module_create(q->pl, C_STR(q, cm))))
			return throw_error(q, cm, q->latest_ctx, "resource_error", "memory");

		cell *spec = c + 1;
		spec += spec->num_cells;
		return do_thread_local(q, spec, c_ctx, m2);
	}

	if (!is_interned(c) || (get_arity(c) != 2) || CMP_STRING_TO_CSTR(q, c, "/"))
		return throw_error(q, c, c_ctx, "type_error", "predicate_indicator");

	cell *name = deref(q, c + 1, c_ctx);
	pl_ctx name_ctx = q->latest_ctx;
	cell *arity = c + 1;
	arity += arity->num_cells;
	arity = deref(q, arity, c_ctx);
	pl_ctx arity_ctx = q->latest_ctx;

	if (is_var(name) || is_var(arity))
		return throw_error(q, c, c_ctx, "instantiation_error", "predicate_indicator");

	if (!is_atom(name))
		return throw_error(q, name, name_ctx, "type_error", "atom");

	if (!is_smallint(arity))
		return throw_error(q, arity, arity_ctx, "type_error", "integer");

	if (get_smallint(arity) < 0)
		return throw_error(q, arity, arity_ctx, "domain_error", "not_less_than_zero");

	const char *src = C_STR(q, name);
	unsigned n = (unsigned)get_smallint(arity);
	cell key = (cell){0};
	key.tag = TAG_INTERNED;
	key.val_off = new_atom(q->pl, src);
	set_arity(&key, n);
	prolog_lock_mod(q->pl, m);
	predicate *pr = find_predicate(m, &key);

	if (pr && !pr->is_dynamic && pr->head) {
		prolog_unlock_mod(q->pl, m);
		cell pi[3];										// Name/Arity, as abolish/1 reports it
		make_instr(pi, g_slash_s, NULL, 2, 2);
		make_atom(pi+1, key.val_off);
		make_int(pi+2, n);
		return throw_error(q, pi, q->st.cur_ctx, "permission_error", "modify,static_procedure");
	}

	set_dynamic_in_db(m, src, n);
	pr = find_predicate(m, &key);

	if (pr && !pr->is_thread_local) {
		pr->is_thread_local = true;
		push_property(m, src, n, "thread_local");
	}

	prolog_unlock_mod(q->pl, m);
	return pr ? true : throw_error(q, c, c_ctx, "resource_error", "memory");
}

static bool bif_thread_local_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return do_thread_local(q, p1, p1_ctx, q->st.m);
}

static bool bif_iso_retract_1(query *q)
{
	GET_FIRST_ARG(p1,callable);

	module *m;
	bool status;

	if (!module_context(q, &p1, &p1_ctx, &m, &status, false)
		|| !rule_head_context(q, &p1, &p1_ctx, &m, &status))
		return status;

	module *lm = m ? m : q->st.m;
	prolog_lock_mod(q->pl, lm);
	bool ok = do_retract(q, m, p1, p1_ctx, DO_RETRACT);
	prolog_unlock_mod(q->pl, lm);
	return ok;
}

static bool bif_iso_retractall_1(query *q)
{
	GET_FIRST_ARG(p1,callable);

	module *m;
	bool status;

	if (!module_context(q, &p1, &p1_ctx, &m, &status, true))
		return status;

	module *lm = m ? m : q->st.m;
	cell *head = deref(q, get_head(p1), p1_ctx);
	predicate *pr = m ? find_predicate(m, head) : search_predicate(lm, head);	// see find_db_predicate()

	if (pr && pr->is_thread_local)
		pr = thread_local_shadow(pr);

	if (!pr) {
		bool found = false;

		if (get_builtin_term(lm, head, &found, NULL), found)
			return throw_error(q, head, q->latest_ctx, "permission_error", "modify,static_procedure");

		return true;
	}

	if (!pr->cnt)
		return true;

	prolog_lock_mod(q->pl, lm);

	while (do_retract(q, m, p1, p1_ctx, DO_RETRACTALL)) {
		if (q->did_throw) {
			prolog_unlock_mod(q->pl, lm);
			return true;
		}

		q->retry = QUERY_RETRY;
		q->total_backtracks++;
		retry_choice(q);
	}

	pr->is_processed = false;
	prolog_unlock_mod(q->pl, lm);
	return true;
}

// Every clause retracted, as retract/1 would, for the last reader to reclaim; hard also marks pr abolished for stale cached pointers.

static void abolish_predicate(query *q, predicate *pr, bool hard)
{
	if (pr->is_dynamic) {
		for (rule *r = pr->head; r; r = r->next)
			retract_from_db(r->owner->m, r);
	} else
		detach_clauses(pr);			// static, as destroy_module/1 finds, with no readers counted

	pr->is_processed = false;

	if (hard) {
		pr->is_abolished = true;
		q->pl->pred_gen++;
	}

	if (pr->is_dynamic && !pr->refcnt)
		reclaim_predicate(q, pr);
}

bool do_abolish(query *q, cell *c_orig, cell *c_pi, bool hard)
{
	predicate *pr = search_predicate(q->st.m, c_pi);
	if (!pr) return true;

	if (!pr->is_dynamic)
		return throw_error(q, c_orig, q->st.cur_ctx, "permission_error", "modify,static_procedure");

	abolish_predicate(q, pr, hard);
	return true;
}

// Abolish Name/Arity, Name//Arity being Name/Arity+2. With force, as SICStus's
// force(true), a static user predicate goes too; a builtin never does.

static bool abolish_pi(query *q, cell *pi, pl_ctx pi_ctx, cell *p_name, pl_ctx p_name_ctx, cell *p_arity, pl_ctx p_arity_ctx, bool dcg, bool force)
{
	if (!is_atom(p_name))
		return throw_error(q, p_name, p_name_ctx, "type_error", "atom");

	if (!is_integer(p_arity))
		return throw_error(q, p_arity, p_arity_ctx, "type_error", "integer");

	if (is_negative(p_arity))
		return throw_error(q, p_arity, p_arity_ctx, "domain_error", "not_less_than_zero");

	// No procedure can have such an arity, so there's nothing to abolish

	if (!is_smallint(p_arity) || ((get_smallint(p_arity) + (dcg ? 2 : 0)) > MAX_PROCEDURE_ARITY))
		return true;

	unsigned arity = get_smallint(p_arity) + (dcg ? 2 : 0);
	bool found = false;

	if (get_builtin_by_atom(q->pl, p_name->val_off, arity, &found, NULL), found)
		return throw_error(q, pi, pi_ctx, "permission_error", "modify,static_procedure");

	cell tmp;
	tmp = *p_name;
	CLR_OP(&tmp);
	set_arity(&tmp, arity);
	prolog_lock_mod(q->pl, q->st.m);
	predicate *pr = force ? search_predicate(q->st.m, &tmp) : NULL;
	bool ok = true;

	if (pr && pr->is_builtin)
		ok = throw_error(q, pi, pi_ctx, "permission_error", "modify,static_procedure");
	else if (pr)
		abolish_predicate(q, pr, true);
	else
		ok = do_abolish(q, pi, &tmp, true);

	prolog_unlock_mod(q->pl, q->st.m);
	return ok;
}

// Name/Arity or Name//Arity, after any M:

static bool abolish_indicator(query *q, cell *p1, pl_ctx p1_ctx, bool force)
{
	if (is_interned(p1) && (p1->val_off == g_colon_s) && (get_arity(p1) == 2)) {
		p1 = deref(q, p1 + 1 + p1[1].num_cells, p1_ctx);
		p1_ctx = q->latest_ctx;
	}

	if (is_var(p1))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

	if (get_arity(p1) != 2)
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	if (CMP_STRING_TO_CSTR(q, p1, "/") && CMP_STRING_TO_CSTR(q, p1, "//"))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *p_name = deref(q, FIRST_ARG(p1), p1_ctx);
	pl_ctx p_name_ctx = q->latest_ctx;
	cell *p_arity = deref(q, NEXT_ARG(FIRST_ARG(p1)), p1_ctx);
	pl_ctx p_arity_ctx = q->latest_ctx;
	return abolish_pi(q, p1, p1_ctx, p_name, p_name_ctx, p_arity, p_arity_ctx, !CMP_STRING_TO_CSTR(q, p1, "//"), force);
}

static bool bif_iso_abolish_1(query *q)
{
	GET_FIRST_ARG(p1,callable);
	return abolish_indicator(q, p1, p1_ctx, false);
}

// The module of a qualified head when it isn't an atom, else NULL.

static cell *bad_db_module(query *q, cell *head, pl_ctx head_ctx)
{
	if (!is_interned(head) || (head->val_off != g_colon_s) || (get_arity(head) != 2))
		return NULL;

	cell *cm = deref(q, FIRST_ARG(head), head_ctx);
	return is_atom(cm) ? NULL : cm;
}

static cell *get_db_head(cell *head)
{
	if (is_interned(head) && (head->val_off == g_colon_s) && (get_arity(head) == 2)) {
		cell *module_name = FIRST_ARG(head);
		return NEXT_ARG(module_name);
	}

	return head;
}

// Goal expansion failed while compiling the clause: raise rather than store it half-expanded (#1175).

static bool throw_expansion_error(query *q, parser *p, cell *tmp)
{
	const char *desc = p->error_desc;
	parser_destroy(p);

	if (!desc || !strcmp(desc, "memory"))
		return throw_error(q, tmp, q->st.cur_ctx, "resource_error", "memory");

	if (!strcmp(desc, "instantiation_error"))
		return throw_error(q, tmp, q->st.cur_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

	if (!strcmp(desc, "goal_expansion_limit"))
		return throw_error(q, tmp, q->st.cur_ctx, "resource_error", desc);

	return throw_error(q, tmp, q->st.cur_ctx, "syntax_error", desc);
}

// Compiles the clause copied into tmp and stores it first or last. *rp, when given, is the new rule, and stays NULL if this threw.

static bool assert_clause(query *q, cell *tmp, bool at_front, rule **rp)
{
	pl_idx num_cells = tmp->num_cells;
	parser *p = parser_create_sized(q->st.m, num_cells);
	CHECKED(p);

	p->cl->cidx = dup_cells(p->cl->cells, tmp, num_cells);

	// The clause outlives the mapping any slice in it points into.
	// Unsliced here, not in the tmp heap: this copy owns the reference the
	// new string carries, and a slice had none for dup_cells() to share.

	if (!unslice_cells(p->cl->cells, num_cells)) {
		parser_destroy(p);
		return throw_error(q, tmp, q->st.cur_ctx, "resource_error", "memory");
	}

	assign_vars(p, 0, true);

	if (p->error) {
		const bool too_many = p->vartab.num_vars >= MAX_VARS;
		parser_destroy(p);
		return throw_error(q, tmp, q->st.cur_ctx, "resource_error", too_many ? "max_vars" : "memory");
	}

	term_to_body(p);

	if (p->error)
		return throw_expansion_error(q, p, tmp);

	cell *h = get_head(p->cl->cells);

	prolog_lock_mod(q->pl, q->st.m);
	rule *r = at_front ? asserta_to_db(q->st.m, p->cl->num_vars, p->cl->cells, false)
		: assertz_to_db(q->st.m, p->cl->num_vars, p->cl->cells, false);
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

	if (rp)
		*rp = r;

	return true;
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

	cell *bad_m = bad_db_module(q, head, q->st.cur_ctx);

	if (bad_m)
		return throw_error(q, bad_m, q->st.cur_ctx, is_var(bad_m) ? "instantiation_error" : "type_error", "module");

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

	return assert_clause(q, tmp, true, NULL);
}

static bool do_assertz_1(query *q)
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

	cell *bad_m = bad_db_module(q, head, q->st.cur_ctx);

	if (bad_m)
		return throw_error(q, bad_m, q->st.cur_ctx, is_var(bad_m) ? "instantiation_error" : "type_error", "module");

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

	return assert_clause(q, tmp, false, NULL);
}

static bool bif_iso_assertz_1(query *q)
{
	return do_assertz_1(q);
}

static bool do_asserta_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	cell *head = deref(q, get_head(p1), p1_ctx);

	if (is_var(head))
		return throw_error(q, head, q->latest_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

	cell *bad_m = bad_db_module(q, head, q->latest_ctx);

	if (bad_m)
		return throw_error(q, bad_m, q->latest_ctx, is_var(bad_m) ? "instantiation_error" : "type_error", "module");

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

	rule *r = NULL;
	bool ok = assert_clause(q, tmp, true, &r);

	if (!r)
		return ok;

	char tmpbuf[128];
	clause_ref_to_buf(r->dbgen_created, tmpbuf, sizeof(tmpbuf));
	cell ref;
	make_cstring(&ref, tmpbuf);
	ok = unify(q, p2, p2_ctx, &ref, q->st.cur_ctx);
	unshare_cell(&ref);
	return ok;
}

static bool bif_asserta_2(query *q)
{
	GET_FIRST_ARG(p1,nonvar);
	GET_NEXT_ARG(p2,var);
	return do_asserta_2(q);
}

static bool do_assertz_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	cell *head = deref(q, get_head(p1), p1_ctx);

	if (is_var(head))
		return throw_error(q, head, q->latest_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

	cell *bad_m = bad_db_module(q, head, q->latest_ctx);

	if (bad_m)
		return throw_error(q, bad_m, q->latest_ctx, is_var(bad_m) ? "instantiation_error" : "type_error", "module");

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

	rule *r = NULL;
	bool ok = assert_clause(q, tmp, false, &r);

	if (!r)
		return ok;

	char tmpbuf[128];
	clause_ref_to_buf(r->dbgen_created, tmpbuf, sizeof(tmpbuf));
	cell ref;
	make_cstring(&ref, tmpbuf);
	ok = unify(q, p2, p2_ctx, &ref, q->st.cur_ctx);
	unshare_cell(&ref);
	return ok;
}

static bool bif_assertz_2(query *q)
{
	GET_FIRST_ARG(p1,nonvar);
	GET_NEXT_ARG(p2,var);
	return do_assertz_2(q);
}

void save_db(stream *str, query *q)
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

			clear_write_options(q);
			print_term_to_stream(q, str, r->cl.cells, 0, 0);

			if (q->last_thing == WAS_SYMBOL)
				stream_write(q, " ", 1, str);

			stream_write(q, ".\n", 2, str);
		}
	}

	q->double_quotes = false;
	q->listing = false;
}

// Both abolish(Name, Arity), as in DEC-10, SWI and XSB, and SICStus's
// abolish(PI, Options): an atom first argument or integer second says which.

static bool bif_abolish_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	cell *p_name = p1;
	pl_ctx p_name_ctx = p1_ctx;

	if (is_interned(p1) && (p1->val_off == g_colon_s) && (get_arity(p1) == 2)) {
		p_name = deref(q, p1 + 1 + p1[1].num_cells, p1_ctx);
		p_name_ctx = q->latest_ctx;
	}

	if (is_var(p_name))
		return throw_error(q, p_name, p_name_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

	if (is_atom(p_name) || is_integer(p2)) {
		if (is_var(p2))
			return throw_error(q, p2, p2_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

		cell pi[3];
		make_instr(pi, g_slash_s, NULL, 2, 2);
		SET_OP(pi, OP_YFX);
		pi[1] = *p_name;

		if (is_smallint(p2))
			pi[2] = *p2;
		else
			make_int(pi+2, 0);				// unused: a bigint arity returns first

		return abolish_pi(q, pi, q->st.cur_ctx, p_name, p_name_ctx, p2, p2_ctx, false, false);
	}

	if (!is_callable(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "callable");

	if (!is_iso_list_or_nil(p2))
		return throw_error(q, p2, p2_ctx, is_var(p2) ? "instantiation_error" : "type_error", "list");

	bool force = false;
	PROLOG_LIST_HANDLER(p2);

	while (is_list(p2)) {
		cell *h = PROLOG_LIST_HEAD(p2);
		cell *c = deref(q, h, p2_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (is_var(c))
			return throw_error(q, c, c_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

		cell *v = is_compound(c) && (get_arity(c) == 1) ? deref(q, FIRST_ARG(c), c_ctx) : NULL;

		if (v && !CMP_STRING_TO_CSTR(q, c, "force") && is_atom(v) && !CMP_STRING_TO_CSTR(q, v, "true"))
			force = true;
		else if (v && !CMP_STRING_TO_CSTR(q, c, "force") && is_atom(v) && !CMP_STRING_TO_CSTR(q, v, "false"))
			force = false;
		else
			return throw_error(q, c, c_ctx, "domain_error", "abolish_option");

		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;

		if (is_var(p2))
			return throw_error(q, p2, p2_ctx, "instantiation_error", "args_not_sufficiently_instantiated");
	}

	return abolish_indicator(q, p1, p1_ctx, force);
}

// Fails for a reference to a clause already gone, erased, retracted or abolished, as in SWI.

bool do_erase(module* m, const char *str)
{
	return erase_from_db(m, clause_ref_from_buf(str)) != NULL;
}

static bool bif_erase_1(query *q)
{
	GET_FIRST_ARG(p1,atom);
	return do_erase(q->st.m, C_STR(q, p1));
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
	save_db(str, q);
	return true;
}

// List from m, as given by M:PI, else the module the call is running in.

static bool save_name(stream *str, query *q, module *m, pl_idx name, pl_int arity, bool alt, bool dump)
{
	FILE *fp = str->fp;
	if (!m)
		m = q->st.dbe ? q->st.dbe->owner->m : q->st.m;

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
				print_term_to_stream(q, str, r->cl.cells, 0, 0);
				stream_write(q, ".\n", 2, str);
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

	module *m = NULL;

	if (p1->val_off == g_colon_s) {
		p1 = p1 + 1;
		cell *cm = deref(q, p1, p1_ctx);

		if (!is_atom(cm))
			return throw_error(q, cm, p1_ctx, is_var(cm) ? "instantiation_error" : "type_error", "module");

		m = find_module(q->pl, C_STR(q, cm));

		if (!m)
			return throw_error(q, cm, p1_ctx, "existence_error", "module");

		p1 += p1->num_cells;
		p1 = deref(q, p1, p1_ctx);
		p1_ctx = q->latest_ctx;
		name = p1->val_off;
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
	return save_name(str, q, m, name, arity, false, false);
}

static bool bif_sys_xlisting_1(query *q)
{
	GET_FIRST_ARG(p1,callable);
	pl_idx name = p1->val_off;
	pl_int arity = -1;

	module *m = NULL;

	if (p1->val_off == g_colon_s) {
		p1 = p1 + 1;
		cell *cm = deref(q, p1, p1_ctx);

		if (!is_atom(cm))
			return throw_error(q, cm, p1_ctx, is_var(cm) ? "instantiation_error" : "type_error", "module");

		m = find_module(q->pl, C_STR(q, cm));

		if (!m)
			return throw_error(q, cm, p1_ctx, "existence_error", "module");

		p1 += p1->num_cells;
		p1 = deref(q, p1, p1_ctx);
		p1_ctx = q->latest_ctx;
		name = p1->val_off;
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
	save_name(str, q, m, name, arity, true, false);
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

	module *m = NULL;

	if (p1->val_off == g_colon_s) {
		p1 = p1 + 1;
		cell *cm = deref(q, p1, p1_ctx);

		if (!is_atom(cm))
			return throw_error(q, cm, p1_ctx, is_var(cm) ? "instantiation_error" : "type_error", "module");

		m = find_module(q->pl, C_STR(q, cm));

		if (!m)
			return throw_error(q, cm, p1_ctx, "existence_error", "module");

		p1 += p1->num_cells;
		p1 = deref(q, p1, p1_ctx);
		p1_ctx = q->latest_ctx;
		name = p1->val_off;
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
	save_name(str, q, m, name, arity, false, true);
	fprintf(str->fp, "\n");
	return true;
}

builtins g_database_bifs[] =
{
	{"abolish", 1, bif_iso_abolish_1, "+predicate_indicator", true, false, BLAH},
	{"thread_local", 1, bif_thread_local_1, "+term", false, false, BLAH},
	{"$destroy_module", 1, bif_sys_destroy_module_1, "+atom", false, false, BLAH},
	{"$create_module", 1, bif_sys_create_module_1, "+atom", false, false, BLAH},
	{"asserta", 1, bif_iso_asserta_1, "+term", true, false, BLAH},
	{"assertz", 1, bif_iso_assertz_1, "+term", true, false, BLAH},
	{"retract", 1, bif_iso_retract_1, "+term", true, false, BLAH},
	{"retractall", 1, bif_iso_retractall_1, "+term", true, false, BLAH},
	{"clause", 2, bif_iso_clause_2, "+term,?term", true, false, BLAH},
	{"$clause", 2, bif_sys_clause_2, "+term,?term", false, false, BLAH},

	{"asserta", 2, bif_asserta_2, "+term,-string", false, false, BLAH},
	{"assertz", 2, bif_assertz_2, "+term,-string", false, false, BLAH},
	{"erase", 1, bif_erase_1, "+string", false, false, BLAH},
	{"clause", 3, bif_clause_3, "?term,?term,-string", false, false, BLAH},
	{"abolish", 2, bif_abolish_2, "+callable,+term", false, false, BLAH},

	{"listing", 0, bif_listing_0, NULL, false, false, BLAH},
	{"listing", 1, bif_listing_1, "+predicate_indicator", false, false, BLAH},

	{"$xlisting", 1, bif_sys_xlisting_1, "+predicate_indicator", false, false, BLAH},
	{"$dlisting", 1, bif_sys_dlisting_1, "+predicate_indicator", false, false, BLAH},
	{"$dump_term", 2, bif_sys_dump_term_2, "+term,+bool", false, false, BLAH},

	{0}
};
