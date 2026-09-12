#include <stdlib.h>

#include "query.h"

static int compare_internal(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth);
static bool compound_pair_seen(query *q, cell *c1, pl_ctx ctx1, cell *c2, pl_ctx ctx2);

static int compare_lists(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	while (is_iso_list(p1) && is_iso_list(p2)) {
		if ((p1 == p2) && (p1_ctx == p2_ctx))
			return 0;

		if (compound_pair_seen(q, p1, p1_ctx, p2, p2_ctx))
			return 0;

		cell *c1 = p1 + 1, *c2 = p2 + 1;
		pl_ctx c1_ctx = p1_ctx, c2_ctx = p2_ctx;
		c1 = deref(q, c1, c1_ctx);
		c1_ctx = q->latest_ctx;
		c2 = deref(q, c2, c2_ctx);
		c2_ctx = q->latest_ctx;

		int val = compare_internal(q, c1, c1_ctx, c2, c2_ctx, depth+1);
		if (val) return val;

		p1 = p1 + 1; p1 += p1->num_cells;
		p2 = p2 + 1; p2 += p2->num_cells;
		p1 = deref(q, p1, p1_ctx);
		p1_ctx = q->latest_ctx;
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;
	}

	return compare_internal(q, p1, p1_ctx, p2, p2_ctx, depth+1);
}

static int compare_structs(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	int val = CMP_STRING_TO_STRING(q, p1, p2);
	if (val) return val;

	if ((p1 == p2) && (p1_ctx == p2_ctx))
		return 0;

	if (compound_pair_seen(q, p1, p1_ctx, p2, p2_ctx))
		return 0;

	uint32_t arity = get_arity(p1);
	p1 = p1 + 1;
	p2 = p2 + 1;

	while (arity--) {
		cell *c1 = p1, *c2 = p2;
		pl_ctx c1_ctx = p1_ctx, c2_ctx = p2_ctx;
		c1 = deref(q, c1, c1_ctx);
		c1_ctx = q->latest_ctx;
		c2 = deref(q, c2, c2_ctx);
		c2_ctx = q->latest_ctx;

		int val = compare_internal(q, c1, c1_ctx, c2, c2_ctx, depth+1);
		if (val) return val;

		p1 += p1->num_cells;
		p2 += p2->num_cells;
	}

	return 0;
}

static int compare_internal(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	if (is_var(p1)) {
		if (is_var(p2)) {
			if (p1_ctx < p2_ctx)
				return -1;

			if (p1_ctx > p2_ctx)
				return 1;

			return p1->var_num < p2->var_num ? -1 : p1->var_num > p2->var_num ? 1 : 0;
		}

		return -1;
	}

	if (is_var(p2))
		return 1;

	if (is_bigint(p1) && is_bigint(p2))
		return mp_int_compare(&p1->val_bigint->ival, &p2->val_bigint->ival);

	if (is_bigint(p1) && is_smallint(p2))
		return mp_int_compare_value(&p1->val_bigint->ival, p2->val_int);

	if (is_bigint(p1) && is_float(p2))
		return 1;

	if (is_bigint(p1))
		return 1;

	if (is_smallint(p1) && is_rational(p2))
		return -mp_rat_compare_value(&p2->val_bigint->irat, p1->val_int, 1);

	if (is_smallint(p1) && is_bigint(p2))
		return -mp_int_compare_value(&p2->val_bigint->ival, p1->val_int);

	if (is_smallint(p1)) {
		if (is_smallint(p2))
			return p1->val_int < p2->val_int ? -1 : p1->val_int > p2->val_int ? 1 : 0;

		if (is_float(p2))
			return 1;

		return -1;
	}

	if (is_rational(p1) && is_rational(p2))
		return mp_rat_compare(&p1->val_bigint->irat, &p2->val_bigint->irat);

	if (is_rational(p1) && is_bigint(p2)) {
		mpq_t tmp;
		mp_int_init_copy(&tmp.num, &p2->val_bigint->ival);
		mp_int_init_value(&tmp.den, 1);
		int ok = mp_rat_compare(&p1->val_bigint->irat, &tmp);
		mp_rat_clear(&tmp);
		return ok;
	}

	if (is_rational(p1) && is_smallint(p2))
		return mp_rat_compare_value(&p1->val_bigint->irat, p2->val_int, 1);

	if (is_rational(p1))
		return 1;

	if (is_bigint(p1) && is_rational(p2)) {
		mpq_t tmp;
		mp_int_init_copy(&tmp.num, &p1->val_bigint->ival);
		mp_int_init_value(&tmp.den, 1);
		int ok = mp_rat_compare(&p2->val_bigint->irat, &tmp);
		mp_rat_clear(&tmp);
		return ok;
	}

	if (is_float(p1)) {
		if (is_float(p2))
			return p1->val_float < p2->val_float ? -1 : p1->val_float > p2->val_float ? 1 : 0;

		return -1;
	}

	if (is_iso_atom(p1) && is_iso_atom(p2))
		return CMP_STRING_TO_STRING(q, p1, p2);

	if (is_codes(p1) && is_string(p2) && !is_codes(p2))
		return -1;

	if (is_codes(p2) && is_string(p1) && !is_codes(p1))
		return -1;

	// A byte string and a text string can hold identical bytes and still
	// be different lists, so the backing store may only be compared when
	// both are walked the same way. Otherwise fall through and go
	// element by element.

	if (is_string(p1) && is_string(p2) && (is_bytes(p1) == is_bytes(p2)))
		return CMP_STRING_TO_STRING(q, p1, p2);

	if (is_iso_atom(p1)) {
		if (is_number(p2))
			return 1;

		return -1;
	}

	if (get_arity(p1) < get_arity(p2))
		return -1;

	if (get_arity(p1) > get_arity(p2))
		return 1;

	if ((is_string(p1) && is_list(p2))
		|| (is_string(p2) && is_list(p1))) {
		PROLOG_LIST_HANDLER(p1);
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p1) && is_list(p2)) {
			cell *c1 = PROLOG_LIST_HEAD(p1);
			c1 = deref(q, c1, p1_ctx);
			pl_ctx c1_ctx = q->latest_ctx;
			cell *c2 = PROLOG_LIST_HEAD(p2);
			c2 = deref(q, c2, p2_ctx);
			pl_ctx c2_ctx = q->latest_ctx;

			int val = compare_internal(q, c1, c1_ctx, c2, c2_ctx, depth+1);
			if (val) return val;

			p1 = PROLOG_LIST_TAIL(p1);
			p1 = deref(q, p1, p1_ctx);
			p1_ctx = q->latest_ctx;
			p2 = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, p2, p2_ctx);
			p2_ctx = q->latest_ctx;
		}

		if (is_list(p1))
			return 1;

		if (is_list(p2))
			return -1;

		return compare_internal(q, p1, p1_ctx, p2, p2_ctx, depth+1);
	}

	if (is_iso_list(p1) && is_iso_list(p2))
		return compare_lists(q, p1, p1_ctx, p2, p2_ctx, depth+1);

	return compare_structs(q, p1, p1_ctx, p2, p2_ctx, depth+1);
}

int compare(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx)
{
	q->is_cyclic1 = q->is_cyclic2 = false;
	if (++q->vgen == 0) q->vgen = 1;
	q->unify_seen_used = 0;
	return compare_internal(q, p1, p1_ctx, p2, p2_ctx, 0);
}

static bool set_var(query *q, const cell *c, pl_ctx c_ctx, cell *v, pl_ctx v_ctx)
{
	const frame *f = GET_FRAME(c_ctx);
	slot *e = get_slot(q, f, c->var_num);
	cell *c_attrs = e->c.val_attrs;

	if (is_managed(v) || (c_ctx != q->st.fp)) {
		if (!add_trail(q, c_ctx, c->var_num, c_attrs))
			return false;
	}

	if (c_attrs)
		q->run_hook = true;

	if (is_var(v)) {
		make_ref(&e->c, v->var_num, v_ctx);

		if ((c_ctx == q->st.fp)
			&& (c_ctx != v_ctx)
			&& !is_temporary(c) && !is_void(c)
			) {
			q->no_recov = true;
			q->total_no_recovs++;
		}
	} else if (is_compound(v)) {
		make_indirect(&e->c, v, v_ctx);

		if ((v_ctx >= q->st.cur_ctx)
			&& (c_ctx != v_ctx)
			&& !is_ground(v)
			){
			q->no_recov = true;
			q->total_no_recovs++;
		}

		if ((c_ctx < q->st.cur_ctx) && !is_ground(v)) {
			frame *fc = GET_CURR_FRAME();
			fc->no_recov = true;
			fc->heap_pinned = true;
			q->no_recov = true;
			q->total_no_recovs++;
		}
	} else {
		e->c = *v;
		share_cell(v);
	}

	return true;
}

void reset_var(query *q, const cell *c, pl_ctx c_ctx, cell *v, pl_ctx v_ctx)
{
	const frame *f = GET_FRAME(c_ctx);
	slot *e = get_slot(q, f, c->var_num);
	share_cell(v);
	unshare_cell(&e->c);
	e->c = *v;
}

void undo_var(query *q, const cell *c, pl_ctx c_ctx)
{
	const frame *f = GET_FRAME(c_ctx);
	slot *e = get_slot(q, f, c->var_num);
	unshare_cell(&e->c);
	e->c.tag = TAG_EMPTY;
	e->c.val_attrs = NULL;
	pop_trail(q);
}

static bool unify_internal(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth);

static bool unify_string_to_list(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	PROLOG_LIST_HANDLER(p1);
	PROLOG_LIST_HANDLER(p2);

	while (is_list(p1) && is_list(p2)) {
		cell *c1 = PROLOG_LIST_HEAD(p1);
		cell *c2 = PROLOG_LIST_HEAD(p2);

		pl_ctx c1_ctx = p1_ctx;
		c2 = deref(q, c2, p2_ctx);
		pl_ctx c2_ctx = q->latest_ctx;

		if (!unify_internal(q, c1, c1_ctx, c2, c2_ctx, 0))
			return false;

		c1 = PROLOG_LIST_TAIL(p1);
		c2 = PROLOG_LIST_TAIL(p2);

		p1 = c1;
		p2 = deref(q, c2, p2_ctx);
		p2_ctx = q->latest_ctx;
	}

	return unify_internal(q, p1, p1_ctx, p2, p2_ctx, 0);
}

static bool unify_integers(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	if (is_bigint(p1)) {
		if (is_rational(p2)) {
			mpq_t tmp;
			mp_int_init_copy(&tmp.num, &p1->val_bigint->ival);
			mp_int_init_value(&tmp.den, 1);
			bool ok = !mp_rat_compare(&p2->val_bigint->irat, &tmp);
			mp_rat_clear(&tmp);
			return ok;
		}

		if (is_bigint(p2))
			return !mp_int_compare(&p1->val_bigint->ival, &p2->val_bigint->ival);

		if (is_smallint(p2))
			return !mp_int_compare_value(&p1->val_bigint->ival, p2->val_int);

		return false;
	}

	if (is_rational(p2))
		return !mp_int_compare_value(&p2->val_bigint->ival, p1->val_int);

	if (is_bigint(p2))
		return !mp_int_compare_value(&p2->val_bigint->ival, p1->val_int);

	if (p2->tag == TAG_INT)
		return p1->val_int == p2->val_int;

	return false;
}

static bool unify_rationals(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	if (is_rational(p1) && is_rational(p2))
		return !mp_rat_compare(&p1->val_bigint->irat, &p2->val_bigint->irat);

	if (is_rational(p1) && is_bigint(p2)) {
		mpq_t tmp;
		mp_int_init_copy(&tmp.num, &p2->val_bigint->ival);
		mp_int_init_value(&tmp.den, 1);
		bool ok = !mp_rat_compare(&p1->val_bigint->irat, &tmp);
		mp_rat_clear(&tmp);
		return ok;
	}

	if (is_rational(p1) && is_integer(p2))
		return !mp_rat_compare_value(&p1->val_bigint->irat, p2->val_int, 1);

	return false;
}

static bool unify_floats(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	if (is_float(p2))
		return p1->val_float == p2->val_float;

	return false;
}

static bool unify_cstrings(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	if (is_cstring(p2) && (C_STRLEN(q, p1) == C_STRLEN(q, p2)))
		return !memcmp(C_STR(q, p1), C_STR(q, p2), C_STRLEN(q, p1));

	if (is_interned(p2) && (C_STRLEN(q, p1) == C_STRLEN(q, p2)))
		return !memcmp(C_STR(q, p1), GET_POOL(q, p2->val_off), C_STRLEN(q, p1));

	return false;
}

// Probe for (c1,ctx1)/(c2,ctx2). Returns the slot to use and sets
// *found. Entries from an earlier compare/unify operation have a stale
// generation and read as empty, which is safe with linear probing: an
// entry inserted this generation can never sit behind a stale slot in
// its own probe chain, because insertion would have stopped there.
//
// Terminates because the caller keeps at least half the table free.

static unsigned unify_seen_slot(const unify_seen_pair *tab, unsigned size,
	cell *c1, pl_ctx ctx1, cell *c2, pl_ctx ctx2, uint32_t gen, bool *found)
{
	uintptr_t h = ((uintptr_t)c1 >> 3) ^ ((uintptr_t)c2 >> 2)
		^ ((uintptr_t)ctx1 << 9) ^ (uintptr_t)ctx2;
	unsigned i = (unsigned)(h & (size - 1));

	while (tab[i].gen == gen) {
		if ((tab[i].c1 == c1) && (tab[i].c2 == c2)
			&& (tab[i].ctx1 == ctx1) && (tab[i].ctx2 == ctx2)) {
			*found = true;
			return i;
		}

		i = (i + 1) & (size - 1);
	}

	*found = false;
	return i;
}

// Allocate, or double and rehash the live entries. False on OOM, where
// the caller skips the memo: slower, never wrong.

static bool unify_seen_grow(query *q)
{
	unsigned newsize = q->unify_seen_size ? q->unify_seen_size * 2 : UNIFY_SEEN_SIZE;
	unify_seen_pair *tab = TPL_calloc(newsize, sizeof(unify_seen_pair));

	if (!tab)
		return false;

	for (unsigned i = 0; i < q->unify_seen_size; i++) {
		unify_seen_pair *p = &q->unify_seen[i];

		if (p->gen != q->vgen)
			continue;

		bool found = false;
		unsigned j = unify_seen_slot(tab, newsize, p->c1, p->ctx1, p->c2, p->ctx2, q->vgen, &found);
		tab[j] = *p;
	}

	TPL_free(q->unify_seen);
	q->unify_seen = tab;
	q->unify_seen_size = newsize;
	return true;
}

// True if this compound pair was already visited by the current
// compare/unify operation; otherwise record it.

static bool compound_pair_seen(query *q, cell *c1, pl_ctx ctx1, cell *c2, pl_ctx ctx2)
{
	if (((q->unify_seen_used + 1) * 2) > q->unify_seen_size) {
		if (!unify_seen_grow(q))
			return false;
	}

	bool found = false;
	unsigned i = unify_seen_slot(q->unify_seen, q->unify_seen_size, c1, ctx1, c2, ctx2, q->vgen, &found);

	if (found)
		return true;

	unify_seen_pair *p = &q->unify_seen[i];
	p->c1 = c1;
	p->c2 = c2;
	p->ctx1 = ctx1;
	p->ctx2 = ctx2;
	p->gen = q->vgen;
	q->unify_seen_used++;
	return false;
}

static bool unify_lists(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	if ((p1 == p2) && (p1_ctx == p2_ctx))
		return true;

	if (compound_pair_seen(q, p1, p1_ctx, p2, p2_ctx))
		return true;

	bool any1 = false, any2 = false;

	while (is_iso_list(p1) && is_iso_list(p2)) {
		cell *c1 = p1 + 1, *c2 = p2 + 1;
		pl_ctx c1_ctx = p1_ctx, c2_ctx = p2_ctx;
		slot *e1 = NULL, *e2 = NULL;
		uint32_t save_vgen, save_vgen2;
		int both = 0;

		DEREF_VAR(any1, both, save_vgen, e1, e1->vgen, c1, c1_ctx, q->vgen);
		DEREF_VAR(any1, both, save_vgen2, e2, e2->vgen2, c2, c2_ctx, q->vgen);

		if (both != 2) {
			if (!unify_internal(q, c1, c1_ctx, c2, c2_ctx, depth+1))
				return false;
		}

		if (e1) e1->vgen = save_vgen;
		if (e2) e2->vgen2 = save_vgen2;
		p1 = p1 + 1; p1 += p1->num_cells;
		p2 = p2 + 1; p2 += p2->num_cells;
		e1 = e2 = NULL;
		int both1 = 0, both2 = 0;

		DEREF_VAR(any2, both1, save_vgen, e1, e1->vgen, p1, p1_ctx, q->vgen);
		DEREF_VAR(any2, both2, save_vgen2, e2, e2->vgen2, p2, p2_ctx, q->vgen);

		if (both1)
			q->is_cyclic1++;

		if (both2)
			q->is_cyclic2++;

		if (q->is_cyclic1 && q->is_cyclic2)
			break;
	}

	return unify_internal(q, p1, p1_ctx, p2, p2_ctx, depth+1);
}

static bool unify_structs(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	if (get_arity(p1) != get_arity(p2))
		return false;

	if (p1->val_off != p2->val_off)
		return false;

	if ((p1 == p2) && (p1_ctx == p2_ctx))
		return true;

	if (compound_pair_seen(q, p1, p1_ctx, p2, p2_ctx))
		return true;

	uint32_t arity = get_arity(p1);
	p1++; p2++;

	while (arity--) {
		pl_ctx c1_ctx = p1_ctx, c2_ctx = p2_ctx;
		cell *c1 = p1, *c2 = p2;
		slot *e1 = NULL, *e2 = NULL;
		uint32_t save_vgen, save_vgen2;
		bool any = false;
		int both = 0;

		DEREF_VAR(any, both, save_vgen, e1, e1->vgen, c1, c1_ctx, q->vgen);
		DEREF_VAR(any, both, save_vgen2, e2, e2->vgen2, c2, c2_ctx, q->vgen);

		if (both != 2) {
			if (!unify_internal(q, c1, c1_ctx, c2, c2_ctx, depth+1))
				return false;
		}

		if (e1) e1->vgen = save_vgen;
		if (e2) e2->vgen2 = save_vgen2;
		p1 += p1->num_cells;
		p2 += p2->num_cells;
	}

	return true;
}

static bool unify_var(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	bool occurs = q->flags.occurs_check != OCCURS_CHECK_FALSE;
	bool check = !is_local(p1) && (occurs || (q->sto_watch && !q->sto_seen));
	bool was_cyclic = check && is_cyclic_term(q, p2, p2_ctx);

	if (!set_var(q, p1, p1_ctx, p2, p2_ctx))
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

	if (check && !was_cyclic && is_cyclic_term(q, p2, p2_ctx)) {
		if (q->sto_watch)
			q->sto_seen = true;

		if (occurs) {
			q->cycle_error = 1;
			return false;
		}
	}

	return true;
}

static bool unify_interned(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	if (is_iso_list(p1) && is_iso_list(p2))
		return unify_lists(q, p1, p1_ctx, p2, p2_ctx, depth);

	if (get_arity(p1) || get_arity(p2))
		return unify_structs(q, p1, p1_ctx, p2, p2_ctx, depth+1);

	if (is_interned(p2))
		return p1->val_off == p2->val_off;

	if (is_cstring(p2) && (C_STRLEN(q, p1) == C_STRLEN(q, p2)))
		return !memcmp(C_STR(q, p2), GET_POOL(q, p1->val_off), C_STRLEN(q, p1));

	return false;
}

struct dispatch {
	uint8_t tag;
	bool (*fn)(query*, cell*, pl_ctx, cell*, pl_ctx, unsigned);
};

static bool unify_blobs(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	(void)q; (void)p1_ctx; (void)p2_ctx; (void)depth;
	return is_blob(p2) && (p1->val_blob == p2->val_blob);
}

// deref() resolves indirects, so unify_internal() should never see one.
// Present so the table covers every tag rather than reading past its
// end - see the note on g_disp below.

static bool unify_indirects(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	(void)q; (void)p1; (void)p1_ctx; (void)p2; (void)p2_ctx; (void)depth;
	return false;
}

static const struct dispatch g_disp[] =
{
	{TAG_EMPTY, NULL},
	{TAG_VAR, NULL},
	{TAG_INTERNED, unify_interned},
	{TAG_CSTR, unify_cstrings},
	{TAG_INT, unify_integers},
	{TAG_FLOAT, unify_floats},
	{TAG_RATIONAL, unify_rationals},
	{TAG_INDIRECT, unify_indirects},
	{TAG_BLOB, unify_blobs},
	{0}
};

static bool unify_internal(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, unsigned depth)
{
	if (is_var(p1) && is_var(p2)) {
		// Var-var with exactly one side attributed: bind the plain var
		// to the attributed one so the attributes remain visible on the
		// surviving root (SICStus attvar semantics - no hook fires for
		// attvar/plain-var unification). If the attributed var lives in
		// a younger frame, pin that frame against recovery.
		const slot *e1 = get_slot(q, GET_FRAME(p1_ctx), p1->var_num);
		const slot *e2 = get_slot(q, GET_FRAME(p2_ctx), p2->var_num);
		bool a1 = !is_ref(&e1->c) && e1->c.val_attrs != NULL;
		bool a2 = !is_ref(&e2->c) && e2->c.val_attrs != NULL;

		if (a1 && !a2) {
			if (!set_var(q, p2, p2_ctx, p1, p1_ctx))
				return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

			if (p1_ctx > p2_ctx) {
				frame *fa = GET_FRAME(p1_ctx);
				fa->no_recov = true;
			}

			return true;
		}

		if (a2 && !a1) {
			if (!set_var(q, p1, p1_ctx, p2, p2_ctx))
				return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

			if (p2_ctx > p1_ctx) {
				frame *fa = GET_FRAME(p2_ctx);
				fa->no_recov = true;
			}

			return true;
		}

		bool ok;

		if (p2_ctx > p1_ctx)
			ok = set_var(q, p2, p2_ctx, p1, p1_ctx);
		else if (p2_ctx < p1_ctx)
			ok = set_var(q, p1, p1_ctx, p2, p2_ctx);
		else if (p2->var_num > p1->var_num)
			ok = set_var(q, p2, p2_ctx, p1, p1_ctx);
		else if (p2->var_num < p1->var_num)
			ok = set_var(q, p1, p1_ctx, p2, p2_ctx);
		else
			ok = true;

		if (!ok)
			return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

		return true;
	}

	if (is_var(p2)) {
		return unify_var(q, p2, p2_ctx, p1, p1_ctx, depth);
	} else if (is_var(p1)) {
		if (depth > 1)
			q->has_vars = true;

		return unify_var(q, p1, p1_ctx, p2, p2_ctx, depth);
	}

	if (is_codes(p1) && is_string(p2) && !is_codes(p2))
		return false;

	if (is_codes(p2) && is_string(p1) && !is_codes(p1))
		return false;

	if (is_string(p1)) {
		if (is_string(p2)) {
			if (is_bytes(p1) == is_bytes(p2))
				return unify_cstrings(q, p1, p1_ctx, p2, p2_ctx, depth);

			return unify_string_to_list(q, p1, p1_ctx, p2, p2_ctx, depth);
		}

		if (is_iso_list(p2))
			return unify_string_to_list(q, p1, p1_ctx, p2, p2_ctx, depth);

		return false;
	}

	if (is_string(p2)) {
		if (is_iso_list(p1))
			return unify_string_to_list(q, p2, p2_ctx, p1, p1_ctx, depth);

		return false;
	}

	if ((q->is_cyclic1 || q->is_cyclic2)) {
		if (depth > 30) {
			//printf("*** OOPS %s %d\n", __FILE__, __LINE__);
			q->cycle_error++;
			return true;
		}
	} else if (depth > MAX_VARS) {
		//printf("*** OOPS %s %d\n", __FILE__, __LINE__);
		q->cycle_error++;
		return true;
	}

	return g_disp[p1->tag].fn(q, p1, p1_ctx, p2, p2_ctx, depth);
}

bool unify(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx)
{
	q->is_cyclic1 = q->is_cyclic2 = false;
	q->has_vars = q->no_recov = false;
	q->run_hook = false;
	q->before_hook_tp = q->st.tp;
	if (++q->vgen == 0) q->vgen = 1;

	// The memo is keyed by vgen, so every entry is invalidated by the
	// bump above and the table is reused as-is; only the occupancy
	// count has to reset, and doing it here keeps that branch out of
	// compound_pair_seen()'s per-pair path.

	q->unify_seen_used = 0;
	bool ok;

	if (!is_var(p1) && is_var(p2))
		ok = unify_internal(q, p2, p2_ctx, p1, p1_ctx, 0);
	else
		ok = unify_internal(q, p1, p1_ctx, p2, p2_ctx, 0);

	if (q->cycle_error) {
		if (q->flags.occurs_check == OCCURS_CHECK_ERROR) {
			q->run_hook = false;
			return throw_error(q, p2, p2_ctx, "representation_error", "term");
		}
	}

	if (!ok) {
		q->run_hook = false;
		return false;
	}

	return true;
}
