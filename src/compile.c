#include <ctype.h>
#include <stdlib.h>

#include "module.h"
#include "parser.h"
#include "prolog.h"
#include "query.h"

static void copy_term(cell **dst, cell **src)
{
	unsigned n = copy_cells(*dst, *src, (*src)->num_cells);
	*dst += n;
	*src += n;
}

static void calln_check(module *m, cell *p1)
{
	bool found = false;

	if ((p1->match = search_predicate(m, p1)) != NULL) {
		p1->flags &= ~FLAG_INTERNED_BUILTIN;
	} else if ((p1->bif_ptr = get_builtin_term(m, p1, &found, NULL)), found) {
		p1->flags |= FLAG_INTERNED_BUILTIN;

		if (get_arity(p1) <= 2) {
			const char *functor = C_STR(m, p1);
			unsigned specifier;

			if (match_op(m, functor, &specifier, false))
				SET_OP(p1, specifier);
		}
	} else {
		p1->flags &= ~FLAG_INTERNED_BUILTIN;
	}
}

static void compile_term(predicate *pr, clause *cl, cell **dst, cell **src)
{
	cell *c = (*src) + 1;

	// T1 , T2

	if (((*src)->val_off == g_conjunction_s) && (get_arity((*src)) == 2)) {
		*src += 1;
		compile_term(pr, cl, dst, src);								// LHS
		compile_term(pr, cl, dst, src);								// RHS
		return;
	}

	// T1 -> T2 ; T3

	if (((*src)->val_off == g_disjunction_s) && (get_arity((*src)) == 2)
		&& c->bif_ptr && (get_arity(c) == 2)
		&& (c->bif_ptr->fn == bif_iso_if_then_2)) {
		*src += 2;
		unsigned var_num = cl->num_vars++;
		cell *save_dst1 = *dst;
		make_instr((*dst)++, g_sys_succeed_on_retry_s, bif_sys_succeed_on_retry_2, 2, 2);
		make_var((*dst)++, g_anon_s, var_num);
		make_uint((*dst)++, 0);										// Dummy value1
		compile_term(pr, cl, dst, src);								// Arg1
		make_instr((*dst)++, g_cut_s, bif_iso_cut_0, 0, 0);
		make_instr((*dst)++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		compile_term(pr, cl, dst, src);								// Arg2
		cell *save_dst2 = *dst;
		make_instr((*dst)++, g_sys_jump_s, bif_sys_jump_1, 1, 1);
		make_uint((*dst)++, 0);										// Dummy value2
		make_uint(save_dst1+2, *dst - save_dst1);					// Real value1
		compile_term(pr, cl, dst, src);								// Arg3
		make_uint(save_dst2+1, *dst - save_dst2);					// Real value2
		make_instr((*dst)++, g_true_s, bif_iso_true_0, 0, 0);		// Landing
		return;
	}

	// T1 *-> T2 ; T3

	if (((*src)->val_off == g_disjunction_s) && (get_arity((*src)) == 2)
		&& c->bif_ptr && (get_arity(c) == 2)
		&& (c->bif_ptr->fn == bif_soft_if_then_2)) {
		*src += 2;
		unsigned var_num = cl->num_vars++;
		cell *save_dst1 = *dst;
		make_instr((*dst)++, g_sys_succeed_on_retry_s, bif_sys_succeed_on_retry_2, 2, 2);
		make_var((*dst)++, g_anon_s, var_num);
		make_uint((*dst)++, 0);										// Dummy value1
		compile_term(pr, cl, dst, src);								// Arg1
		make_instr((*dst)++, g_sys_cut_s, bif_sys_cut_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		make_instr((*dst)++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		compile_term(pr, cl, dst, src);								// Arg2
		cell *save_dst2 = *dst;
		make_instr((*dst)++, g_sys_jump_s, bif_sys_jump_1, 1, 1);
		make_uint((*dst)++, 0);										// Dummy value2
		make_uint(save_dst1+2, *dst - save_dst1);					// Real value1
		compile_term(pr, cl, dst, src);								// Arg3
		make_uint(save_dst2+1, *dst - save_dst2);					// Real value2
		make_instr((*dst)++, g_true_s, bif_iso_true_0, 0, 0);		// Landing
		return;
	}

	// T1 ; T2

	if (((*src)->val_off == g_disjunction_s) && (get_arity((*src)) == 2)) {
		*src += 1;
		cell *save_dst1 = *dst;
		make_instr((*dst)++, g_sys_succeed_on_retry_s, bif_sys_succeed_on_retry_1, 1, 1);
		make_uint((*dst)++, 0);										// Dummy value1
		compile_term(pr, cl, dst, src);								// LHS
		cell *save_dst2 = *dst;
		make_instr((*dst)++, g_sys_jump_s, bif_sys_jump_1, 1, 1);
		make_uint((*dst)++, 0);										// Dummy value1
		make_uint(save_dst1+1, *dst - save_dst1);					// Real value1
		compile_term(pr, cl, dst, src);								// RHS
		make_uint(save_dst2+1, *dst - save_dst2);					// Real value2
		make_instr((*dst)++, g_true_s, bif_iso_true_0, 0, 0);		// Landing
		return;
	}

	// T1 -> T2

	if (((*src)->val_off == g_if_then_s) && (get_arity((*src)) == 2)) {
		unsigned var_num = cl->num_vars++;
		*src += 1;
		make_instr((*dst)++, g_sys_fail_on_retry_s, bif_sys_fail_on_retry_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		compile_term(pr, cl, dst, src);								// Arg1
		make_instr((*dst)++, g_cut_s, bif_iso_cut_0, 0, 0);
		make_instr((*dst)++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		compile_term(pr, cl, dst, src);								// Arg2
		return;
	}

	// T1 *-> T2

	if (((*src)->val_off == g_soft_cut_s) && (get_arity((*src)) == 2)) {
		unsigned var_num = cl->num_vars++;
		*src += 1;
		make_instr((*dst)++, g_sys_fail_on_retry_s, bif_sys_fail_on_retry_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		compile_term(pr, cl, dst, src);								// Arg1
		make_instr((*dst)++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		compile_term(pr, cl, dst, src);								// Arg2
		return;
	}

	if (((*src)->val_off == g_if_s) && (get_arity((*src)) == 3)) {
		*src += 1;
		unsigned var_num = cl->num_vars++;
		cell *save_dst1 = *dst;
		make_instr((*dst)++, g_sys_succeed_on_retry_s, bif_sys_succeed_on_retry_2, 2, 2);
		make_var((*dst)++, g_anon_s, var_num);
		make_uint((*dst)++, 0);										// Dummy value1
		compile_term(pr, cl, dst, src);								// Arg1
		make_instr((*dst)++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		compile_term(pr, cl, dst, src);								// Arg2
		cell *save_dst2 = *dst;
		make_instr((*dst)++, g_sys_jump_s, bif_sys_jump_1, 1, 1);
		make_uint((*dst)++, 0);										// Dummy value2
		make_uint(save_dst1+2, *dst - save_dst1);					// Real value1
		compile_term(pr, cl, dst, src);								// Arg3
		make_uint(save_dst2+1, *dst - save_dst2);					// Real value2
		make_instr((*dst)++, g_true_s, bif_iso_true_0, 0, 0);		// Landing
		return;
	}

	if (((*src)->val_off == g_call_s) && (get_arity((*src)) > 1) && is_callable(c)) {
		unsigned var_num = cl->num_vars++;
		uint32_t arity = get_arity((*src)) - 1;
		unsigned save_num_cells = (*src)->num_cells;
		*src += 1;
		make_instr((*dst)++, g_sys_fail_on_retry_s, bif_sys_fail_on_retry_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		make_instr((*dst)++, g_sys_call_check_s, bif_sys_call_check_1, 1, save_num_cells-1);
		cell *save_dst = *dst;
		copy_term(dst, src);										// Functor
		cell *target = save_dst;

		// call(M:Goal, A...) must append to GOAL, not to the ':'
		// wrapper. Bumping the wrapper yields ':'/N with N>2 - a shape
		// only bif_iso_qualify_n() understands, and one that took the
		// runtime through a different module-resolution path than a
		// plain M:Goal(A...) body goal. Appending to the inner goal
		// keeps ':'/2, which bif_iso_qualify_2() already resolves in
		// the module it names.

		if (is_interned(save_dst) && (save_dst->val_off == g_colon_s)
			&& (get_arity(save_dst) == 2)) {
			cell *mod = save_dst + 1;
			target = mod + mod->num_cells;
		}

		set_arity(target, get_arity(target) + arity);

		while (arity--)
			copy_term(dst, src);									// Args

		target->num_cells = *dst - target;

		if (target == save_dst)
			calln_check(pr->m, save_dst);
		else
			save_dst->num_cells = *dst - save_dst;
		*dst += copy_cells(*dst, save_dst, save_dst->num_cells);
		make_instr((*dst)++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		return;
	}

	if (((*src)->val_off == g_call_s) && (get_arity((*src)) == 1)) {
		unsigned var_num = cl->num_vars++;
		*src += 1;
		make_instr((*dst)++, g_sys_fail_on_retry_s, bif_sys_fail_on_retry_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);

		if (is_var(*src) || is_builtin(*src)) {
			make_instr((*dst)++, g_sys_call_check_s, bif_sys_call_check_1, 1, (*src)->num_cells);
			*dst += copy_cells(*dst, *src, (*src)->num_cells);		// Arg2
		}

		compile_term(pr, cl, dst, src);								// Arg1
		make_instr((*dst)++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		return;
	}

	if (((*src)->val_off == g_once_s) && (get_arity((*src)) == 1)) {
		unsigned var_num = cl->num_vars++;
		*src += 1;
		make_instr((*dst)++, g_sys_fail_on_retry_s, bif_sys_fail_on_retry_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);

		if (is_var(*src) || is_builtin(*src)) {
			make_instr((*dst)++, g_sys_call_check_s, bif_sys_call_check_1, 1, (*src)->num_cells);
			*dst += copy_cells(*dst, *src, (*src)->num_cells);		// Arg2
		}

		compile_term(pr, cl, dst, src);								// Arg1
		make_instr((*dst)++, g_cut_s, bif_iso_cut_0, 0, 0);
		make_instr((*dst)++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		return;
	}

	if (((*src)->val_off == g_ignore_s) && (get_arity((*src)) == 1)) {
		unsigned var_num = cl->num_vars++;
		*src += 1;
		cell *save_dst = *dst;
		make_instr((*dst)++, g_sys_succeed_on_retry_s, bif_sys_succeed_on_retry_2, 2, 2);
		make_var((*dst)++, g_anon_s, var_num);
		make_uint((*dst)++, 0);										// Dummy value

		if (is_var(*src) || is_builtin(*src)) {
			make_instr((*dst)++, g_sys_call_check_s, bif_sys_call_check_1, 1, (*src)->num_cells);
			*dst += copy_cells(*dst, *src, (*src)->num_cells);		// Arg2
		}

		compile_term(pr, cl, dst, src);								// Arg1
		make_instr((*dst)++, g_cut_s, bif_iso_cut_0, 0, 0);
		make_instr((*dst)++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		make_uint(save_dst+2, *dst - save_dst);						// Real value
		make_instr((*dst)++, g_true_s, bif_iso_true_0, 0, 0);		// Landing
		return;
	}

	if (((*src)->val_off == g_negation_s) && (get_arity((*src)) == 1)) {
		unsigned var_num = cl->num_vars++;
		*src += 1;
		cell *save_dst = *dst;
		make_instr((*dst)++, g_sys_succeed_on_retry_s, bif_sys_succeed_on_retry_2, 2, 2);
		make_var((*dst)++, g_anon_s, var_num);
		make_uint((*dst)++, 0);										// Dummy value

		if (is_builtin(*src)) {
			make_instr((*dst)++, g_sys_call_check_s, bif_sys_call_check_1, 1, (*src)->num_cells);
			*dst += copy_cells(*dst, *src, (*src)->num_cells);		// Arg2
		}

		copy_term(dst, src);										// Not compile_term
		make_instr((*dst)++, g_cut_s, bif_iso_cut_0, 0, 0);
		make_instr((*dst)++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		make_instr((*dst)++, g_fail_s, bif_iso_fail_0, 0, 0);
		make_uint(save_dst+2, *dst - save_dst);						// Real value
		make_instr((*dst)++, g_true_s, bif_iso_true_0, 0, 0);		// Landing
		return;
	}

	if (((*src)->val_off == g_notunify_s) && (get_arity((*src)) == 2)) {
		unsigned var_num = cl->num_vars++;
		*src += 1;
		cell *save_dst = *dst;
		make_instr((*dst)++, g_sys_succeed_on_retry_s, bif_sys_succeed_on_retry_2, 2, 2);
		make_var((*dst)++, g_anon_s, var_num);
		make_uint((*dst)++, 0);										// Dummy value
		cell *save_dst1 = *dst;
		make_instr((*dst), g_unify_s, bif_iso_unify_2, 2, 0);
		SET_OP(*dst, OP_XFX); (*dst)++;
		copy_term(dst, src);										// Arg1
		copy_term(dst, src);										// Arg2
		save_dst1->num_cells = *dst - save_dst1;					// Real value
		make_instr((*dst)++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num);
		make_instr((*dst)++, g_fail_s, bif_iso_fail_0, 0, 0);
		make_uint(save_dst+2, *dst - save_dst);						// Real value
		make_instr((*dst)++, g_true_s, bif_iso_true_0, 0, 0);		// Landing
		return;
	}

	if (((*src)->val_off == g_colon_s) && (get_arity((*src)) == 2) && is_callable((c))) {
		unsigned var_num1 = cl->num_vars++;
		*src += 1;
		make_instr((*dst)++, g_sys_module_s, bif_sys_module_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num1);
		make_instr((*dst)++, g_sys_module_s, bif_sys_module_1, 1, (*src)->num_cells);
		copy_term(dst, src);										// Arg1

		if (is_builtin(*src) && is_op(*src)) {
			make_instr((*dst)++, g_sys_call_check_s, bif_sys_call_check_1, 1, (*src)->num_cells);
			*dst += copy_cells(*dst, *src, (*src)->num_cells);		// Arg2
		}

		copy_term(dst, src);										// Arg2
		make_instr((*dst)++, g_sys_module_s, bif_sys_module_1, 1, 1);
		make_var((*dst)++, g_anon_s, var_num1);
		return;
	}

#if 0
	if (!is_builtin(*src)) {
		predicate *pr2 = search_predicate(pr->m,  *src);

		if (pr2 && !pr2->is_dynamic) {
		}
	}
#endif

	copy_term(dst, src);
}

void compile_clause(predicate *pr, clause *cl, cell *body)
{
	if (cl->alt)
		return;

	pl_idx num_cells = cl->cidx - (body - cl->cells);
	cl->alt = TPL_malloc(sizeof(cell)*num_cells*100+1024); // FIXME

	// The compiled body is an optimisation: query.c falls back to the
	// clause itself when alt is null, so a failure here just leaves the
	// predicate uncompiled.

	if (!cl->alt)
		return;

	cell *dst = cl->alt, *src = body;
	compile_term(pr, cl, &dst, &src);
	assert(src->tag == TAG_END);
	dst += copy_cells(dst, src, 1);
	cell *alt = TPL_realloc(cl->alt, sizeof(cell)*((dst-cl->alt)));

	// Shrinking, so a failure is unlikely - but keep the larger buffer
	// rather than losing it.

	if (alt)
		cl->alt = alt;
}
