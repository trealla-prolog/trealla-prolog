// Native tabling support: Phase 1, the trie.
//
// A trie maps *canonical* term images to values. Canonicalization walks
// the (dereferenced) term left-to-right emitting one key cell per step;
// unbound variables are numbered by first appearance, which makes trie
// lookup variant-matching for free: f(X,Y) and f(A,B) take the same
// path, f(X,X) takes a different one.
//
// Nodes are first-child/next-sibling; fanout in tabled-call workloads is
// small, so linear sibling search beats per-node hashing until proven
// otherwise by a profiler.
//
// Sharp edge, documented: a cstr "string" cell and the equivalent
// cons-list of chars are DIFFERENT trie paths. Within one program the
// representation is consistent (copy_term preserves it), so this only
// matters for mixed-representation calls. Revisit if it ever bites.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "module.h"
#include "query.h"
#include "heap.h"

typedef struct tnode_ {
	cell key;
	struct tnode_ *child, *sibling;	// all children, insertion-linked
	struct tnode_ *parent;		// item 5: reconstruction walks leaf->root
	struct tnode_ *hnext;		// hash-bucket chain (when indexed)
	struct thash_ *index;		// hash over THIS node's children, or NULL
	unsigned nchildren;
	void *value;			// table handle; unused by the tests
	bool is_leaf;
} tnode;

// Above this many children a node's child list gets a hash index:
// linear sibling scans are quadratic for flat key spaces (a tabled
// fib(N,_) puts every distinct N at the same level).

#define TRIE_INDEX_THRESHOLD 16

typedef struct thash_ {
	tnode **buckets;
	unsigned nbuckets, count;
} thash;

// ---------------------------------------------------------------------
// Key comparison. Keys are single canonical cells: TAG_VAR cells carry a
// canonical number in var_num; interned cells compare on functor+arity
// (num_cells is position-dependent and is normalized to 1 at emit time);
// numbers compare by value (small/big int cross-compare included); text
// atoms compare equal whether interned or cstring, but char-list strings
// only match char-list strings.

static bool key_is_atomish(const cell *c)
{
	return (is_interned(c) && !get_arity(c)) || (is_cstring(c) && !is_string(c));
}

static bool key_eq(query *q, const cell *a, const cell *b)
{
	if (is_var(a) || is_var(b))
		return is_var(a) && is_var(b) && (a->var_num == b->var_num);

	// Same-text atoms may arrive interned or as cstrings.

	if (key_is_atomish(a) && key_is_atomish(b)) {
		if (is_interned(a) && is_interned(b))
			return a->val_off == b->val_off;

		size_t al = C_STRLEN(q, a), bl = C_STRLEN(q, b);
		return (al == bl) && !memcmp(C_STR(q, a), C_STR(q, b), al);
	}

	if (a->tag != b->tag)
		return false;

	switch (a->tag) {
	case TAG_INTERNED:
		return (a->val_off == b->val_off) && (get_arity(a) == get_arity(b));
	case TAG_INT:
		if (is_bigint(a) && is_bigint(b))
			return mp_int_compare(&a->val_bigint->ival, &b->val_bigint->ival) == 0;
		if (is_bigint(a))
			return mp_int_compare_value(&a->val_bigint->ival, b->val_int) == 0;
		if (is_bigint(b))
			return mp_int_compare_value(&b->val_bigint->ival, a->val_int) == 0;
		return a->val_int == b->val_int;
	case TAG_FLOAT: {
		// Bit compare, NOT ==: key_hash hashes the bit pattern, so C
		// equality would merge 0.0/-0.0 (and never match NaN) while the
		// hash separates them - an inconsistency that makes indexed
		// lookups miss. Bitwise is also the right identity for a table
		// key (variant, not arithmetic, equality).
		union { double d; uint64_t u; } ua = { .d = a->val_float };
		union { double d; uint64_t u; } ub = { .d = b->val_float };
		return ua.u == ub.u;
	}
	case TAG_RATIONAL:
		return mp_rat_compare(&a->val_bigint->irat, &b->val_bigint->irat) == 0;
	case TAG_CSTR: {
		if (is_string(a) != is_string(b))
			return false;
		size_t al = C_STRLEN(q, a), bl = C_STRLEN(q, b);
		return (al == bl) && !memcmp(C_STR(q, a), C_STR(q, b), al);
	}
	default:
		return false;
	}
}

// Hash consistent with key_eq: atomish keys (interned atoms and plain
// cstrings) hash their TEXT so both representations collide correctly.

static unsigned key_hash(query *q, const cell *c)
{
	if (is_var(c))
		return 0x9e3779b9u ^ c->var_num;

	if (key_is_atomish(c) || (is_cstring(c) && is_string(c))) {
		const char *p = C_STR(q, c);
		size_t len = C_STRLEN(q, c);
		unsigned h = 2166136261u;

		for (size_t i = 0; i < len; i++)
			h = (h ^ (unsigned char)p[i]) * 16777619u;

		return is_string(c) ? h ^ 0x51175117u : h;
	}

	if (is_interned(c))
		return (unsigned)c->val_off * 33u + get_arity(c);

	switch (c->tag) {
	case TAG_INT:
		if (is_bigint(c)) {
			mp_int z = &c->val_bigint->ival;
			unsigned h = 0x811c9dc5u;

			for (mp_size i = 0; i < MP_USED(z); i++)
				h = (h ^ (unsigned)MP_DIGITS(z)[i]) * 16777619u;

			return h;
		}

		return (unsigned)((uint64_t)c->val_int ^ ((uint64_t)c->val_int >> 32));
	case TAG_FLOAT: {
		union { double d; uint64_t u; } u = { .d = c->val_float };
		return (unsigned)(u.u ^ (u.u >> 32));
	}
	case TAG_RATIONAL: {
		mp_rat r = &c->val_bigint->irat;
		unsigned h = 0x811c9dc5u;

		for (mp_size i = 0; i < MP_USED(&r->num); i++)
			h = (h ^ (unsigned)MP_DIGITS(&r->num)[i]) * 16777619u;

		for (mp_size i = 0; i < MP_USED(&r->den); i++)
			h = (h ^ (unsigned)MP_DIGITS(&r->den)[i]) * 16777619u;

		return h;
	}
	default:
		return 0;
	}
}

static void thash_insert(thash *h, tnode *n, unsigned hv)
{
	unsigned b = hv % h->nbuckets;
	n->hnext = h->buckets[b];
	h->buckets[b] = n;
	h->count++;
}

static void thash_remove(query *q, thash *h, tnode *n)
{
	tnode **pp = &h->buckets[key_hash(q, &n->key) % h->nbuckets];

	while (*pp) {
		if (*pp == n) {
			*pp = n->hnext;
			h->count--;
			return;
		}

		pp = &(*pp)->hnext;
	}
}

static bool thash_grow(query *q, thash *h)
{
	unsigned nb = h->nbuckets * 2;
	tnode **nbk = TPL_calloc(nb, sizeof(tnode*));
	if (!nbk) return false;
	tnode **old = h->buckets;
	unsigned oldn = h->nbuckets;
	h->buckets = nbk;
	h->nbuckets = nb;
	h->count = 0;

	for (unsigned i = 0; i < oldn; i++) {
		for (tnode *n = old[i]; n; ) {
			tnode *next = n->hnext;
			thash_insert(h, n, key_hash(q, &n->key));
			n = next;
		}
	}

	TPL_free(old);
	return true;
}

static bool trie_index_children(query *q, tnode *parent)
{
	thash *h = TPL_calloc(1, sizeof(thash));
	if (!h) return false;
	h->nbuckets = 64;
	h->buckets = TPL_calloc(h->nbuckets, sizeof(tnode*));
	if (!h->buckets) { TPL_free(h); return false; }

	for (tnode *n = parent->child; n; n = n->sibling)
		thash_insert(h, n, key_hash(q, &n->key));

	parent->index = h;
	return true;
}

// ---------------------------------------------------------------------
// Walk state: canonical variable numbering + trie cursor.

typedef struct {
	query *q;
	tnode **root;			// where the trie hangs
	tnode *node;			// last node stepped onto
	struct { pl_ctx ctx; unsigned num; } *vars;
	unsigned num_vars, max_vars;
	bool create;			// insert vs lookup
	bool created_any;

	// The first node this walk created, and where it hangs. Everything
	// created afterwards is inside its subtree, so unlinking this one
	// discards exactly what the walk added - see trie_insert_().

	tnode **first_slot;
	tnode *first_node, *first_parent;
	bool oom;
	bool attvar;			// hit an attributed variable

	// Restraints (item 1): max_size is a term-size budget in canonical
	// key cells, 0 = unbounded. size counts every step regardless of
	// hit/create, so a term that shares most of its path with an
	// existing one still counts at its full size, not just what it
	// added.

	unsigned max_size, size;
	bool restrained;

	// Answer subsumption (item 2): skip_arg is the 1-based position of
	// the aggregated argument in the OUTERMOST functor, 0 = none. Its
	// cells are never walked, so the trie encodes only the "key"
	// arguments - two answers agreeing on those collide regardless of
	// what they carry at skip_arg. at_top marks "still processing the
	// outermost functor's direct arguments"; it is cleared the moment
	// that functor's own key step is taken, so skip_arg can never
	// apply inside a nested subterm (a mode spec only ever names a
	// top-level argument).

	unsigned skip_arg;
	bool at_top;
} twalk;

static void twalk_init(twalk *w, query *q, tnode **root, bool create)
{
	memset(w, 0, sizeof(*w));
	w->q = q;
	w->root = root;
	w->create = create;
	w->at_top = true;
}

static void twalk_done(twalk *w)
{
	if (w->vars)
		TPL_free(w->vars);
}

static int twalk_var_num(twalk *w, pl_ctx ctx, unsigned var_num)
{
	for (unsigned i = 0; i < w->num_vars; i++) {
		if ((w->vars[i].ctx == ctx) && (w->vars[i].num == var_num))
			return (int)i;
	}

	if (w->num_vars >= w->max_vars) {
		w->max_vars = w->max_vars ? w->max_vars*2 : 16;
		void *tmp = TPL_realloc(w->vars, sizeof(w->vars[0]) * w->max_vars);
		if (!tmp) { w->oom = true; return -1; }
		w->vars = tmp;
	}

	w->vars[w->num_vars].ctx = ctx;
	w->vars[w->num_vars].num = var_num;
	return (int)w->num_vars++;
}

// Descend one step on the canonical key cell, creating the node when
// inserting. Returns false when lookup misses (or OOM).

static bool trie_step(twalk *w, const cell *key)
{
	if (w->max_size && (++w->size > w->max_size)) {
		w->restrained = true;
		return false;
	}

	tnode *parent = w->node;
	tnode **slot = parent ? &parent->child : w->root;
	thash *h = parent ? parent->index : NULL;

	if (h) {
		unsigned hv = key_hash(w->q, key);

		for (tnode *n = h->buckets[hv % h->nbuckets]; n; n = n->hnext) {
			if (key_eq(w->q, &n->key, key)) {
				w->node = n;
				return true;
			}
		}
	} else {
		for (tnode *n = *slot; n; n = n->sibling) {
			if (key_eq(w->q, &n->key, key)) {
				w->node = n;
				return true;
			}
		}
	}

	if (!w->create)
		return false;

	tnode *n = TPL_calloc(1, sizeof(tnode));
	if (!n) { w->oom = true; return false; }
	n->key = *key;

	// A slice has no refcount for share_cell() to take and the trie outlives
	// the mapping, so the node's copy is given characters of its own instead.

	if (is_slice(&n->key)) {
		if (!unslice_cells(&n->key, 1)) {
			TPL_free(n);
			w->oom = true;
			return false;
		}
	} else
		share_cell(&n->key);		// bigints/cstrings are refcounted

	n->parent = parent;		// item 5
	n->sibling = *slot;
	*slot = n;
	w->node = n;

	if (!w->created_any) {
		w->first_slot = slot;
		w->first_node = n;
		w->first_parent = parent;
	}

	w->created_any = true;

	if (parent) {
		parent->nchildren++;

		// Both of these are best-effort: the index is only an
		// optimisation, and the sibling chain is always correct on its
		// own. Failing the insert here used to be worse than useless -
		// a failed grow returned before thash_insert(), leaving the new
		// node in the sibling chain but absent from the index, so the
		// next lookup missed it and created a DUPLICATE key. That
		// breaks the dedup the answer trie exists for.

		if (h) {
			if (h->count >= h->nbuckets - h->nbuckets/4)
				thash_grow(w->q, h);		// denser if it fails, still correct

			thash_insert(h, n, key_hash(w->q, key));
		} else if (parent->nchildren > TRIE_INDEX_THRESHOLD)
			trie_index_children(w->q, parent);	// unindexed if it fails
	}

	return true;
}

// Emit the canonical key sequence for (c, ctx). Recursive on term depth;
// tabled calls are shallow. Returns false on lookup miss / unsupported.

static bool trie_walk(twalk *w, cell *c, pl_ctx ctx)
{
	// The last argument is handled by looping rather than recursing.
	// Recursion here is bounded by term depth, and while a tabled CALL
	// is shallow an ANSWER need not be: a list is right-nested '.'/2,
	// so a 100k-element answer recursed 100k frames and overflowed the
	// stack (SIGSEGV, no error term). Iterating on the final argument
	// makes any right-nested term O(1) deep.

	for (;;) {
	c = deref(w->q, c, ctx);
	ctx = w->q->latest_ctx;

	if (is_var(c)) {
		// Attributed variables cannot be represented in a call variant:
		// the attributes are not part of the term, so two calls with
		// different constraints would share a table. Reject rather than
		// silently give wrong answers.
		{
			query *q = w->q;
			const frame *f = GET_FRAME(ctx);
			const slot *e = get_slot(q, f, c->var_num);

			if (e->c.val_attrs) {
				w->attvar = true;
				return false;
			}
		}

		int n = twalk_var_num(w, ctx, c->var_num);
		if (n < 0) return false;
		cell key = {0};
		key.tag = TAG_VAR;
		key.num_cells = 1;
		key.var_num = (unsigned)n;
		return trie_step(w, &key);
	}

	if (is_interned(c)) {
		cell key = *c;
		key.num_cells = 1;		// position-dependent; normalize
		key.flags = 0;			// strip OP etc annotations
		key.match = NULL;

		bool top = w->at_top;
		w->at_top = false;

		if (!trie_step(w, &key))
			return false;

		if (!get_arity(c))
			return true;

		cell *arg = c + 1;
		const unsigned last = get_arity(c) - 1;	// arity >= 1, checked above

		for (unsigned i = 0; i < last; i++) {
			if (!(top && ((i+1) == w->skip_arg))) {
				if (!trie_walk(w, arg, ctx))
					return false;
			}

			arg += arg->num_cells;
		}

		if (top && ((last+1) == w->skip_arg))
			return true;		// last argument is the skipped one

		c = arg;				// last argument: loop, don't recurse
		continue;
	}

	switch (c->tag) {
	case TAG_INT:
	case TAG_FLOAT:
	case TAG_RATIONAL:
	case TAG_CSTR: {
		cell key = *c;
		key.num_cells = 1;
		return trie_step(w, &key);
	}
	default:
		return false;			// blobs, streams etc: not tableable
	}
	}
}

// Full-term insert: returns leaf node, sets *existed when this exact
// canonical term had been inserted before (the dedup signal).

static void trie_free(tnode *n);

static tnode *trie_insert_(query *q, tnode **root, cell *c, pl_ctx ctx, bool *existed, bool *attvar, unsigned max_size, bool *restrained, unsigned skip_arg)
{
	twalk w;
	twalk_init(&w, q, root, true);
	w.max_size = max_size;
	w.skip_arg = skip_arg;
	bool ok = trie_walk(&w, c, ctx);
	tnode *leaf = w.node;
	bool fresh = w.created_any;
	if (attvar) *attvar = w.attvar;
	if (restrained) *restrained = w.restrained;
	twalk_done(&w);

	if (!ok || !leaf) {
		// The walk failed part-way - an attributed variable, a blob, a
		// restraint breach, or OOM - after possibly creating nodes for
		// the arguments it did get through. Those are unreachable
		// (never marked is_leaf) but they were never reclaimed either,
		// so a program looping on a tabled call with an untabelable
		// answer grew the trie on every throw. Discard what this walk
		// added.

		if (w.first_node) {
			*w.first_slot = w.first_node->sibling;

			if (w.first_parent) {
				w.first_parent->nchildren--;

				if (w.first_parent->index)
					thash_remove(q, w.first_parent->index, w.first_node);
			}

			w.first_node->sibling = NULL;	// don't walk into the live trie
			trie_free(w.first_node);
		}

		return NULL;
	}

	*existed = !fresh && leaf->is_leaf;
	leaf->is_leaf = true;
	return leaf;
}

// Explicit worklist rather than recursion on ->child. Siblings already
// iterated, but children recursed, so the trie for one long-list answer
// - a chain one node wide and as deep as the list is long - overflowed
// the stack on teardown for the same reason trie_walk did on insert.
//
// The nodes are being freed anyway, so ->sibling is reused as the
// worklist link. Each child is pushed individually: linear overall.

static void trie_free(tnode *n)
{
	tnode *stack = n;

	while (stack) {
		tnode *cur = stack;
		stack = cur->sibling;

		for (tnode *ch = cur->child, *next; ch; ch = next) {
			next = ch->sibling;
			ch->sibling = stack;
			stack = ch;
		}

		if (cur->index) {
			TPL_free(cur->index->buckets);
			TPL_free(cur->index);
		}

		unshare_cell(&cur->key);
		TPL_free(cur);
	}
}

// ---------------------------------------------------------------------
// Tables. A table owns an answer trie (dedup), the answers in insertion
// order, suspended consumers (detached continuation images), and its
// scheduling state. Handles cross the Prolog boundary as integers -
// they only ever flow between '$tbl_*' builtins.
//
// Scheduling: adding an answer to a table that has suspensions (or a
// suspension to a table that has answers) enqueues the table on the
// global worklist. '$tbl_pop_worklist' MATERIALIZES the new work - the
// cartesian pairs (new answers x all suspensions) + (old answers x new
// suspensions) - and resets the cursors, so work arriving during a
// drain is simply picked up by a later enqueue/pop round (batched
// scheduling, like the library's worklists but without the attvars).

enum { TBL_FRESH=0, TBL_ACTIVE=1, TBL_COMPLETE=2 };

typedef struct tbl_ans_ {
	// Item 5: exactly one of these carries the answer. image is a full
	// second copy of the term; leaf is its path in the answer trie,
	// which exists either way, so reconstructing from it costs nothing
	// extra in memory. image is kept only where the trie cannot answer:
	// a SUBSUMPTIVE table omits the aggregated argument from the trie
	// by construction (that is how answers collide and combine), so the
	// path there is missing exactly the value that matters.
	cell *image;
	struct tnode_ *leaf;
	struct tbl_ans_ *next;

	// Answer subsumption (item 2). is_new: still in the table's
	// unproc_ans segment, not yet drained by '$tbl_pop_worklist' - an
	// update arriving before that first drain needs no special
	// handling, the normal (new answers x all suspensions) pass
	// already covers it. in_update_queue/update_next: once drained
	// (is_new false), an in-place value update is queued here instead,
	// so pop_worklist can re-pair this SAME node against every current
	// suspension - a consumer that already read the old value must see
	// the new one.

	bool is_new;
	bool in_update_queue;
	struct tbl_ans_ *update_next;
} tbl_ans;

typedef struct tbl_susp_ {
	cell *image;
	struct tbl_susp_ *next;
} tbl_susp;

typedef struct tbl_pair_ {
	tbl_ans *a;
	tbl_susp *s;
	struct tbl_pair_ *next;
} tbl_pair;

typedef struct table_ {
	tnode *answers;			// dedup trie over answer terms
	tbl_ans *first_ans, *last_ans;
	tbl_susp *first_susp, *last_susp;
	tbl_ans *unproc_ans;		// first answer not yet paired, or NULL
	tbl_susp *unproc_susp;		// first suspension not yet paired, or NULL
	tbl_pair *pending;		// materialized work for '$tbl_wkl_work'
	int status;
	bool in_wl;
	unsigned scc;			// owning SCC id (0 = none yet)
	unsigned n_answers;		// count for max_answers_for_subgoal

	// Answer subsumption (item 2): ":- table path(_,_,min)". agg_pos is
	// the 1-based position of the aggregated argument, 0 = not
	// subsumptive (the common case - every field below is unused then).
	// agg_max: false = min, true = max, both over standard order.
	// update_head/tail: answers whose VALUE changed in place since the
	// last drain, queued for re-pairing - see tbl_ans_.

	unsigned agg_pos;
	bool agg_max;
	tbl_ans *update_head, *update_tail;

	// Identity of the call this table answers, so abolish_table/1 can
	// find every variant of one predicate, and the trie leaf pointing
	// here, so it can be reset to "no table yet" when we drop it.

	pl_idx functor;
	unsigned arity;
	tnode *leaf;
	unsigned slot;			// index into tbl_state.slots (handle identity)

	// Incremental tabling (item 3). deps is this table's dependency
	// set, flushed here from its SCC on completion; completed_at is the
	// pl->dbgen the table completed at. Validation is a PULL done by
	// the owning thread at lookup, never a push from the asserting
	// thread: tables are per-thread but the database is shared, so
	// invalidating from the writer's side would mean touching another
	// thread's tables.

	struct tbl_dep_ *deps;
	uint64_t completed_at;
	bool is_incremental;
	bool deps_incomplete;		// see tscc: recompute rather than trust

	// Shared completed tables (item 4). wants_shared is the ":- table
	// p/1 as shared" declaration; is_shared means ownership has
	// actually transferred to the registry, which only happens on a
	// clean completion. Until then the table is private and mutable
	// like any other.

	bool wants_shared;
	bool is_shared;

	// The call variant, needed to key the shared trie at publication -
	// by then the original call is long gone. Stored ONLY for tables
	// that declared "as shared", so a normal table pays nothing.

	cell *key_image;

	struct table_ *wl_next, *all_next, *fresh_next;
} table;

// Strongly-connected-component stack.
//
// A fresh variant called underneath a running leader is COMPLETED in its
// own nested SCC rather than suspending the consumer. That matters
// because a consumer cannot always be suspended: a tabled call inside
// findall/3 or setof/3 has its continuation buried in the collector's
// C-level state, so a captured goal-list continuation cannot resume it.
// Completing the subgoal avoids the suspension entirely, which is also
// what SWI-Prolog does.
//
// Suspension is then needed only for a genuine cycle - a call to a
// variant that is already ACTIVE (an ancestor). If such a suspension
// targets a table belonging to an OUTER SCC, this SCC is not independent
// after all: on pop its tables are merged into the parent (SCC merging)
// and the parent finishes them.

// Incremental tabling (item 3): one edge of a table's dependency set.
// Predicates are keyed by (module, functor, arity) rather than held as
// a raw predicate* - destroy_predicate() runs at module teardown, and
// Phase 1 already paid once for raw pointers outliving their target.
// A table edge names another table's slot+serial handle, validated the
// same way every other handle is.

typedef struct tbl_dep_ {
	struct tbl_dep_ *next;
	bool is_table;			// false = predicate edge, true = table edge

	// predicate edge
	module *m;
	pl_idx functor;
	unsigned arity;

	// table edge
	uint64_t handle;
} tbl_dep;

typedef struct {
	unsigned id;
	table *fresh_head;		// tables owned by this SCC
	unsigned dep_min;		// smallest outer SCC id depended on (0 = none)

	// Dependencies collected while THIS SCC is the one being completed.
	// Attribution is per-SCC, not per-table: the SCC is already the
	// unit of completion, so it is the natural unit of invalidation,
	// and its push/pop bracket is the only one in the driver that is
	// safe against backtracking (see DESIGN-tabling-phase2.md item 3).
	// Flushed onto every table in fresh_head at mark_all_complete.

	tbl_dep *deps;
	bool deps_incomplete;		// a dep was lost to OOM; refuse to validate
} tscc;

// --- per-thread state ---
//
// This was file statics (two prolog instances silently shared tables),
// then per-prolog (one thread per instance could table, the rest got
// resource_error). It is now per-thread: allocated on first tabled
// call, freed by tabling_destroy_thread() when the thread retires and
// by tabling_destroy() sweeping every slot at pl_destroy().

typedef struct {
	tscc *scc;
	unsigned scc_depth, scc_max, scc_next_id;

	// Bumped by abolish; enumerations carry the generation they started
	// in and stop cleanly if the tables were pulled out from under them.
	// int64_t (not uint64_t) to match q->st.v2, where they stash it.

	int64_t generation;

	// Non-zero while a leader is driving completion. Abolishing must not
	// free tables that live frames are still enumerating.

	unsigned in_use;

	// Set when a worker raised an exception during the current leader's
	// fixpoint. Such a fixpoint may have gathered only part of the
	// answers, so its tables must NOT be cached as complete.

	bool saw_exception;

	tnode *variants;
	table *all_tables, *wl_head, *fresh_head;
	bool leader;

	// Handles are (serial, index) pairs into this array rather than raw
	// pointers, so a handle held across abolish_all_tables/0 is detected
	// instead of dereferenced. Releasing a slot bumps its serial, which
	// invalidates every handle that referred to it.

	struct { table *t; uint32_t serial; } *slots;
	unsigned nslots, slots_cap;

#if USE_THREADS
#endif

} tbl_state;

// Per-THREAD state. threads[0] is the main thread - the same
// q->thread_ptr ? : &pl->threads[0] idiom used in query.c, toplevel.c,
// bif_os.c and bif_threads.c.
//
// This is what makes tabling thread-safe without a single lock: a
// table is only ever reachable from the thread that created it, so
// there is nothing to race. The alternative - locking shared tables -
// cannot work here anyway, because the leader's critical section spans
// completion/0, a PROLOG loop that runs arbitrary user code between
// '$tbl_*' calls. No lock can be held across that.
//
// The cost is that threads do not share completed tables and each
// recomputes its own. Sharing completed (hence immutable) tables via a
// published registry is the natural next step and does not disturb
// this invariant.

static tbl_state *tbl(query *q)
{
	thread *self = q->thread_ptr ? q->thread_ptr : q->pl->main_thread;

	if (!self->tabling_state) {
		tbl_state *s = TPL_calloc(1, sizeof(tbl_state));
		if (!s) return NULL;
		s->generation = 1;
		s->scc_next_id = 1;
		self->tabling_state = s;
	}

	return (tbl_state*)self->tabling_state;
}

static unsigned tbl_scc_id(const tbl_state *s)
{
	return s->scc_depth ? s->scc[s->scc_depth-1].id : 0;
}

// --- shared completed tables (item 4) ---
//
// Threads tabling the same predicate each recompute it. A table
// declared ":- table p/1 as shared" is still BUILT privately, with no
// locking, exactly as before - the leader's critical section spans
// completion/0, a Prolog loop running arbitrary user code, and no lock
// survives that. It is only PUBLISHED once complete, and a completed
// table is immutable.
//
// The locking has to be stated precisely or it is a data race:
//
//   - the table is fully built and never written again BEFORE
//     publication;
//   - publication happens under the registry mutex;
//   - LOOKUP ALSO happens under that mutex - it is the acquire against
//     the publisher's release that makes the contents visible;
//   - only after lookup returns does the reader touch the table, and
//     by then it is immutable.
//
// Not "read without a lock" - one short lock to find it, then no lock
// to use it. Reading an answer bumps refcounts on shared subcells, but
// pl_refcnt is _Atomic under USE_THREADS, so that is safe.

typedef struct {
	lock guard;
	tnode *variants;			// shared variant trie
	struct { table *t; uint32_t serial; } *slots;
	unsigned nslots, slots_cap;
	table *all;				// every published table, for teardown
	bool inited;
} tbl_shared;

// Bit 31 of the slot index says which array a handle indexes. Nobody
// has 2^31 tables, and it keeps a handle a single integer across the
// Prolog boundary as before.

#define TBL_SHARED_BIT 0x80000000u

// Non-creating: resolvers run on every handle and must not conjure a
// registry for a program that never shares anything.

static tbl_shared *tbl_shared_peek(query *q)
{
	return (tbl_shared*)q->pl->tbl_shared;
}

#if USE_THREADS
static tbl_shared *tbl_shared_get(query *q)
{
	tbl_shared *sh = (tbl_shared*)q->pl->tbl_shared;

	if (!sh) {
		acquire_lock(&q->pl->guard);
		sh = (tbl_shared*)q->pl->tbl_shared;

		if (!sh) {
			sh = TPL_calloc(1, sizeof(tbl_shared));

			if (sh) {
				init_lock(&sh->guard);
				sh->inited = true;
				pl_publish_barrier();
				q->pl->tbl_shared = sh;
			}
		}

		release_lock(&q->pl->guard);
	}

	return sh;
}
#else
// NOTHREADS is the WASI configuration, which is what embedders ship.
// Sharing is meaningless with one thread, so it compiles out and
// "as shared" simply leaves the table private.
static tbl_shared *tbl_shared_get(query *q) { (void)q; return NULL; }
#endif

// --- incremental tabling (item 3): dependency collection ---
//
// Non-allocating: enter_predicate() calls into here on every call to an
// incremental predicate, and a program that never tables must not have
// tabling state created underneath it.

static tbl_state *tbl_peek(query *q)
{
	thread *self = q->thread_ptr ? q->thread_ptr : q->pl->main_thread;
	return (tbl_state*)self->tabling_state;
}

static void tbl_deps_free(tbl_dep *d)
{
	while (d) {
		tbl_dep *next = d->next;
		TPL_free(d);
		d = next;
	}
}

// Dep sets are small (a table consults a handful of predicates), so a
// linear dedup scan beats any index here.

static void tbl_scc_add_dep(tscc *top, const tbl_dep *want)
{
	for (tbl_dep *d = top->deps; d; d = d->next) {
		if (d->is_table != want->is_table)
			continue;

		if (want->is_table) {
			if (d->handle == want->handle)
				return;
		} else if ((d->m == want->m) && (d->functor == want->functor)
			&& (d->arity == want->arity))
			return;
	}

	tbl_dep *d = TPL_calloc(1, sizeof(tbl_dep));

	if (!d) {
		// A dep we failed to record is a table that would later look
		// valid when it is not. Mark the SCC so its tables refuse to
		// validate at all and simply recompute - slow, but not wrong.

		top->deps_incomplete = true;
		return;
	}

	*d = *want;
	d->next = top->deps;
	top->deps = d;
}

// Called from enter_predicate() when pr->is_incremental. Attribution is
// to the SCC currently being completed, which is correct for a resumed
// continuation too - measured at 1157 checks / 0 mismatches, see the
// design doc.

void tbl_note_predicate_dep(query *q, predicate *pr)
{
	tbl_state *s = tbl_peek(q);

	if (!s || !s->scc_depth)
		return;

	tbl_dep want = {0};
	want.m = pr->m;
	want.functor = pr->key.val_off;
	want.arity = get_arity(&pr->key);
	tbl_scc_add_dep(&s->scc[s->scc_depth-1], &want);
}

static pl_idx s_fresh, s_active, s_complete, s_min, s_max;

static void tbl_intern_atoms(query *q)
{
	if (!s_fresh) {
		s_fresh = new_atom(q->pl, "fresh");
		s_active = new_atom(q->pl, "active");
		s_complete = new_atom(q->pl, "complete");
		s_min = new_atom(q->pl, "min");
		s_max = new_atom(q->pl, "max");
	}
}

// Detached term image, the '$bb_put' pattern: one consistent copy, then
// malloc'd cells (managed subcells refcounted by dup_cells).

static cell *tbl_image(query *q, cell *c, pl_ctx ctx)
{
	if (!init_tmp_heap(q))
		return NULL;

	cell *tmp = copy_term_to_tmp(q, c, ctx, false);
	if (!tmp) return NULL;

	cell *val = TPL_malloc(sizeof(cell)*tmp->num_cells);
	if (!val) return NULL;
	pl_idx num_cells = dup_cells(val, tmp, tmp->num_cells);

	// The table outlives the mapping any slice in it points into.
	// Unsliced here, not in the tmp heap: this copy owns the reference the
	// new string carries, and a slice had none for dup_cells() to share.

	if (!unslice_cells(val, num_cells)) {
		unshare_cells(val, num_cells);
		TPL_free(val);
		return NULL;
	}

	return val;
}

// --- item 5: rebuild an answer from its path in the answer trie ---
//
// Every answer already has a path; the stored image was a second copy
// of the same information. Reconstruction walks leaf->root (which is
// why tnode carries a parent pointer), reverses, then re-emits the
// canonical key sequence as a real term.
//
// The result is in IMAGE format - a detached, malloc'd cell array - so
// every existing reader can go on calling import_term() on it and
// nothing else has to understand tries. The caller frees it.

static void tbl_emit_term(cell *out, unsigned *oi, tnode **path, unsigned *pi)
{
	unsigned start = (*oi)++;
	const cell *k = &path[(*pi)++]->key;
	out[start] = *k;
	share_cell(&out[start]);
	unsigned arity = is_interned(k) ? get_arity(k) : 0;

	for (unsigned a = 0; a < arity; a++)
		tbl_emit_term(out, oi, path, pi);

	out[start].num_cells = *oi - start;
}

static cell *tbl_reconstruct(query *q, tnode *leaf)
{
	(void)q;
	unsigned n = 0;

	for (tnode *p = leaf; p; p = p->parent)
		n++;

	if (!n)
		return NULL;

	tnode **path = TPL_malloc(sizeof(tnode*) * n);

	if (!path)
		return NULL;

	unsigned i = n;

	for (tnode *p = leaf; p; p = p->parent)
		path[--i] = p;

	// One trie node emits exactly one cell, so the path length IS the
	// cell count - no second pass needed to size the term.

	cell *out = TPL_malloc(sizeof(cell) * n);

	if (!out) {
		TPL_free(path);
		return NULL;
	}

	unsigned oi = 0, pi = 0;
	tbl_emit_term(out, &oi, path, &pi);
	TPL_free(path);
	return out;
}

// The one place that knows which representation an answer uses.

static cell *tbl_answer_term(query *q, const tbl_ans *a)
{
	if (a->image)
		return import_term(q, a->image, q->st.cur_ctx);

	cell *img = tbl_reconstruct(q, a->leaf);

	if (!img)
		return NULL;

	cell *tmp = import_term(q, img, q->st.cur_ctx);
	unshare_cells(img, img->num_cells);
	TPL_free(img);
	return tmp;
}

static void tbl_image_free(cell *c)
{
	if (!c) return;
	unshare_cells(c, c->num_cells);
	TPL_free(c);
}

static void tbl_free_pending(table *t)
{
	tbl_pair *p = t->pending;

	while (p) {
		tbl_pair *next = p->next;
		TPL_free(p);
		p = next;
	}

	t->pending = NULL;
}

static void tbl_enqueue(tbl_state *s, table *t)
{
	if (t->in_wl)
		return;

	t->in_wl = true;
	t->wl_next = s->wl_head;
	s->wl_head = t;
}

static void tbl_destroy(table *t)
{
	tbl_deps_free(t->deps);		// item 3
	t->deps = NULL;
	tbl_image_free(t->key_image);	// item 4
	t->key_image = NULL;
	trie_free(t->answers);

	for (tbl_ans *a = t->first_ans; a; ) {
		tbl_ans *next = a->next;
		tbl_image_free(a->image);
		TPL_free(a);
		a = next;
	}

	for (tbl_susp *s = t->first_susp; s; ) {
		tbl_susp *next = s->next;
		tbl_image_free(s->image);
		TPL_free(s);
		s = next;
	}

	tbl_free_pending(t);
	TPL_free(t);
}

// Table handles cross into Prolog as integers. The '$tbl_*' builtins
// are ordinary entries in the builtins table - the '$' is a naming
// convention, not access control, and the README documents users
// calling other '$' predicates directly - so any program can reach
// them with an integer of its choosing:
//
//     ?- '$tbl_set_status'(0, complete).
//
// That used to cast 0 to a table* and dereference it. Worse, a handle
// obtained legitimately and then used after abolish_all_tables/0 was a
// use-after-free, which ASAN confirms at '$tbl_get_answer'.
//
// So a handle is no longer a pointer. It is a (serial, index) pair into
// tbl_state.slots: the index finds the slot, and the serial must match
// the one the slot held when the handle was made. Releasing a slot
// bumps its serial, so every handle to it stops validating. The cell is
// also tagged FLAG_INT_TABLE, the way streams use FLAG_INT_STREAM, so a
// plain integer is rejected before any of that.

#define TBL_HANDLE(idx, ser) (((pl_uint)(ser) << 32) | (pl_uint)(idx))

static bool tbl_slot_alloc(tbl_state *s, table *t)
{
	for (unsigned i = 0; i < s->nslots; i++) {
		if (!s->slots[i].t) {
			s->slots[i].t = t;
			t->slot = i;
			return true;
		}
	}

	if (s->nslots >= s->slots_cap) {
		unsigned cap = s->slots_cap ? s->slots_cap * 2 : 16;
		void *tmp = TPL_realloc(s->slots, sizeof(*s->slots) * cap);
		if (!tmp) return false;
		s->slots = tmp;
		memset(&s->slots[s->slots_cap], 0,
			sizeof(*s->slots) * (cap - s->slots_cap));
		s->slots_cap = cap;
	}

	s->slots[s->nslots].t = t;
	s->slots[s->nslots].serial = 1;
	t->slot = s->nslots++;
	return true;
}

#if USE_THREADS
// Caller holds sh->guard.

static bool tbl_shared_slot_alloc(tbl_shared *sh, table *t)
{
	for (unsigned i = 0; i < sh->nslots; i++) {
		if (!sh->slots[i].t) {
			sh->slots[i].t = t;
			t->slot = i;
			return true;
		}
	}

	if (sh->nslots >= sh->slots_cap) {
		unsigned cap = sh->slots_cap ? sh->slots_cap * 2 : 16;

		// Bit 31 of the index is the shared flag, so the array can
		// never legitimately reach it.

		if (cap >= TBL_SHARED_BIT) return false;

		void *tmp = TPL_realloc(sh->slots, sizeof(*sh->slots) * cap);
		if (!tmp) return false;
		sh->slots = tmp;
		memset(&sh->slots[sh->slots_cap], 0,
			sizeof(*sh->slots) * (cap - sh->slots_cap));
		sh->slots_cap = cap;
	}

	sh->slots[sh->nslots].t = t;
	sh->slots[sh->nslots].serial = 1;
	t->slot = sh->nslots++;
	return true;
}
#endif

static void tbl_slot_release(tbl_state *s, const table *t)
{
	if (!s->slots || (t->slot >= s->nslots) || (s->slots[t->slot].t != t))
		return;

	s->slots[t->slot].t = NULL;
	s->slots[t->slot].serial++;		// invalidates outstanding handles
}

// A published table's slot lives in the shared registry, so its handle
// carries TBL_SHARED_BIT and every resolver routes on it. t->is_shared
// is set only once ownership has actually transferred.

static void make_tbl_handle_(tbl_state *s, tbl_shared *sh, cell *tmp, const table *t)
{
	if (t->is_shared && sh)
		make_uint(tmp, TBL_HANDLE(t->slot | TBL_SHARED_BIT, sh->slots[t->slot].serial));
	else
		make_uint(tmp, TBL_HANDLE(t->slot, s->slots[t->slot].serial));

	tmp->flags |= FLAG_INT_TABLE;
}

#define make_tbl_handle(s, tmp, t) make_tbl_handle_((s), tbl_shared_peek(q), (tmp), (t))

// Same value, without a cell - item 3 stores it in a dependency edge so
// a stale table edge is detected by the usual slot+serial validation
// rather than by dereferencing freed memory.

static uint64_t tbl_handle_value_(const tbl_state *s, const tbl_shared *sh, const table *t)
{
	if (t->is_shared && sh)
		return TBL_HANDLE(t->slot | TBL_SHARED_BIT, sh->slots[t->slot].serial);

	return TBL_HANDLE(t->slot, s->slots[t->slot].serial);
}

#define tbl_handle_value(s, t) tbl_handle_value_((s), tbl_shared_peek(q), (t))

// Resolve a raw handle value. The shared half takes the registry lock:
// that acquire is what pairs with the publisher's release and makes the
// table's contents visible to this thread. The table is immutable by
// then, so nothing is held once the lookup returns.

static table *tbl_resolve(tbl_state *s, tbl_shared *sh, uint64_t v)
{
	unsigned raw = (unsigned)(v & 0xffffffffu);
	uint32_t ser = (uint32_t)(v >> 32);

	if (raw & TBL_SHARED_BIT) {
		unsigned idx = raw & ~TBL_SHARED_BIT;

		if (!sh)
			return NULL;

		acquire_lock(&sh->guard);
		table *t = ((idx < sh->nslots) && sh->slots[idx].t
			&& (sh->slots[idx].serial == ser)) ? sh->slots[idx].t : NULL;
		release_lock(&sh->guard);
		return t;
	}

	if (!s || (raw >= s->nslots) || !s->slots[raw].t
		|| (s->slots[raw].serial != ser))
		return NULL;

	return s->slots[raw].t;
}

#define tbl_handle_from_value(s, v) tbl_resolve((s), tbl_shared_peek(q), (v))

static table *tbl_handle_(tbl_state *s, tbl_shared *sh, cell *c)
{
	if (!is_integer(c) || !(c->flags & FLAG_INT_TABLE))
		return NULL;

	return tbl_resolve(s, sh, c->val_uint);
}

#define tbl_handle(s, c) tbl_handle_((s), tbl_shared_peek(q), (c))

// Does this term image contain variables? Imported answers that are
// ground need no frame protection (see tbl_pin_answer_frame below), and
// ground answers are the common case, so this keeps the fast path free.

static bool tbl_has_vars(const cell *c)
{
	const cell *p = c;

	for (pl_idx i = 0; i < c->num_cells; i++, p++) {
		if (is_var(p))
			return true;
	}

	return false;
}

// import_term creates an imported term's variables in the CURRENT frame.
// Once unified into the caller's term, any structure the caller now
// holds (eg. an answer argument that is a list containing a shared
// variable) points at slots of THIS frame. Trimming the frame on
// deterministic exit recycles those slots and silently breaks variable
// identity: two occurrences of one answer variable stop being the same
// variable, so binding one no longer binds the other.
//
// The VM protects the analogous case itself - set_var() raises
// q->no_recov when a caller variable is bound to a non-ground compound
// living in a younger frame - but that flag is only transferred to the
// next frame created, so a binding performed inside a builtin never
// reaches the frame that owns the variables. Pin it explicitly.

static void tbl_pin_answer_frame(query *q, const cell *tmp)
{
	if (!tbl_has_vars(tmp))
		return;

	frame *f = GET_FRAME(q->st.cur_ctx);
	f->no_recov = true;
}

// '$tbl_variant_table'(+Variant, -Handle, -Status)

// --- item 4: publication ---
//
// Transfer ownership of a completed table from the building thread to
// the shared registry. Everything before this point was thread-private
// and unlocked; from here the table is immutable and visible to all.
//
// Ownership TRANSFERS rather than being copied: the table leaves the
// thread's all_tables list and its private slot is released, so the
// builder afterwards reaches it through the shared path like any other
// thread. Two threads publishing the same variant race, and the loser
// simply keeps its private copy - correct, just not shared.

#if USE_THREADS
static void tbl_publish(query *q, tbl_state *s, table *t)
{
	tbl_shared *sh = tbl_shared_get(q);

	if (!sh)
		return;

	acquire_lock(&sh->guard);

	// Someone else got there first with this variant.

	// The stored image is detached, so bring it back onto the heap
	// before walking it - trie_insert_ derefs against a live context.

	cell *key = import_term(q, t->key_image, q->st.cur_ctx);

	if (!key) {
		release_lock(&sh->guard);
		return;
	}

	bool existed = false, attvar = false, restrained = false;
	tnode *leaf = trie_insert_(q, &sh->variants, key, q->st.cur_ctx,
		&existed, &attvar, 0, &restrained, 0);

	if (!leaf || leaf->value) {
		release_lock(&sh->guard);
		return;
	}

	if (!tbl_shared_slot_alloc(sh, t)) {
		release_lock(&sh->guard);
		return;
	}

	// Unlink from the private side. The private slot must be released
	// AFTER the shared one is taken, so t->slot is never ambiguous.

	for (table **pp = &s->all_tables; *pp; pp = &(*pp)->all_next) {
		if (*pp == t) { *pp = t->all_next; break; }
	}

	if (t->leaf)
		t->leaf->value = NULL;		// private variant trie forgets it

	t->leaf = leaf;
	leaf->value = t;
	t->all_next = sh->all;
	sh->all = t;
	t->is_shared = true;

	// Everything above must be visible before any reader can find it.
	// The release paired with each reader's acquire on this same lock.

	pl_publish_barrier();
	release_lock(&sh->guard);
}
// Reader side. Look the variant up in the shared registry BEFORE
// building anything privately. The lock is held only across the trie
// walk; the table it returns is immutable, so nothing is held while it
// is used.

static table *tbl_shared_lookup(query *q, cell *c, pl_ctx c_ctx)
{
	tbl_shared *sh = tbl_shared_peek(q);

	if (!sh || !sh->variants)
		return NULL;

	acquire_lock(&sh->guard);
	twalk w;
	twalk_init(&w, q, &sh->variants, false);
	bool ok = trie_walk(&w, c, c_ctx);
	tnode *leaf = w.node;
	twalk_done(&w);
	table *t = (ok && leaf && leaf->is_leaf) ? leaf->value : NULL;
	release_lock(&sh->guard);
	return t;
}
#else
static void tbl_publish(query *q, tbl_state *s, table *t)
{
	(void)q; (void)s; (void)t;
}

static table *tbl_shared_lookup(query *q, cell *c, pl_ctx c_ctx)
{
	(void)q; (void)c; (void)c_ctx;
	return NULL;
}
#endif

// --- item 3: validate-on-read ---
//
// Drop a completed incremental table back to FRESH when anything it
// depends on has changed since it completed. Invalidation is a full
// drop, NOT '$tbl_reset_incomplete' (which deliberately keeps answers):
// with answer subsumption landed, leaf->value in the answer trie points
// at live tbl_ans structs, so freeing answers without clearing the trie
// would leave dangling pointers in the dedup path.

static void tbl_drop_answers(table *t)
{
	trie_free(t->answers);
	t->answers = NULL;

	for (tbl_ans *a = t->first_ans; a; ) {
		tbl_ans *next = a->next;
		tbl_image_free(a->image);
		TPL_free(a);
		a = next;
	}

	t->first_ans = t->last_ans = t->unproc_ans = NULL;
	t->update_head = t->update_tail = NULL;
	t->n_answers = 0;
	tbl_free_pending(t);
}

static bool tbl_deps_changed(query *q, tbl_state *s, table *t, unsigned depth)
{
	if (t->deps_incomplete)
		return true;

	// A cycle in the table graph (mutual recursion across SCCs) would
	// otherwise recurse forever. Depth-capping is conservative in the
	// safe direction: treat it as changed and recompute.

	if (depth > 32)
		return true;

	for (tbl_dep *d = t->deps; d; d = d->next) {
		if (!d->is_table) {
			cell tmp = (cell){0};
			tmp.tag = TAG_INTERNED;
			tmp.val_off = d->functor;
			set_arity(&tmp, d->arity);
			predicate *pr = find_predicate(d->m, &tmp);

			// Gone entirely (module unloaded, predicate abolished) is
			// a change like any other.

			if (!pr || (pr->last_modified > t->completed_at))
				return true;
		} else {
			table *dep = tbl_handle_from_value(s, d->handle);

			// Stale handle: the table it named has been reclaimed, so
			// we cannot show it is still valid.

			if (!dep)
				return true;

			if (dep->status != TBL_COMPLETE)
				continue;

			if (tbl_deps_changed(q, s, dep, depth+1))
				return true;
		}
	}

	return false;
}

static void tbl_revalidate(query *q, tbl_state *s, table *t)
{
	// Nothing in the database has changed since this table completed,
	// so nothing it depends on can have. One comparison, and the common
	// case never walks the dep list at all.

	if ((uint64_t)q->pl->dbgen == t->completed_at)
		return;

	if (!tbl_deps_changed(q, s, t, 0))
		return;

	tbl_drop_answers(t);

	for (tbl_susp *sp = t->first_susp; sp; ) {
		tbl_susp *next = sp->next;
		tbl_image_free(sp->image);
		TPL_free(sp);
		sp = next;
	}

	t->first_susp = t->last_susp = t->unproc_susp = NULL;
	tbl_deps_free(t->deps);
	t->deps = NULL;
	t->deps_incomplete = false;
	t->in_wl = false;
	t->status = TBL_FRESH;
}

static bool bif_tbl_variant_table_3(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	GET_NEXT_ARG(p3,any);

	tbl_intern_atoms(q);

	// Item 4: a published table answers this variant already. Take it
	// before building anything privately - that saving IS the feature.
	// The lock is dropped inside the lookup; what comes back is
	// immutable, so it is safe to hold across the unifications below.

	{
		table *sht = tbl_shared_lookup(q, p1, p1_ctx);

		if (sht) {
			cell tmp;
			make_tbl_handle(s, &tmp, sht);

			if (!unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
				return false;

			make_atom(&tmp, s_complete);
			return unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
		}
	}

	bool existed = false, attvar = false, restrained = false;
	tnode *leaf = trie_insert_(q, &s->variants, p1, p1_ctx, &existed, &attvar,
		q->pl->tbl_max_subgoal_size, &restrained, 0);

	if (!leaf) {
		if (attvar)
			return throw_error(q, p1, p1_ctx, "type_error", "free_variable");

		if (restrained)
			return throw_error(q, p1, p1_ctx, "resource_error", "max_table_subgoal_size");

		return throw_error(q, p1, p1_ctx, "representation_error", "tabled_call");
	}

	table *t = leaf->value;

	if (!t) {
		t = TPL_calloc(1, sizeof(table));
		CHECKED(t);
		t->status = TBL_FRESH;
		// A non-interned callable would record functor 0 and then be
		// invisible to abolish_table/1. Callables reaching here are
		// interned in practice; if that ever changes this needs a real
		// key rather than a silent 0.

		t->functor = is_interned(p1) ? p1->val_off : 0;
		t->arity = get_arity(p1);
		t->leaf = leaf;
		t->all_next = s->all_tables;
		s->all_tables = t;
		leaf->value = t;

		if (!tbl_slot_alloc(s, t)) {
			s->all_tables = t->all_next;
			leaf->value = NULL;
			TPL_free(t);
			return throw_error(q, p1, p1_ctx, "resource_error", "memory");
		}
	}

	// Item 3, validate-on-read. A COMPLETE incremental table whose
	// dependencies have moved on since it completed is dropped back to
	// FRESH here, so the status returned below makes the caller
	// recompute it. Done by the owning thread at lookup rather than by
	// the asserting thread at write time - tables are per-thread, the
	// database is not.

	if ((t->status == TBL_COMPLETE) && t->is_incremental)
		tbl_revalidate(q, s, t);

	// Item 3, table->table edge: this lookup happens while some SCC is
	// being completed, so that SCC's tables depend on this one. Handles
	// the transitive case - A calls B, B reads q - without needing the
	// walk to understand tabling at all.

	if (s->scc_depth && (t->scc != tbl_scc_id(s))) {
		tbl_dep want = {0};
		want.is_table = true;
		want.handle = tbl_handle_value(s, t);
		tbl_scc_add_dep(&s->scc[s->scc_depth-1], &want);
	}

	cell tmp;
	make_tbl_handle(s, &tmp, t);

	if (!unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
		return false;

	make_atom(&tmp, t->status == TBL_FRESH ? s_fresh :
		t->status == TBL_ACTIVE ? s_active : s_complete);
	return unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
}

// '$tbl_set_subsumptive'(+Handle, +Pos, +Op) - declares this table
// mode-directed: Pos (1-based) is the aggregated argument, Op is `min`
// or `max`. Called once, right after a FRESH table is created, by the
// driver looking up the predicate's ":- table Name(...)" mode spec.
// Idempotent - setting it again on an already-configured table is a
// harmless no-op in practice, since the driver only calls it on the
// fresh->active transition.

// '$tbl_set_pred_incremental'(+Name, +Arity) - mark a DYNAMIC predicate
// as one whose changes invalidate tables. Backs incremental/1, which
// is what ":- incremental q/1" runs (an unknown directive is executed
// as an ordinary goal, so this needs no parser support - unlike
// ":- dynamic q/1 as incremental", whose `as` lands in the parser's
// predicate-indicator error path).

static bool bif_tbl_set_pred_incremental_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,integer);

	if (is_negative(p2) || is_bigint(p2))
		return throw_error(q, p2, p2_ctx, "domain_error", "not_less_than_zero");

	unsigned arity = (unsigned)get_smallint(p2);

	if (arity > MAX_PROCEDURE_ARITY)
		return throw_error(q, p2, p2_ctx, "representation_error", "max_arity");

	cell tmp = (cell){0};
	tmp.tag = TAG_INTERNED;
	tmp.val_off = p1->val_off;
	set_arity(&tmp, arity);

	// The module being CONSULTED, not q->st.m. This runs as a goal from
	// incremental/1, which lives in module `tabling`, so q->st.m is
	// `tabling` - resolving there missed the user's predicate entirely
	// and silently created an empty one inside the library instead.
	// pl->m is what the parser is loading into, which is what a
	// directive means by an unqualified name (same answer
	// set_dynamic_in_db gets from p->m).

	module *m = q->pl->m ? q->pl->m : q->st.m;
	predicate *pr = find_predicate(m, &tmp);

	// Declared before any clause exists is the normal case (it follows
	// a ":- dynamic q/1"), so create rather than complain.

	if (!pr) pr = create_predicate(m, &tmp, NULL);

	if (!pr)
		return throw_error(q, p1, p1_ctx, "existence_error", "procedure");

	pr->is_incremental = true;
	return true;
}

// '$tbl_set_incremental'(+Handle) - mark a TABLE as incremental, ie.
// worth collecting dependencies for and re-validating on lookup.

// '$tbl_set_shared'(+Handle, +Variant) - declare this table publishable
// once complete. Takes the call term because publication happens at
// completion, long after the original call is gone, and the shared
// registry's trie has to be keyed on it.

static bool bif_tbl_set_shared_2(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);
	table *t = tbl_handle(s, p1);

	if (!t)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	// Invalidation MUTATES a completed table (drops answers, resets to
	// FRESH), which is exactly what publication promises never happens.
	// Allowing both would let any reader free memory another thread is
	// walking - silent and intermittent. Refuse instead.

	if (t->is_incremental)
		return throw_error(q, p1, p1_ctx, "domain_error", "shared_incremental_table");

	if (!t->key_image) {
		t->key_image = tbl_image(q, p2, p2_ctx);
		CHECKED(t->key_image);
	}

	t->wants_shared = true;
	return true;
}

static bool bif_tbl_set_incremental_1(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	table *t = tbl_handle(s, p1);

	if (!t)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	t->is_incremental = true;
	return true;
}

// '$tbl_specs_declared' / '$tbl_has_specs' - see prolog.tbl_any_specs.

static bool bif_tbl_specs_declared_0(query *q)
{
	q->pl->tbl_any_specs = true;
	return true;
}

static bool bif_tbl_has_specs_0(query *q)
{
	return q->pl->tbl_any_specs;
}

static bool bif_tbl_set_subsumptive_3(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);
	GET_NEXT_ARG(p3,atom);
	table *t = tbl_handle(s, p1);

	if (!t)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	if (is_negative(p2) || !get_smallint(p2))
		return throw_error(q, p2, p2_ctx, "domain_error", "not_less_than_zero");

	tbl_intern_atoms(q);

	if (p3->val_off == s_min) t->agg_max = false;
	else if (p3->val_off == s_max) t->agg_max = true;
	else return throw_error(q, p3, p3_ctx, "domain_error", "table_mode");

	t->agg_pos = (unsigned)get_smallint(p2);
	return true;
}

// Live-or-imported term, walk to the Nth (1-based) argument. Used both
// on the new answer (still live in the query) and on a stored answer's
// detached image (import_term'd into the query first) - same shape
// either way once the caller has a cell+ctx pair.

static cell *tbl_nth_arg(query *q, cell *c, pl_ctx ctx, unsigned n, pl_ctx *out_ctx)
{
	c = deref(q, c, ctx);
	ctx = q->latest_ctx;

	if (!is_structure(c) || !n || (n > get_arity(c)))
		return NULL;

	cell *arg = c + 1;

	for (unsigned i = 1; i < n; i++)
		arg += arg->num_cells;

	arg = deref(q, arg, ctx);
	*out_ctx = q->latest_ctx;
	return arg;
}

// '$tbl_set_status'(+Handle, +Status)

static bool bif_tbl_set_status_2(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,atom);
	tbl_intern_atoms(q);
	table *t = tbl_handle(s, p1);

	if (!t)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	if (p2->val_off == s_fresh) t->status = TBL_FRESH;
	else if (p2->val_off == s_active) t->status = TBL_ACTIVE;
	else if (p2->val_off == s_complete) t->status = TBL_COMPLETE;
	else return throw_error(q, p2, p2_ctx, "domain_error", "table_status");

	return true;
}

// '$tbl_add_answer'(+Handle, +Answer) - semidet: FAILS on duplicate.

static bool bif_tbl_add_answer_2(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);
	table *t = tbl_handle(s, p1);

	if (!t)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	// Answer subsumption (item 2): t->agg_pos != 0 means the answer
	// trie is keyed on every argument EXCEPT agg_pos (trie_insert_'s
	// skip_arg), so two answers agreeing on the rest collide - existed
	// then means "this key already has a stored value", not "duplicate
	// answer", and is handled below instead of the plain dedup return.

	bool existed = false, attvar = false, restrained = false;
	tnode *leaf = trie_insert_(q, &t->answers, p2, p2_ctx, &existed, &attvar,
		q->pl->tbl_max_answer_size, &restrained, t->agg_pos);

	if (!leaf) {
		if (attvar)
			return throw_error(q, p2, p2_ctx, "type_error", "free_variable");

		if (restrained)
			return throw_error(q, p2, p2_ctx, "resource_error", "max_table_answer_size");

		return throw_error(q, p2, p2_ctx, "representation_error", "tabled_answer");
	}

	if (existed && !t->agg_pos)
		return false;

	if (existed) {
		// Subsumptive and this key has a stored answer already: compare
		// the new value at agg_pos against the stored one and replace
		// only if it is better. A rejected (dominated) answer is exactly
		// like a plain duplicate - nothing changes, fail.

		pl_ctx new_ctx;
		cell *new_agg = tbl_nth_arg(q, p2, p2_ctx, t->agg_pos, &new_ctx);

		if (!new_agg)
			return throw_error(q, p2, p2_ctx, "type_error", "callable");

		tbl_ans *old = leaf->value;
		cell *old_full = tbl_answer_term(q, old);
		CHECKED(old_full);
		pl_ctx old_ctx;
		cell *old_agg = tbl_nth_arg(q, old_full, q->st.cur_ctx, t->agg_pos, &old_ctx);
		if (!old_agg) return throw_error(q, p2, p2_ctx, "type_error", "callable");

		int c = compare(q, new_agg, new_ctx, old_agg, old_ctx);
		bool better = t->agg_max ? (c > 0) : (c < 0);

		if (!better)
			return false;

		cell *new_image = tbl_image(q, p2, p2_ctx);
		CHECKED(new_image);
		tbl_image_free(old->image);
		old->image = new_image;

		// Already covered by the (new answers x all suspensions) pass
		// this cycle - queueing it too would just re-pair it twice.

		if (!old->is_new && !old->in_update_queue) {
			old->in_update_queue = true;
			old->update_next = NULL;

			if (t->update_tail) t->update_tail->update_next = old;
			else t->update_head = old;

			t->update_tail = old;
		}

		if (t->first_susp)
			tbl_enqueue(s, t);

		return true;
	}

	// max_answers_for_subgoal: this answer is genuinely new (existed ==
	// false), so it is the one that would push the table over the
	// limit. The trie leaf stays marked is_leaf (see trie_insert_) - a
	// re-derivation after the exception unwinds this table back to
	// FRESH (run_scc's catch) hits the identical answer at the
	// identical count and raises the same error again, which is the
	// point: a breached table stays diagnostic, not silently partial.
	//
	// For a subsumptive table this bounds the number of DISTINCT KEYS,
	// not raw answers - an update to an existing key never reaches
	// here, matching "subsumption bounds tables from the other side".

	if (q->pl->tbl_max_answers_for_subgoal && (t->n_answers >= q->pl->tbl_max_answers_for_subgoal))
		return throw_error(q, p2, p2_ctx, "resource_error", "max_answers_for_subgoal");

	t->n_answers++;

	tbl_ans *a = TPL_calloc(1, sizeof(tbl_ans));
	CHECKED(a);
	a->leaf = leaf;

	// Item 5: the trie path IS the answer, so no second copy - except
	// for a subsumptive table, whose trie deliberately omits the
	// aggregated argument and so cannot reproduce it.

	if (t->agg_pos) {
		a->image = tbl_image(q, p2, p2_ctx);
		CHECKED(a->image);
	}

	a->is_new = true;

	if (t->agg_pos)
		leaf->value = a;

	if (t->last_ans) t->last_ans->next = a; else t->first_ans = a;
	t->last_ans = a;

	if (!t->unproc_ans)
		t->unproc_ans = a;

	if (t->first_susp)
		tbl_enqueue(s, t);

	return true;
}

// '$tbl_get_answer'(+Handle, ?Answer) - nondet enumeration.

static bool bif_tbl_get_answer_2(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);
	table *t = tbl_handle(s, p1);

	if (!t)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	// Stop cleanly if the tables were abolished under a live enumeration.

	if (q->retry && (q->st.v2 != s->generation))
		return false;

	tbl_ans *a = q->retry ? (tbl_ans*)(size_t)q->st.v1 : t->first_ans;

	if (!a)
		return false;

	// No choice point on the last answer. That is only sound because a
	// COMPLETE table is immutable: nothing appends after completion, so
	// the answer that has no successor now will not acquire one. Phase 2
	// breaks this in two places - answer subsumption *updates* existing
	// answers, and batched scheduling posts answers before completion -
	// and either would silently drop answers here.

	if (a->next) {
		q->st.v1 = (uint64_t)(size_t)a->next;
		q->st.v2 = s->generation;
		CHECKED(push_choice(q));
	}

	cell *tmp = tbl_answer_term(q, a);
	CHECKED(tmp);
	tbl_pin_answer_frame(q, tmp);
	return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
}

// '$tbl_add_suspension'(+Handle, +Dependency)

static bool bif_tbl_add_suspension_2(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);
	table *t = tbl_handle(s, p1);

	if (!t)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	// Depending on a table owned by an outer SCC means this SCC cannot
	// complete on its own (see the SCC comment above).

	if (s->scc_depth && t->scc && (t->scc < tbl_scc_id(s))) {
		tscc *top = &s->scc[s->scc_depth-1];

		if (!top->dep_min || (t->scc < top->dep_min))
			top->dep_min = t->scc;
	}

	tbl_susp *sp = TPL_calloc(1, sizeof(tbl_susp));
	CHECKED(sp);
	sp->image = tbl_image(q, p2, p2_ctx);
	CHECKED(sp->image);

	if (t->last_susp) t->last_susp->next = sp; else t->first_susp = sp;
	t->last_susp = sp;

	if (!t->unproc_susp)
		t->unproc_susp = sp;

	if (t->first_ans)
		tbl_enqueue(s, t);

	return true;
}

// '$tbl_pop_worklist'(-Handle) - semidet; materializes the new work.

static bool bif_tbl_pop_worklist_1(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,any);

	// Only drain tables owned by the SCC we are completing; work for
	// outer SCCs is left for their own completion loops.

	unsigned scc_id = tbl_scc_id(s);
	table *t = s->wl_head, *prev = NULL;

	while (t && (t->scc != scc_id)) {
		prev = t;
		t = t->wl_next;
	}

	if (!t)
		return false;

	if (prev)
		prev->wl_next = t->wl_next;
	else
		s->wl_head = t->wl_next;

	t->wl_next = NULL;
	t->in_wl = false;

	tbl_free_pending(t);
	tbl_pair **tail = &t->pending;

	// (new answers x all suspensions) - also the point where a node
	// stops being "new": from here on, a subsumption update to it must
	// go through the update queue below to be seen again.

	for (tbl_ans *a = t->unproc_ans; a; a = a->next) {
		a->is_new = false;

		for (tbl_susp *sp = t->first_susp; sp; sp = sp->next) {
			tbl_pair *p = TPL_malloc(sizeof(tbl_pair));
			CHECKED(p);
			p->a = a; p->s = sp; p->next = NULL;
			*tail = p; tail = &p->next;
		}
	}

	// (old answers x new suspensions); old = before unproc_ans

	for (tbl_ans *a = t->first_ans; a && a != t->unproc_ans; a = a->next) {
		for (tbl_susp *sp = t->unproc_susp; sp; sp = sp->next) {
			tbl_pair *p = TPL_malloc(sizeof(tbl_pair));
			CHECKED(p);
			p->a = a; p->s = sp; p->next = NULL;
			*tail = p; tail = &p->next;
		}
	}

	// (updated answers x all suspensions) - answer subsumption (item
	// 2): a value updated in place (not a brand-new key, see
	// bif_tbl_add_answer_2) must be re-delivered to every CURRENT
	// suspension, including ones already paired with the stale value -
	// append-only pairing above would silently leave them with it.

	for (tbl_ans *a = t->update_head; a; a = a->update_next) {
		for (tbl_susp *sp = t->first_susp; sp; sp = sp->next) {
			tbl_pair *p = TPL_malloc(sizeof(tbl_pair));
			CHECKED(p);
			p->a = a; p->s = sp; p->next = NULL;
			*tail = p; tail = &p->next;
		}
	}

	for (tbl_ans *a = t->update_head; a; ) {
		tbl_ans *next = a->update_next;
		a->in_update_queue = false;
		a->update_next = NULL;
		a = next;
	}

	t->update_head = t->update_tail = NULL;
	t->unproc_ans = NULL;
	t->unproc_susp = NULL;

	cell tmp;
	make_tbl_handle(s, &tmp, t);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

// '$tbl_wkl_work'(+Handle, -Answer, -Dependency) - nondet over the
// materialized pairs; each solution gets fresh copies.

static bool bif_tbl_wkl_work_3(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);
	GET_NEXT_ARG(p3,any);
	table *t = tbl_handle(s, p1);

	if (!t)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	if (q->retry && (q->st.v2 != s->generation))
		return false;

	tbl_pair *p = q->retry ? (tbl_pair*)(size_t)q->st.v1 : t->pending;

	if (!p)
		return false;

	if (p->next) {
		q->st.v1 = (uint64_t)(size_t)p->next;
		q->st.v2 = s->generation;
		CHECKED(push_choice(q));
	}

	cell *ta = tbl_answer_term(q, p->a);
	CHECKED(ta);
	tbl_pin_answer_frame(q, ta);

	if (!unify(q, p2, p2_ctx, ta, q->st.cur_ctx))
		return false;

	cell *td = import_term(q, p->s->image, q->st.cur_ctx);
	CHECKED(td);
	tbl_pin_answer_frame(q, td);
	return unify(q, p3, p3_ctx, td, q->st.cur_ctx);
}

// Leader flag (the "scheduling component" of the Desouter driver).

static bool bif_tbl_leader_0(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);
	return s->scc_depth != 0;
}

// '$tbl_push_scc'(+Handle): open a nested SCC owned by this table.

static bool bif_tbl_push_scc_1(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	table *t = tbl_handle(s, p1);

	if (!t)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	if (s->scc_depth >= s->scc_max) {
		unsigned nmax = s->scc_max ? s->scc_max*2 : 64;
		tscc *tmp = TPL_realloc(s->scc, sizeof(tscc)*nmax);
		CHECKED(tmp);
		s->scc = tmp;
		s->scc_max = nmax;
	}

	tscc *top = &s->scc[s->scc_depth++];
	top->id = s->scc_next_id++;
	top->dep_min = 0;
	top->fresh_head = t;
	top->deps = NULL;			// item 3
	top->deps_incomplete = false;
	t->scc = top->id;
	t->fresh_next = NULL;
	s->in_use++;
	return true;
}

// '$tbl_pop_scc'(-Escaped): close it. Escaped == true means this SCC
// depends on an outer one, so its tables are merged into the parent
// (which will complete them) instead of being completed here.

static bool bif_tbl_pop_scc_1(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,any);

	if (!s->scc_depth)
		return false;

	tscc *top = &s->scc[--s->scc_depth];

	if (s->in_use)
		s->in_use--;

	bool escaped = top->dep_min != 0;

	if (escaped && s->scc_depth) {
		tscc *parent = &s->scc[s->scc_depth-1];

		// Merge: re-tag and hand our tables to the parent.

		table *t = top->fresh_head, *last = NULL;

		for (; t; t = t->fresh_next) {
			t->scc = parent->id;
			last = t;
		}

		if (last) {
			last->fresh_next = parent->fresh_head;
			parent->fresh_head = top->fresh_head;
		}

		// Still depending further out? Propagate.

		if (top->dep_min < parent->id) {
			if (!parent->dep_min || (top->dep_min < parent->dep_min))
				parent->dep_min = top->dep_min;
		}

		// Item 3: the tables went to the parent, so their dependencies
		// must go with them - the parent completes them and will flush
		// its dep set onto them.

		for (tbl_dep *d = top->deps; d; ) {
			tbl_dep *next = d->next;
			tbl_scc_add_dep(parent, d);
			TPL_free(d);
			d = next;
		}

		if (top->deps_incomplete)
			parent->deps_incomplete = true;

		top->deps = NULL;
	}

	tbl_deps_free(top->deps);		// non-escaping: flushed at completion
	top->deps = NULL;

	top->fresh_head = NULL;
	cell tmp;
	make_atom(&tmp, escaped ? g_true_s : g_false_s);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

// '$tbl_scc_escaped' - semidet: the SCC being completed depends on an
// outer one, so its tables are the parent's to complete, not ours.

static bool bif_tbl_scc_escaped_0(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	(void)q;

	if (!s->scc_depth)
		return false;

	return s->scc[s->scc_depth-1].dep_min != 0;
}

// '$tbl_mark_all_complete' - complete every table created since the
// leader started (the library's "newly created table identifiers").

static bool bif_tbl_mark_all_complete_0(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	(void)q;

	if (!s->scc_depth)
		return true;

	tscc *top = &s->scc[s->scc_depth-1];

	for (table *t = top->fresh_head; t; ) {
		table *next = t->fresh_next;
		t->status = TBL_COMPLETE;
		t->fresh_next = NULL;
		tbl_free_pending(t);

		// A completed table never resumes consumers again: its
		// suspensions (whole continuation images) are dead weight.
		// Freeing here is a large win for many-table workloads.

		for (tbl_susp *sp = t->first_susp; sp; ) {
			tbl_susp *snext = sp->next;
			tbl_image_free(sp->image);
			TPL_free(sp);
			sp = snext;
		}

		t->first_susp = t->last_susp = t->unproc_susp = NULL;

		// Item 3: flush the SCC's dependency set onto each table it
		// completed. Only tables of incremental predicates keep it -
		// for anything else the set is dead weight and the table is
		// permanent as before.

		if (t->is_incremental) {
			tbl_deps_free(t->deps);
			t->deps = NULL;
			t->deps_incomplete = top->deps_incomplete;

			for (tbl_dep *d = top->deps; d; d = d->next) {
				tbl_dep *copy = TPL_calloc(1, sizeof(tbl_dep));

				if (!copy) {
					t->deps_incomplete = true;
					break;
				}

				*copy = *d;
				copy->next = t->deps;
				t->deps = copy;
			}

			t->completed_at = (uint64_t)q->pl->dbgen;
		}

		// Item 4: publish. The table is complete and will never be
		// written again, so hand it to the registry. Only a CLEAN
		// completion qualifies - saw_exception is checked by the
		// caller, and an incremental table is refused at declaration
		// time because invalidation would mutate it under readers.

		if (t->wants_shared && !t->is_shared)
			tbl_publish(q, s, t);

		t = next;
	}

	tbl_deps_free(top->deps);
	top->deps = NULL;
	top->deps_incomplete = false;
	top->fresh_head = NULL;
	s->saw_exception = false;
	return true;
}

// Roll back tables left ACTIVE by an aborted leader so a later call
// re-computes them instead of suspending on a table nobody will finish.

static bool bif_tbl_note_exception_0(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	(void)q;
	s->saw_exception = true;
	return true;
}

static bool bif_tbl_saw_exception_0(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	(void)q;
	return s->saw_exception;
}

static bool bif_tbl_reset_incomplete_0(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	(void)q;

	if (!s->scc_depth)
		return true;

	tscc *rtop = &s->scc[s->scc_depth-1];

	for (table *t = rtop->fresh_head; t; ) {
		table *next = t->fresh_next;

		if (t->status != TBL_COMPLETE) {
			t->status = TBL_FRESH;
			tbl_free_pending(t);

			for (tbl_susp *sp = t->first_susp; sp; ) {
				tbl_susp *snext = sp->next;
				tbl_image_free(sp->image);
				TPL_free(sp);
				sp = snext;
			}

			t->first_susp = t->last_susp = t->unproc_susp = NULL;
			t->unproc_ans = t->first_ans;
			t->in_wl = false;

			// Recomputation restarts from scratch, so every existing
			// answer is "new" again for pairing purposes (matches
			// unproc_ans above) - and any pending subsumption update is
			// moot, since the recompute will re-derive and re-queue
			// whatever still needs it.

			for (tbl_ans *a = t->first_ans; a; a = a->next) {
				a->is_new = true;
				a->in_update_queue = false;
				a->update_next = NULL;
			}

			t->update_head = t->update_tail = NULL;
		}

		t->fresh_next = NULL;
		t = next;
	}

	rtop->fresh_head = NULL;
	s->saw_exception = false;
	return true;
}

// Drop every table. Shared by abolish_all_tables/0 and by
// tabling_destroy() at instance teardown.

// Item 4: retire every published table. It is NOT freed - another
// thread may be reading one right now, and freeing under a live reader
// is the silent, intermittent use-after-free this whole design exists
// to avoid. Instead it is unpublished: dropped from the shared variant
// trie so the next call misses and recomputes, and its slot serial
// bumped so outstanding handles stop validating. The memory is
// reclaimed at pl_destroy(), when every thread is provably done.
//
// So abolish gets correct SEMANTICS at the cost of holding the old
// table's memory until teardown. Freeing it earlier needs the
// generation-keyed deferred reclamation the design doc describes,
// which is not built.

static void tbl_shared_retire_all(query *q)
{
	tbl_shared *sh = tbl_shared_peek(q);

	if (!sh)
		return;

#if USE_THREADS
	acquire_lock(&sh->guard);
#endif

	for (table *t = sh->all; t; t = t->all_next) {
		if (!t->is_shared)
			continue;

		if (t->leaf) {
			t->leaf->value = NULL;
			t->leaf = NULL;
		}

		if (t->slot < sh->nslots && sh->slots[t->slot].t == t) {
			sh->slots[t->slot].t = NULL;
			sh->slots[t->slot].serial++;
		}

		t->is_shared = false;		// retired; awaiting teardown
	}

#if USE_THREADS
	release_lock(&sh->guard);
#endif
}

static void tbl_clear_all(tbl_state *s)
{
	for (table *t = s->all_tables; t; ) {
		table *next = t->all_next;
		tbl_slot_release(s, t);
		tbl_destroy(t);
		t = next;
	}

	trie_free(s->variants);
	s->variants = NULL;
	s->all_tables = s->wl_head = s->fresh_head = NULL;
	s->leader = false;
	s->scc_depth = 0;

	// The SCC stack is a high-water-mark array that otherwise lives as
	// long as the instance; nothing is on it here, so hand it back.

	TPL_free(s->scc);
	s->scc = NULL;
	s->scc_max = 0;
	s->saw_exception = false;
}

// Free one thread's tables. Called when a thread slot retires, so a
// long-lived process spawning many threads does not accumulate them.

void tabling_destroy_thread(thread *t)
{
	tbl_state *s = (tbl_state*)t->tabling_state;

	if (!s)
		return;

	tbl_clear_all(s);
	TPL_free(s->slots);
	TPL_free(s);
	t->tabling_state = NULL;
}

// Instance teardown: sweep every struct, live or retired - the main
// thread included, and any that exited without going through the retire
// path. A retired thread keeps its tabling state until its struct is
// reused, so the free list has to be swept too.

void tabling_destroy(prolog *pl)
{
	for (thread *t = pl->live_head; t; t = t->live_next)
		tabling_destroy_thread(t);

	for (thread *t = pl->free_head; t; t = t->free_next)
		tabling_destroy_thread(t);

	// Item 4: published tables outlive the thread that built them, so
	// they are reclaimed here rather than with any thread's state.
	// Every thread is done by now, which is what makes it safe to free
	// them without the deferred-reclamation dance a live abolish needs.

	tbl_shared *sh = (tbl_shared*)pl->tbl_shared;

	if (sh) {
		for (table *t = sh->all; t; ) {
			table *next = t->all_next;
			tbl_destroy(t);
			t = next;
		}

		trie_free(sh->variants);
		TPL_free(sh->slots);
#if USE_THREADS
		if (sh->inited) deinit_lock(&sh->guard);
#endif
		TPL_free(sh);
		pl->tbl_shared = NULL;
	}
}

// abolish_table/1: drop every variant of ONE predicate. Without this the
// only way to invalidate a table after assert/retract is
// abolish_all_tables/0, which throws away unrelated work too.
//
// The trie node stays; resetting its value to NULL just makes the next
// call see a fresh variant. Nodes are cheap and the same call is likely
// to come back.

static bool bif_tbl_abolish_1(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,integer);

	// Same rule as abolish_all_tables/0: never while a leader is
	// driving completion or a frame is still enumerating.

	if (s->in_use)
		return throw_error(q, p1, p1_ctx, "permission_error", "modify,table");

	const pl_idx functor = p1->val_off;
	const unsigned arity = (unsigned)get_smallint(p2);
	table *prev = NULL;
	unsigned dropped = 0;

	for (table *t = s->all_tables; t; ) {
		table *next = t->all_next;

		if ((t->functor == functor) && (t->arity == arity)) {
			if (t->leaf)
				t->leaf->value = NULL;

			if (prev)
				prev->all_next = next;
			else
				s->all_tables = next;

			tbl_slot_release(s, t);
			tbl_destroy(t);
			dropped++;
		} else
			prev = t;

		t = next;
	}

	// Only disturb live enumerations if something actually went.

	if (dropped)
		s->generation++;

	return true;
}

static bool bif_tbl_abolish_all_tables_0(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	// Freeing tables while a leader is driving completion (or while an
	// enumeration frame is live) would leave dangling handles behind.

	if (s->in_use)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "permission_error", "modify,table");

	s->generation++;
	tbl_clear_all(s);
	tbl_shared_retire_all(q);	// item 4
	return true;
}

builtins g_tabling_bifs[] =
{
	{"$tbl_variant_table", 3, bif_tbl_variant_table_3, "+term,-integer,-atom", false, false, BLAH},
	{"$tbl_set_status", 2, bif_tbl_set_status_2, "+integer,+atom", false, false, BLAH},
	{"$tbl_set_subsumptive", 3, bif_tbl_set_subsumptive_3, "+integer,+integer,+atom", false, false, BLAH},
	{"$tbl_set_pred_incremental", 2, bif_tbl_set_pred_incremental_2, "+atom,+integer", false, false, BLAH},
	{"$tbl_set_incremental", 1, bif_tbl_set_incremental_1, "+integer", false, false, BLAH},
	{"$tbl_set_shared", 2, bif_tbl_set_shared_2, "+integer,+term", false, false, BLAH},
	{"$tbl_specs_declared", 0, bif_tbl_specs_declared_0, "", false, false, BLAH},
	{"$tbl_has_specs", 0, bif_tbl_has_specs_0, "", false, false, BLAH},
	{"$tbl_add_answer", 2, bif_tbl_add_answer_2, "+integer,+term", false, false, BLAH},
	{"$tbl_get_answer", 2, bif_tbl_get_answer_2, "+integer,?term", false, false, BLAH},
	{"$tbl_add_suspension", 2, bif_tbl_add_suspension_2, "+integer,+term", false, false, BLAH},
	{"$tbl_pop_worklist", 1, bif_tbl_pop_worklist_1, "-integer", false, false, BLAH},
	{"$tbl_wkl_work", 3, bif_tbl_wkl_work_3, "+integer,-term,-term", false, false, BLAH},
	{"$tbl_leader", 0, bif_tbl_leader_0, "", false, false, BLAH},
	{"$tbl_push_scc", 1, bif_tbl_push_scc_1, "+integer", false, false, BLAH},
	{"$tbl_pop_scc", 1, bif_tbl_pop_scc_1, "-atom", false, false, BLAH},
	{"$tbl_scc_escaped", 0, bif_tbl_scc_escaped_0, "", false, false, BLAH},
	{"$tbl_mark_all_complete", 0, bif_tbl_mark_all_complete_0, "", false, false, BLAH},
	{"$tbl_reset_incomplete", 0, bif_tbl_reset_incomplete_0, "", false, false, BLAH},
	{"$tbl_note_exception", 0, bif_tbl_note_exception_0, "", false, false, BLAH},
	{"$tbl_saw_exception", 0, bif_tbl_saw_exception_0, "", false, false, BLAH},
	{"$tbl_abolish", 2, bif_tbl_abolish_1, "+atom,+integer", false, false, BLAH},
	{"$tbl_abolish_all_tables", 0, bif_tbl_abolish_all_tables_0, "", false, false, BLAH},


	{0}
};
