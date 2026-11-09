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

void clr_accum(cell *p)
{
	if (is_bigint(p) && p->val_bigint && !p->val_bigint->refcnt) {
		mp_int_clear(&p->val_bigint->ival);
		TPL_free(p->val_bigint);
	} else if (is_rational(p) && (p->flags & FLAG_INT_BIG) && p->val_bigint && !p->val_bigint->refcnt) {	// flags as for a bigint: eval() clears them on q->accum once copied, and a rational left there was freed twice
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

// A rational with a float is taken as a float, where the float's bits were read as an integer

#define RAT_FLOAT_OP(op,x,y) { \
	const pl_flt x_ = (x), y_ = (y); \
	q->accum.tag = TAG_FLOAT; \
	q->accum.flags = 0; \
	q->accum.val_float = x_ op y_; \
	if (isinf(x_) || isinf(y_) || isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow"); \
}

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
			mp_rat_##op2(&tmp, &p2.val_bigint->irat, &q->tmp_irat); \
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
		} else if (is_float(&p2)) { \
			RAT_FLOAT_OP(op, RATIONAL_TO_DOUBLE(&p1.val_bigint->irat), p2.val_float); \
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
			mp_rat_##op2(&tmp, &p2.val_bigint->irat, &q->tmp_irat); \
			if (errno == ENOMEM)										\
				return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory"); \
			mp_rat_clear(&tmp); \
			SET_RAT_ACCUM2(); \
		} else if (is_float(&p1)) { \
			RAT_FLOAT_OP(op, p1.val_float, RATIONAL_TO_DOUBLE(&p2.val_bigint->irat)); \
		} else { \
			mpq_t tmp; \
			mp_rat_init(&tmp); \
			mp_rat_set_value(&tmp, p1.val_int, 1); \
			mp_rat_##op2(&tmp, &p2.val_bigint->irat, &q->tmp_irat); \
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

// The nearest double, rounded once with ties to even, as SWI's mpz_fdiv: a 54-bit quotient whose last bit is
// a guard and whose remainder is a sticky bit. Converting each half and dividing rounded twice, one step off.

static pl_flt RATIONAL_TO_DOUBLE(mpq_t *v)
{
	const int sign = mp_int_compare_zero(&v->num);
	const long bn = mp_int_count_bits(&v->num), bd = mp_int_count_bits(&v->den);

	if (!sign)
		return 0.0;

	if ((bn <= 53) && (bd <= 53))		// both exact, so one division rounds once
		return BIGINT_TO_DOUBLE(&v->num) / BIGINT_TO_DOUBLE(&v->den);

	if (bn - bd > 1025)					// at least 2^1024
		return sign < 0 ? -INFINITY : INFINITY;

	if (bn - bd < -1075)				// under 2^-1076, below half the smallest subnormal
		return sign < 0 ? -0.0 : 0.0;

	mpz_t a, b, qt, r;
	mp_int_init(&a); mp_int_init(&b); mp_int_init(&qt); mp_int_init(&r);
	mp_int_abs(&v->num, &a);
	mp_int_copy(&v->den, &b);
	long k = 54 - (bn - bd);			// a * 2^k / b lies in [2^53, 2^55)

	if (k > 0)
		mp_int_mul_pow2(&a, k, &a);
	else if (k < 0)
		mp_int_mul_pow2(&b, -k, &b);

	mp_int_div(&a, &b, &qt, &r);
	bool sticky = mp_int_compare_zero(&r) != 0;
	mp_usmall q54 = 0;
	mp_int_to_uint(&qt, &q54);
	mp_int_clear(&a); mp_int_clear(&b); mp_int_clear(&qt); mp_int_clear(&r);

	if (q54 >> 54) {
		sticky |= q54 & 1;
		q54 >>= 1;
		k--;
	}

	const long e = 53 - k;				// the value lies in [2^e, 2^(e+1))

	if (e < -1022) {					// subnormal: fewer bits, so the guard sits lower
		const long extra = -1022 - e;

		if (extra > 54) {
			sticky |= q54 != 0;
			q54 = 0;
		} else {
			sticky |= (q54 & ((1ULL << extra) - 1)) != 0;
			q54 >>= extra;
		}

		k -= extra;
	}

	const bool guard = q54 & 1;
	q54 >>= 1;

	if (guard && (sticky || (q54 & 1)))
		q54++;

	const pl_flt d = ldexp((pl_flt)q54, (int)(1 - k));	// exact, or inf past the top
	return sign < 0 ? -d : d;
}

// A number as a double, false where a bigint or rational is too big for one

bool num_to_double(cell *c, pl_flt *d)
{
	if (is_float(c))
		*d = c->val_float;
	else if (is_smallint(c))
		*d = (pl_flt)c->val_int;
	else if (is_rational(c))
		*d = RATIONAL_TO_DOUBLE(&c->val_bigint->irat);
	else
		*d = BIGINT_TO_DOUBLE(&c->val_bigint->ival);

	return is_float(c) || !isinf(*d);
}

// A bigint or rational past a double's range: a float function raises float_overflow, as float/1, SWI and Scryer do, where inf went in

static bool too_big_for_float(cell *c)
{
	pl_flt d;
	return (is_bigint(c) || is_rational(c)) && !num_to_double(c, &d);
}

// q->tmp_ival into accum, as a smallint where it fits, where a small value left as a bigint inside an expression was taken as huge

static bool accum_int(query *q, cell *p1)
{
	mp_small v;

	if (mp_int_to_int(&q->tmp_ival, &v) == MP_OK) {
		q->accum.tag = TAG_INT;
		q->accum.flags = 0;
		q->accum.val_int = v;
		return true;
	}

	SET_ACCUM2();
	return true;
}

// The exact value of a finite double, reduced: an odd 53-bit mantissa over or times a power of two

static void float_to_rat(pl_flt f, mpq_t *r)
{
	mp_int_set_value(&r->den, 1);

	if (f == 0.0) {
		mp_int_zero(&r->num);
		return;
	}

	int e;
	mp_small mant = (mp_small)ldexp(frexp(fabs(f), &e), 53);
	e -= 53;

	while (!(mant & 1)) {
		mant >>= 1;
		e++;
	}

	mp_int_set_value(&r->num, f < 0 ? -mant : mant);

	if (e > 0)
		mp_int_mul_pow2(&r->num, e, &r->num);
	else if (e < 0)
		mp_int_mul_pow2(&r->den, -e, &r->den);
}

// Halfway from x to the double next to it in that direction, mirrored where that neighbour is infinite

static void float_half_ulp(pl_flt f, pl_flt toward, mpq_t *x, mpq_t *out)
{
	pl_flt nb = nextafter(f, toward);

	if (isinf(nb)) {
		float_to_rat(nextafter(f, -toward), out);
		mp_rat_sub(x, out, out);
		mp_rat_add(x, out, out);
	} else
		float_to_rat(nb, out);

	mp_rat_add(x, out, out);
	mp_int_mul_pow2(&out->den, 1, &out->den);
	mp_rat_reduce(out);
}

// q->tmp_irat into accum, as an integer where its denominator is 1

static bool accum_rat(query *q, cell *pp)
{
	cell p1 = *pp;
	mp_rat_reduce(&q->tmp_irat);

	if (!mp_int_compare_value(&q->tmp_irat.den, 1)) {
		mp_int_copy(&q->tmp_irat.num, &q->tmp_ival);
		return accum_int(q, pp);
	}

	SET_RAT_ACCUM2();
	return true;
}

// The first continued-fraction convergent of the float's exact value that rounds back to the float, as SWI's
// mpq_set_double does: it stops on the convergent's correctly rounded double, here an exact test against the
// interval of reals that round to f, ties going to the even mantissa

static void float_rationalize(pl_flt f, mpq_t *out)
{
	mpq_t x, lo, hi, c;
	mpz_t m, n, p, qq, v, w, d, u, t;
	mp_rat_init(&x); mp_rat_init(&lo); mp_rat_init(&hi); mp_rat_init(&c);
	mp_int_init(&m); mp_int_init(&n); mp_int_init(&p); mp_int_init(&qq);
	mp_int_init(&v); mp_int_init(&w); mp_int_init(&d); mp_int_init(&u); mp_int_init(&t);

	float_to_rat(f, &x);
	float_half_ulp(f, -INFINITY, &x, &lo);
	float_half_ulp(f, INFINITY, &x, &hi);
	uint64_t bits;
	memcpy(&bits, &f, sizeof(bits));
	const bool even = !(bits & 1);
	mp_int_set_value(&m, 1);
	mp_int_set_value(&n, 0);
	mp_int_set_value(&p, 0);
	mp_int_set_value(&qq, 1);
	mp_int_copy(&x.num, &v);
	mp_int_copy(&x.den, &w);

	for (;;) {
		mp_int_div(&v, &w, &d, &u);

		if (mp_int_compare_zero(&u) < 0) {	// floor, whichever way imath rounds
			mp_int_sub_value(&d, 1, &d);
			mp_int_add(&u, &w, &u);
		}

		mp_int_mul(&d, &m, &t);
		mp_int_add(&p, &t, &p);
		mp_int_mul(&d, &n, &t);
		mp_int_add(&qq, &t, &qq);

		if (!mp_int_compare_zero(&u))
			break;

		mp_int_copy(&p, &c.num);
		mp_int_copy(&qq, &c.den);
		int below = mp_rat_compare(&c, &lo), above = mp_rat_compare(&c, &hi);

		if (((below > 0) && (above < 0)) || (even && (!below || !above)))
			break;

		mp_int_copy(&w, &v);
		mp_int_copy(&u, &w);
		mp_int_swap(&m, &p);
		mp_int_swap(&n, &qq);
	}

	mp_int_copy(&p, &out->num);
	mp_int_copy(&qq, &out->den);
	mp_rat_clear(&x); mp_rat_clear(&lo); mp_rat_clear(&hi); mp_rat_clear(&c);
	mp_int_clear(&m); mp_int_clear(&n); mp_int_clear(&p); mp_int_clear(&qq);
	mp_int_clear(&v); mp_int_clear(&w); mp_int_clear(&d); mp_int_clear(&u); mp_int_clear(&t);
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

// Arithmetic recurses through here: stop at the depth limit, or at half the stack in the -O0 builds (all DEBUG), whose frames reach that far sooner
static bool eval_too_deep(query *q)
{
#ifdef DEBUG
	if ((uintptr_t)__builtin_frame_address(0) < q->eval_floor)
		return true;
#endif
	return q->eval_depth > q->depth_limit;
}

static void start_eval(query *q)
{
	q->eval_depth = 0;
#ifdef DEBUG
	uintptr_t base = (uintptr_t)__builtin_frame_address(0), budget = (uintptr_t)q->depth_limit * 512;
	q->eval_floor = base > budget ? base - budget : 0;
#endif
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
	if (!c->bif_ptr->evaluable && (c->val_off != g_float_s) && (c->bif_ptr->fn != bif_rational_1))
		return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");
	else if (eval_too_deep(q))
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "stack");
	else {
		unsigned depth = q->eval_depth++;	// a depth, not a count, so wide expressions are not refused
		c->bif_ptr->fn(q);
		q->eval_depth = depth;
	}

	q->eval = save_calc;

	if (!q->did_throw) {
		q->st.instr = save;
		q->st.cur_ctx = save_ctx;
	}

	return true;
}

bool call_userfun(query *q, cell *c, pl_ctx c_ctx)
{
	// An expression built or read at run time arrives untagged, so bind it to the evaluable of
	// its name and arity as rebind_builtin() does for a goal, rather than refusing it (#1183).

	bool found = false, evaluable = false;
	builtins *b = get_builtin_term(q->st.m, c, &found, &evaluable);

	if (found && evaluable && b) {
		c->bif_ptr = b;
		c->flags |= FLAG_INTERNED_EVALUABLE;
		return call_builtin(q, c, c_ctx);
	}

	return throw_error(q, c, c_ctx, "type_error", "evaluable");
}

static bool bif_iso_is_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2_tmp,any);
	start_eval(q);
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
			q->accum.tag = TAG_FLOAT;			// first: accum still said rational, and the error read the double as its pointer
			q->accum.flags = 0;
			q->accum.val_float = RATIONAL_TO_DOUBLE(&p1.val_bigint->irat);
			if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
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

	if (is_smallint(&p1) && (p1.val_int == PL_INT_MIN)) {
		mp_int_set_value(&q->tmp_ival, p1.val_int);
		mp_int_abs(&q->tmp_ival, &q->tmp_ival);
		SET_ACCUM();
	} else if (is_smallint(&p1))
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
	p1.tag = TAG_INT;				// accum takes the number, where sharing it let p1's cleanup free a bigint under accum
	p1.flags = 0;
	return true;
}

static bool bif_iso_negative_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	q->accum.tag = p1.tag;

	if (is_smallint(&p1) && (p1.val_int == PL_INT_MIN)) {
		mp_int_set_value(&q->tmp_ival, p1.val_int);
		mp_int_neg(&q->tmp_ival, &q->tmp_ival);
		SET_ACCUM();
	} else if (is_smallint(&p1))
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
		p1.tag = TAG_INT;			// as for +/1
		p1.flags = 0;
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

// A type test as a goal; in arithmetic a float's exact value as a rational, as SWI and ECLiPSe have it

bool bif_rational_1(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);

	if (!q->eval)
		return is_rational(p1_tmp) || is_integer(p1_tmp);

	errno = 0;
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_integer(&p1) || is_rational(&p1)) {
		q->accum = p1;						// accum takes the number, as in +/1
		p1.tag = TAG_INT;
		p1.flags = 0;
		return true;
	}

	if (!is_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");

	float_to_rat(p1.val_float, &q->tmp_irat);
	return accum_rat(q, &p1);
}

// The simplest-looking rational that rounds back to the float, as SWI has it: rationalize(0.1) is 1r10

static bool bif_rationalize_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);

	if (is_integer(&p1) || is_rational(&p1)) {
		q->accum = p1;						// accum takes the number, as in +/1
		p1.tag = TAG_INT;
		p1.flags = 0;
		return true;
	}

	if (!is_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");

	float_rationalize(p1.val_float, &q->tmp_irat);
	return accum_rat(q, &p1);
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
		if (mp_int_copy(&p1.val_bigint->irat.num, &q->tmp_irat.num) == MP_MEMORY)	// was 1, halving 2r3 rdiv 2^70
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	} else if (is_rational(&p1) && is_smallint(&p2)) {
		if (mp_int_mul_value(&p1.val_bigint->irat.den, p2.val_int, &q->tmp_irat.den) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		if (mp_int_copy(&p1.val_bigint->irat.num, &q->tmp_irat.num) == MP_MEMORY)	// was left from the last rational worked out
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	} else if (is_bigint(&p1) && is_rational(&p2)) {
		if (mp_int_mul(&p2.val_bigint->irat.den, &p1.val_bigint->ival, &q->tmp_irat.num) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		if (mp_int_copy(&p2.val_bigint->irat.num, &q->tmp_irat.den) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	} else if (is_bigint(&p1) && is_bigint(&p2)) {
		if (mp_int_copy(&p1.val_bigint->ival, &q->tmp_irat.num) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		if (mp_int_copy(&p2.val_bigint->ival, &q->tmp_irat.den) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		if (mp_int_copy(&p1.val_bigint->ival, &q->tmp_irat.num) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		if (mp_int_set_value(&q->tmp_irat.den, p2.val_int) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
	} else if (is_smallint(&p1) && is_bigint(&p2)) {
		if (mp_int_set_value(&q->tmp_irat.num, p1.val_int) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		if (mp_int_copy(&p2.val_bigint->ival, &q->tmp_irat.den) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		mp_rat_reduce(&q->tmp_irat);
	} else if (is_smallint(&p1) && is_rational(&p2)) {
		if (mp_int_mul_value(&p2.val_bigint->irat.den, p1.val_int, &q->tmp_irat.num) == MP_MEMORY)
			return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");
		if (mp_int_copy(&p2.val_bigint->irat.num, &q->tmp_irat.den) == MP_MEMORY)
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
	pl_flt d;

	if (is_var(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");

	if (!is_number(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");

	if (!num_to_double(&p1, &d))		// any number, where a rational was refused and a negative bigint undefined
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

	q->accum.tag = TAG_FLOAT;
	q->accum.flags = 0;
	q->accum.val_float = exp(d);

	if (isinf(q->accum.val_float))
		return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

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

		q->accum.tag = TAG_FLOAT;
		q->accum.flags = 0;
		q->accum.val_float = sqrt(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
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
	} else if (is_rational(&p1)) {
		if (mp_rat_compare_zero(&p1.val_bigint->irat) <= 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.tag = TAG_FLOAT;
		q->accum.flags = 0;
		q->accum.val_float = log(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		if (isinf(q->accum.val_float)) return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
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

// A rational rounded exactly, as SWI and Scryer do: 0 floor, 1 ceiling, 2 truncate, 3 round half up as for a float

static bool rat_round(query *q, cell *p1, int how)
{
	mpz_t a, b, r;
	mp_int_init_copy(&a, &p1->val_bigint->irat.num);
	mp_int_init_copy(&b, &p1->val_bigint->irat.den);
	mp_int_init(&r);

	if (mp_int_compare_zero(&b) < 0) {
		mp_int_neg(&a, &a);
		mp_int_neg(&b, &b);
	}

	if (how == 3) {						// floor((2a+b) / 2b)
		mp_int_mul_value(&a, 2, &a);
		mp_int_add(&a, &b, &a);
		mp_int_mul_value(&b, 2, &b);
	}

	mp_int_div(&a, &b, &q->tmp_ival, &r);
	const int rs = mp_int_compare_zero(&r);

	if (rs < 0)							// a truncating division: step down to the floor
		mp_int_sub_value(&q->tmp_ival, 1, &q->tmp_ival);

	if (rs && ((how == 1) || ((how == 2) && (mp_int_compare_zero(&a) < 0))))
		mp_int_add_value(&q->tmp_ival, 1, &q->tmp_ival);

	mp_int_clear(&a);
	mp_int_clear(&b);
	mp_int_clear(&r);
	return accum_int(q, p1);
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
	} else if (is_rational(&p1)) {
		return rat_round(q, &p1, 2);
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
	} else if (is_rational(&p1)) {
		return rat_round(q, &p1, 3);
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (is_integer(&p1)) {
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
	} else if (is_rational(&p1)) {
		return rat_round(q, &p1, 1);
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (is_integer(&p1)) {
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
	} else if (is_rational(&p1)) {
		// Taken as a float, as Scryer does, where SWI gives an exact integer
		pl_flt d;
		q->accum.tag = TAG_FLOAT;
		q->accum.flags = 0;

		if (!num_to_double(&p1, &d))
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.val_float = trunc(d);
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (is_integer(&p1)) {
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
	} else if (is_rational(&p1)) {
		// Taken as a float, as Scryer does, where SWI gives an exact rational
		pl_flt d;
		q->accum.tag = TAG_FLOAT;
		q->accum.flags = 0;

		if (!num_to_double(&p1, &d))
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.val_float = d - trunc(d);
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (is_integer(&p1)) {
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
	} else if (is_rational(&p1)) {
		return rat_round(q, &p1, 0);
	} else if (is_var(&p1)) {
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");
	} else if (is_integer(&p1)) {
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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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
		q->accum.tag = TAG_FLOAT;
		q->accum.flags = 0;
		q->accum.val_float = tan(RATIONAL_TO_DOUBLE(&p1.val_bigint->irat));
		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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
	} else if (is_number(&p1) && is_number(&p2)) {		// a bigint or rational, never zero, where they were refused
		pl_flt d1, d2;

		if (!num_to_double(&p1, &d1) || !num_to_double(&p2, &d2))
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

		q->accum.val_float = atan2(d1, d2);
		q->accum.tag = TAG_FLOAT;
	} else if (is_var(&p1) || is_var(&p2)) {
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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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

	if (too_big_for_float(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

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

// The magnitude of X with the sign of Y, -0.0 included, where an integer X kept its own sign and bigints and rationals were refused

static bool bif_iso_copysign_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_var(&p1) || is_var(&p2))
		return throw_error(q, is_var(&p1) ? &p1 : &p2, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");

	if (!is_number(&p1) || !is_number(&p2))
		return throw_error(q, !is_number(&p1) ? &p1 : &p2, q->st.cur_ctx, "type_error", "evaluable");

	const bool neg = is_float(&p2) ? signbit(p2.val_float)
		: is_smallint(&p2) ? (p2.val_int < 0)
		: is_bigint(&p2) ? (mp_int_compare_zero(&p2.val_bigint->ival) < 0)
		: (mp_rat_compare_zero(&p2.val_bigint->irat) < 0);

	if (is_float(&p1)) {
		q->accum.val_float = neg ? -fabs(p1.val_float) : fabs(p1.val_float);
		q->accum.tag = TAG_FLOAT;
		q->accum.flags = 0;
	} else if (is_rational(&p1)) {
		mp_rat_abs(&p1.val_bigint->irat, &q->tmp_irat);
		if (neg) mp_rat_neg(&q->tmp_irat, &q->tmp_irat);
		SET_RAT_ACCUM2();
	} else {
		if (is_smallint(&p1))
			mp_int_set_value(&q->tmp_ival, p1.val_int);
		else
			mp_int_copy(&p1.val_bigint->ival, &q->tmp_ival);

		mp_int_abs(&q->tmp_ival, &q->tmp_ival);
		if (neg) mp_int_neg(&q->tmp_ival, &q->tmp_ival);
		return accum_int(q, &p1);
	}

	return true;
}

// The largest integer << or ^ will build, in bits; past it is a resource error, not an imath allocation that thrashes

#define MAX_INT_BITS ((pl_int)1 << 32)


// X ** Y, and X ^ Y with a float or a rational: a bigint or rational taken as a double, as Scryer does, zero to a negative power undefined

static bool float_pow(query *q, cell *p1, cell *p2)
{
	pl_flt x, y;

	if (!num_to_double(p1, &x))
		return throw_error(q, p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

	if (!num_to_double(p2, &y))
		return throw_error(q, p2, q->st.cur_ctx, "evaluation_error", "float_overflow");

	if ((x == 0.0) && (y < 0.0))
		return throw_error(q, p2, q->st.cur_ctx, "evaluation_error", "undefined");

	q->accum.tag = TAG_FLOAT;			// before the errors below, which read accum, still a rational or bigint from an argument
	q->accum.flags = 0;
	q->accum.val_float = pow(x, y);

	if (isinf(q->accum.val_float))
		return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

	if (isnan(q->accum.val_float))
		return throw_error(q, p1, q->st.cur_ctx, "evaluation_error", "undefined");

	return true;
}

static bool bif_iso_pow_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if (is_number(&p1) && is_number(&p2))
		return float_pow(q, &p1, &p2);

	if (is_var(&p1) || is_var(&p2))
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");

	return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
}

static bool bif_iso_powi_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if ((is_float(&p1) || is_float(&p2) || is_rational(&p1) || is_rational(&p2)) && is_number(&p1) && is_number(&p2))
		return float_pow(q, &p1, &p2);

	if (!is_integer(&p1) || !is_integer(&p2)) {
		if (is_var(&p1) || is_var(&p2))
			return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");

		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
	}

	// By value: a bigint can hold a small one, from >>, mod, gcd or rational arithmetic

	mp_small base = 0, exp = 0;
	const bool small_base = is_smallint(&p1) ? (base = p1.val_int, true) : (mp_int_to_int(&p1.val_bigint->ival, &base) == MP_OK);
	const bool small_exp = is_smallint(&p2) ? (exp = p2.val_int, true) : (mp_int_to_int(&p2.val_bigint->ival, &exp) == MP_OK);

	// 0, 1 and -1 have an answer for any exponent, a bigint or a negative one too

	if (small_base && (base >= -1) && (base <= 1)) {
		const bool odd = small_exp ? (exp & 1) : mp_int_is_odd(&p2.val_bigint->ival);

		if ((base == 0) && is_negative(&p2))
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

		q->accum.tag = TAG_INT;
		q->accum.flags = 0;
		q->accum.val_int = base == 1 ? 1 : base == -1 ? (odd ? -1 : 1) : (small_exp && !exp) ? 1 : 0;
		return true;
	}

	if (is_negative(&p2))
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "float");

	// Any other base has at least msb(|X|) bits per unit of exponent

	uint64_t bits = 0;

	if (!small_base)
		bits = mp_int_count_bits(&p1.val_bigint->ival) - 1;
	else
		for (uint64_t n = base < 0 ? 0 - (uint64_t)base : (uint64_t)base; n > 1; n >>= 1)
			bits++;

	if (!small_exp || ((uint64_t)exp > (uint64_t)MAX_INT_BITS / bits))
		return throw_error(q, &p2, q->st.cur_ctx, "resource_error", "memory");

	if (!exp) {
		q->accum.tag = TAG_INT;
		q->accum.flags = 0;
		q->accum.val_int = 1;
		return true;
	}

	if ((small_base ? mp_int_expt_value(base, exp, &q->tmp_ival) : mp_int_expt(&p1.val_bigint->ival, exp, &q->tmp_ival)) != MP_OK)
		return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");

	if (errno == ENOMEM)
		return throw_error(q, &p1, q->st.cur_ctx, "resource_error", "memory");

	mp_small i;

	if (mp_int_to_int(&q->tmp_ival, &i) != MP_OK) {		// past either end of a smallint
		SET_ACCUM();
		return true;
	}

	q->accum.tag = TAG_INT;
	q->accum.flags = 0;
	q->accum.val_int = i;
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
	} else if ((is_rational(&p1) || is_rational(&p2)) && is_number(&p1) && is_number(&p2)) {
		// A rational is taken as a float, as int/int gives one, as Scryer and SWI's iso mode do
		pl_flt d1, d2;
		q->accum.tag = TAG_FLOAT;			// first: accum may still say rational, and an error would read the double as its pointer
		q->accum.flags = 0;

		if (!num_to_double(&p1, &d1))
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

		if (!num_to_double(&p2, &d2))
			return throw_error(q, &p2, q->st.cur_ctx, "evaluation_error", "float_overflow");

		if ((d2 == 0.0) && !is_rational(&p2))
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		// A rational divisor too small for a double is not zero: 0 over it is 0, not undefined
		q->accum.val_float = (d1 == 0.0) && (d2 == 0.0) ? (mp_rat_compare_zero(&p2.val_bigint->irat) < 0 ? -d1 : d1) : d1 / d2;

		if (isinf(q->accum.val_float))
			return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");
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
	if (y == -1)
		return 0;	// INT64_MIN % -1 traps on x86-64

	pl_int r = x % y;

	if ( (r != 0)
		&& (r<0) != (y<0) )
		r += y;

  return r;
}

static void big_mod(mpz_t *x, mpz_t *y, mpz_t *r)
{
	mp_int_div(x, y, NULL, r);

	if ((mp_int_compare_zero(r) != 0)
		&& ((mp_int_compare_value(r, 0) < 0) != (mp_int_compare_value(y, 0) < 0))) {
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
		if (p2.val_int == 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

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
		big_mod(&p1.val_bigint->ival, &p2.val_bigint->ival, &tmp3);
		mp_int_sub(&p1.val_bigint->ival, &tmp3, &tmp4);
		mp_int_div(&tmp4, &p2.val_bigint->ival, &q->tmp_ival, NULL);
		SET_ACCUM();
		mp_int_clear(&tmp3);
		mp_int_clear(&tmp4);
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		if (p2.val_int == 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		mpz_t tmp2, tmp3, tmp4;
		mp_int_init_value(&tmp2, p2.val_int);
		mp_int_init(&tmp3);
		mp_int_init(&tmp4);
		big_mod(&p1.val_bigint->ival, &tmp2, &tmp3);
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
		big_mod(&tmp1, &p2.val_bigint->ival, &tmp3);
		mp_int_sub(&tmp1, &tmp3, &tmp4);
		mp_int_div(&tmp4, &p2.val_bigint->ival, &q->tmp_ival, NULL);
		SET_ACCUM();
		mp_int_clear(&tmp1);
		mp_int_clear(&tmp3);
		mp_int_clear(&tmp4);
	} else if (is_smallint(&p1) && is_smallint(&p2)) {
		if (p2.val_int == 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		if ((p2.val_int == -1) && (p1.val_int == PL_INT_MIN)) {
			mp_int_set_value(&q->tmp_ival, p1.val_int);
			mp_int_neg(&q->tmp_ival, &q->tmp_ival);
			SET_ACCUM();
			return true;
		}

		// Truncate, then step down: p1 minus the floor-mod can overflow near INT64_MAX
		pl_int quot = p1.val_int / p2.val_int, r = p1.val_int % p2.val_int;

		if ((r != 0) && ((r ^ p2.val_int) < 0))
			quot -= 1;

		q->accum.val_int = quot;
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

		// INT64_MIN % -1 traps on x86-64
		q->accum.val_int = (p2.val_int == -1) ? 0 : p1.val_int % p2.val_int;
		q->accum.tag = TAG_INT;
	} else if (is_bigint(&p1) && is_bigint(&p2)) {
		mp_int_div(&p1.val_bigint->ival, &p2.val_bigint->ival, NULL, &q->tmp_ival);
		SET_ACCUM();
	} else if (is_bigint(&p1) && is_smallint(&p2)) {
		if (p2.val_int == 0)
			return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

		mpz_t tmp;
		mp_int_init_value(&tmp, p2.val_int);
		mp_int_div(&p1.val_bigint->ival, &tmp, NULL, &q->tmp_ival);
		mp_int_clear(&tmp);
		SET_ACCUM();
	} else if (is_smallint(&p1) && is_bigint(&p2)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, p1.val_int);
		mp_int_div(&tmp, &p2.val_bigint->ival, NULL, &q->tmp_ival);
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

// Two numbers, one a rational, compared exactly unless one is a float

static int rat_num_compare(cell *p1, cell *p2)
{
	if (is_float(p1) || is_float(p2)) {
		pl_flt d1, d2;
		num_to_double(p1, &d1);
		num_to_double(p2, &d2);
		return (d1 > d2) - (d1 < d2);
	}

	mpq_t a, b;
	mp_rat_init(&a);
	mp_rat_init(&b);
	cell *c[2] = {p1, p2};
	mpq_t *r[2] = {&a, &b};

	for (int i = 0; i < 2; i++) {
		if (is_rational(c[i]))
			mp_rat_copy(&c[i]->val_bigint->irat, r[i]);
		else if (is_bigint(c[i])) {
			mp_rat_set_value(r[i], 0, 1);
			mp_int_copy(&c[i]->val_bigint->ival, &r[i]->num);
		} else
			mp_rat_set_value(r[i], c[i]->val_int, 1);
	}

	const int cmp = mp_rat_compare(&a, &b);
	mp_rat_clear(&a);
	mp_rat_clear(&b);
	return (cmp > 0) - (cmp < 0);
}

// max/min with a rational: a tie with a float gives the float, as for an integer

#define RAT_MAX_MIN(want) { \
	const int cmp = rat_num_compare(&p1, &p2); \
	cell *c = !cmp ? (is_float(&p1) ? &p1 : &p2) : (cmp == (want)) ? &p1 : &p2; \
	if (is_rational(c)) { \
		mp_rat_copy(&c->val_bigint->irat, &q->tmp_irat); \
		SET_RAT_ACCUM2(); \
	} else if (is_bigint(c)) { \
		mp_int_copy(&c->val_bigint->ival, &q->tmp_ival); \
		SET_ACCUM(); \
	} else \
		q->accum = *c; \
	return true; \
}

static bool bif_iso_max_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);

	if ((is_rational(&p1) || is_rational(&p2)) && is_number(&p1) && is_number(&p2))
		RAT_MAX_MIN(1);

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

	if ((is_rational(&p1) || is_rational(&p2)) && is_number(&p1) && is_number(&p2))
		RAT_MAX_MIN(-1);

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

// x << n and x >> n as SWI and Scryer have them: a negative count shifts
// the other way, a right shift rounds down, so that a negative x ends at
// -1, not 0, however far it's shifted, and a left shift too big to hold
// is a resource error. Not undefined C for a count of 64 or more, nor an
// imath assertion for a negative one, nor a garbage result for a bigint one.

static bool do_shift(query *q, cell *a, cell *b, bool left)
{
	cell p1 = *a;						// SET_ACCUM()'s culprit

	if (is_var(a) || is_var(b))
		return throw_error(q, a, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");

	if (!is_integer(a))
		return throw_error(q, a, q->st.cur_ctx, "type_error", "integer");

	if (!is_integer(b))
		return throw_error(q, b, q->st.cur_ctx, "type_error", "integer");

	bool neg = is_negative(a);

	if (is_negative(b))
		left = !left;

	// A count too big for a small integer: everything is shifted out, or
	// would need more memory than there is.

	// By value: a bigint can hold a small count, from >>, mod, gcd or rational arithmetic

	mp_small count = 0;
	const bool small_count = is_smallint(b) ? (count = get_smallint(b), true) : (mp_int_to_int(&b->val_bigint->ival, &count) == MP_OK);

	if (!small_count || (count == INT64_MIN)) {
		if (is_zero(a) || !left) {
			q->accum.tag = TAG_INT;
			q->accum.val_int = !left && neg ? -1 : 0;
			return true;
		}

		return throw_error(q, b, q->st.cur_ctx, "resource_error", "memory");
	}

	pl_int n = count < 0 ? -count : count;

	if (!left) {
		if (is_smallint(a)) {
			q->accum.tag = TAG_INT;
			q->accum.val_int = n >= 64 ? (neg ? -1 : 0) : get_smallint(a) >> n;
			return true;
		}

		if (!neg)
			mp_int_div_pow2(&a->val_bigint->ival, n, &q->tmp_ival, NULL);
		else {
			mpz_t tmp;							// floor, where imath truncates: -((-x - 1) >> n) - 1
			mp_int_init(&tmp);
			mp_int_neg(&a->val_bigint->ival, &tmp);
			mp_int_sub_value(&tmp, 1, &tmp);
			mp_int_div_pow2(&tmp, n, &q->tmp_ival, NULL);
			mp_int_clear(&tmp);
			mp_int_add_value(&q->tmp_ival, 1, &q->tmp_ival);
			mp_int_neg(&q->tmp_ival, &q->tmp_ival);
		}

		SET_ACCUM();
		return true;
	}

	if (is_zero(a)) {
		q->accum.tag = TAG_INT;
		q->accum.val_int = 0;
		return true;
	}

	if (n > MAX_INT_BITS)
		return throw_error(q, b, q->st.cur_ctx, "resource_error", "memory");

	if (is_smallint(a) && (n < 32) && (get_smallint(a) < INT32_MAX) && (get_smallint(a) > INT32_MIN)) {
		q->accum.tag = TAG_INT;
		q->accum.val_int = get_smallint(a) * ((pl_int)1 << n);	// a negative x's << is undefined C
		return true;
	}

	if (is_smallint(a)) {
		mpz_t tmp;
		mp_int_init_value(&tmp, get_smallint(a));
		mp_int_mul_pow2(&tmp, n, &q->tmp_ival);
		mp_int_clear(&tmp);
	} else
		mp_int_mul_pow2(&a->val_bigint->ival, n, &q->tmp_ival);

	SET_ACCUM();
	return true;
}

static bool bif_iso_shl_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	return do_shift(q, &p1, &p2, true);
}

static bool bif_iso_shr_2(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	return do_shift(q, &p1, &p2, false);
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
	start_eval(q);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	COMPARE_OP(==,p1,p2);
	return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
}

static bool bif_iso_numeric_ne_2(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	start_eval(q);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	COMPARE_OP(!=,p1,p2);
	return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
}

static bool bif_iso_numeric_ge_2(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	start_eval(q);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	COMPARE_OP(>=,p1,p2);
	return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
}

static bool bif_iso_numeric_gt_2(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	start_eval(q);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	COMPARE_OP(>,p1,p2);
	return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
}

static bool bif_iso_numeric_le_2(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	start_eval(q);
	CLEANUP cell p1 = eval(q, p1_tmp);
	CLEANUP cell p2 = eval(q, p2_tmp);
	COMPARE_OP(<=,p1,p2);
	return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");
}

static bool bif_iso_numeric_lt_2(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);
	GET_NEXT_ARG(p2_tmp,any);
	start_eval(q);
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
	cell *c[2] = {&p1, &p2};
	pl_flt d[2];

	for (int i = 0; i < 2; i++) {
		if (is_var(c[i]))
			return throw_error(q, c[i], q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");

		if (!is_number(c[i]))
			return throw_error(q, c[i], q->st.cur_ctx, "type_error", "evaluable");
	}

	// Any number, where a bigint left accum unset and gave garbage and a rational was refused

	for (int i = 0; i < 2; i++) {
		if (!num_to_double(c[i], &d[i]))
			return throw_error(q, c[i], q->st.cur_ctx, "evaluation_error", "float_overflow");

		if (d[i] == 0.0)
			return throw_error(q, c[i], q->st.cur_ctx, "evaluation_error", "zero_divisor");

		if (d[i] < 0.0)
			return throw_error(q, c[i], q->st.cur_ctx, "evaluation_error", "undefined");
	}

	q->accum.tag = TAG_FLOAT;
	q->accum.flags = 0;
	q->accum.val_float = log(d[1]) / log(d[0]);

	if (isinf(q->accum.val_float))
		return throw_error(q, &q->accum, q->st.cur_ctx, "evaluation_error", "float_overflow");

	return true;
}

static bool bif_log10_1(query *q)
{
	START_FUNCTION(q);
	GET_FIRST_ARG(p1_tmp,any);
	CLEANUP cell p1 = eval(q, p1_tmp);
	pl_flt d;

	if (is_var(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "instantiation_error", "not_sufficiently_instantiated");

	if (!is_number(&p1))
		return throw_error(q, &p1, q->st.cur_ctx, "type_error", "evaluable");

	if (!num_to_double(&p1, &d))		// any number, where a bigint or rational was refused
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "float_overflow");

	if (d == 0.0)
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "zero_divisor");

	if (d < 0.0)
		return throw_error(q, &p1, q->st.cur_ctx, "evaluation_error", "undefined");

	q->accum.tag = TAG_FLOAT;
	q->accum.flags = 0;
	q->accum.val_float = log10(d);
	return true;
}

static pl_flt rnd(query *q)
{
	return (pl_flt)tpl_rand_r(&q->rand_seed) / (pl_flt)RAND_MAX;
}

// A seed goes through murmur3's finaliser, as rand_r()'s first value is nearly proportional to it; the inverse lets get_seed/1 hand back one that restores the state

static unsigned seed_mix(unsigned h)
{
	h ^= h >> 16;
	h *= 0x85ebca6bu;
	h ^= h >> 13;
	h *= 0xc2b2ae35u;
	return h ^ (h >> 16);
}

static unsigned seed_unmix(unsigned h)
{
	h ^= h >> 16;
	h *= 0x7ed1b41du;
	h ^= (h >> 13) ^ (h >> 26);
	h *= 0xa5cb9243u;
	return h ^ (h >> 16);
}

static bool bif_set_seed_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	q->rand_seed = seed_mix((unsigned)p1->val_uint);
	srand(q->rand_seed);
	return true;
}

static bool bif_get_seed_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	cell tmp;
	make_uint(&tmp, seed_unmix(q->rand_seed));
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

// On magnitudes, so the result is never negative and INT64_MIN can't trap
static uint64_t gcd(pl_int num, pl_int remainder)
{
	uint64_t a = (num < 0) ? -(uint64_t)num : (uint64_t)num;
	uint64_t b = (remainder < 0) ? -(uint64_t)remainder : (uint64_t)remainder;

	while (b != 0) {
		uint64_t t = a % b;
		a = b;
		b = t;
	}

	return a;
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
		uint64_t g = gcd(p1.val_int, p2.val_int);

		if (g > (uint64_t)PL_INT_MAX) {	// 2^63, from INT64_MIN with 0 or itself
			mp_int_set_uvalue(&q->tmp_ival, g);
			SET_ACCUM();
		} else {
			q->accum.val_int = (pl_int)g;
			q->accum.tag = TAG_INT;
		}
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

	{"rationalize", 1, bif_rationalize_1, "+number,-number", false, true, BLAH},
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
