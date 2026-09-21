#include <stdlib.h>
#include <stdio.h>

#include "module.h"
#include "query.h"

#define DO_DUMP 0

#define DUMP_TERM2(s,k,c,c_ctx,running) { \
	fprintf(stderr, "%s %s ", s, k); \
	q->nl = true; q->quoted = true; \
	print_term(q, stderr, c, c_ctx, running); \
	q->nl = false; q->quoted = false; \
}


// Attributed-variable support for the blackboard, done natively.
//
// The raw '$bb_put' copy drops variable attributes (they live in frame
// slots, not in the term). To let attributed terms survive a round-trip
// we store an augmented image '$bb'('$bb_attv'(T, [V-Attrs|...])) where
// the pair list records, for every attributed variable reachable from T
// (transitively, including through attribute values - attvar graphs may
// be cyclic through attributes), the raw slot attribute list. On
// '$bb_get' the attributes are re-attached directly to the freshly
// imported variables' slots. No Prolog-level residualization runs, and
// no cooperation from attribute_goals//1 hooks is needed.

static pl_idx g_bb_attv_s = 0;

typedef struct {
	pl_ctx ctx;
	unsigned var_num;
	cell *attrs;
} bb_attv_ent;

typedef struct {
	cell *c;
	pl_ctx ctx;
} bb_blk_ent;

typedef struct {
	bb_attv_ent *ents;
	unsigned num_ents, max_ents;
	bb_blk_ent *blks;
	unsigned num_blks, done_blks, max_blks;
	const cell **seen_blks;
	unsigned num_seen, max_seen;
} bb_attv_scan;

static bool bb_scan_push_blk(bb_attv_scan *sc, cell *c, pl_ctx ctx)
{
	for (unsigned i = 0; i < sc->num_seen; i++) {
		if (sc->seen_blks[i] == c)
			return true;
	}

	if (sc->num_seen >= sc->max_seen) {
		sc->max_seen = sc->max_seen ? sc->max_seen*2 : 64;
		sc->seen_blks = TPL_realloc(sc->seen_blks, sizeof(cell*)*sc->max_seen);
		if (!sc->seen_blks) return false;
	}

	sc->seen_blks[sc->num_seen++] = c;

	if (sc->num_blks >= sc->max_blks) {
		sc->max_blks = sc->max_blks ? sc->max_blks*2 : 64;
		sc->blks = TPL_realloc(sc->blks, sizeof(bb_blk_ent)*sc->max_blks);
		if (!sc->blks) return false;
	}

	sc->blks[sc->num_blks].c = c;
	sc->blks[sc->num_blks].ctx = ctx;
	sc->num_blks++;
	return true;
}

// Collect every attributed variable reachable from (T,T_ctx), including
// those reachable only through other variables' attribute values.

static bool bb_collect_attv(query *q, cell *T, pl_ctx T_ctx, bb_attv_scan *sc)
{
	memset(sc, 0, sizeof(*sc));

	if (!bb_scan_push_blk(sc, T, T_ctx))
		return false;

	while (sc->done_blks < sc->num_blks) {
		bb_blk_ent blk = sc->blks[sc->done_blks++];
		cell *c = blk.c;

		for (pl_idx i = 0; i < blk.c->num_cells; i++, c++) {
			if (!is_var(c))
				continue;

			cell *d = deref(q, c, blk.ctx);
			pl_ctx d_ctx = q->latest_ctx;

			if (!is_var(d)) {
				if (is_compound(d) && !bb_scan_push_blk(sc, d, d_ctx))
					return false;

				continue;
			}

			const frame *f = GET_FRAME(d_ctx);
			const slot *e = get_slot(q, f, d->var_num);
			cell *attrs = e->c.val_attrs;

			if (!attrs)
				continue;

			bool found = false;

			for (unsigned j = 0; j < sc->num_ents; j++) {
				if ((sc->ents[j].ctx == d_ctx) && (sc->ents[j].var_num == d->var_num)) {
					found = true;
					break;
				}
			}

			if (found)
				continue;

			if (sc->num_ents >= sc->max_ents) {
				sc->max_ents = sc->max_ents ? sc->max_ents*2 : 64;
				sc->ents = TPL_realloc(sc->ents, sizeof(bb_attv_ent)*sc->max_ents);
				if (!sc->ents) return false;
			}

			sc->ents[sc->num_ents].ctx = d_ctx;
			sc->ents[sc->num_ents].var_num = d->var_num;
			sc->ents[sc->num_ents].attrs = attrs;
			sc->num_ents++;

			if (!bb_scan_push_blk(sc, attrs, d_ctx))
				return false;
		}
	}

	return true;
}

static void bb_scan_free(bb_attv_scan *sc)
{
	if (sc->ents) TPL_free(sc->ents);
	if (sc->blks) TPL_free(sc->blks);
	if (sc->seen_blks) TPL_free(sc->seen_blks);
}

// Build the by-ref source image '$bb'('$bb_attv'(T, [V-Attrs|...])) in a
// malloc'd scratch buffer, ready for one consistent copy_term_to_tmp.
// Returns the buffer (caller frees) or NULL.

static cell *bb_build_attv_image(query *q, cell *bb_wrapper, cell *T, pl_ctx T_ctx, bb_attv_scan *sc)
{
	unsigned n = sc->num_ents;
	pl_idx list_cells = 1;			// trailing nil

	for (unsigned i = 0; i < n; i++)
		list_cells += 3 + sc->ents[i].attrs->num_cells; // dot + minus + var + attrs

	pl_idx total = 2 + T->num_cells + list_cells;
	cell *S = TPL_malloc(sizeof(cell)*total);
	if (!S) return NULL;

	pl_idx idx = 0;
	S[idx] = *bb_wrapper;			// '$bb'/1 header
	S[idx].num_cells = total;
	idx++;

	if (!g_bb_attv_s)
		g_bb_attv_s = new_atom(q->pl, "$bb_attv");

	make_instr(S+idx, g_bb_attv_s, NULL, 2, total-2);
	idx++;

	idx += copy_cells_by_ref(S+idx, T, T_ctx, T->num_cells);

	// Remaining sizes from each pair onward, for the dot num_cells.
	pl_idx rest = list_cells;

	for (unsigned i = 0; i < n; i++) {
		pl_idx pair_sz = 2 + sc->ents[i].attrs->num_cells; // minus + var + attrs
		make_instr(S+idx, g_dot_s, NULL, 2, rest-1);
		idx++;
		make_instr(S+idx, g_minus_s, NULL, 2, pair_sz-1);
		idx++;
		make_ref(S+idx, sc->ents[i].var_num, sc->ents[i].ctx);
		idx++;
		idx += copy_cells_by_ref(S+idx, sc->ents[i].attrs, sc->ents[i].ctx, sc->ents[i].attrs->num_cells);
		rest -= 1 + pair_sz;
	}

	make_atom(S+idx, g_nil_s);
	idx++;
	(void)idx;
	return S;
}

// On retrieval of an augmented image, re-attach the recorded attributes
// to the freshly imported variables and return a plain '$bb'(T) view.

static cell *bb_reattach_attv(query *q, cell *tmp)
{
	if (!g_bb_attv_s)
		g_bb_attv_s = new_atom(q->pl, "$bb_attv");

	cell *inner = tmp+1;

	if (!is_interned(inner) || (inner->val_off != g_bb_attv_s) || (get_arity(inner) != 2))
		return tmp;

	cell *T = inner+1;
	cell *lst = T + T->num_cells;

	while (is_interned(lst) && (lst->val_off == g_dot_s) && (get_arity(lst) == 2)) {
		cell *pair = lst+1;		// '-'(V, Attrs)
		cell *v = pair+1;
		cell *attrs = v+1;
		cell *d = deref(q, v, q->st.cur_ctx);
		pl_ctx d_ctx = q->latest_ctx;

		if (is_var(d)) {
			const frame *f = GET_FRAME(d_ctx);
			slot *e = get_slot(q, f, d->var_num);

			if (!e->c.val_attrs) {
				// The imported cells were stripped of their ref flags by
				// rebase_term. Attribute cells outlive this call and get
				// read from other frames, so they must carry an explicit
				// context again.
				cell *ac = attrs;

				for (pl_idx j = 0; j < attrs->num_cells; j++, ac++) {
					if (is_var(ac) && !is_ref(ac)) {
						ac->flags |= FLAG_VAR_REF;
						ac->val_ctx = q->st.cur_ctx;
					}
				}

				if (!add_trail(q, d_ctx, d->var_num, e->c.val_attrs))
					return NULL;
				e->c.val_attrs = attrs;
				q->attrs_used = true;
			}
		}

		lst = pair + pair->num_cells;
	}

	cell *tmp2 = alloc_heap(q, 1 + T->num_cells);
	if (!tmp2) return NULL;
	*tmp2 = *tmp;
	tmp2->num_cells = 1 + T->num_cells;
	dup_cells_by_ref(tmp2+1, T, q->st.cur_ctx, T->num_cells);
	return tmp2;
}

// Format the blackboard key module:key (with a :b suffix for the
// backtrackable variant) into buf. Returns false if it would not fit,
// so the caller can throw rather than silently truncate a key long
// enough to collide with another.

static bool bb_make_key(query *q, char *buf, size_t bufsz, const char *mod, cell *key, bool backtrackable)
{
	int n;

	if (is_atom(key))
		n = snprintf(buf, bufsz, backtrackable ? "%s:%s:b" : "%s:%s", mod, C_STR(q, key));
	else
		n = snprintf(buf, bufsz, backtrackable ? "%s:%d:b" : "%s:%d", mod, (int)get_smallint(key));

	return (n > 0) && ((size_t)n < bufsz);
}

static bool bif_bb_b_put_2(query *q)
{
	GET_FIRST_ARG(p1,nonvar);
	GET_NEXT_ARG(p2,any);

	if (is_compound(p1) &&
		((p1->val_off != g_colon_s) || (get_arity(p1) != 2)))
		return throw_error(q, p1, p1_ctx, "type_error", "callable");

	module *m;
	char tmpbuf[1024];

	if (is_compound(p1)) {
		cell *p1_m = p1 + 1;
		p1 = p1_m + p1_m->num_cells;

		if (!is_atom(p1_m) || !is_smallint_or_atom(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		m = find_module(q->pl, C_STR(q, p1_m));

		if (!m)
			return throw_error(q, p1_m, p1_ctx, "existence_error", "module");
	} else
		m = q->pl->global_bb ? q->pl->user_m : q->st.m;

	if (!bb_make_key(q, tmpbuf, sizeof(tmpbuf), m->name, p1, true))
		return throw_error(q, p1, p1_ctx, "representation_error", "bb_key");

	if (DO_DUMP) DUMP_TERM2("bb_b_put", tmpbuf, p2, p2_ctx, 1);

	CHECKED(init_tmp_heap(q));
	cell *tmp = clone_term_to_tmp(q, p2, p2_ctx);
	CHECKED(tmp);
	// The entry outlives the mapping any slice in it points into.

	if (!unslice_cells(tmp, tmp->num_cells))
		return throw_error(q, tmp, q->st.cur_ctx, "resource_error", "memory");

	pl_idx num_cells = tmp->num_cells;
	cell *val = TPL_malloc(sizeof(cell)*num_cells);
	CHECKED(val);
	dup_cells(val, tmp, tmp->num_cells);
	val->flags |= FLAG_LIVE;
	char *key = TPL_strdup(tmpbuf);
	CHECKED(key);
	CHECKED(undo_on_backtrack(q, key, UNDO_BBOARD));

	prolog_lock(q->pl);
	sl_set(m->keyval, key, val);
	prolog_unlock(q->pl);

	return true;
}

static bool bif_bb_put_2(query *q)
{
	GET_FIRST_ARG(p1,nonvar);
	GET_NEXT_ARG(p2,any);

	if (is_compound(p1) &&
		((p1->val_off != g_colon_s) || (get_arity(p1) != 2)))
		return throw_error(q, p1, p1_ctx, "type_error", "callable");

	module *m;
	char tmpbuf1[1024], tmpbuf2[1024];

	if (is_compound(p1)) {
		cell *p1_m = p1 + 1;
		p1 = p1_m + p1_m->num_cells;

		if (!is_atom(p1_m) || !is_smallint_or_atom(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		m = find_module(q->pl, C_STR(q, p1_m));

		if (!m)
			return throw_error(q, p1_m, p1_ctx, "existence_error", "module");
	} else
		m = q->pl->global_bb ? q->pl->user_m : q->st.m;

	if (!bb_make_key(q, tmpbuf1, sizeof(tmpbuf1), m->name, p1, true))
		return throw_error(q, p1, p1_ctx, "representation_error", "bb_key");

	const char *key1 = tmpbuf1;

	if (!bb_make_key(q, tmpbuf2, sizeof(tmpbuf2), m->name, p1, false))
		return throw_error(q, p1, p1_ctx, "representation_error", "bb_key");

	if (DO_DUMP) DUMP_TERM2("bb_put", tmpbuf2, p2, p2_ctx, 1);

	char *key2 = TPL_strdup(tmpbuf2);

	// If the term contains attributed variables, store an augmented
	// image carrying the raw attributes so they survive the round-trip.

	cell *scratch = NULL;
	cell *src = p2;
	pl_ctx src_ctx = p2_ctx;

	if (is_compound(p2) && (get_arity(p2) == 1)) {
		bb_attv_scan sc;

		if (bb_collect_attv(q, p2+1, p2_ctx, &sc) && sc.num_ents) {
			scratch = bb_build_attv_image(q, p2, p2+1, p2_ctx, &sc);

			if (scratch) {
				src = scratch;
				src_ctx = q->st.cur_ctx;
			}
		}

		bb_scan_free(&sc);
	}

	CHECKED(init_tmp_heap(q));
	cell *tmp = copy_term_to_tmp(q, src, src_ctx, false);
	if (scratch) TPL_free(scratch);
	CHECKED(tmp);
	// The entry outlives the mapping any slice in it points into.

	if (!unslice_cells(tmp, tmp->num_cells))
		return throw_error(q, tmp, q->st.cur_ctx, "resource_error", "memory");

	pl_idx num_cells = tmp->num_cells;
	cell *val = TPL_malloc(sizeof(cell)*num_cells);
	CHECKED(val);
	dup_cells(val, tmp, tmp->num_cells);

	prolog_lock(q->pl);

	while (sl_del(m->keyval, key1))
		;

	while (sl_del(m->keyval, key2))
		;

	sl_app(m->keyval, key2, val);
	prolog_unlock(q->pl);

	return true;
}

static cell *bb_import_term_to_heap(query *q, cell *c, pl_ctx c_ctx)
{
	cell *tmp = alloc_heap(q, c->num_cells);
	if (!tmp) return NULL;
	dup_cells_by_ref(tmp, c, c_ctx, c->num_cells);
	return tmp;
}

static bool bif_bb_get_2(query *q)
{
	GET_FIRST_ARG(p1,nonvar);

	if (is_compound(p1) &&
		((p1->val_off != g_colon_s) || (get_arity(p1) != 2)))
		return throw_error(q, p1, p1_ctx, "type_error", "callable");

	module *m;
	char tmpbuf[1024];

	if (is_compound(p1)) {
		cell *p1_m = p1 + 1;
		p1 = p1_m + p1_m->num_cells;

		if (!is_atom(p1_m) || !is_smallint_or_atom(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		m = find_module(q->pl, C_STR(q, p1_m));

		if (!m)
			return throw_error(q, p1_m, p1_ctx, "existence_error", "module");
	} else
		m = q->pl->global_bb ? q->pl->user_m : q->st.m;

	if (!bb_make_key(q, tmpbuf, sizeof(tmpbuf), m->name, p1, true))
		return throw_error(q, p1, p1_ctx, "representation_error", "bb_key");

	const char *key = tmpbuf;
	cell *val;

	prolog_lock(q->pl);

	if (!sl_get(m->keyval, key, (void*)&val)) {
		// The plain key is shorter than the :b form already checked, so it cannot overflow here under the lock.
		bb_make_key(q, tmpbuf, sizeof(tmpbuf), m->name, p1, false);
		key = tmpbuf;

		if (!sl_get(m->keyval, key, (void*)&val)) {
			prolog_unlock(q->pl);
			return false;
		}
	}

	prolog_unlock(q->pl);
	cell *tmp = val->flags & FLAG_LIVE ?
		bb_import_term_to_heap(q, val, q->st.cur_ctx) :
		import_term(q, val, q->st.cur_ctx);
	CHECKED(tmp);

	if (!(val->flags & FLAG_LIVE) && is_compound(tmp) && (get_arity(tmp) == 1)) {
		tmp = bb_reattach_attv(q, tmp);
		CHECKED(tmp);
	}

	GET_FIRST_ARG(p1x,nonvar);
	GET_NEXT_ARG(p2,any);

	if (DO_DUMP) DUMP_TERM2("bb_get", tmpbuf, tmp, q->st.cur_ctx, 1);

	return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
}

static bool bif_bb_delete_2(query *q)
{
	GET_FIRST_ARG(p1,nonvar);

	if (is_compound(p1) &&
		((p1->val_off != g_colon_s) || (get_arity(p1) != 2)))
		return throw_error(q, p1, p1_ctx, "type_error", "callable");

	module *m;
	char tmpbuf[1024];

	if (is_compound(p1)) {
		cell *p1_m = p1 + 1;
		p1 = p1_m + p1_m->num_cells;

		if (!is_atom(p1_m) || !is_smallint_or_atom(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		m = find_module(q->pl, C_STR(q, p1_m));

		if (!m)
			return throw_error(q, p1_m, p1_ctx, "existence_error", "module");
	} else
		m = q->pl->global_bb ? q->pl->user_m : q->st.m;

	if (!bb_make_key(q, tmpbuf, sizeof(tmpbuf), m->name, p1, false))
		return throw_error(q, p1, p1_ctx, "representation_error", "bb_key");

	const char *key = tmpbuf;
	cell *val;

	prolog_lock(q->pl);

	if (!sl_get(m->keyval, key, (void*)&val)) {
		prolog_unlock(q->pl);
		return false;
	}

	cell *tmp = val->flags & FLAG_LIVE ?
		bb_import_term_to_heap(q, val, q->st.cur_ctx) :
		import_term(q, val, q->st.cur_ctx);
	CHECKED(tmp, prolog_unlock(q->pl));

	// As bb_get does, re-attach any stored attributes to the imported variables.

	if (!(val->flags & FLAG_LIVE) && is_compound(tmp) && (get_arity(tmp) == 1)) {
		tmp = bb_reattach_attv(q, tmp);
		CHECKED(tmp, prolog_unlock(q->pl));
	}

	GET_FIRST_ARG(p1x,nonvar);
	GET_NEXT_ARG(p2,any);

	if (DO_DUMP) DUMP_TERM2("bb_delete", tmpbuf, tmp, q->st.cur_ctx, 1);

	if (!unify(q, p2, p2_ctx, tmp, q->st.cur_ctx)) {
		prolog_unlock(q->pl);
		return false;
	}

	bool ok = sl_del(m->keyval, key);

	if (ok) {
		while (sl_del(m->keyval, key))
			;
	}

	prolog_unlock(q->pl);
	return ok;
}

static bool bif_sys_bb_is_live_1(query *q)
{
	GET_FIRST_ARG(p1,nonvar);

	if (is_compound(p1) &&
		((p1->val_off != g_colon_s) || (get_arity(p1) != 2)))
		return throw_error(q, p1, p1_ctx, "type_error", "callable");

	module *m;
	char tmpbuf[1024];

	if (is_compound(p1)) {
		cell *p1_m = p1 + 1;
		p1 = p1_m + p1_m->num_cells;

		if (!is_atom(p1_m) || !is_smallint_or_atom(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		m = find_module(q->pl, C_STR(q, p1_m));

		if (!m)
			return throw_error(q, p1_m, p1_ctx, "existence_error", "module");
	} else
		m = q->pl->global_bb ? q->pl->user_m : q->st.m;

	if (!bb_make_key(q, tmpbuf, sizeof(tmpbuf), m->name, p1, true))
		return throw_error(q, p1, p1_ctx, "representation_error", "bb_key");

	const char *key = tmpbuf;
	cell *val;
	bool live = false;

	prolog_lock(q->pl);

	if (sl_get(m->keyval, key, (void*)&val))
		live = (val->flags & FLAG_LIVE) ? true : false;

	prolog_unlock(q->pl);
	return live;
}

builtins g_bboard_bifs[] =
{
	{"$bb_b_put", 2, bif_bb_b_put_2, ":atom,+term", false, false, BLAH},
	{"$bb_put", 2, bif_bb_put_2, ":atom,+term", false, false, BLAH},
	{"$bb_get", 2, bif_bb_get_2, ":atom,?term", false, false, BLAH},
	{"$bb_delete", 2, bif_bb_delete_2, ":atom,?term", false, false, BLAH},
	{"$bb_is_live", 1, bif_sys_bb_is_live_1, ":atom", false, false, BLAH},

	{0}
};
