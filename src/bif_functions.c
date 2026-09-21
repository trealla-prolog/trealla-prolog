#ifdef _WIN32
#define _CRT_RAND_S
#define rand_r rand_s
#endif

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <fenv.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "module.h"
#include "query.h"

static inline unsigned tpl_rand_r(unsigned *seed)
{
#if defined(__COSMOPOLITAN__) || defined(__riscos__)
	*seed = *seed * 1103515245u + 12345u;
	return *seed & RAND_MAX;
#else
	return rand_r(seed);
#endif
}

#define SET_ACCUM() {											\
	if (errno == ENOMEM)										\
		return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
	q->accum.tag = TAG_INT;										\
	q->accum.val_bigint = TPL_malloc(sizeof(bigint));				\
	if (errno == ENOMEM)										\
		return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
	if (mp_int_init_copy(&q->accum.val_bigint->ival, &q->tmp_ival) == MP_MEMORY) {\
		return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
	} \
	if (errno == ENOMEM)										\
		return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
	q->accum.flags = FLAG_INT_BIG | FLAG_MANAGED;								\
	q->accum.val_bigint->refcnt = 0;							\
}

#define SET_ACCUM2() {											\
	if (errno == ENOMEM)										\
		return throw_error(q, p1, q->st.cur_ctx, "resource_error", "memory"); \
	q->accum.tag = TAG_INT;										\
	q->accum.val_bigint = TPL_malloc(sizeof(bigint));				\
	if (errno == ENOMEM)										\
		return throw_error(q, p1, q->st.cur_ctx, "resource_error", "memory"); \
	if (mp_int_init_copy(&q->accum.val_bigint->ival, &q->tmp_ival) == MP_MEMORY) {\
		return throw_error(q, p1, q->st.cur_ctx, "resource_error", "memory"); \
	} \
	if (errno == ENOMEM)										\
		return throw_error(q, p1, q->st.cur_ctx, "resource_error", "memory"); \
	q->accum.flags = FLAG_INT_BIG | FLAG_MANAGED;								\
	q->accum.val_bigint->refcnt = 0;							\
}

#define SET_RAT_ACCUM2() {											\
	if (errno == ENOMEM)										\
		return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
	mp_rat_reduce(&q->tmp_irat);	\
	if (mp_int_compare_value(&q->tmp_irat.den, 1)) { \
		q->accum.tag = TAG_RATIONAL;										\
		q->accum.val_bigint = TPL_malloc(sizeof(bigint));				\
		if (errno == ENOMEM)										\
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
		if (mp_rat_init_copy(&q->accum.val_bigint->irat, &q->tmp_irat) == MP_MEMORY) {\
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
		} \
		q->accum.flags = FLAG_INT_BIG | FLAG_MANAGED;								\
		q->accum.val_bigint->refcnt = 0;							\
	} else { \
		q->accum.tag = TAG_INT;										\
		q->accum.val_bigint = TPL_malloc(sizeof(bigint));				\
		if (errno == ENOMEM)										\
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
		if (mp_int_init_copy(&q->accum.val_bigint->ival, &q->tmp_irat.num) == MP_MEMORY) {\
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
		} \
		q->accum.flags = FLAG_INT_BIG | FLAG_MANAGED;								\
		q->accum.val_bigint->refcnt = 0;							\
	} \
	if (errno == ENOMEM)										\
		return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
}

static void clr_accum(cell *p)
{
	if (is_bigint(p) && p->val_bigint && !p->val_bigint->refcnt) {
		mp_int_clear(&p->val_bigint->ival);
		TPL_free(p->val_bigint);
	} else if (is_rational(p) && p->val_bigint && !p->val_bigint->refcnt) {
		mp_rat_clear(&p->val_bigint->irat);
		TPL_free(p->val_bigint);
	}

	p->tag = TAG_INT;
	p->flags = 0;
}

#define CLEANUP __attribute__((cleanup (clr_accum)))

#if defined(__SIZEOF_INT128__)

#define ON_OVERFLOW(op,v1,v2)									\
	__int128_t tmp = (__int128_t)v1 op v2;						\
	if ((tmp > INT64_MAX) || (tmp < INT64_MIN))

#else

#define ON_OVERFLOW(op,v1,v2)									\
	pl_int tmp = (pl_int)v1 op v2;								\
	if ((v1) >= INT32_MAX ||									\
		(v1) <= INT32_MIN ||									\
		(v2) >= INT32_MAX ||									\
		(v2) <= INT32_MIN)
#endif

#define DO_OP(op,op2,p1,p2) \
	if (is_smallint(&p1) && is_smallint(&p2)) { \
		ON_OVERFLOW(op, p1.val_int, p2.val_int) { \
			mpz_t tmp; \
			mp_int_init_value(&tmp, p1.val_int); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
			mp_int_##op2##_value(&tmp, p2.val_int, &q->tmp_ival); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory"); \
			mp_int_clear(&tmp); \
			SET_ACCUM(); \
		} else { \
			q->accum.val_int = tmp; \
			q->accum.tag = TAG_INT; \
		} \
	} else if (is_bigint(&p1)) { \
		if (is_bigint(&p2)) { \
			mp_int_##op2(&p1.val_bigint->ival, &p2.val_bigint->ival, &q->tmp_ival); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory"); \
			if (mp_int_compare_value(&q->tmp_ival, MP_SMALL_MAX) > 0) { \
				SET_ACCUM(); \
			} else if (mp_int_compare_value(&q->tmp_ival, MP_SMALL_MIN) < 0) { \
				SET_ACCUM(); \
			} else { \
				mp_small i; \
				mp_int_to_int(&q->tmp_ival, &i); \
				q->accum.val_int = i; \
				q->accum.tag = TAG_INT; \
				q->accum.flags = 0; \
			} \
		} else if (is_smallint(&p2)) { \
			mp_int_##op2##_value(&p1.val_bigint->ival, p2.val_int, &q->tmp_ival); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
			SET_ACCUM(); \
		} else if (is_float(&p2)) { \
			pl_flt d = BIGINT_TO_DOUBLE(&p1.val_bigint->ival); \
			q->accum.val_float = d op p2.val_float; \
			if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow"); \
			q->accum.tag = TAG_FLOAT; \
			q->accum.flags = 0; \
		} else if (is_rational(&p2)) { \
			mpq_t tmp; \
			mp_int_init_copy(&tmp.num, &p1.val_bigint->ival); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
			mp_int_init_value(&tmp.den, 1); \
			mp_rat_##op2(&p2.val_bigint->irat, &tmp, &q->tmp_irat); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory"); \
			mp_rat_clear(&tmp); \
			SET_RAT_ACCUM2(); \
		} \
	} else if (is_bigint(&p2)) { \
		if (is_smallint(&p1)) { \
			mpz_t tmp; \
			mp_int_init_value(&tmp, p1.val_int); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
			mp_int_##op2(&tmp, &p2.val_bigint->ival, &q->tmp_ival); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory"); \
			mp_int_clear(&tmp); \
			SET_ACCUM(); \
		} else if (is_float(&p1)) { \
			pl_flt d = BIGINT_TO_DOUBLE(&p2.val_bigint->ival); \
			q->accum.val_float = p1.val_float op d; \
			if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow"); \
			q->accum.tag = TAG_FLOAT; \
			q->accum.flags = 0; \
		} else if (is_rational(&p1)) { \
			mpq_t tmp; \
			mp_int_init_copy(&tmp.num, &p2.val_bigint->ival); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
			mp_int_init_value(&tmp.den, 1); \
			mp_rat_##op2(&p1.val_bigint->irat, &tmp, &q->tmp_irat); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
			mp_rat_clear(&tmp); \
			SET_RAT_ACCUM2(); \
		} \
	} else if (is_smallint(&p1) && is_float(&p2)) { \
		q->accum.val_float = (pl_flt)p1.val_int op p2.val_float; \
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow"); \
		q->accum.tag = TAG_FLOAT; \
	} else if (is_float(&p1) && is_float(&p2)) { \
		q->accum.val_float = p1.val_float op p2.val_float; \
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow"); \
		q->accum.tag = TAG_FLOAT; \
	} else if (is_float(&p1) && is_smallint(&p2)) { \
		q->accum.val_float = p1.val_float op p2.val_int; \
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow"); \
		q->accum.tag = TAG_FLOAT; \
	} else if (is_rational(&p1)) { \
		if (is_rational(&p2)) { \
			mp_rat_##op2(&p1.val_bigint->irat, &p2.val_bigint->irat, &q->tmp_irat); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory"); \
			SET_RAT_ACCUM2(); \
		} else if (is_bigint(&p2)) { \
			mpq_t tmp; \
			mp_int_init_copy(&tmp.num, &p2.val_bigint->ival); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory"); \
			mp_int_init_value(&tmp.den, 1); \
			mp_rat_##op2(&p1.val_bigint->irat, &tmp, &q->tmp_irat); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory"); \
			mp_rat_clear(&tmp); \
			SET_RAT_ACCUM2(); \
		} else { \
			mpq_t tmp; \
			mp_rat_init(&tmp); \
			mp_rat_set_value(&tmp, p2.val_int, 1); \
			mp_rat_##op2(&p1.val_bigint->irat, &tmp, &q->tmp_irat); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory"); \
			mp_rat_clear(&tmp); \
			SET_RAT_ACCUM2(); \
		} \
	} else if (is_rational(&p2)) { \
		if (is_bigint(&p1)) { \
			mpq_t tmp; \
			mp_int_init_copy(&tmp.num, &p1.val_bigint->ival); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory"); \
			mp_int_init_value(&tmp.den, 1); \
			mp_rat_##op2(&p2.val_bigint->irat, &tmp, &q->tmp_irat); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory"); \
			mp_rat_clear(&tmp); \
			SET_RAT_ACCUM2(); \
		} else { \
			mpq_t tmp; \
			mp_rat_init(&tmp); \
			mp_rat_set_value(&tmp, p1.val_int, 1); \
			mp_rat_##op2(&p2.val_bigint->irat, &tmp, &q->tmp_irat); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory"); \
			mp_rat_clear(&tmp); \
			SET_RAT_ACCUM2(); \
		} \
	} else if (is_var(&p1) || is_var(&p2)) { \
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated"); \
	} else { \
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable"); \
	}

static pl_flt BIGINT_TO_DOUBLE(mpz_t *v)
{
	double d;
	mp_int_to_double(v, &d);
	return d;
}

static pl_flt RATIONAL_TO_DOUBLE(mpq_t *v)
{
	return BIGINT_TO_DOUBLE(&v->num) / BIGINT_TO_DOUBLE(&v->den);
}

static mp_result mp_int_divx(mp_int a, mp_int b, mp_int q)
{
	return mp_int_div(a, b, q, NULL);
}

static mp_result mp_int_divx_value(mp_int a, mp_small b, mp_int q)
{
	return mp_int_div_value(a, b, q, NULL);
}

static mp_result mp_rat_divx(mp_rat a, mp_rat b, mp_rat q)
{
	return mp_rat_div(a, b, q);
}

bool call_builtin(query *q, cell *c, pl_ctx c_ctx)
{
	cell *save = q->st.instr;
	pl_ctx save_ctx = q->st.cur_ctx;
	bool save_calc = q->eval;
	q->st.instr = c;
	q->st.cur_ctx = c_ctx;
	q->eval = true;

#if USE_FFI
	if (c->bif_ptr->ffi)
		wrap_ffi_function(q, c->bif_ptr);
	else
#endif
	if (!c->bif_ptr->evaluable && (c->val_off != g_float_s))
		return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");
	else if (q->max_eval_depth++ > g_max_depth)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "type_error", "evaluable");
	else
		c->bif_ptr->fn(q);

	q->eval = save_calc;

	if (!q->did_throw) {
		q->st.instr = save;
		q->st.cur_ctx = save_ctx;
	}

	return true;
}

bool call_userfun(query *q, cell *c, pl_ctx c_ctx)
{
	return throw_error(q, c, c_ctx, "type_error", "evaluable");
}

static bool bif_iso_is_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2_tmp,any);
	q->max_eval_depth = 0;
	CLEANUP cell p2 = eval(q, p2_tmp);
	p2.num_cells = 1;

	if (!is_number(&p2))
		return throw_error(q, &p2, p2_tmp_ctx, "type_error", "evaluable");

	if (is_float(&p2) && isnan(p2.val_float))
		return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "undefined");

	pl_int val;

	if (is_bigint(&p2)) {
		mp_small tmp;
		if (mp_int_to_int(&p2.val_bigint->ival, &tmp) != MP_RANGE) {
			if (tmp != PL_INT_MIN) {
				// The accumulator's bigint carries refcnt 0 (nobody
				// owns it yet - see SET_ACCUM), so unshare_cell()
				// wrapped the count to UINT_MAX rather than freeing,
				// and q->accum's flags are already cleared by then, so
				// the clr_accum() below could not release it either.
				// Every bigint result that narrows to a small int
				// leaked its bigint. Release it through the cell that
				// still carries the flags.

				clr_accum(&p2);
				make_int(&p2, tmp);
			}


		}
	}

	bool ok = unify(q, p1, p1_ctx, &p2, q->st.cur_ctx);
	clr_accum(&q->accum);
	return ok;
}

bool bif_iso_float_1(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);

	if (q->eval) {
		CLEANUP cell p1 = eval(q, p1_tmp);

		if (is_float(&p1)) {
			q->accum.val_float = p1.val_float;
			q->accum.tag = TAG_FLOAT;
			return true;
		}

		if (is_rational(&p1)) {
			q->accum.val_float = RATIONAL_TO_DOUBLE(&p1.val_bigint->irat);
			if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
			q->accum.tag = TAG_FLOAT;
			q->accum.flags = 0;
			return true;
		}

		if (is_bigint(&p1)) {
			q->accum.val_float = BIGINT_TO_DOUBLE(&p1.val_bigint->ival);
			if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
			q->accum.tag = TAG_FLOAT;
			return true;
		}

		if (is_smallint(&p1)) {
			q->accum.val_float = (pl_flt)p1.val_int;
			q->accum.tag = TAG_FLOAT;
			return true;
		}

		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer_or_float");
	}

	return is_float(p1_tmp);
}

bool bif_iso_integer_1(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);

	if (q->eval) {
		CLEANUP cell p1 = eval(q, p1_tmp);

		if (is_integer(&p1)) {
			share_cell(&p1);
			q->accum = p1;
			return true;
		}

		if (is_float(&p1) && (p1.val_float < (pl_flt)PL_INT_MAX) && (p1.val_float > (pl_flt)PL_INT_MIN)) {
			q->accum.val_int = (pl_int)p1.val_float;
			q->accum.tag = TAG_INT;
			return true;
		}

		if (is_float(&p1)) {
			mp_int_set_double(&q->tmp_ival, p1.val_float);
			SET_ACCUM();
			return true;
		}

		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer_or_float");
	}

	return is_integer(p1_tmp);
}

static bool bif_iso_abs_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	q->accum.tag = p1.tag;

	if (is_smallint(&p1))
		q->accum.val_int = llabs((long long)p1.val_int);
	else if (is_bigint(&p1)) {
		mp_int_abs(&p1.val_bigint->ival, &q->tmp_ival);
		SET_ACCUM();
	} else if (is_rational(&p1)) {
		mp_rat_abs(&p1.val_bigint->irat, &q->tmp_irat);
		SET_RAT_ACCUM2();
	} else if (is_float(&p1))
		q->accum.val_float = fabs(p1.val_float);
	else
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");

	return true;
}

static bool bif_iso_sign_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	q->accum.tag = TAG_INT;
	q->accum.flags = 0;

	if (is_smallint(&p1))
		q->accum.val_int = p1.val_int < 0 ? -1 : p1.val_int > 0  ? 1 : 0;
	else if (is_bigint(&p1))
		q->accum.val_int = mp_int_compare_zero(&p1.val_bigint->ival);
	else if (is_rational(&p1))
		q->accum.val_int = mp_rat_compare_zero(&p1.val_bigint->irat);
	else if (is_float(&p1)) {
		q->accum.tag = TAG_FLOAT;
		q->accum.val_float = p1.val_float < 0 ? -1 : p1.val_float > 0  ? 1 : 0;
	} else
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");

	return true;
}

static bool bif_iso_positive_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	q->accum = p1;
	return true;
}

static bool bif_iso_negative_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	q->accum.tag = p1.tag;

	if (is_smallint(&p1))
		q->accum.val_int = -p1.val_int;
	else if (is_bigint(&p1)) {
		mp_int_neg(&p1.val_bigint->ival, &q->tmp_ival);
		SET_ACCUM();
	} else if (is_rational(&p1)) {
		mp_rat_neg(&p1.val_bigint->irat, &q->tmp_irat);
		SET_RAT_ACCUM2();
	} else if (is_float(&p1))
		q->accum.val_float = -p1.val_float;
	else if (is_var(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	else
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");

	return true;
}

static bool bif_iso_epsilon_0(query *q)
{
	START_FUNCTION(q);
	q->accum.val_float = DBL_EPSILON;
	q->accum.tag = TAG_FLOAT;
	return true;
}

static bool bif_iso_pi_0(query *q)
{
	START_FUNCTION(q);
	q->accum.val_float = M_PI;
	q->accum.tag = TAG_FLOAT;
	return true;
}

static bool bif_iso_e_0(query *q)
{
	START_FUNCTION(q);
	q->accum.val_float = M_E;
	q->accum.tag = TAG_FLOAT;
	return true;
}

static bool bif_numerator_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (!is_integer(&p1) && !is_rational(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "rational"); \

	if (is_integer(&p1)) {
		q->accum = p1;
		return true;
	}

	q->accum.tag = TAG_INT;
	q->accum.val_bigint = TPL_malloc(sizeof(bigint));
	if (errno == ENOMEM)
		return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	mp_int_init_copy(&q->accum.val_bigint->ival, &p1.val_bigint->irat.num);
	if (errno == ENOMEM)
		return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	q->accum.flags = FLAG_INT_BIG | FLAG_MANAGED;
	q->accum.val_bigint->refcnt = 0;
	return true;
}

static bool bif_denominator_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (!is_integer(&p1) && !is_rational(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "rational"); \

	if (is_integer(&p1)) {
		cell tmp;
		make_int(&tmp, 1);
		q->accum = tmp;
		return true;
	}

	q->accum.tag = TAG_INT;
	q->accum.val_bigint = TPL_malloc(sizeof(bigint));
	if (errno == ENOMEM)
		return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	mp_int_init_copy(&q->accum.val_bigint->ival, &p1.val_bigint->irat.den);
	if (errno == ENOMEM)
		return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	q->accum.flags = FLAG_INT_BIG | FLAG_MANAGED;
	q->accum.val_bigint->refcnt = 0;
	return true;
}

static bool bif_rational_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return is_rational(p1) || is_integer(p1);
}

static bool bif_rdiv_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (!is_integer(&p1) && !is_rational(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");

	if (!is_integer(&p2) && !is_rational(&p2))
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");

	if (is_zero(&p2))
		return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "zero_divisor");

	if (is_rational(&p1) && is_rational(&p2)) {
		if (mp_int_mul(&p1.val_bigint->irat.num, &p2.val_bigint->irat.den, &q->tmp_irat.num) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		if (mp_int_mul(&p1.val_bigint->irat.den, &p2.val_bigint->irat.num, &q->tmp_irat.den) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	} else if (is_rational(&p1) && is_bigint(&p2)) {
		if (mp_int_mul(&p1.val_bigint->irat.den, &p2.val_bigint->ival, &q->tmp_irat.den) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		if (mp_int_set_value(&q->tmp_irat.num, 1) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	} else if (is_rational(&p1) && is_smallint(&p2)) {
		if (mp_int_mul_value(&p1.val_bigint->irat.den, p2.val_int, &q->tmp_irat.den) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	} else if (is_bigint(&p1) && is_rational(&p2)) {
		if (mp_int_mul(&p2.val_bigint->irat.den, &p1.val_bigint->ival, &q->tmp_irat.num) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		if (mp_int_init_copy(&q->tmp_irat.den, &p2.val_bigint->irat.num) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	} else if (is_bigint(&p1) && is_bigint(&p2)) {
		if (mp_int_init_copy(&q->tmp_irat.num, &p1.val_bigint->ival) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		if (mp_int_init_copy(&q->tmp_irat.den, &p2.val_bigint->ival) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		if (mp_int_init_copy(&q->tmp_irat.num, &p1.val_bigint->ival) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		if (mp_int_set_value(&q->tmp_irat.den, p2.val_int) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	} else if (is_smallint(&p1) && is_bigint(&p2)) {
		if (mp_int_set_value(&q->tmp_irat.num, p1.val_int) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		if (mp_int_init_copy(&q->tmp_irat.den, &p2.val_bigint->ival) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		mp_rat_reduce(&q->tmp_irat);
	} else if (is_smallint(&p1) && is_rational(&p2)) {
		if (mp_int_mul_value(&p2.val_bigint->irat.den, p1.val_int, &q->tmp_irat.num) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		if (mp_int_init_copy(&q->tmp_irat.den, &p2.val_bigint->irat.num) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	} else if (is_smallint(&p1) && is_smallint(&p2)) {
		if (mp_rat_set_value(&q->tmp_irat, p1.val_int, p2.val_int) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	} else {
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");
	}

	SET_RAT_ACCUM2();
	return true;
}

bool bif_iso_add_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	DO_OP(+, add, p1, p2);
	return true;
}

static bool bif_iso_sub_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	DO_OP(-, sub, p1, p2);
	return true;
}

static bool bif_iso_mul_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	DO_OP(*, mul, p1, p2);
	return true;
}

static bool bif_iso_exp_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_bigint(&p1)) {
		if (mp_int_compare_zero(&p1.val_bigint->ival) <= 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = exp(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		q->accum.tag = TAG_FLOAT;

	} else if (is_smallint(&p1)) {
		q->accum.val_float = exp((pl_flt)p1.val_int);

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;

	} else if (is_float(&p1)) {
		q->accum.val_float = exp(p1.val_float);

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	return true;
}

static bool bif_iso_sqrt_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_bigint(&p1)) {
		if (mp_int_compare_zero(&p1.val_bigint->ival) < 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = sqrt(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		if (mp_rat_compare_zero(&p1.val_bigint->irat) < 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = sqrt(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		q->accum.tag = TAG_FLOAT;
	} else if (is_smallint(&p1)) {
		if (p1.val_int < 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = sqrt((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		if (p1.val_float == -1)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = sqrt(p1.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	return true;
}

static bool bif_iso_log_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_bigint(&p1)) {
		if (mp_int_compare_zero(&p1.val_bigint->ival) <= 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = log(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		q->accum.tag = TAG_FLOAT;
	} if (is_rational(&p1)) {
		if (mp_rat_compare_zero(&p1.val_bigint->irat) <= 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = log(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		q->accum.tag = TAG_FLOAT;
	} else if (is_smallint(&p1)) {
		if (p1.val_int <= 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = log((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		if (p1.val_float <= 0.0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = log(p1.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	return true;
}

static bool bif_popcount_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (!is_integer(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");

	if (is_bigint(&p1)) {
		mp_usmall count = 0;

		if (mp_int_popcount(&p1.val_bigint->ival, &count) != MP_OK)
			return throw_error(q, &p1, q->st.cur_ctx, "domain_error", "not_less_than_zero");

		q->accum.val_int = count;
	} else {
		if (p1.val_int < 0)
			return throw_error(q, &p1, q->st.cur_ctx, "domain_error", "not_less_than_zero");

		uint64_t n = p1.val_int;
		uint64_t count = 0;

		while (n > 0) {
			n = n & (n - 1);
			count++;
		}

		q->accum.val_int = count;
	}

	q->accum.tag = TAG_INT;
	return true;
}

static bool bif_lsb_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (!is_integer(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");

	if (is_bigint(&p1)) {
		mp_usmall count = 0;

		if (mp_int_lsb(&p1.val_bigint->ival, &count) != MP_OK)
			return throw_error(q, &p1, q->st.cur_ctx, "domain_error", "not_less_than_one");

		q->accum.val_int = count;
	} else {
		if (p1.val_int < 1)
			return throw_error(q, &p1, q->st.cur_ctx, "domain_error", "not_less_than_one");

		uint64_t n = p1.val_int;
    	uint64_t lsb = 0;
    	while ((n & 1) == 0) {
    	    ++lsb;
    	    n = n >> 1;
    	}

		q->accum.val_int = lsb;
	}

	q->accum.tag = TAG_INT;
	return true;
}

static bool bif_msb_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (!is_integer(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");

	if (is_bigint(&p1)) {
		mp_usmall count = 0;

		if (mp_int_msb(&p1.val_bigint->ival, &count) != MP_OK)
			return throw_error(q, &p1, q->st.cur_ctx, "domain_error", "not_less_than_one");

		q->accum.val_int = count;
	} else {
		if (p1.val_int < 1)
			return throw_error(q, &p1, q->st.cur_ctx, "domain_error", "not_less_than_one");

		uint64_t n = p1.val_int;
    	uint64_t msb = -1;
    	while (n != 0) {
    	    msb++;
    	    n = n >> 1;
    	}

		q->accum.val_int = msb;
	}

	q->accum.tag = TAG_INT;
	return true;
}

static bool bif_iso_truncate_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_float(&p1)) {
		q->accum.val_int = (pl_int)trunc(p1.val_float);

#ifdef FE_INVALID
		if (fetestexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW)) {
			mp_int_set_double(&q->tmp_ival, p1.val_float);
			SET_ACCUM();
			feclearexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW);
		} else
#endif
			q->accum.tag = TAG_INT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (is_integer(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "float");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	return true;
}

static bool bif_iso_round_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_float(&p1)) {
		q->accum.val_int = (pl_int)floor(p1.val_float+0.5);

#ifdef FE_INVALID
		if (fetestexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW)) {
			mp_int_set_double(&q->tmp_ival, p1.val_float);
			SET_ACCUM();
			feclearexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW);
		} else
#endif
			q->accum.tag = TAG_INT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (is_smallint(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "float");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	return true;
}

static bool bif_iso_ceiling_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_float(&p1)) {
		q->accum.val_int = (pl_int)ceil(p1.val_float);

#ifdef FE_INVALID
		if (fetestexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW)) {
			mp_int_set_double(&q->tmp_ival, p1.val_float);
			SET_ACCUM();
			feclearexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW);
		} else
#endif
			q->accum.tag = TAG_INT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (is_smallint(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "float");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	return true;
}

static bool bif_iso_float_integer_part_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	// Not a cast to pl_int: that is undefined once the value is outside
	// int64, and every float at or above 2^53 already is a whole number.

	if (is_float(&p1)) {
		q->accum.val_float = trunc(p1.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (is_smallint(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "float");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	return true;
}

static bool bif_iso_float_fractional_part_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_float(&p1)) {
		q->accum.val_float = p1.val_float - trunc(p1.val_float);

		// Finite in, finite out - only a non-finite argument, which
		// arithmetic here cannot produce, leaves anything to catch.

		if (isnan(q->accum.val_float))
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (is_smallint(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "float");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	return true;
}

static bool bif_iso_floor_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_float(&p1)) {
		q->accum.val_int = (pl_int)floor(p1.val_float);

#ifdef FE_INVALID
		if (fetestexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW)) {
			mp_int_set_double(&q->tmp_ival, p1.val_float);
			SET_ACCUM();
			feclearexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW);
		} else
#endif
			q->accum.tag = TAG_INT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (is_smallint(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "float");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	return true;
}

static bool bif_iso_sin_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = sin((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = sin(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = sin(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = sin(p1.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_iso_cos_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = cos((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = cos(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = cos(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = cos(p1.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_iso_tan_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = tan((pl_flt)p1.val_int);
		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = tan(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = tan(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = tan(p1.val_float);
		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_iso_asin_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = asin((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = asin(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = asin(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = asin(p1.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_iso_acos_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = acos((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = acos(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = acos(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = acos(p1.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_iso_atan_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = atan((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = atan(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = atan(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = atan(p1.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_iso_atan2_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_smallint(&p1) && is_smallint(&p2)) {
		if ((p1.val_int == 0) && (p2.val_int == 0))
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = atan2((pl_flt)p1.val_int, (pl_flt)p2.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_smallint(&p1) && is_float(&p2)) {
		if ((p1.val_int == 0) && (p2.val_float == 0.0))
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = atan2((pl_flt)p1.val_int, p2.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1) && is_float(&p2)) {
		if ((p1.val_float == 0.0) && (p2.val_int == 0))
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = atan2(p1.val_float, p2.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1) && is_smallint(&p2)) {
		if ((p1.val_float == 0.0) && (p2.val_int == 0))
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = atan2(p1.val_float, (pl_flt)p2.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_sinh_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = sinh((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = sinh(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = sinh(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = sinh(p1.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_cosh_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = cosh((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = cosh(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = cosh(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = cosh(p1.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_tanh_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = tanh((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = tanh(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = tanh(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = tanh(p1.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_asinh_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = asinh((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = asinh(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = asinh(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = asinh(p1.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_acosh_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = acosh((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = acosh(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = acosh(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = acosh(p1.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_atanh_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = atanh((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = atanh(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = atanh(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = atanh(p1.val_float);
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "undefined");
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_erf_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = erf((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = erf(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = erf(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = erf(p1.val_float);
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "undefined");
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_erfc_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_float = 1.0 - erf((pl_flt)p1.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1)) {
		q->accum.val_float = 1.0 - erf(BIGINT_TO_DOUBLE(&p1.val_bigint->ival));
		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1)) {
		q->accum.val_float = 1.0 - erf(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1)) {
		q->accum.val_float = 1.0 - erf(p1.val_float);
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "undefined");
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isinf(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_iso_copysign_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_smallint(&p1) && is_smallint(&p2)) {
		q->accum = p1;

		if (p2.val_int < 0)
			q->accum.val_int = -llabs((long long)p1.val_int);

		q->accum.tag = TAG_INT;
	} else if (is_smallint(&p1) && is_float(&p2)) {
		q->accum = p1;

		if (p2.val_float < 0.0)
			q->accum.val_int = -llabs((long long)p1.val_int);

		q->accum.tag = TAG_INT;
	} else if (is_float(&p1) && is_float(&p2)) {
		q->accum.val_float = copysign(p1.val_float, p2.val_float);
		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1) && is_smallint(&p2)) {
		q->accum.val_float = copysign(p1.val_float, p2.val_int);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	return true;
}

static bool bif_iso_pow_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_smallint(&p1) && is_bigint(&p2)) {
		mp_small tmp;
		mp_int_to_int(&p2.val_bigint->ival, &tmp);
		q->accum.val_float = pow((pl_flt)p1.val_int, (pl_flt)tmp);
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		if ((mp_int_compare_zero(&p1.val_bigint->ival) == 0) && (p2.val_int < 0))
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = pow(BIGINT_TO_DOUBLE(&p1.val_bigint->ival), (pl_flt)p2.val_int);
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		q->accum.tag = TAG_FLOAT;
		return true;
	} else if (is_smallint(&p1) && is_smallint(&p2)) {
		if ((p1.val_int == 0) && (p2.val_int < 0))
			return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = pow((pl_flt)p1.val_int, (pl_flt)p2.val_int);

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_smallint(&p1) && is_float(&p2)) {
		if ((p1.val_int == 0) && (p2.val_float < 0.0))
			return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = pow((pl_flt)p1.val_int, p2.val_float);

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1) && is_float(&p2)) {
		if ((p1.val_float == 0.0) && (p2.val_float < 0.0))
			return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = pow(p1.val_float, p2.val_float);

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1) && is_smallint(&p2)) {
		if ((p1.val_float == 0.0) && (p2.val_int < 0))
			return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.val_float = pow(p1.val_float, p2.val_int);

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_iso_powi_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_bigint(&p1) && is_bigint(&p2)) {
		if (is_negative(&p2))
			return throw_error(q, &p2, q->st.cur_ctx, "type_error", "greater_zero");

		if (mp_int_expt_full(&p1.val_bigint->ival, &p2.val_bigint->ival, &q->tmp_ival) != MP_OK)
			return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory");

		SET_ACCUM();
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		if (p2.val_int < 0)
			return throw_error(q, &p2, q->st.cur_ctx, "type_error", "greater_zero");

		if (p2.val_int > (INT32_MAX/2))
			return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory");

		if (mp_int_expt(&p1.val_bigint->ival, p2.val_int, &q->tmp_ival) != MP_OK)
			return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory");

		SET_ACCUM();
	} else if (is_bigint(&p2) && is_smallint(&p1)) {
		if (is_negative(&p2))
			return throw_error(q, &p2, q->st.cur_ctx, "type_error", "greater_zero");

		mpz_t tmp;
		mp_int_init_value(&tmp, p1.val_int);

		if (mp_int_expt_full(&tmp, &p2.val_bigint->ival, &q->tmp_ival) != MP_OK) {
			mp_int_clear(&tmp);
			return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory");
		}

		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_smallint(&p1) && is_smallint(&p2)) {
		if ((p1.val_int == 0) && (p2.val_int < 0))
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		if ((llabs(p1.val_int) != 1) && (p2.val_int < 0))
			return throw_error(q, &p1, q->st.cur_ctx, "type_error", "float");

		if (p2.val_int == 0) {
			q->accum.val_int = 1;
			q->accum.tag = TAG_INT;
			return true;
		}

		if (p2.val_int < 0) {
			q->accum.val_int = pow(p1.val_int, p2.val_int);
			q->accum.tag = TAG_INT;
			return true;
		}

		if (mp_int_expt_value(p1.val_int, p2.val_int, &q->tmp_ival) != MP_OK)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");

		if (errno == ENOMEM)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");

		if (mp_int_compare_value(&q->tmp_ival, MP_SMALL_MAX) > 0) {
			SET_ACCUM();
			return true;
		}

		mp_small i;
		mp_int_to_int(&q->tmp_ival, &i);
		q->accum.val_int = i;
		q->accum.tag = TAG_INT;
	} else if (is_smallint(&p1) && is_float(&p2)) {
		q->accum.val_float = pow(p1.val_int, p2.val_float);

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1) && is_float(&p2)) {
		q->accum.val_float = pow(p1.val_float, p2.val_float);

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1) && is_smallint(&p2)) {
		q->accum.val_float = pow(p1.val_float, p2.val_int);

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_iso_divide_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_bigint(&p1) && is_bigint(&p2)) {
		q->accum.val_float = BIGINT_TO_DOUBLE(&p1.val_bigint->ival);
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		pl_flt d = BIGINT_TO_DOUBLE(&p2.val_bigint->ival);
		if (isinf(d)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		if (d == 0.0)
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		q->accum.val_float /= d;
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		q->accum.val_float = BIGINT_TO_DOUBLE(&p1.val_bigint->ival);
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		if (p2.val_int == 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		q->accum.val_float /= p2.val_int;
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p1) && is_float(&p2)) {
		if (p2.val_float == 0.0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		q->accum.val_float = BIGINT_TO_DOUBLE(&p1.val_bigint->ival) / p2.val_float;
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p2) && is_smallint(&p1)) {
		q->accum.val_float = p1.val_int;
		pl_flt d = BIGINT_TO_DOUBLE(&p2.val_bigint->ival);
		if (isinf(d)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		if (d == 0.0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		q->accum.val_float /= d;

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_bigint(&p2) && is_float(&p1)) {
		pl_flt d = BIGINT_TO_DOUBLE(&p2.val_bigint->ival);
		if (isinf(d)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
		if (d == 0.0) return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		q->accum.val_float = p1.val_float / d;

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_smallint(&p1) && is_smallint(&p2)) {
		if (p2.val_int == 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		q->accum.val_float = (pl_flt)p1.val_int / p2.val_int;

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_smallint(&p1) && is_float(&p2)) {
		if (p2.val_float == 0.0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		q->accum.val_float = (pl_flt)p1.val_int / p2.val_float;

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_rational(&p1) && is_smallint(&p2)) {
		q->accum.val_float = RATIONAL_TO_DOUBLE(&p1.val_bigint->irat);
		q->accum.val_float /= p2.val_int;

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1) && is_float(&p2)) {
		if (p2.val_float == 0.0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		q->accum.val_float = p1.val_float / p2.val_float;

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1) && is_smallint(&p2)) {
		if (p2.val_int == 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		q->accum.val_float = p1.val_float / p2.val_int;

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_float(&q->accum) && isnan(q->accum.val_float))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_iso_divint_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_integer(&p1) && is_integer(&p2)) {
		if (is_bigint(&p2) && mp_int_compare_zero(&p2.val_bigint->ival) == 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		if (is_smallint(&p2) && get_smallint(&p2) == 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		DO_OP(/, divx, p1, p2);
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (!is_integer(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");
	} else if (!is_integer(&p2)) {
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

static pl_int mod(pl_int x, pl_int y)
{
	pl_int r = x % y;

	if ( (r != 0)
		&& (r<0) != (y<0) )
		r += y;

  return r;
}

static void big_mod(mpz_t *x, mpz_t *y, mpz_t *r)
{
	mp_int_div(x, y, NULL, r);

	if ((mp_int_compare_value(r, 0) < 0) != (mp_int_compare_value(y, 0) < 0)) {
		mp_int_add(r, y, r);
	}
}

static bool bif_iso_mod_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_smallint(&p1) && is_smallint(&p2)) {
		if (p2.val_int == 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		q->accum.val_int = mod(p1.val_int, p2.val_int);
		q->accum.tag = TAG_INT;
	} else if (is_bigint(&p1) && is_bigint(&p2)) {
		big_mod(&p1.val_bigint->ival, &p2.val_bigint->ival, &q->tmp_ival);
		SET_ACCUM();
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p2.val_int);
		big_mod(&p1.val_bigint->ival, &tmp, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_smallint(&p1) && is_bigint(&p2)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p1.val_int);
		big_mod(&tmp, &p2.val_bigint->ival, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (!is_integer(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");
	} else if (!is_integer(&p2)) {
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

static bool bif_iso_div_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_bigint(&p1) && is_bigint(&p2)) {
		mpz_t tmp3, tmp4;
		mp_int_init(&tmp3);
		mp_int_init(&tmp4);
		mp_int_mod(&p1.val_bigint->ival, &p2.val_bigint->ival, &tmp3);
		mp_int_sub(&p1.val_bigint->ival, &tmp3, &tmp4);
		mp_int_div(&tmp4, &p2.val_bigint->ival, &q->tmp_ival, NULL);
		SET_ACCUM();
		mp_int_clear(&tmp3);
		mp_int_clear(&tmp4);
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		mpz_t tmp2, tmp3, tmp4;
		mp_int_init_value(&tmp2, p2.val_int);
		mp_int_init(&tmp3);
		mp_int_init(&tmp4);
		mp_int_mod(&p1.val_bigint->ival, &tmp2, &tmp3);
		mp_int_sub(&p1.val_bigint->ival, &tmp3, &tmp4);
		mp_int_div(&tmp4, &tmp2, &q->tmp_ival, NULL);
		SET_ACCUM();
		mp_int_clear(&tmp2);
		mp_int_clear(&tmp3);
		mp_int_clear(&tmp4);
	} else if (is_bigint(&p2) && is_smallint(&p1)) {
		mpz_t tmp1, tmp3, tmp4;
		mp_int_init_value(&tmp1, p1.val_int);
		mp_int_init(&tmp3);
		mp_int_init(&tmp4);
		mp_int_mod(&tmp1, &p2.val_bigint->ival, &tmp3);
		mp_int_sub(&tmp1, &tmp3, &tmp4);
		mp_int_div(&tmp4, &p2.val_bigint->ival, &q->tmp_ival, NULL);
		SET_ACCUM();
		mp_int_clear(&tmp1);
		mp_int_clear(&tmp3);
		mp_int_clear(&tmp4);
	} else if (is_smallint(&p1) && is_smallint(&p2)) {
		if (p2.val_int == 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

        q->accum.val_int = p1.val_int % p2.val_int;

		if ((q->accum.val_int != 0) && ((q->accum.val_int ^ p2.val_int) < 0))
			q->accum.val_int = q->accum.val_int + p2.val_int;

		q->accum.val_int = (p1.val_int - q->accum.val_int) / p2.val_int;
		q->accum.tag = TAG_INT;
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (!is_integer(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");
	} else if (!is_integer(&p2)) {
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

static bool bif_iso_rem_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_smallint(&p1) && is_smallint(&p2)) {
		if (p2.val_int == 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		q->accum.val_int = p1.val_int % p2.val_int;
		q->accum.tag = TAG_INT;
	} if (is_bigint(&p1) && is_bigint(&p2)) {
		mp_int_mod(&p1.val_bigint->ival, &p2.val_bigint->ival, &q->tmp_ival);
		SET_ACCUM();
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p2.val_int);
		mp_int_mod(&p1.val_bigint->ival, &tmp, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_smallint(&p1) && is_bigint(&p2)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p1.val_int);
		mp_int_mod(&tmp, &p2.val_bigint->ival, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (!is_integer(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");
	} else if (!is_integer(&p2)) {
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

static bool bif_iso_max_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

 	if (is_smallint(&p1) && is_smallint(&p2)) {
		if (p1.val_int >= p2.val_int)
			q->accum = p1;
		else
			q->accum = p2;

		q->accum.tag = TAG_INT;
	} else if (is_bigint(&p1)) {
		if (is_bigint(&p2)) {
			if (mp_int_compare(&p1.val_bigint->ival, &p2.val_bigint->ival) >= 0) {
				mp_int_copy(&p1.val_bigint->ival, &q->tmp_ival);
			} else {
				mp_int_copy(&p2.val_bigint->ival, &q->tmp_ival);
			}
		} else if (is_smallint(&p2)) {
			if (mp_int_compare_value(&p1.val_bigint->ival, p2.val_int) >= 0) {
				mp_int_copy(&p1.val_bigint->ival, &q->tmp_ival);
			} else {
				mp_int_set_value(&q->tmp_ival, p2.val_int);
			}
		} else if (is_float(&p2)) {
			pl_flt f1 = BIGINT_TO_DOUBLE(&p1.val_bigint->ival);
			if (isinf(f1)) return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow"); \

			if (f1 > p2.val_float)
				mp_int_copy(&p1.val_bigint->ival, &q->tmp_ival);
			else {
				q->accum = p2;
				return true;
			}
		} else
			return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");

		SET_ACCUM();
	} else if (is_bigint(&p2)) {
		if (is_smallint(&p1)) {
			if (mp_int_compare_value(&p2.val_bigint->ival, p1.val_int) >= 0) {
				mp_int_copy(&p2.val_bigint->ival, &q->tmp_ival);
			} else {
				mp_int_set_value(&q->tmp_ival, p1.val_int);
			}
		} else if (is_float(&p1)) {
			pl_flt f2 = BIGINT_TO_DOUBLE(&p2.val_bigint->ival);
			if (isinf(f2)) return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "float_overflow"); \

			if (f2 > p1.val_float)
				mp_int_copy(&p2.val_bigint->ival, &q->tmp_ival);
			else {
				q->accum = p1;
				return true;
			}
		} else
			return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");

		SET_ACCUM();
	} else if (is_smallint(&p1) && is_float(&p2)) {
		pl_flt f1 = (pl_flt)p1.val_int;

		if (f1 > p2.val_float)
			q->accum = p1;
		else
			q->accum = p2;
	} else if (is_smallint(&p2) && is_float(&p1)) {
		pl_flt f2 = (pl_flt)p2.val_int;

		if (f2 > p1.val_float)
			q->accum = p2;
		else
			q->accum = p1;
	} else if (is_float(&p1) && is_float(&p2)) {
		if (p1.val_float > p2.val_float)
			q->accum = p1;
		else
			q->accum = p2;
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (!is_smallint(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");
	} else if (!is_smallint(&p2)) {
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

static bool bif_iso_min_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_smallint(&p1) && is_smallint(&p2)) {
		if (p1.val_int <= p2.val_int)
			q->accum = p1;
		else
			q->accum = p2;

		q->accum.tag = TAG_INT;
	} else if (is_bigint(&p1)) {
		if (is_bigint(&p2)) {
			if (mp_int_compare(&p1.val_bigint->ival, &p2.val_bigint->ival) <= 0) {
				mp_int_copy(&p1.val_bigint->ival, &q->tmp_ival);
			} else {
				mp_int_copy(&p2.val_bigint->ival, &q->tmp_ival);
			}
		} else if (is_smallint(&p2)) {
			if (mp_int_compare_value(&p1.val_bigint->ival, p2.val_int) <= 0) {
				mp_int_copy(&p1.val_bigint->ival, &q->tmp_ival);
			} else {
				mp_int_set_value(&q->tmp_ival, p2.val_int);
			}
		} else if (is_float(&p2)) {
			pl_flt f1 = BIGINT_TO_DOUBLE(&p1.val_bigint->ival);
			if (isinf(f1)) return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow"); \

			if (f1 < p2.val_float)
				mp_int_copy(&p1.val_bigint->ival, &q->tmp_ival);
			else {
				q->accum = p2;
				return true;
			}
		} else
			return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");

		SET_ACCUM();
	} else if (is_bigint(&p2)) {
		if (is_smallint(&p1)) {
			if (mp_int_compare_value(&p2.val_bigint->ival, p1.val_int) <= 0) {
				mp_int_copy(&p2.val_bigint->ival, &q->tmp_ival);
			} else {
				mp_int_set_value(&q->tmp_ival, p1.val_int);
			}
		} else if (is_float(&p1)) {
			pl_flt f2 = BIGINT_TO_DOUBLE(&p2.val_bigint->ival);
			if (isinf(f2)) return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "float_overflow"); \

			if (f2 < p1.val_float)
				mp_int_copy(&p2.val_bigint->ival, &q->tmp_ival);
			else {
				q->accum = p1;
				return true;
			}
		} else
			return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");

		SET_ACCUM();
	} else if (is_smallint(&p1) && is_float(&p2)) {
		pl_flt f1 = (pl_flt)p1.val_int;

		if (f1 < p2.val_float)
			q->accum = p1;
		else
			q->accum = p2;
	} else if (is_smallint(&p2) && is_float(&p1)) {
		pl_flt f2 = (pl_flt)p2.val_int;

		if (f2 < p1.val_float)
			q->accum = p2;
		else
			q->accum = p1;
	} else if (is_float(&p1) && is_float(&p2)) {
		if (p1.val_float < p2.val_float)
			q->accum = p1;
		else
			q->accum = p2;
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (!is_smallint(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");
	} else if (!is_smallint(&p2)) {
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

static bool bif_iso_xor_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_bigint(&p1) && is_bigint(&p2)) {
		mp_int_xor(&p1.val_bigint->ival, &p2.val_bigint->ival, &q->tmp_ival);
		SET_ACCUM();
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p2.val_int);
		mp_int_xor(&p1.val_bigint->ival, &tmp, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_bigint(&p2) && is_smallint(&p1)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p1.val_int);
		mp_int_xor(&p2.val_bigint->ival, &tmp, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_smallint(&p1) && is_smallint(&p2)) {
		q->accum.val_int = p1.val_int ^ p2.val_int;
		q->accum.tag = TAG_INT;
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (!is_integer(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");
	} else if (!is_integer(&p2)) {
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

static bool bif_iso_or_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_bigint(&p1) && is_bigint(&p2)) {
		mp_int_or(&p1.val_bigint->ival, &p2.val_bigint->ival, &q->tmp_ival);
		SET_ACCUM();
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p2.val_int);
		mp_int_or(&p1.val_bigint->ival, &tmp, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_bigint(&p2) && is_smallint(&p1)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p1.val_int);
		mp_int_or(&p2.val_bigint->ival, &tmp, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_smallint(&p1) && is_smallint(&p2)) {
		q->accum.val_int = p1.val_int | p2.val_int;
		q->accum.tag = TAG_INT;
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (!is_integer(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");
	} else if (!is_integer(&p2)) {
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

static bool bif_iso_and_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_bigint(&p1) && is_bigint(&p2)) {
		mp_int_and(&p1.val_bigint->ival, &p2.val_bigint->ival, &q->tmp_ival);
		SET_ACCUM();
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p2.val_int);
		mp_int_and(&p1.val_bigint->ival, &tmp, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_bigint(&p2) && is_smallint(&p1)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p1.val_int);
		mp_int_and(&p2.val_bigint->ival, &tmp, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_smallint(&p1) && is_smallint(&p2)) {
		q->accum.val_int = p1.val_int & p2.val_int;
		q->accum.tag = TAG_INT;
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (!is_integer(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");
	} else if (!is_integer(&p2)) {
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

static bool bif_iso_shl_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_bigint(&p1) && is_smallint(&p2)) {
		mp_int_mul_pow2(&p1.val_bigint->ival, p2.val_int, &q->tmp_ival);
		SET_ACCUM();
	} else if (is_smallint(&p1) && is_smallint(&p2)) {
		if (p2.val_int < 64) {

			q->accum.val_int = p1.val_int << p2.val_int;

			if ((p1.val_int >= INT32_MAX) ||
				(p1.val_int <= INT32_MIN) ||
				(p2.val_int >= 32)) {
			} else {
				q->accum.tag = TAG_INT;
				return true;
			}
		}

		mpz_t tmp;
		mp_int_init_value(&tmp, p1.val_int);
		mp_int_mul_pow2(&tmp, p2.val_int, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (!is_integer(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");
	} else if (!is_integer(&p2)) {
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

static bool bif_iso_shr_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_bigint(&p1) && is_smallint(&p2)) {
		mp_int_div_pow2(&p1.val_bigint->ival, p2.val_int, &q->tmp_ival, NULL);
		SET_ACCUM();
	} if (is_smallint(&p1) && is_smallint(&p2)) {
		q->accum.val_int = p1.val_int >> p2.val_int;
		q->accum.tag = TAG_INT;
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (!is_integer(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");
	} else if (!is_integer(&p2)) {
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

static bool bif_iso_neg_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_smallint(&p1)) {
		q->accum.val_int = ~p1.val_int;
		q->accum.tag = TAG_INT;
	} else if (is_bigint(&p1)) {
		mp_int_add_value(&p1.val_bigint->ival, 1, &q->tmp_ival);
		mp_int_neg(&q->tmp_ival, &q->tmp_ival);
		SET_ACCUM();
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

static bool bif_iso_compare_eq_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	return compare(q, p1, p1_ctx, p2, p2_ctx) == 0;
}

static bool bif_iso_compare_ne_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	return compare(q, p1, p1_ctx, p2, p2_ctx) != 0;
}

static bool bif_iso_compare_lt_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	return compare(q, p1, p1_ctx, p2, p2_ctx) < 0;
}

static bool bif_iso_compare_le_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	return compare(q, p1, p1_ctx, p2, p2_ctx) <= 0;
}

static bool bif_iso_compare_gt_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	return compare(q, p1, p1_ctx, p2, p2_ctx) > 0;
}

static bool bif_iso_compare_ge_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	return compare(q, p1, p1_ctx, p2, p2_ctx) >= 0;
}

#define COMPARE_OP(op,p1,p2) \
	if (is_smallint(&p1) && is_smallint(&p2)) \
		return p1.val_int op p2.val_int; \
	else if (is_smallint(&p1) && is_bigint(&p2)) \
		return -mp_int_compare_value(&p2.val_bigint->ival, p1.val_int) op 0; \
	else if (is_smallint(&p1) && is_rational(&p2)) \
		return -mp_rat_compare_value(&p2.val_bigint->irat, p1.val_int, 1) op 0; \
	else if (is_smallint(&p1) && is_float(&p2)) \
		return p1.val_int op p2.val_float; \
	else if (is_bigint(&p1) && is_smallint(&p2)) \
		return mp_int_compare_value(&p1.val_bigint->ival, p2.val_int) op 0; \
	else if (is_bigint(&p1) && is_bigint(&p2)) \
		return mp_int_compare(&p1.val_bigint->ival, &p2.val_bigint->ival) op 0; \
	else if (is_bigint(&p1) && is_rational(&p2)) { \
		mpq_t tmp; \
		mp_int_init_copy(&tmp.num, &p1.val_bigint->ival); \
		mp_int_init_value(&tmp.den, 1); \
		int ok = mp_rat_compare(&tmp, &p2.val_bigint->irat) op 0; \
		mp_rat_clear(&tmp); \
		return ok; \
	} else if (is_bigint(&p1) && is_float(&p2)) { \
		pl_flt f1 = BIGINT_TO_DOUBLE(&p1.val_bigint->ival); \
		return f1 op p2.val_float; \
	} else if (is_rational(&p1) && is_rational(&p2)) { \
		return mp_rat_compare(&p1.val_bigint->irat, &p2.val_bigint->irat) op 0; \
	} else if (is_rational(&p1) && is_bigint(&p2)) { \
		mpq_t tmp; \
		mp_int_init_copy(&tmp.num, &p2.val_bigint->ival); \
		mp_int_init_value(&tmp.den, 1); \
		bool ok = mp_rat_compare(&p1.val_bigint->irat, &tmp) op 0; \
		mp_rat_clear(&tmp); \
		return ok; \
	} else if (is_rational(&p1) && is_smallint(&p2)) \
		return mp_rat_compare_value(&p1.val_bigint->irat, p2.val_int, 1) op 0; \
	else if (is_rational(&p1) && is_float(&p2)) { \
		pl_flt f1 = RATIONAL_TO_DOUBLE(&p1.val_bigint->irat); \
		if (isinf(f1)) return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow"); \
		return f1 op p2.val_float; \
	} else if (is_float(&p1) && is_smallint(&p2)) \
		return p1.val_float op p2.val_int; \
	else if (is_float(&p1) && is_float(&p2)) \
		return p1.val_float op p2.val_float; \
	else if (is_float(&p1) && is_bigint(&p2)) { \
		pl_flt f2 = BIGINT_TO_DOUBLE(&p2.val_bigint->ival); \
		if (isinf(f2)) return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "float_overflow"); \
		return p1.val_float op f2; \
	} else if (is_float(&p1) && is_rational(&p2)) { \
		pl_flt f2 = RATIONAL_TO_DOUBLE(&p2.val_bigint->irat); \
		if (isinf(f2)) return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "float_overflow"); \
		return p1.val_float op f2; \
	}

static bool bif_iso_numeric_eq_2(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	q->max_eval_depth = 0;
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	COMPARE_OP(==,p1,p2);
	return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
}

static bool bif_iso_numeric_ne_2(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	q->max_eval_depth = 0;
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	COMPARE_OP(!=,p1,p2);
	return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
}

static bool bif_iso_numeric_ge_2(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	q->max_eval_depth = 0;
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	COMPARE_OP(>=,p1,p2);
	return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
}

static bool bif_iso_numeric_gt_2(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	q->max_eval_depth = 0;
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	COMPARE_OP(>,p1,p2);
	return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
}

static bool bif_iso_numeric_le_2(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	q->max_eval_depth = 0;
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	COMPARE_OP(<=,p1,p2);
	return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
}

static bool bif_iso_numeric_lt_2(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	q->max_eval_depth = 0;
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	COMPARE_OP(<,p1,p2);
	return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
}

static bool bif_log_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (is_var(&p2)) {
		return throw_error(q, &p2, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (! is_integer(&p1) && ! is_float(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	} else if (! is_integer(&p2) && ! is_float(&p2)){
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "evaluable");
	}

	if (is_smallint(&p1)) {
		if (p1.val_int == 0) {
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");
		} else if (p1.val_int < 0) {
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");
		}
	} else if (is_float(&p1)) {
		if (p1.val_float == 0.0) {
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");
		} else if (p1.val_float < 0.0) {
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");
		}
	}

	if (is_smallint(&p2)) {
		if (p2.val_int == 0) {
			return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "zero_divisor");
		} else if (p2.val_int < 0) {
			return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "undefined");
		}
	} else if (is_float(&p2)) {
		if (p2.val_float == 0.0) {
			return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "zero_divisor");
		} else if (p2.val_float < 0.0) {
			return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "undefined");
		}
	}

	if (is_smallint(&p1) && is_smallint(&p2)) {
		q->accum.val_float = log(p2.val_int) / log(p1.val_int);

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_smallint(&p1) && is_float(&p2)) {
		q->accum.val_float = log(p2.val_float) / log(p1.val_int);

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1) && is_smallint(&p2)) {
		q->accum.val_float = log(p2.val_int) / log(p1.val_float);

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	} else if (is_float(&p1) && is_float(&p2)) {
		q->accum.val_float = log(p2.val_float) / log(p1.val_float);

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.tag = TAG_FLOAT;
	}

	return true;
}

static bool bif_log10_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else 	if (is_smallint(&p1)) {
		if (p1.val_int == 0) {
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");
		} else if (p1.val_int < 0) {
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");
		} else {
			q->accum.val_float = log10(p1.val_int);
			q->accum.tag = TAG_FLOAT;
		}
	} else if (is_float(&p1)) {
		if (p1.val_float == 0.0) {
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");
		} else if (p1.val_float < 0.0) {
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");
		} else {
			q->accum.val_float = log10(p1.val_float);
			q->accum.tag = TAG_FLOAT;
		}
	} else {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	return true;
}

static pl_flt rnd(query *q)
{
	return (pl_flt)tpl_rand_r(&q->rand_seed) / (pl_flt)RAND_MAX;
}

static bool bif_set_seed_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	q->rand_seed = (unsigned)p1->val_uint;
	srand(q->rand_seed);
	return true;
}

static bool bif_get_seed_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	cell tmp;
	make_uint(&tmp, q->rand_seed);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_random_between_3(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);
	GET_NEXT_ARG(p3,integer_or_var);

	if (!is_smallint(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "integer");

	if (!is_smallint(p2))
		return throw_error(q, p2, p2_ctx, "type_error", "integer");

	cell tmp;
	pl_int r = rnd(q) * ((int64_t)RAND_MAX+1);
	make_int(&tmp, get_smallint(p1) + (r % get_smallint(p2)));
	return unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_random_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	cell tmp;
	make_float(&tmp, rnd(q));
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_random_integer_0(query *q)
{
	START_FUNCTION(q);
	q->accum.tag = TAG_INT;
	q->accum.val_int = tpl_rand_r(&q->rand_seed);
	return true;
}

static bool bif_random_float_0(query *q)
{
	START_FUNCTION(q);
	q->accum.tag = TAG_FLOAT;
	q->accum.val_float = rnd(q);
	return true;
}

static bool bif_rand_0(query *q)
{
	START_FUNCTION(q);
	q->accum.tag = TAG_INT;
	q->accum.val_int = rnd(q) * ((int64_t)RAND_MAX+1);
	return true;
}

static bool bif_rand_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	cell tmp;
	make_int(&tmp, rnd(q) * ((int64_t)RAND_MAX+1));
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static pl_int gcd(pl_int num, pl_int remainder)
{
	if (remainder == 0)
		return num;

	return gcd(remainder, num % remainder);
}

static bool bif_gcd_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_bigint(&p1) && is_bigint(&p2)) {
		mp_int_gcd(&p1.val_bigint->ival, &p2.val_bigint->ival, &q->tmp_ival);
		SET_ACCUM();
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p2.val_int);
		mp_int_gcd(&p1.val_bigint->ival, &tmp, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_bigint(&p2) && is_smallint(&p1)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p1.val_int);
		mp_int_gcd(&tmp, &p2.val_bigint->ival, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_smallint(&p1) && is_smallint(&p2)) {
		q->accum.val_int = gcd(p1.val_int, p2.val_int);
		q->accum.tag = TAG_INT;
	} else if (is_var(&p1) || is_var(&p2)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (!is_integer(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "integer");
	} else if (!is_integer(&p2)) {
		return throw_error(q, &p2, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

static bool bif_divmod_4(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);
	GET_NEXT_ARG(p3,integer_or_var);
	GET_NEXT_ARG(p4,integer_or_var);

	if (is_bigint(p1) && is_bigint(p2)) {
		mp_int_div(&p1->val_bigint->ival, &p2->val_bigint->ival, &q->tmp_ival, NULL);
		SET_ACCUM2();

		if (!unify(q, p3, p3_ctx, &q->accum, q->st.cur_ctx))
			return false;

		big_mod(&p1->val_bigint->ival, &p2->val_bigint->ival, &q->tmp_ival);
		SET_ACCUM2();
		return unify(q, p4, p4_ctx, &q->accum, q->st.cur_ctx);
	} else if (is_bigint(p1) && is_smallint(p2)) {
		mp_int_div_value(&p1->val_bigint->ival, p2->val_int, &q->tmp_ival, NULL);
		SET_ACCUM2();

        if (!unify(q, p3, p3_ctx, &q->accum, q->st.cur_ctx)) {
			clr_accum(&q->accum);
			return false;
		}

		mpz_t tmp;
		mp_int_init_value(&tmp, p2->val_int);
		big_mod(&p1->val_bigint->ival, &tmp, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM2();
        return unify(q, p4, p4_ctx, &q->accum, q->st.cur_ctx);
	} else if (is_bigint(p2) && is_smallint(p1)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p1->val_int);
		mp_int_div(&tmp, &p2->val_bigint->ival, &q->tmp_ival, NULL);
		SET_ACCUM2();

		if (!unify(q, p3, p3_ctx, &q->accum, q->st.cur_ctx)) {
			mp_int_clear(&tmp);
			return false;
		}

		big_mod(&tmp, &p2->val_bigint->ival, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM2();
		return unify(q, p4, p4_ctx, &q->accum, q->st.cur_ctx);
	} else if (is_smallint(p1) && is_smallint(p2)) {
		if (p2->val_int == 0)
			return throw_error(q, p2, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		cell tmp;
        q->accum.val_int = p1->val_int / p2->val_int;
        make_int(&tmp, q->accum.val_int);

        if (!unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx))
			return false;

        q->accum.val_int = mod(p1->val_int, p2->val_int);
        make_int(&tmp, q->accum.val_int);
        return unify(q, p4, p4_ctx, &tmp, q->st.cur_ctx);
	} else {
		return throw_error(q, p1, q->st.cur_ctx, "type_error", "integer");
	}

	return true;
}

builtins g_evaluable_bifs[] =
{
	// Predicate...

	{"=:=", 2, bif_iso_numeric_eq_2, "+number,+number", true, false, BLAH},
	{"=\\=", 2, bif_iso_numeric_ne_2, "+number,+number", true, false, BLAH},
	{">", 2, bif_iso_numeric_gt_2, "+number,+number", true, false, BLAH},
	{">=", 2, bif_iso_numeric_ge_2, "+number,+number", true, false, BLAH},
	{"=<", 2, bif_iso_numeric_le_2, "+number,+number", true, false, BLAH},
	{"<", 2, bif_iso_numeric_lt_2, "+number,+number", true, false, BLAH},

	{"==", 2, bif_iso_compare_eq_2, "+term,+term", true, false, BLAH},
	{"\\==", 2, bif_iso_compare_ne_2, "+term,+term", true, false, BLAH},
	{"@>", 2, bif_iso_compare_gt_2, "+term,+term", true, false, BLAH},
	{"@>=", 2, bif_iso_compare_ge_2, "+term,+term", true, false, BLAH},
	{"@=<", 2, bif_iso_compare_le_2, "+term,+term", true, false, BLAH},
	{"@<", 2, bif_iso_compare_lt_2, "+term,+term", true, false, BLAH},

	{"is", 2, bif_iso_is_2, "?number,+number", true, false, BLAH},
	{"float", 1, bif_iso_float_1, "+number", true, false, BLAH},
	{"integer", 1, bif_iso_integer_1, "+number", true, false, BLAH},

	// ...non-ISO...

	{"setrand", 1, bif_set_seed_1, "+integer", false, false, BLAH},
	{"srandom", 1, bif_set_seed_1, "+integer", false, false, BLAH},
	{"set_seed", 1, bif_set_seed_1, "+integer", false, false, BLAH},
	{"get_seed", 1, bif_get_seed_1, "-integer", false, false, BLAH},
	{"rand", 1, bif_rand_1, "?integer", false, false, BLAH},
	{"random", 1, bif_random_1, "?integer", false, false, BLAH},
	{"random_between", 3, bif_random_between_3, "?integer,?integer,-integer", false, false, BLAH},
	{"rational", 1, bif_rational_1, "+term", false, false, BLAH},

	// Functions...

	{"+", 1, bif_iso_positive_1, "+number,-number", true, true, BLAH},
	{"-", 1, bif_iso_negative_1, "+number,-number", true, true, BLAH},
	{"abs", 1, bif_iso_abs_1, "+number,-number", true, true, BLAH},
	{"sign", 1, bif_iso_sign_1, "+number,-number", true, true, BLAH},
	{"epsilon", 0, bif_iso_epsilon_0, "-float", true, true, BLAH},
	{"pi", 0, bif_iso_pi_0, "-float", true, true, BLAH},
	{"e", 0, bif_iso_e_0, "-float", true, true, BLAH},
	{"+", 2, bif_iso_add_2, "+number,+number,-number", true, true, BLAH},
	{"-", 2, bif_iso_sub_2, "+number,+number,-number", true, true, BLAH},
	{"*", 2, bif_iso_mul_2, "+number,+number,-number", true, true, BLAH},
	{"/", 2, bif_iso_divide_2, "+number,+number,-float", true, true, BLAH},
	{"//", 2, bif_iso_divint_2, "+integer,+integer,-integer", true, true, BLAH},
	{"div", 2, bif_iso_div_2, "+integer,+integer,-integer", true, true, BLAH},
	{"mod", 2, bif_iso_mod_2, "+integer,+integer,-integer", true, true, BLAH},
	{"rem", 2, bif_iso_rem_2, "+integer,+integer,-integer", true, true, BLAH},
	{"max", 2, bif_iso_max_2, "+number,+number,-number", true, true, BLAH},
	{"min", 2, bif_iso_min_2, "+number,+number,-number", true, true, BLAH},
	{"xor", 2, bif_iso_xor_2, "+integer,+integer,-integer", true, true, BLAH},
	{"/\\", 2, bif_iso_and_2, "+integer,+integer,-integer", true, true, BLAH},
	{"\\/", 2, bif_iso_or_2, "+integer,+integer,-integer", true, true, BLAH},
	{"<<", 2, bif_iso_shl_2, "+integer,-integer", true, true, BLAH},
	{">>", 2, bif_iso_shr_2, "+integer,-integer", true, true, BLAH},
	{"\\", 1, bif_iso_neg_1, "+integer,-integer", true, true, BLAH},
	{"**", 2, bif_iso_pow_2, "+number,+number,-float", true, true, BLAH},
	{"^", 2, bif_iso_powi_2, "+number,+number,-integer", true, true, BLAH},
	{"exp", 1, bif_iso_exp_1, "+number,-float", true, true, BLAH},
	{"sqrt", 1, bif_iso_sqrt_1, "+number,-float", true, true, BLAH},
	{"log", 1, bif_iso_log_1, "+number,-float", true, true, BLAH},

	{"sin", 1, bif_iso_sin_1, "+number,-float", true, true, BLAH},
	{"cos", 1, bif_iso_cos_1, "+number,-float", true, true, BLAH},
	{"tan", 1, bif_iso_tan_1, "+number,-float", true, true, BLAH},
	{"asin", 1, bif_iso_asin_1, "+number,-float", true, true, BLAH},
	{"acos", 1, bif_iso_acos_1, "+number,-float", true, true, BLAH},
	{"atan", 1, bif_iso_atan_1, "+number,-float", true, true, BLAH},

	{"atan2", 2, bif_iso_atan2_2, "+number,+number,-float", true, true, BLAH},
	{"copysign", 2, bif_iso_copysign_2, "+number,-number", false, true, BLAH},
	{"truncate", 1, bif_iso_truncate_1, "+float,-integer", true, true, BLAH},
	{"round", 1, bif_iso_round_1, "+float,-integer", true, true, BLAH},
	{"ceiling", 1, bif_iso_ceiling_1, "+float,-integer", true, true, BLAH},
	{"floor", 1, bif_iso_floor_1, "+float,-integer", true, true, BLAH},
	{"float_integer_part", 1, bif_iso_float_integer_part_1, "+float,-integer", true, true, BLAH},
	{"float_fractional_part", 1, bif_iso_float_fractional_part_1, "+float,-float", true, true, BLAH},

	// ...non-ISO...

	{"sinh", 1, bif_sinh_1, "+number,-float", false, true, BLAH},
	{"cosh", 1, bif_cosh_1, "+number,-float", false, true, BLAH},
	{"tanh", 1, bif_tanh_1, "+number,-float", false, true, BLAH},
	{"asinh", 1, bif_asinh_1, "+number,-float", false, true, BLAH},
	{"acosh", 1, bif_acosh_1, "+number,-float", false, true, BLAH},
	{"atanh", 1, bif_atanh_1, "+number,-float", false, true, BLAH},

	{"erf", 1, bif_erf_1, "+number,-float", false, true, BLAH},
	{"erfc", 1, bif_erfc_1, "+number,-float", false, true, BLAH},
	{"numerator", 1, bif_numerator_1, "+rational,-integer", false, true, BLAH},
	{"denominator", 1, bif_denominator_1, "+rational,-integer", false, true, BLAH},
	{"rdiv", 2, bif_rdiv_2, "+integer,+integer,-rational", false, true, BLAH},

	{"divmod", 4, bif_divmod_4, "+integer,+integer,?integer,?integer", false, false, BLAH},
	{"log", 2, bif_log_2, "+number,+number,-float", false, true, BLAH},
	{"log10", 1, bif_log10_1, "+number,-float", false, true, BLAH},
	{"random_integer", 0, bif_random_integer_0, "-integer", false, true, BLAH},
	{"random_float", 0, bif_random_float_0, "-float", false, true, BLAH},
	{"rand", 0, bif_rand_0, "-integer", false, true, BLAH},
	{"gcd", 2, bif_gcd_2, "+integer,+integer,-integer", false, true, BLAH},
	{"popcount", 1, bif_popcount_1, "+integer,-integer", false, true, BLAH},
	{"lsb", 1, bif_lsb_1, "+integer,-integer", false, true, BLAH},
	{"msb", 1, bif_msb_1, "+integer,-integer", false, true, BLAH},

	{0}
};
