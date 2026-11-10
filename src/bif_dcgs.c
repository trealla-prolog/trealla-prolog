// Native DCG translation: the translator and its bifs.
//
// This is a cell -> cell rewrite of what library/dcgs.pl does with =..,
// append/3 and subsumes_term/2. Nothing here creates a query, prints a
// term, or re-parses one: the output is built in a plain cell arena and
// blitted into final storage by the caller.
//
// Layer 1 (everything above the bif table) never throws and never
// touches the query heap. It records error *intent* in the dcg_ctx and
// returns DCG_ERROR; the caller decides whether that becomes a parser
// error (consult time, later phases) or throw_error() (runtime, here).
// That split exists because compile-time expansion may not raise an
// error at a different moment than the runtime would - see section 5.3
// of docs/native-dcg-design.md.
//
// The one concession to Layer 1 "has no query dependency": ctx->q is
// used for deref() and for fresh variables, and only when non-NULL. At
// consult time the clause cells carry no bindings and the parser
// supplies its own variables; that path is dcg_expand_clause(), at the
// foot of this file.
//
// Note that DCG_DECLINE is never returned from here. Layer 1 always
// translates or errors; the decline decision is dcg_is_constr(), which
// '$dcg_body'/4 tests before calling in. The DECLINE branches in both
// bifs are belt and braces.
//
// This reproduces library/dcgs.pl's translation, with ONE deliberate
// divergence: a nonvar non-callable in non-terminal position raises
// type_error(callable, T) here, where the reference silently drops the
// S0/S arguments and lets call/1 report the whole body instead. That is
// issue #1102 (== #832), and it is why '$dcg_rule'/2 is not a drop-in
// oracle match for dcg_rule/2.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "module.h"
#include "prolog.h"
#include "query.h"
#include "heap.h"
#include "parser.h"

// Three outcomes, not two. A caller must be able to tell "this is not a
// construct I handle, fall through to an ordinary non-terminal call"
// from "this is a construct and it is malformed". '$dcg_body'/4 turns
// DECLINE into failure so phrase/3 falls through to call(M:B,S0,S).

typedef enum {
	DCG_OK = 0,		// translated; arena holds the goal
	DCG_DECLINE,		// not a construct; caller falls through
	DCG_ERROR		// error intent recorded in ctx, or OOM
} dcg_rc;

typedef struct {
	cell *buf;
	unsigned len, cap;
} dcg_arena;

typedef struct {
	dcg_arena ar;
	prolog *pl;
	module *m;

	// Exactly one of these is set. The query path derefs, makes fresh
	// variables with create_vars(), and emits variables as REFS carrying
	// a context. The parser path has no bindings to deref, numbers fresh
	// variables straight off p->cl->num_vars, and must emit plain clause
	// variables - a ref in a consulted clause would be nonsense.

	query *q;
	parser *p;
	bool by_ref;		// true on the query path

	// The context fresh variables carry. q->st.cur_ctx on the query
	// path, 0 at consult time. Held here rather than fetched from the
	// query, so nothing below has to know which path it is on.

	pl_ctx v_ctx;
	unsigned nvars;		// consult-time fresh-variable counter
	unsigned depth;
	bool oom;

	// Pending error, raised by the caller in its own idiom. culprit
	// points into the SOURCE term, so it is valid only while that term
	// is alive - every caller raises before returning.

	const char *err_type, *err_expected;
	const cell *culprit;
	pl_ctx culprit_ctx;

	// Set when the arena holds a fully-built ball instead. The reference
	// raises two shapes that throw_error3() cannot compose, because its
	// context is always Name/Arity: [culprit-Term] for \+ and ->, and
	// must_be/2 for a partial terminal list. Matching them keeps the
	// phase 0 differential harness a tight net, so that the divergence
	// list stays what it is meant to be - deliberate semantic choices
	// (issue #1102), not incidental error formatting.

	bool has_ball;
} dcg_ctx;

// File-local interned atoms. Those that already exist as globals
// (g_conjunction_s, g_disjunction_s, g_if_then_s, g_negation_s,
// g_cut_s, g_call_s, g_colon_s, g_unify_s, g_dot_s, g_nil_s,
// g_braces_s, g_neck_s, g_dcg_s) are used directly.

static pl_idx g_bar_s, g_phrase_s, g_string_prefix_s;
static pl_idx g_repr_err_s, g_dcg_body_s, g_culprit_s;
static pl_idx g_inst_err_s, g_must_be_s, g_type_error_s, g_list_s;
// new_atom() is idempotent and takes g_symtab_guard, and the symbol
// table is process-global, so caching the offsets in statics is sound
// across prolog instances. The ordering was not: writing the "done"
// flag last did not stop the compiler or CPU making that store visible
// before the g_* stores, so a second thread could skip initialisation
// and then read an offset that had not been assigned - a zero pl_idx,
// i.e. silently building terms with the wrong functor.
//
// Interning is cheap and idempotent, so the fix is to drop the fast
// path and let every caller run it. bif_tabling.c's tbl_intern_atoms()
// had the same shape and the same change.

static void dcg_init_atoms(prolog *pl)
{
	g_bar_s = new_atom(pl, "|");
	g_phrase_s = new_atom(pl, "phrase");
	g_string_prefix_s = new_atom(pl, "$string_prefix");
	g_repr_err_s = new_atom(pl, "representation_error");
	g_dcg_body_s = new_atom(pl, "dcg_body");
	g_culprit_s = new_atom(pl, "culprit");
	g_inst_err_s = new_atom(pl, "instantiation_error");
	g_must_be_s = new_atom(pl, "must_be");
	g_type_error_s = new_atom(pl, "type_error");
	g_list_s = new_atom(pl, "list");
}

// Bounds the conjunction/alternation spine and the module-qualification
// nest. The terminal-list walk is iterative and not covered by this.

#define MAX_DCG_DEPTH 2000

// Above this many bytes a string terminal is emitted as a
// '$string_prefix'/3 call rather than materialised into the clause as
// two cells per character. Section 6 picked ~64 as the crossover; the
// cost being avoided is clause size, not speed - see the bif's comment.

#define DCG_STRING_INLINE_MAX 64

// --- arena ---
//
// Cells are appended in prefix order and num_cells is patched on the way
// out, the same idiom heap.c uses. Managed cells copied in from the
// source are shared on the way in (dup_cells_by_ref) and released by
// arena_free() if translation is abandoned; on success the buffer's
// references transfer wholesale to the heap copy, so that path frees the
// buffer WITHOUT unsharing.

static bool arena_reserve(dcg_ctx *c, unsigned n)
{
	if (c->oom)
		return false;

	if ((c->ar.len + n) <= c->ar.cap)
		return true;

	unsigned cap = c->ar.cap ? c->ar.cap : 64;

	while (cap < (c->ar.len + n))
		cap *= 2;

	cell *buf = TPL_realloc(c->ar.buf, sizeof(cell) * cap);

	if (!buf) {
		c->oom = true;
		return false;
	}

	c->ar.buf = buf;
	c->ar.cap = cap;
	return true;
}

static cell *arena_alloc(dcg_ctx *c, unsigned n)
{
	if (!arena_reserve(c, n))
		return NULL;

	cell *ptr = c->ar.buf + c->ar.len;
	c->ar.len += n;
	return ptr;
}

static void arena_release(dcg_ctx *c)
{
	if (c->ar.buf) {
		unshare_cells(c->ar.buf, c->ar.len);
		TPL_free(c->ar.buf);
	}

	c->ar.buf = NULL;
	c->ar.len = c->ar.cap = 0;
}

static unsigned emit_open(dcg_ctx *c, pl_idx functor, unsigned arity)
{
	unsigned at = c->ar.len;
	cell *t = arena_alloc(c, 1);

	if (!t)
		return 0;

	make_struct(t, functor, arity, 0);
	return at;
}

// Every open cell's extent runs to whatever the arena holds now, so
// this is correct whenever it is called after the cell's last argument.

static void emit_close(dcg_ctx *c, unsigned at)
{
	if (c->oom)
		return;

	c->ar.buf[at].num_cells = c->ar.len - at;
}

static bool emit_atom(dcg_ctx *c, pl_idx functor)
{
	cell *t = arena_alloc(c, 1);

	if (!t)
		return false;

	make_atom(t, functor);
	return true;
}

// Copy a source subterm in. Variables become refs carrying their own
// context, which is what makes the emitted goal meaningful once it is
// unified into the caller's frame.

static bool emit_term(dcg_ctx *c, const cell *t, pl_ctx t_ctx)
{
	cell *dst = arena_alloc(c, t->num_cells);

	if (!dst)
		return false;

	if (c->by_ref)
		dup_cells_by_ref(dst, t, t_ctx, t->num_cells);
	else
		dup_cells(dst, t, t->num_cells);

	return true;
}

static bool emit_cell(dcg_ctx *c, const cell *t)
{
	cell *dst = arena_alloc(c, 1);

	if (!dst)
		return false;

	*dst = *t;
	share_cell(dst);
	return true;
}

static void set_error(dcg_ctx *c, const char *type, const char *expected, const cell *culprit, pl_ctx culprit_ctx)
{
	if (c->err_type || c->has_ball)
		return;			// first error wins

	c->err_type = type;
	c->err_expected = expected;
	c->culprit = culprit;
	c->culprit_ctx = culprit_ctx;
}

// The partial translation is dead the moment an error is raised, so the
// ball is built in the same arena - reset first, then emit.

static bool start_ball(dcg_ctx *c)
{
	if (c->err_type || c->has_ball)
		return false;

	arena_release(c);
	return !c->oom;
}

// error(representation_error(dcg_body), [culprit-Culprit])

static void set_ball_repr(dcg_ctx *c, const cell *culprit, pl_ctx culprit_ctx)
{
	if (!start_ball(c))
		return;

	unsigned err = emit_open(c, g_error_s, 2);
	unsigned re = emit_open(c, g_repr_err_s, 1);

	if (c->oom || !emit_atom(c, g_dcg_body_s))
		return;

	emit_close(c, re);

	unsigned lst = emit_open(c, g_dot_s, 2);
	unsigned pair = emit_open(c, g_minus_s, 2);

	if (c->oom
		|| !emit_atom(c, g_culprit_s)
		|| !emit_term(c, culprit, culprit_ctx))
		return;

	emit_close(c, pair);

	if (!emit_atom(c, g_nil_s))
		return;

	emit_close(c, lst);
	emit_close(c, err);
	c->has_ball = true;
}

// The reference reaches both of these through must_be(list, [T|Ts]) in
// dcg_cbody/4, so both carry a must_be/2 context:
//
//   partial list  [x|_]  ->  error(instantiation_error, must_be/2)
//   improper list [x|y]  ->  error(type_error(list,[x|y]), must_be/2)

static void emit_must_be_context(dcg_ctx *c)
{
	unsigned sl = emit_open(c, g_slash_s, 2);

	if (c->oom || !emit_atom(c, g_must_be_s))
		return;

	cell n;
	make_int(&n, 2);

	if (!emit_cell(c, &n))
		return;

	emit_close(c, sl);
}

static void set_ball_must_be_inst(dcg_ctx *c)
{
	if (!start_ball(c))
		return;

	unsigned err = emit_open(c, g_error_s, 2);

	if (c->oom || !emit_atom(c, g_inst_err_s))
		return;

	emit_must_be_context(c);

	if (c->oom)
		return;

	emit_close(c, err);
	c->has_ball = true;
}

static void set_ball_must_be_list(dcg_ctx *c, const cell *culprit, pl_ctx culprit_ctx)
{
	if (!start_ball(c))
		return;

	unsigned err = emit_open(c, g_error_s, 2);
	unsigned te = emit_open(c, g_type_error_s, 2);

	if (c->oom
		|| !emit_atom(c, g_list_s)
		|| !emit_term(c, culprit, culprit_ctx))
		return;

	emit_close(c, te);
	emit_must_be_context(c);

	if (c->oom)
		return;

	emit_close(c, err);
	c->has_ball = true;
}

// --- source inspection ---

static cell *dcg_deref(dcg_ctx *c, const cell *t, pl_ctx t_ctx, pl_ctx *out_ctx)
{
	if (!c->q) {
		*out_ctx = t_ctx;
		return (cell*)t;
	}

	cell *r = deref(c->q, (cell*)t, t_ctx);
	*out_ctx = c->q->latest_ctx;
	return r;
}

static bool is_functor(const cell *t, pl_idx functor, unsigned arity)
{
	return is_interned(t) && (t->val_off == functor) && (get_arity(t) == arity);
}

static cell *nth_arg(const cell *t, unsigned n)
{
	cell *a = (cell*)t + 1;

	while (n--)
		a += a->num_cells;

	return a;
}

// 7.14 constructs. Note call/1 and phrase/1..3 ONLY: the reference has
// no clause for call/N with N>1, and for those arities the non-terminal
// path produces the identical goal anyway, so the distinction is not
// observable. Kept faithful rather than tidied.

static bool dcg_is_constr(const cell *t)
{
	if (is_nil(t))
		return true;

	if (is_iso_list(t) || is_string(t))
		return true;

	if (!is_interned(t))
		return false;

	const pl_idx f = t->val_off;
	const unsigned a = get_arity(t);

	if ((a == 2) && ((f == g_conjunction_s) || (f == g_disjunction_s)
		|| (f == g_bar_s) || (f == g_if_then_s)))
		return true;

	if ((a == 1) && ((f == g_braces_s) || (f == g_call_s) || (f == g_negation_s)))
		return true;

	if ((f == g_phrase_s) && (a >= 1) && (a <= 3))
		return true;

	if ((a == 0) && (f == g_cut_s))
		return true;

	return false;
}

// --- fresh variables ---

static bool new_var(dcg_ctx *c, cell *out)
{
	if (c->q) {
		int n = create_vars(c->q, 1);

		if (n < 0) {
			c->oom = true;
			return false;
		}

		make_ref(out, (unsigned)n, c->v_ctx);
		return true;
	}

	// Consult time. Section 10 option (a): named, and registered in the
	// vartab by the assign_vars() that runs after us - see the note below
	// for why option (b) cannot work. dcg_expand_clause() is called from
	// tokenize() immediately BEFORE assign_vars(), and phase 4 must
	// preserve that order.

	if (c->p->cl->num_vars >= MAX_VARS) {
		set_error(c, "resource_error", "max_vars", NULL, 0);
		return false;
	}

	// Emitted NAMED and unnumbered: assign_vars() runs after this and
	// assigns the slot, registering the name in the vartab as it goes.
	//
	// That registration is the whole point. goal_expansion() prints a
	// goal and re-parses it, reconnecting variables by name through the
	// inherited vartab; a variable with no entry there comes back as a
	// different one, which silently unthreads S0/S. Section 10 option (b)
	// - anonymous, temporary, no vartab entry - cannot work for that
	// reason, and FLAG_VAR_TEMPORARY separately breaks head-argument
	// sharing.
	//
	// The name only has to be unique within this clause. A user variable
	// literally named _S<n> in the same clause would merge with ours;
	// the old round trip had the same exposure with its generated names.

	char name[32];
	snprintf(name, sizeof(name), "_S%u", c->nvars++);
	pl_idx off = new_atom(c->pl, name);

	if (off == ERR_IDX) {
		c->oom = true;
		return false;
	}

	make_var(out, off, 0);
	return true;
}

// --- terminals ---
//
// Emit <Terminals ++ Tail>, which is dcg_terminals/3's append/3 done at
// translate time: a proper list becomes cons cells with Tail as the
// final tail. Iterative, with an explicit index array, because a
// terminal list can be very long (section 9) and this must not sit on
// the C stack.
//
// Strings are materialised, which is what the reference does and what
// section 6 settles on: the CONSUMING direction stays fast regardless,
// because unify.c slices, so this costs cells only when generating.

static dcg_rc emit_terminals(dcg_ctx *c, const cell *l, pl_ctx l_ctx,
	const cell *tail, pl_ctx tail_ctx, const cell *whole, pl_ctx whole_ctx)
{
	unsigned *opens = NULL;
	unsigned n_opens = 0, cap_opens = 0;
	cell *p = (cell*)l;
	pl_ctx p_ctx = l_ctx;
	dcg_rc rc = DCG_OK;
	PROLOG_LIST_HANDLER(p);

	while (is_list(p)) {
		if (n_opens == cap_opens) {
			unsigned newcap = cap_opens ? cap_opens * 2 : 32;
			unsigned *tmp = TPL_realloc(opens, sizeof(unsigned) * newcap);

			if (!tmp) {
				c->oom = true;
				rc = DCG_ERROR;
				goto done;
			}

			opens = tmp;
			cap_opens = newcap;
		}

		cell *h = PROLOG_LIST_HEAD(p);
		pl_ctx h_ctx;
		h = dcg_deref(c, h, p_ctx, &h_ctx);

		opens[n_opens++] = emit_open(c, g_dot_s, 2);

		if (c->oom) {
			rc = DCG_ERROR;
			goto done;
		}

		if (!emit_term(c, h, h_ctx)) {
			rc = DCG_ERROR;
			goto done;
		}

		cell *t = PROLOG_LIST_TAIL(p);
		p = dcg_deref(c, t, p_ctx, &p_ctx);
	}

	if (is_var(p)) {
		// Partial list. The reference's dcg_cbody/4 calls
		// must_be(list, [T|Ts]), which throws instantiation_error.
		// On the term_expansion path that propagates, so '$dcg_rule'/2
		// must raise here too.
		//
		// The goal_expansion path defers instead, where the tail may be
		// bound by runtime (section 5.2). Raising unconditionally here
		// is still right: the deferral is the CALLER's, and lives in
		// library/dcgs.pl's user:goal_expansion, which catches a
		// throwing translation and declines the hook so the ordinary
		// phrase/3 call survives to runtime. That is broader than the
		// reference's error_goal/2, which defers instantiation_error
		// and rethrows the rest.
		set_ball_must_be_inst(c);
		rc = DCG_ERROR;
		goto done;
	}

	if (!is_nil(p)) {
		set_ball_must_be_list(c, whole, whole_ctx);
		rc = DCG_ERROR;
		goto done;
	}

	if (!emit_term(c, tail, tail_ctx)) {
		rc = DCG_ERROR;
		goto done;
	}

	// Each open cons cell extends to the end of what we have emitted,
	// so the patch is index-independent and order-independent.

	for (unsigned i = 0; i < n_opens; i++)
		emit_close(c, opens[i]);

done:
	TPL_free(opens);
	return rc;
}

// --- body translation ---

static dcg_rc xlate_body(dcg_ctx *c, const cell *b, pl_ctx b_ctx,
	const cell *s0, pl_ctx s0_ctx, const cell *s, pl_ctx s_ctx);

// S0 = S, the identity thread.

static dcg_rc emit_unify(dcg_ctx *c, const cell *s0, pl_ctx s0_ctx, const cell *s, pl_ctx s_ctx)
{
	unsigned at = emit_open(c, g_unify_s, 2);

	if (c->oom
		|| !emit_term(c, s0, s0_ctx)
		|| !emit_term(c, s, s_ctx))
		return DCG_ERROR;

	emit_close(c, at);
	return DCG_OK;
}

// NonTerminal with S0,S appended. M:NT composes.

static dcg_rc xlate_nonterminal(dcg_ctx *c, const cell *nt, pl_ctx nt_ctx,
	const cell *s0, pl_ctx s0_ctx, const cell *s, pl_ctx s_ctx)
{
	if (++c->depth > MAX_DCG_DEPTH) {
		set_error(c, "resource_error", "dcg_nesting", nt, nt_ctx);
		c->depth--;
		return DCG_ERROR;
	}

	if (is_functor(nt, g_colon_s, 2)) {
		pl_ctx mod_ctx, inner_ctx;
		cell *mod = dcg_deref(c, nth_arg(nt, 0), nt_ctx, &mod_ctx);
		cell *inner = dcg_deref(c, nth_arg(nt, 1), nt_ctx, &inner_ctx);
		unsigned at = emit_open(c, g_colon_s, 2);

		if (c->oom || !emit_term(c, mod, mod_ctx)) {
			c->depth--;
			return DCG_ERROR;
		}

		dcg_rc rc = xlate_nonterminal(c, inner, inner_ctx, s0, s0_ctx, s, s_ctx);

		if (rc == DCG_OK)
			emit_close(c, at);

		c->depth--;
		return rc;
	}

	c->depth--;

	if (is_var(nt)) {
		set_error(c, "instantiation_error", "callable", nt, nt_ctx);
		return DCG_ERROR;
	}

	// Section 5.3: a nonvar non-callable here is a permanent condition -
	// 1 can never become callable - so it is decidable now and raised
	// now, with the BARE subterm as culprit. This is the deliberate
	// divergence from the reference, which drops S0/S and defers to
	// call/1's whole-body report. Issue #1102.

	if (!is_callable(nt)) {
		set_error(c, "type_error", "callable", nt, nt_ctx);
		return DCG_ERROR;
	}

	unsigned at = emit_open(c, nt->val_off, get_arity(nt) + 2);

	if (c->oom)
		return DCG_ERROR;

	for (unsigned i = 0; i < get_arity(nt); i++) {
		pl_ctx a_ctx;
		cell *a = dcg_deref(c, nth_arg(nt, i), nt_ctx, &a_ctx);

		if (!emit_term(c, a, a_ctx))
			return DCG_ERROR;
	}

	if (!emit_term(c, s0, s0_ctx) || !emit_term(c, s, s_ctx))
		return DCG_ERROR;

	emit_close(c, at);
	return DCG_OK;
}

// (A, B) and (If -> Then) share a shape: translate left S0->S1, right
// S1->S, under the given functor.

static dcg_rc xlate_pair(dcg_ctx *c, pl_idx functor, const cell *b, pl_ctx b_ctx,
	const cell *s0, pl_ctx s0_ctx, const cell *s, pl_ctx s_ctx)
{
	cell s1;

	if (!new_var(c, &s1))
		return DCG_ERROR;

	pl_ctx l_ctx, r_ctx;
	cell *lhs = dcg_deref(c, nth_arg(b, 0), b_ctx, &l_ctx);
	cell *rhs = dcg_deref(c, nth_arg(b, 1), b_ctx, &r_ctx);
	unsigned at = emit_open(c, functor, 2);

	if (c->oom)
		return DCG_ERROR;

	dcg_rc rc = xlate_body(c, lhs, l_ctx, s0, s0_ctx, &s1, c->v_ctx);

	if (rc != DCG_OK)
		return rc;

	rc = xlate_body(c, rhs, r_ctx, &s1, c->v_ctx, s, s_ctx);

	if (rc != DCG_OK)
		return rc;

	emit_close(c, at);
	return DCG_OK;
}

// (A ; B) and (A | B): both branches thread S0 -> S.

static dcg_rc xlate_alt(dcg_ctx *c, const cell *b, pl_ctx b_ctx,
	const cell *s0, pl_ctx s0_ctx, const cell *s, pl_ctx s_ctx)
{
	pl_ctx l_ctx, r_ctx;
	cell *lhs = dcg_deref(c, nth_arg(b, 0), b_ctx, &l_ctx);
	cell *rhs = dcg_deref(c, nth_arg(b, 1), b_ctx, &r_ctx);
	unsigned at = emit_open(c, g_disjunction_s, 2);

	if (c->oom)
		return DCG_ERROR;

	// Section 5.1, the asymmetry that must be preserved - and it is
	// narrower than "an if-then condition inside an alternation".
	//
	// The reference's ;-with-if-then clause calls dcg_cbody/4 directly
	// on the condition, bypassing dcg_constr/1 and therefore its throw.
	// Its '|' clause does NOT: it calls dcg_body/4 on both branches, and
	// dcg_body/4 goes through dcg_constr/1, which throws. So:
	//
	//     a --> (b -> c ; d)     translates
	//     a --> (b -> c | d)     representation_error(dcg_body)
	//
	// Quad 22 is exactly the '|' form and accepts either answer; quad 23
	// is the ';' form and requires the permissive one. Reproducing the
	// reference means applying the bypass to ';' ONLY.

	dcg_rc rc;

	if ((b->val_off == g_disjunction_s) && is_functor(lhs, g_if_then_s, 2))
		rc = xlate_pair(c, g_if_then_s, lhs, l_ctx, s0, s0_ctx, s, s_ctx);
	else
		rc = xlate_body(c, lhs, l_ctx, s0, s0_ctx, s, s_ctx);

	if (rc != DCG_OK)
		return rc;

	rc = xlate_body(c, rhs, r_ctx, s0, s0_ctx, s, s_ctx);

	if (rc != DCG_OK)
		return rc;

	emit_close(c, at);
	return DCG_OK;
}

static dcg_rc xlate_body(dcg_ctx *c, const cell *b, pl_ctx b_ctx,
	const cell *s0, pl_ctx s0_ctx, const cell *s, pl_ctx s_ctx)
{
	if (c->oom)
		return DCG_ERROR;

	if (++c->depth > MAX_DCG_DEPTH) {
		set_error(c, "resource_error", "dcg_nesting", b, b_ctx);
		c->depth--;
		return DCG_ERROR;
	}

	dcg_rc rc;

	// 1. Var: deferred, never an error at translate time (section 5.2).

	if (is_var(b)) {
		unsigned at = emit_open(c, g_phrase_s, 3);

		if (!c->oom
			&& emit_term(c, b, b_ctx)
			&& emit_term(c, s0, s0_ctx)
			&& emit_term(c, s, s_ctx)) {
			emit_close(c, at);
			rc = DCG_OK;
		} else
			rc = DCG_ERROR;

		c->depth--;
		return rc;
	}

	// 2. [] (7.14.1)

	if (is_nil(b)) {
		rc = emit_unify(c, s0, s0_ctx, s, s_ctx);
		c->depth--;
		return rc;
	}

	// 3. [T|Ts] and string cells (7.14.2): S0 = <Ts ++ S>

	if (is_iso_list(b) || is_string(b)) {
		// A long literal becomes a call rather than thousands of cells.

		if (is_string(b) && (_CSTRING_LEN(b) > DCG_STRING_INLINE_MAX)) {
			unsigned at = emit_open(c, g_string_prefix_s, 3);

			if (c->oom
				|| !emit_term(c, b, b_ctx)
				|| !emit_term(c, s, s_ctx)
				|| !emit_term(c, s0, s0_ctx)) {
				c->depth--;
				return DCG_ERROR;
			}

			emit_close(c, at);
			c->depth--;
			return DCG_OK;
		}

		unsigned at = emit_open(c, g_unify_s, 2);

		if (c->oom || !emit_term(c, s0, s0_ctx)) {
			c->depth--;
			return DCG_ERROR;
		}

		rc = emit_terminals(c, b, b_ctx, s, s_ctx, b, b_ctx);

		if (rc == DCG_OK)
			emit_close(c, at);

		c->depth--;
		return rc;
	}

	if (!is_interned(b)) {
		// 15. nonvar, non-callable: raised here, never emitted bare.
		rc = xlate_nonterminal(c, b, b_ctx, s0, s0_ctx, s, s_ctx);
		c->depth--;
		return rc;
	}

	const pl_idx f = b->val_off;
	const unsigned a = get_arity(b);

	// 4. (A, B) (7.14.3)

	if ((a == 2) && (f == g_conjunction_s)) {
		rc = xlate_pair(c, g_conjunction_s, b, b_ctx, s0, s0_ctx, s, s_ctx);
		c->depth--;
		return rc;
	}

	// 5/6. (A ; B) (7.14.4) and (A | B) (7.14.6), both emitted as ;

	if ((a == 2) && ((f == g_disjunction_s) || (f == g_bar_s))) {
		rc = xlate_alt(c, b, b_ctx, s0, s0_ctx, s, s_ctx);
		c->depth--;
		return rc;
	}

	// 12. (If -> Then) (7.14.12) at top level: an error, per the
	// reference's dcg_constr/1. Reached inside ; via xlate_alt, which
	// does not come through here. Section 5.1.

	if ((a == 2) && (f == g_if_then_s)) {
		set_ball_repr(c, b, b_ctx);
		c->depth--;
		return DCG_ERROR;
	}

	// 11. \+ G (7.14.11): an error, same source.

	if ((a == 1) && (f == g_negation_s)) {
		set_ball_repr(c, b, b_ctx);
		c->depth--;
		return DCG_ERROR;
	}

	// 7. {G} (7.14.7): contents never inspected, they go to call/1
	// unexpanded - which is why a non-callable inside {} gets the
	// whole-term culprit and is NOT our business (quads 10, 15, 37).

	if ((a == 1) && (f == g_braces_s)) {
		pl_ctx g_ctx;
		cell *g = dcg_deref(c, nth_arg(b, 0), b_ctx, &g_ctx);
		unsigned at = emit_open(c, g_conjunction_s, 2);

		if (c->oom || !emit_term(c, g, g_ctx)) {
			c->depth--;
			return DCG_ERROR;
		}

		rc = emit_unify(c, s0, s0_ctx, s, s_ctx);

		if (rc == DCG_OK)
			emit_close(c, at);

		c->depth--;
		return rc;
	}

	// 10. ! (7.14.10): (!, S0 = S)

	if ((a == 0) && (f == g_cut_s)) {
		unsigned at = emit_open(c, g_conjunction_s, 2);

		if (c->oom || !emit_atom(c, g_cut_s)) {
			c->depth--;
			return DCG_ERROR;
		}

		rc = emit_unify(c, s0, s0_ctx, s, s_ctx);

		if (rc == DCG_OK)
			emit_close(c, at);

		c->depth--;
		return rc;
	}

	// 8/9. call/1 (7.14.8) and phrase/1..3 (7.14.9+): arguments are NOT
	// checked through (quads 32, 45) - just append S0,S.

	if (((a == 1) && (f == g_call_s))
		|| ((f == g_phrase_s) && (a >= 1) && (a <= 3))) {
		unsigned at = emit_open(c, f, a + 2);

		if (c->oom) {
			c->depth--;
			return DCG_ERROR;
		}

		for (unsigned i = 0; i < a; i++) {
			pl_ctx arg_ctx;
			cell *arg = dcg_deref(c, nth_arg(b, i), b_ctx, &arg_ctx);

			if (!emit_term(c, arg, arg_ctx)) {
				c->depth--;
				return DCG_ERROR;
			}
		}

		if (!emit_term(c, s0, s0_ctx) || !emit_term(c, s, s_ctx)) {
			c->depth--;
			return DCG_ERROR;
		}

		emit_close(c, at);
		c->depth--;
		return DCG_OK;
	}

	// 13/14. M:Body and any other callable: an ordinary non-terminal.

	rc = xlate_nonterminal(c, b, b_ctx, s0, s0_ctx, s, s_ctx);
	c->depth--;
	return rc;
}

// --- moving the arena into the query heap ---
//
// dup_cells, not copy_cells, is what the design calls for when the
// source may be released first. Here the arena's references transfer
// wholesale, so a plain copy is right and the arena must NOT be
// unshared afterwards.

// A term synthesized cell-by-cell carries no builtin pointer and no
// operator specifier, so calling it raises existence_error even for
// something as ordinary as =/2. On the consult path process_clause()
// supplies both; on the runtime path nothing does, and the output of
// '$dcg_body'/4 is meant to be CALLED. =../2 does exactly this after
// building its term (bif_predicates.c) - same three steps, same order.
//
// The reference implementation never needed it: its goals were copies of
// cells from its own compiled clauses, which already carried the flags.

static void arena_resolve(dcg_ctx *c, query *q)
{
	for (unsigned i = 0; i < c->ar.len; i++) {
		cell *x = c->ar.buf + i;

		if (!is_interned(x) || !is_callable(x))
			continue;

		bool found = false;
		builtins *ptr = get_builtin_term(q->st.m, x, &found, NULL);

		if (found) {
			x->bif_ptr = ptr;

			if (ptr->evaluable)
				x->flags |= FLAG_INTERNED_EVALUABLE;
			else
				x->flags |= FLAG_INTERNED_BUILTIN;
		}

		unsigned specifier;

		if (!GET_OP(x) && search_op(q->st.m, C_STR(q, x), &specifier, get_arity(x) == 1)) {
			if ((get_arity(x) == 2) && IS_INFIX(specifier))
				SET_OP(x, specifier);
			else if ((get_arity(x) == 1) && IS_POSTFIX(specifier))
				SET_OP(x, specifier);
			else if ((get_arity(x) == 1) && IS_PREFIX(specifier))
				SET_OP(x, specifier);
		}
	}
}

static cell *arena_to_heap(dcg_ctx *c, query *q)
{
	arena_resolve(c, q);

	cell *dst = alloc_heap(q, c->ar.len);

	if (!dst)
		return NULL;

	copy_cells(dst, c->ar.buf, c->ar.len);
	TPL_free(c->ar.buf);
	c->ar.buf = NULL;
	c->ar.len = c->ar.cap = 0;
	return dst;
}

// Takes over the arena in every path: a caller that reaches here must
// NOT have released it, because the ball may be sitting in it.

static bool dcg_raise(query *q, dcg_ctx *c)
{
	if (c->oom) {
		arena_release(c);
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");
	}

	if (c->has_ball) {
		cell *ball = arena_to_heap(c, q);

		if (!ball)
			return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

		// Same idiom as throw/1: the ball travels as printed text.

		char *s = print_ball_to_strbuf(q, ball, q->st.cur_ctx);

		if (!s)
			return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

		q->did_throw = true;
		bool ok = find_exception_handler(q, s);
		TPL_free(s);
		return ok;
	}

	arena_release(c);

	if (!c->err_type)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

	return throw_error(q, (cell*)c->culprit, c->culprit_ctx, c->err_type, c->err_expected);
}

static void dcg_ctx_init(dcg_ctx *c, query *q)
{
	memset(c, 0, sizeof(*c));
	c->pl = q->pl;
	c->m = q->st.m;
	c->q = q;
	c->by_ref = true;
	c->v_ctx = q->st.cur_ctx;
	dcg_init_atoms(q->pl);
}

// '$dcg_body'(+Body, ?S0, ?S, -Goal)
//
// FAILS for a non-construct, so phrase/3 falls through to
// call(M:B,S0,S) and quad 2 keeps working. Throws only where ISO
// requires, including the section 5.3 type_error(callable, T).

static bool bif_dcg_body_4(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	GET_NEXT_ARG(p3,any);
	GET_NEXT_ARG(p4,var);

	if (is_var(p1))
		return false;

	if (!dcg_is_constr(p1))
		return false;

	dcg_ctx c;
	dcg_ctx_init(&c, q);
	dcg_rc rc = xlate_body(&c, p1, p1_ctx, p2, p2_ctx, p3, p3_ctx);

	if (rc == DCG_DECLINE) {
		arena_release(&c);
		return false;
	}

	if (rc != DCG_OK)
		return dcg_raise(q, &c);		// owns the arena

	cell *goal = arena_to_heap(&c, q);

	if (!goal) {
		arena_release(&c);
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");
	}

	return unify(q, p4, p4_ctx, goal, q->st.cur_ctx);
}

// '$dcg_rule'(+Rule, -Clause)
//
// Head shapes, per the reference's four dcg_rule/2 clauses:
//
//   H --> B            ->  H(S0,S) :- B'(S0,S)
//   H, PB --> B        ->  H(S0,S) :- B'(S0,S1), S = <PB ++ S1>
//   M:H --> B, and M:H, PB --> B
//
// Note the argument order in the pushback case: the reference has
// dcg_terminals(Terminals, S, S1, Goal2), i.e. S = <PB ++ S1>, not the
// other way round.

// Translate (Head --> Body) into (Head' :- Body') in the arena. Shared
// by both front-ends; on anything but DCG_OK the arena still belongs to
// the caller, which must release it or hand it to dcg_raise().
//
// Head shapes, per the reference's four dcg_rule/2 clauses:
//
//   H --> B            ->  H(S0,S) :- B'(S0,S)
//   H, PB --> B        ->  H(S0,S) :- B'(S0,S1), S = <PB ++ S1>
//   M:H --> B, and M:H, PB --> B
//
// Note the argument order in the pushback case: the reference has
// dcg_terminals(Terminals, S, S1, Goal2), i.e. S = <PB ++ S1>, not the
// other way round.

static dcg_rc xlate_rule(dcg_ctx *c, const cell *rule, pl_ctx rule_ctx, pl_ctx v_ctx)
{
	pl_ctx head_ctx, body_ctx;
	cell *head = dcg_deref(c, nth_arg(rule, 0), rule_ctx, &head_ctx);
	cell *body = dcg_deref(c, nth_arg(rule, 1), rule_ctx, &body_ctx);

	// Split an optional pushback list off the head.

	cell *pushback = NULL;
	pl_ctx pushback_ctx = 0;

	if (is_functor(head, g_conjunction_s, 2)) {
		pl_ctx nt_ctx;
		cell *nt = dcg_deref(c, nth_arg(head, 0), head_ctx, &nt_ctx);
		pushback = dcg_deref(c, nth_arg(head, 1), head_ctx, &pushback_ctx);
		head = nt;
		head_ctx = nt_ctx;
	}

	cell s0, s, s1;

	if (!new_var(c, &s0) || !new_var(c, &s))
		return DCG_ERROR;

	bool have_s1 = false;

	if (pushback) {
		if (!new_var(c, &s1))
			return DCG_ERROR;

		have_s1 = true;
	}

	unsigned neck = emit_open(c, g_neck_s, 2);
	dcg_rc rc = c->oom ? DCG_ERROR : xlate_nonterminal(c, head, head_ctx, &s0, v_ctx, &s, v_ctx);

	if (rc != DCG_OK)
		return rc;

	if (!have_s1) {
		rc = xlate_body(c, body, body_ctx, &s0, v_ctx, &s, v_ctx);
	} else {
		unsigned conj = emit_open(c, g_conjunction_s, 2);
		rc = c->oom ? DCG_ERROR : xlate_body(c, body, body_ctx, &s0, v_ctx, &s1, v_ctx);

		if (rc == DCG_OK) {
			unsigned eq = emit_open(c, g_unify_s, 2);

			if (c->oom || !emit_cell(c, &s))
				rc = DCG_ERROR;
			else {
				rc = emit_terminals(c, pushback, pushback_ctx, &s1, v_ctx, pushback, pushback_ctx);

				if (rc == DCG_OK) {
					emit_close(c, eq);
					emit_close(c, conj);
				}
			}
		}
	}

	if (rc != DCG_OK)
		return rc;

	emit_close(c, neck);
	return DCG_OK;
}

static bool bif_dcg_rule_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,var);

	if (is_var(p1))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "dcg_rule");

	if (!is_functor(p1, g_dcg_s, 2))
		return false;

	dcg_ctx c;
	dcg_ctx_init(&c, q);
	dcg_rc rc = xlate_rule(&c, p1, p1_ctx, c.v_ctx);

	if (rc != DCG_OK) {
		if (rc == DCG_DECLINE) {
			arena_release(&c);
			return false;
		}

		return dcg_raise(q, &c);	// owns the arena
	}

	cell *out = arena_to_heap(&c, q);

	if (!out) {
		arena_release(&c);
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");
	}

	return unify(q, p2, p2_ctx, out, q->st.cur_ctx);
}

// --- consult-time front end ------------------------------------------
//
// Replaces dcg_expansion(), which created a query, ran dcg_translate/2,
// printed the result canonically, spun up a fresh parser and re-tokenized
// it - per DCG clause. Everything in section 1.1's hazard table came from
// that round trip; none of it survives here.

static clause *arena_to_clause(dcg_ctx *c)
{
	// Headroom, so the first in-place growth downstream
	// (expand_meta_predicate, goal_expansion, insert_call_here) does not
	// immediately realloc. Correctness no longer depends on it - the two
	// stale-pointer bugs in parser.c that an exactly-sized clause used to
	// expose are fixed - but a parsed clause arrives with slack from
	// make_room()'s 3/2 growth and there is no reason to be stingier.

	const unsigned cap = c->ar.len + 64;
	clause *cl = TPL_calloc(1, sizeof(clause) + (sizeof(cell) * cap));

	if (!cl)
		return NULL;

	cl->num_allocated_cells = cap;
	cl->cidx = c->ar.len;

	// assign_vars() runs next and recomputes num_vars from scratch.

	cl->num_vars = 0;

	// A plain copy: the arena's references transfer wholesale, so the
	// arena must NOT be unshared afterwards. clear_clause() will release
	// exactly these cidx cells when the clause dies.

	copy_cells(cl->cells, c->ar.buf, c->ar.len);
	TPL_free(c->ar.buf);
	c->ar.buf = NULL;
	c->ar.len = c->ar.cap = 0;
	return cl;
}

bool dcg_expand_clause(parser *p)
{
	dcg_ctx c;
	memset(&c, 0, sizeof(c));
	c.pl = p->m->pl;
	c.m = p->m;
	c.p = p;
	c.by_ref = false;
	dcg_init_atoms(c.pl);

	dcg_rc rc = xlate_rule(&c, p->cl->cells, 0, c.v_ctx);

	if (rc != DCG_OK) {
		arena_release(&c);

		// The parser has no exception channel - that is section 1.1's
		// last row, and fixing it is not this phase's job. But it can at
		// least say what went wrong, where; dcg_expansion() set the flag
		// and said nothing at all.

		if (c.oom)
			fprintf(stderr, "Error: DCG translation out of memory, %s:%d\n",
				get_loaded(p->m, p->m->filename), p->line_num);
		else
			fprintf(stderr, "Error: %s in DCG rule, %s:%d\n",
				c.has_ball ? "representation_error(dcg_body) or type_error" :
				c.err_type ? c.err_type : "malformed DCG rule",
				get_loaded(p->m, p->m->filename), p->line_num);

		p->error_desc = "dcg_body";
		p->error = true;
		return false;
	}

	// term_to_body() computes cells->num_cells as cidx-1, i.e. it expects
	// a trailing TAG_END the way a tokenized clause has one.

	cell *end = arena_alloc(&c, 1);

	if (!end) {
		arena_release(&c);
		p->error_desc = "memory";
		p->error = true;
		return false;
	}

	make_end(end);

	// At THIS point in tokenize() - before assign_vars() - the root cell's
	// num_cells still counts the trailing TAG_END; the check just above
	// assign_vars rejects a clause where num_cells < cidx, and
	// term_to_body() subtracts the END later ("Drops TAG_END"). Insert
	// after term_to_body instead and the opposite convention applies.

	c.ar.buf[0].num_cells = c.ar.len;
	clause *cl = arena_to_clause(&c);

	if (!cl) {
		arena_release(&c);
		p->error_desc = "memory";
		p->error = true;
		return false;
	}

	clear_clause(p->cl);
	TPL_free(p->cl);
	p->cl = cl;
	return true;
}

// '$string_prefix'(+Str, ?Tail, ?S0)   -- S0 = Str ++ Tail
//
// Emitted instead of materialising a long string terminal into the
// clause. Section 6 chose to materialise, which is correct but stores
// two cells per character IN THE CLAUSE: 200 rules with a 4 KB literal
// cost 100 MB of RSS and 2.4s to consult, against 11.7 MB and 0.06s for
// a one-character literal.
//
// Consuming walks Str and S0 in lockstep. That is O(1) per character
// even for a huge S0, because list_tail() slices a string in place
// rather than copying (the same property section 6 verified for
// unify.c). Only the generating direction materialises, and then on the
// heap per call rather than permanently in the clause.

static bool sp_construct(query *q, cell *l, pl_ctx l_ctx, cell *tail, pl_ctx tail_ctx, cell **out)
{
	unsigned n = 0;

	{
		cell *p = l;
		PROLOG_LIST_HANDLER(p);

		while (is_list(p)) {
			n++;
			p = PROLOG_LIST_TAIL(p);
		}
	}

	cell *dst = alloc_heap(q, (n * 2) + tail->num_cells);

	if (!dst)
		return false;

	cell *w = dst;
	cell *p = l;
	PROLOG_LIST_HANDLER(p);

	while (is_list(p)) {
		cell *h = PROLOG_LIST_HEAD(p);
		make_struct(w, g_dot_s, 2, 0);
		w->num_cells = 1 + 1 + 0;	// patched below
		w++;
		*w = *h;
		share_cell(w);
		w++;
		p = PROLOG_LIST_TAIL(p);
	}

	dup_cells_by_ref(w, tail, tail_ctx, tail->num_cells);

	// Patch each cons cell's extent, innermost last: cell i spans
	// everything from itself to the end.

	cell *end = dst + (n * 2) + tail->num_cells;

	for (unsigned i = 0; i < n; i++) {
		cell *c = dst + (i * 2);
		c->num_cells = (unsigned)(end - c);
	}

	*out = dst;
	return true;
}

static bool bif_dcg_string_prefix_3(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	GET_NEXT_ARG(p3,any);

	cell *l = p1;
	pl_ctx l_ctx = p1_ctx;
	cell *s = p3;
	pl_ctx s_ctx = p3_ctx;
	PROLOG_LIST_HANDLER(l);
	PROLOG_LIST_HANDLER(s);

	while (is_list(l)) {
		if (is_var(s)) {
			// Ran out of bound input: build what is left plus the tail
			// and bind it. Covers both an unbound S0 and a partial one.

			cell *tmp = NULL;

			if (!sp_construct(q, l, l_ctx, p2, p2_ctx, &tmp))
				return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

			return unify(q, s, s_ctx, tmp, q->st.cur_ctx);
		}

		if (!is_list(s))
			return false;

		cell *lh = PROLOG_LIST_HEAD(l);
		cell *sh = PROLOG_LIST_HEAD(s);
		sh = deref(q, sh, s_ctx);
		pl_ctx sh_ctx = q->latest_ctx;

		if (!unify(q, lh, l_ctx, sh, sh_ctx))
			return false;

		l = PROLOG_LIST_TAIL(l);
		s = PROLOG_LIST_TAIL(s);
		s = deref(q, s, s_ctx);
		s_ctx = q->latest_ctx;
	}

	return unify(q, p2, p2_ctx, s, s_ctx);
}

builtins g_dcgs_bifs[] =
{
	{"$dcg_rule", 2, bif_dcg_rule_2, "+term,-term", false, false, BLAH},
	{"$dcg_body", 4, bif_dcg_body_4, "+term,?term,?term,-term", false, false, BLAH},
	{"$string_prefix", 3, bif_dcg_string_prefix_3, "+term,?term,?term", false, false, BLAH},

	{0}
};
