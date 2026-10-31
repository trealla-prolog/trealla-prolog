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

	// Answer subsumption (item 2): skip_mask has bit i set for each
	// moded argument i+1 of the OUTERMOST functor, 0 = none. Their
	// cells are never walked, so the trie encodes only the "key"
	// arguments - two answers agreeing on those collide regardless of
	// what they carry in the moded ones. at_top marks "still processing
	// the outermost functor's direct arguments"; it is cleared the moment
	// that functor's own key step is taken, so skip_mask can never apply
	// inside a nested subterm (a mode spec only ever names a top-level
	// argument).

	uint64_t skip_mask;
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

static bool twalk_skips(const twalk *w, unsigned i)
{
	return (i < 64) && ((w->skip_mask >> i) & 1);
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
			if (!(top && twalk_skips(w, i))) {
				if (!trie_walk(w, arg, ctx))
					return false;
			}

			arg += arg->num_cells;
		}

		if (top && twalk_skips(w, last))
			return true;		// last argument is a skipped one

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

static tnode *trie_insert_(query *q, tnode **root, cell *c, pl_ctx ctx, bool *existed, bool *attvar, unsigned max_size, bool *restrained, uint64_t skip_mask, unsigned *num_vars)
{
	twalk w;
	twalk_init(&w, q, root, true);
	w.max_size = max_size;
	w.skip_mask = skip_mask;
	bool ok = trie_walk(&w, c, ctx);
	tnode *leaf = w.node;
	bool fresh = w.created_any;
	if (attvar) *attvar = w.attvar;
	if (restrained) *restrained = w.restrained;
	if (num_vars) *num_vars = w.num_vars;
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
enum { TBL_AGG_MIN, TBL_AGG_MAX, TBL_AGG_FIRST, TBL_AGG_LAST, TBL_AGG_JOIN };

// Item 6 stage B: a literal an answer is conditional on. Like item 3's
// table edges it holds a handle value, validated on use, not a pointer.
// aid names an answer by its index in that table's cond[]; TBL_NEG
// means tnot of the table's call instead.

#define TBL_NEG (~0u)

typedef struct {
	uint64_t th;
	unsigned aid;
} tbl_delem;

typedef struct tbl_dlist_ {
	struct tbl_dlist_ *next;
	unsigned n;
	tbl_delem e[];
} tbl_dlist;

static void tbl_dlists_free(tbl_dlist *dl)
{
	while (dl) {
		tbl_dlist *next = dl->next;
		TPL_free(dl);
		dl = next;
	}
}

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

	// Item 6 stage B. dl: the alternative delay lists the answer holds
	// under, NULL once it is unconditional. aid: its index in the
	// table's cond[]. dead: simplified away; unlinked at completion and
	// kept, through cond[], only so delay elements naming it resolve.

	tbl_dlist *dl;
	unsigned aid;
	bool dead;
} tbl_ans;

// A suspended consumer. delays is the delay list it suspended with (NULL
// when empty), restored when it resumes; target is the table a negative
// suspension waits on.

typedef struct tbl_susp_ {
	cell *image;
	cell *delays;
	uint64_t target;
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
	unsigned dep_stamp;		// id of the last SCC to record an edge here
	unsigned walk_stamp;		// last tbl_update_refused() walk to visit it
	unsigned n_answers;		// count for max_answers_for_subgoal
	bool ground;			// the call has no variables, so one answer settles it

	// Answer subsumption (item 2): ":- table path(_,_,min)". agg_pos is
	// the 1-based position of the first moded argument, 0 = not moded
	// (the common case - every field below is unused then), and
	// agg_mask has a bit for every moded argument. agg_op says how a
	// new answer for a key joins the stored one: min and max over
	// standard order, first and last, all on a single argument and in
	// C, or join, by '$tbl_moded_join' in Prolog for the rest.
	// update_head/tail: answers whose VALUE changed in place since the
	// last drain, queued for re-pairing - see tbl_ans_.

	unsigned agg_pos;
	uint64_t agg_mask;
	uint8_t agg_op;
	tbl_ans *update_head, *update_tail;

	// Identity of the call this table answers, so abolish_table/1 can
	// find every variant of one predicate, and the trie leaf pointing
	// here, so it can be reset to "no table yet" when we drop it.

	pl_idx functor;
	unsigned arity;
	tnode *leaf;
	unsigned slot;			// index into tbl_state.slots (handle identity)

	// Incremental tabling (item 3). depset is this table's dependency
	// set, handed over by its SCC on completion and shared with every
	// other incremental table that SCC completed; completed_at is the
	// pl->dbgen the table completed at. Validation is a PULL done by
	// the owning thread at lookup, never a push from the asserting
	// thread: tables are per-thread but the database is shared, so
	// invalidating from the writer's side would mean touching another
	// thread's tables.

	struct tbl_depset_ *depset;
	uint64_t completed_at;
	bool is_incremental;

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

	// Item 6 stage B: conditional answers by aid, so a delay element can
	// name one; live answers counted each way, for tnot/1; negative
	// suspensions, resumed with a delay at the SCC's fixpoint.

	tbl_ans **cond;
	unsigned n_cond, cond_cap;
	unsigned n_uncond, n_condlive;
	tbl_susp *first_neg, *last_neg;
	unsigned settle_ix;		// its index while its SCC settles

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
	bool called;			// a call, not only retract/1 or clause/2

	// table edge
	uint64_t handle;
} tbl_dep;

typedef struct {
	unsigned id;
	table *fresh_head, *fresh_tail;	// tables owned by this SCC
	unsigned dep_min;		// smallest outer SCC id depended on (0 = none)

	// Dependencies collected while THIS SCC is the one being completed.
	// Attribution is per-SCC, not per-table: the SCC is already the
	// unit of completion, so it is the natural unit of invalidation,
	// and its push/pop bracket is the only one in the driver that is
	// safe against backtracking (see DESIGN-tabling-phase2.md item 3).
	// Flushed onto every table in fresh_head at mark_all_complete.
	//
	// Table edges are appended unchecked and spliced whole into the
	// parent on a merge: re-adding them through a dedup scan made a deep
	// chain of escaping SCCs cubic. Predicate edges are few - one per
	// incremental dynamic predicate called - so they stay distinct.

	tbl_dep *deps, *deps_tail;	// table edges
	tbl_dep *pdeps;			// predicate edges
	bool deps_incomplete;		// a dep was lost to OOM; refuse to validate

	// Item 6 stage B: the negative suspensions being delayed this round,
	// and the delay list of whoever called the leader, set aside while
	// its own workers run and restored when the SCC is popped.

	tbl_susp *negs;
	cell *caller_delays;
} tscc;

// Released slot indices, so allocating a slot is O(1) rather than a scan of every slot.

typedef struct {
	unsigned *idx;
	unsigned n, cap;
} tbl_freelist;

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
	unsigned walks;			// tbl_update_refused()'s walks, for walk_stamp

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

	// Item 6 stage B: set once this thread has delayed a literal. Until
	// then no derivation can be conditional, so the delay list is never
	// read and a program without tnot/1 pays only this test.

	bool wfs;

	tnode *variants;
	table *all_tables, *wl_head, *fresh_head;
	bool leader;

	// Handles are (serial, index) pairs into this array rather than raw
	// pointers, so a handle held across abolish_all_tables/0 is detected
	// instead of dereferenced. Releasing a slot bumps its serial, which
	// invalidates every handle that referred to it.

	struct { table *t; uint32_t serial; } *slots;
	unsigned nslots, slots_cap;
	tbl_freelist free;

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
	tbl_freelist free;
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

// One completed SCC's dependencies, shared by the incremental tables it
// completed: a copy each made an SCC of n tables and n edges quadratic.
// incomplete: a dep was lost to OOM, so recompute rather than trust.

typedef struct tbl_depset_ {
	unsigned refcnt;
	bool incomplete;
	tbl_dep *list;
} tbl_depset;

// What a table holds when its SCC's set could not be allocated: never
// valid, so it recomputes.

static tbl_depset s_depset_lost = { .refcnt = 1, .incomplete = true };

static void tbl_depset_release(tbl_depset *ds)
{
	if (ds && (ds != &s_depset_lost) && !--ds->refcnt) {
		tbl_deps_free(ds->list);
		TPL_free(ds);
	}
}

// Record a dependency of the SCC being completed. A predicate edge is
// checked against the few already there; a table edge is not - the
// caller's stamp keeps one SCC from recording a table twice, and a
// repeat across a merge costs only a duplicate.

static void tbl_scc_add_dep(tscc *top, const tbl_dep *want)
{
	if (!want->is_table) {
		for (tbl_dep *d = top->pdeps; d; d = d->next) {
			if ((d->m == want->m) && (d->functor == want->functor)
				&& (d->arity == want->arity)) {
				d->called |= want->called;
				return;
			}
		}
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

	if (!want->is_table) {
		d->next = top->pdeps;
		top->pdeps = d;
		return;
	}

	d->next = top->deps;
	top->deps = d;

	if (!top->deps_tail)
		top->deps_tail = d;
}

// Does t belong to the SCC being completed? SCC ids grow with depth and
// a merged SCC's tables keep their id, so the current SCC owns every
// active table whose id is at least its own; a merge need not re-tag.

static bool tbl_in_top_scc(const tbl_state *s, const table *t)
{
	return s->scc_depth && (t->status == TBL_ACTIVE)
		&& (t->scc >= s->scc[s->scc_depth-1].id);
}

// Called from enter_predicate() when pr->is_incremental. Attribution is
// to the SCC currently being completed, which is correct for a resumed
// continuation too - measured at 1157 checks / 0 mismatches, see the
// design doc.

void tbl_note_predicate_dep(query *q, predicate *pr, bool called)
{
	tbl_state *s = tbl_peek(q);

	if (!s || !s->scc_depth || !q->pl->tbl_incremental)
		return;

	tbl_dep want = {0};
	want.m = pr->m;
	want.functor = pr->key.val_off;
	want.arity = get_arity(&pr->key);
	want.called = called;
	tbl_scc_add_dep(&s->scc[s->scc_depth-1], &want);
}

static pl_idx s_fresh, s_active, s_complete, s_min, s_max, s_first, s_last, s_join, s_moded_join;
static pl_idx s_neg, s_pos, s_none, s_conditional, s_unconditional, s_tnot, s_undefined, s_negations;

static void tbl_intern_atoms(query *q)
{
	if (!s_fresh) {
		s_fresh = new_atom(q->pl, "fresh");
		s_active = new_atom(q->pl, "active");
		s_complete = new_atom(q->pl, "complete");
		s_min = new_atom(q->pl, "min");
		s_max = new_atom(q->pl, "max");
		s_first = new_atom(q->pl, "first");
		s_last = new_atom(q->pl, "last");
		s_join = new_atom(q->pl, "join");
		s_moded_join = new_atom(q->pl, "$tbl_moded_join");
		s_neg = new_atom(q->pl, "neg");
		s_pos = new_atom(q->pl, "pos");
		s_none = new_atom(q->pl, "none");
		s_conditional = new_atom(q->pl, "conditional");
		s_unconditional = new_atom(q->pl, "unconditional");
		s_tnot = new_atom(q->pl, "tnot");
		s_undefined = new_atom(q->pl, "undefined");
		s_negations = new_atom(q->pl, "negations");
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

static void tbl_susps_free(tbl_susp *sp)
{
	while (sp) {
		tbl_susp *next = sp->next;
		tbl_image_free(sp->image);
		tbl_image_free(sp->delays);
		TPL_free(sp);
		sp = next;
	}
}

// Free a table's answers: first the dead ones, which simplification
// unlinked and only cond[] still holds, then the linked ones - cond[]
// names those too, so it must not be read after.

static void tbl_answers_free(table *t)
{
	for (unsigned i = 0; i < t->n_cond; i++) {
		tbl_ans *a = t->cond[i];

		if (a->dead) {
			tbl_image_free(a->image);
			tbl_dlists_free(a->dl);
			TPL_free(a);
		}
	}

	for (tbl_ans *a = t->first_ans; a; ) {
		tbl_ans *next = a->next;
		tbl_image_free(a->image);
		tbl_dlists_free(a->dl);
		TPL_free(a);
		a = next;
	}

	TPL_free(t->cond);
	t->cond = NULL;
	t->n_cond = t->cond_cap = 0;
	t->n_uncond = t->n_condlive = 0;
	t->first_ans = t->last_ans = NULL;
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
	tbl_depset_release(t->depset);	// item 3
	t->depset = NULL;
	tbl_image_free(t->key_image);	// item 4
	t->key_image = NULL;
	trie_free(t->answers);
	tbl_answers_free(t);
	tbl_susps_free(t->first_susp);
	tbl_susps_free(t->first_neg);
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

// On OOM the index is just not reused, which costs a slot, not correctness.

static void tbl_free_push(tbl_freelist *f, unsigned i)
{
	if (f->n >= f->cap) {
		unsigned cap = f->cap ? f->cap * 2 : 16;
		void *tmp = TPL_realloc(f->idx, sizeof(*f->idx) * cap);
		if (!tmp) return;
		f->idx = tmp;
		f->cap = cap;
	}

	f->idx[f->n++] = i;
}

static bool tbl_slot_alloc(tbl_state *s, table *t)
{
	if (s->free.n) {
		unsigned i = s->free.idx[--s->free.n];
		s->slots[i].t = t;
		t->slot = i;
		return true;
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
	if (sh->free.n) {
		unsigned i = sh->free.idx[--sh->free.n];
		sh->slots[i].t = t;
		t->slot = i;
		return true;
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
	tbl_free_push(&s->free, t->slot);
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

// --- Item 6 stage B: delay lists ---
//
// A derivation's delay list is the b_setval/2 variable '$tbl_delays': a
// list of neg(TableHandle) and pos(TableHandle, Aid). b_setval makes it
// follow backtracking, which every path through the driver relies on.
// It is our own ground term, stored contiguously, so it is walked by
// layout rather than dereferenced.

static const char *tbl_delays_key = "$tbl_delays";

// The current delay list, or NULL when it is empty - always, until this
// thread has delayed something.

static cell *tbl_delays_now(query *q, const tbl_state *s)
{
	if (!s->wfs)
		return NULL;

	cell *d = gvar_peek(q, tbl_delays_key);
	return (d && !is_nil(d)) ? d : NULL;
}

static bool tbl_delems_add(tbl_delem **e, unsigned *n, unsigned *cap, tbl_delem d)
{
	for (unsigned i = 0; i < *n; i++) {
		if (((*e)[i].th == d.th) && ((*e)[i].aid == d.aid))
			return true;
	}

	if (*n == *cap) {
		unsigned ncap = *cap ? *cap * 2 : 8;
		tbl_delem *tmp = TPL_realloc(*e, sizeof(tbl_delem) * ncap);

		if (!tmp)
			return false;

		*e = tmp;
		*cap = ncap;
	}

	(*e)[(*n)++] = d;
	return true;
}

static bool tbl_delems_decode(const cell *l, tbl_delem **e, unsigned *n, unsigned *cap)
{
	while (l && is_iso_list(l)) {
		const cell *h = l + 1;
		tbl_delem d;
		d.th = h[1].val_uint;
		d.aid = (get_arity(h) == 2) ? (unsigned)h[2].val_uint : TBL_NEG;

		if (!tbl_delems_add(e, n, cap, d))
			return false;

		l = h + h->num_cells;
	}

	return true;
}

// Set the delay list to base (a stored list, or NULL) plus extra (or
// NULL), backtrackably.

// A delay element as call_delays/2's list holds it: neg(T) or pos(T, A).

static void tbl_make_delem(cell *tmp, const tbl_delem *d)
{
	bool neg = d->aid == TBL_NEG;
	make_struct(tmp, neg ? s_neg : s_pos, neg ? 1 : 2, neg ? 1 : 2);
	make_uint(tmp+1, d->th);
	tmp[1].flags |= FLAG_INT_TABLE;

	if (!neg)
		make_uint(tmp+2, d->aid);
}

static bool tbl_delays_build(query *q, const cell *base, const tbl_delem *extra)
{
	tbl_intern_atoms(q);
	tbl_delem *e = NULL;
	unsigned n = 0, cap = 0;

	if ((base && !tbl_delems_decode(base, &e, &n, &cap))
		|| (extra && !tbl_delems_add(&e, &n, &cap, *extra))) {
		TPL_free(e);
		return false;
	}

	cell nil, *l = &nil;
	make_atom(&nil, g_nil_s);

	for (unsigned i = 0; i < n; i++) {
		cell tmp[3];
		tbl_make_delem(tmp, &e[i]);

		if (!(i ? append_list(q, tmp) : allocate_list(q, tmp))) {
			TPL_free(e);
			return false;
		}
	}

	TPL_free(e);

	if (n && !(l = end_list(q)))
		return false;

	return gvar_b_set(q, tbl_delays_key, l, q->st.cur_ctx);
}

// A suspension: its dependency image, and the delay list it resumes with.

static tbl_susp *tbl_susp_new(query *q, const tbl_state *s, cell *dep, pl_ctx dep_ctx)
{
	tbl_susp *sp = TPL_calloc(1, sizeof(tbl_susp));

	if (!sp)
		return NULL;

	sp->image = tbl_image(q, dep, dep_ctx);
	const cell *d = tbl_delays_now(q, s);

	if (d && (sp->delays = TPL_malloc(sizeof(cell) * d->num_cells)))
		dup_cells(sp->delays, d, d->num_cells);

	if (!sp->image || (d && !sp->delays)) {
		tbl_image_free(sp->image);
		TPL_free(sp->delays);
		TPL_free(sp);
		return NULL;
	}

	return sp;
}

static bool tbl_dlist_same(const tbl_dlist *a, const tbl_delem *e, unsigned n)
{
	if (a->n != n)
		return false;

	for (unsigned i = 0; i < n; i++) {
		unsigned j = 0;

		while ((j < n) && ((a->e[j].th != e[i].th) || (a->e[j].aid != e[i].aid)))
			j++;

		if (j == n)
			return false;
	}

	return true;
}

static tbl_dlist *tbl_dlist_new(const tbl_delem *e, unsigned n)
{
	tbl_dlist *dl = TPL_malloc(sizeof(tbl_dlist) + sizeof(tbl_delem) * n);

	if (!dl)
		return NULL;

	dl->next = NULL;
	dl->n = n;
	memcpy(dl->e, e, sizeof(tbl_delem) * n);
	return dl;
}

// Truth of a delay element now: 1 true, 0 false, -1 not yet known. Only
// asked at the fixpoint of the SCC completing, whose answer sets are
// then final but for simplification: every table a delay names is in
// that SCC or already complete, or the SCC would have escaped.

static int tbl_delem_value(query *q, tbl_state *s, const tbl_delem *d)
{
	table *t = tbl_handle_from_value(s, d->th);

	if (!t)
		return -1;

	if (d->aid == TBL_NEG) {
		if (t->n_uncond)
			return 0;

		return t->n_condlive ? -1 : 1;
	}

	if (d->aid >= t->n_cond)
		return -1;

	const tbl_ans *a = t->cond[d->aid];

	if (a->dead)
		return 0;

	return a->dl ? -1 : 1;
}

// Simplification and answer completion (item 6 stages B and C), at the
// SCC's fixpoint and before anything is marked complete:
// '$tbl_get_answer' trusts a complete table never to change.
//
// Each element of a conditional answer's delay lists is classified once.
// One already decided settles at once: false kills its list, true drops
// out of it. One on a conditional answer of this SCC, or on tnot/1 of one
// of its tables still in doubt, is watched and counted pending in its
// list, as are those in doubt for good in a table already complete.
// Deciding an answer or a negation then visits the lists watching it:
// false kills a list, and an answer left with none is false; true counts
// a list down, and an answer whose list reaches zero is true. This is
// linear, where rescanning until nothing changed was quadratic in the
// length of a chain.
//
// Answer completion then finds what only a positive loop holds up. An
// answer is supported if one of its lists has every positive element on
// a supported answer or on one outside the SCC; a negation in doubt
// withdraws no support. The unsupported are false, which propagates as
// above, and support is counted again until none lacks it. What is still
// undecided is undefined.

enum { TBL_UNDEC, TBL_TRUE, TBL_FALSE };

typedef struct {
	query *q;
	tbl_state *s;
	table **tabs;			// the SCC's tables with conditional answers
	unsigned m, n;			// how many, and their answers
	unsigned *base;			// answer index of each table's cond[0]
	unsigned *ctab;			// table of each answer
	unsigned *dfirst;		// first list of each answer
	unsigned *nlive;		// lists not killed, per answer
	unsigned *ast, *tst;		// truth of each answer, and of tnot/1 of each table
	unsigned *sup, *sq;
	tbl_dlist **dls;
	unsigned *down;			// answer of each list
	unsigned *pend;			// elements in doubt, per list; ~0u once killed
	unsigned *acnt;			// positive elements on answers not yet supported
	unsigned *wstart, *w;		// lists watching answer x, or tnot/1 of table x-n
	unsigned *queue, qh, qt;
} tbl_settle;

// Carve n unsigneds off a block allocated whole.

static unsigned *tbl_carve(unsigned **p, size_t n)
{
	unsigned *r = *p;
	*p += n;
	return r;
}

static tbl_ans *tbl_settle_ans(const tbl_settle *st, unsigned c)
{
	unsigned j = st->ctab[c];
	return st->tabs[j]->cond[c - st->base[j]];
}

// An element's truth if settled, else TBL_UNDEC with *watch the answer,
// or n plus the table, it waits on: ~0u when it waits for good.

static int tbl_settle_elem(const tbl_settle *st, const tbl_delem *d, unsigned *watch)
{
	query *q = st->q;
	table *t = tbl_handle_from_value(st->s, d->th);
	unsigned j = t ? t->settle_ix : 0;
	*watch = ~0u;

	if (!t || (j >= st->m) || (st->tabs[j] != t)) {
		int v = tbl_delem_value(q, st->s, d);
		return v < 0 ? TBL_UNDEC : v ? TBL_TRUE : TBL_FALSE;
	}

	if (d->aid == TBL_NEG) {
		if (st->tst[j] == TBL_UNDEC)
			*watch = st->n + j;

		return st->tst[j];
	}

	if (d->aid >= t->n_cond)
		return TBL_UNDEC;

	unsigned c = st->base[j] + d->aid;

	if (st->ast[c] == TBL_UNDEC)
		*watch = c;

	return st->ast[c];
}

static void tbl_settle_decide(tbl_settle *st, unsigned c, int v)
{
	unsigned j = st->ctab[c];
	table *t = st->tabs[j];
	st->ast[c] = v;
	st->queue[st->qt++] = c;
	t->n_condlive--;

	if (v == TBL_TRUE)
		t->n_uncond++;

	if ((st->tst[j] == TBL_UNDEC) && (t->n_uncond || !t->n_condlive)) {
		st->tst[j] = t->n_uncond ? TBL_FALSE : TBL_TRUE;
		st->queue[st->qt++] = st->n + j;
	}
}

static void tbl_settle_drain(tbl_settle *st)
{
	while (st->qh < st->qt) {
		unsigned x = st->queue[st->qh++];
		int v = x < st->n ? st->ast[x] : st->tst[x - st->n];

		for (unsigned k = st->wstart[x]; k < st->wstart[x+1]; k++) {
			unsigned d = st->w[k], c = st->down[d];

			if ((st->pend[d] == ~0u) || (st->ast[c] != TBL_UNDEC))
				continue;

			if (v == TBL_TRUE) {
				if (!--st->pend[d])
					tbl_settle_decide(st, c, TBL_TRUE);
			} else {
				st->pend[d] = ~0u;

				if (!--st->nlive[c])
					tbl_settle_decide(st, c, TBL_FALSE);
			}
		}
	}
}

// Answer completion: decide the unsupported false, saying if there were any.

static bool tbl_settle_support(tbl_settle *st)
{
	unsigned qh = 0, qt = 0;

	for (unsigned c = 0; c < st->n; c++) {
		st->sup[c] = false;

		if (st->ast[c] != TBL_UNDEC)
			continue;

		for (unsigned d = st->dfirst[c]; d < st->dfirst[c+1]; d++) {
			if (st->pend[d] == ~0u)
				continue;

			const tbl_dlist *dl = st->dls[d];
			unsigned n = 0, watch;

			for (unsigned k = 0; k < dl->n; k++) {
				if ((tbl_settle_elem(st, &dl->e[k], &watch) == TBL_UNDEC) && (watch < st->n))
					n++;
			}

			st->acnt[d] = n;

			if (!n && !st->sup[c]) {
				st->sup[c] = true;
				st->sq[qt++] = c;
			}
		}
	}

	while (qh < qt) {
		unsigned x = st->sq[qh++];

		for (unsigned k = st->wstart[x]; k < st->wstart[x+1]; k++) {
			unsigned d = st->w[k], c = st->down[d];

			if ((st->pend[d] == ~0u) || (st->ast[c] != TBL_UNDEC) || st->sup[c])
				continue;

			if (!--st->acnt[d]) {
				st->sup[c] = true;
				st->sq[qt++] = c;
			}
		}
	}

	bool found = false;

	for (unsigned c = 0; c < st->n; c++) {
		if ((st->ast[c] == TBL_UNDEC) && !st->sup[c]) {
			tbl_settle_decide(st, c, TBL_FALSE);
			found = true;
		}
	}

	return found;
}

static bool tbl_settle_answers(query *q, tbl_state *s, tscc *top)
{
	tbl_settle st = { .q = q, .s = s };

	for (table *t = top->fresh_head; t; t = t->fresh_next) {
		if (t->n_cond) {
			st.m++;
			st.n += t->n_cond;
		}
	}

	if (!st.n)
		return true;

	size_t nm = (size_t)st.n + st.m;

	if (!(st.tabs = TPL_calloc(1, sizeof(table*) * st.m + sizeof(unsigned) * (6 * (size_t)st.n + 2 * st.m + 2 * nm + 2))))
		return false;

	unsigned *p = (unsigned*)(st.tabs + st.m);
	st.base = tbl_carve(&p, st.m);
	st.tst = tbl_carve(&p, st.m);
	st.ctab = tbl_carve(&p, st.n);
	st.nlive = tbl_carve(&p, st.n);
	st.ast = tbl_carve(&p, st.n);
	st.sup = tbl_carve(&p, st.n);
	st.sq = tbl_carve(&p, st.n);
	st.dfirst = tbl_carve(&p, st.n + 1);
	st.wstart = tbl_carve(&p, nm + 1);
	st.queue = tbl_carve(&p, nm);

	unsigned j = 0, c = 0, d = 0;

	for (table *t = top->fresh_head; t; t = t->fresh_next) {
		if (!t->n_cond)
			continue;

		t->settle_ix = j;
		st.tabs[j] = t;
		st.base[j] = c;
		st.tst[j] = t->n_uncond ? TBL_FALSE : t->n_condlive ? TBL_UNDEC : TBL_TRUE;

		for (unsigned i = 0; i < t->n_cond; i++, c++) {
			const tbl_ans *a = t->cond[i];
			st.ctab[c] = j;
			st.ast[c] = a->dead ? TBL_FALSE : a->dl ? TBL_UNDEC : TBL_TRUE;
		}

		j++;
	}

	// Count the lists, and the watchers of each answer and negation.

	for (c = 0; c < st.n; c++) {
		st.dfirst[c] = d;

		if (st.ast[c] != TBL_UNDEC)
			continue;

		for (const tbl_dlist *dl = tbl_settle_ans(&st, c)->dl; dl; dl = dl->next, d++) {
			for (unsigned k = 0; k < dl->n; k++) {
				unsigned watch;

				if ((tbl_settle_elem(&st, &dl->e[k], &watch) == TBL_UNDEC) && (watch != ~0u))
					st.wstart[watch+1]++;
			}
		}
	}

	st.dfirst[st.n] = d;

	for (unsigned x = 0; x < nm; x++)
		st.wstart[x+1] += st.wstart[x];

	if (!(st.dls = TPL_malloc(sizeof(tbl_dlist*) * d + sizeof(unsigned) * (3 * (size_t)d + st.wstart[nm]) + 1))) {
		TPL_free(st.tabs);
		return false;
	}

	p = (unsigned*)(st.dls + d);
	st.down = tbl_carve(&p, d);
	st.pend = tbl_carve(&p, d);
	st.acnt = tbl_carve(&p, d);
	st.w = tbl_carve(&p, st.wstart[nm]);

	for (c = d = 0; c < st.n; c++) {
		if (st.ast[c] != TBL_UNDEC)
			continue;

		for (tbl_dlist *dl = tbl_settle_ans(&st, c)->dl; dl; dl = dl->next, d++) {
			bool killed = false;
			unsigned pend = 0;

			for (unsigned k = 0; k < dl->n; k++) {
				unsigned watch;
				int v = tbl_settle_elem(&st, &dl->e[k], &watch);

				if (v == TBL_FALSE)
					killed = true;
				else if (v == TBL_UNDEC) {
					pend++;

					if (watch != ~0u)
						st.w[st.wstart[watch]++] = d;
				}
			}

			st.dls[d] = dl;
			st.down[d] = c;
			st.pend[d] = killed ? ~0u : pend;

			if (!killed)
				st.nlive[c]++;
		}
	}

	for (unsigned x = nm; x; x--)
		st.wstart[x] = st.wstart[x-1];

	st.wstart[0] = 0;

	for (c = 0; c < st.n; c++) {
		if (st.ast[c] != TBL_UNDEC)
			continue;

		if (!st.nlive[c]) {
			tbl_settle_decide(&st, c, TBL_FALSE);
			continue;
		}

		for (d = st.dfirst[c]; d < st.dfirst[c+1]; d++) {
			if (!st.pend[d]) {
				tbl_settle_decide(&st, c, TBL_TRUE);
				break;
			}
		}
	}

	tbl_settle_drain(&st);

	while (tbl_settle_support(&st))
		tbl_settle_drain(&st);

	// Write the outcome back: decided answers lose their lists, and the
	// undefined keep only the lists and elements still in doubt.

	for (c = 0; c < st.n; c++) {
		tbl_ans *a = tbl_settle_ans(&st, c);

		if ((st.ast[c] != TBL_UNDEC) && a->dl) {
			tbl_dlists_free(a->dl);
			a->dl = NULL;
			a->dead = st.ast[c] == TBL_FALSE;
		}
	}

	for (c = 0; c < st.n; c++) {
		if (st.ast[c] != TBL_UNDEC)
			continue;

		tbl_dlist **pp = &tbl_settle_ans(&st, c)->dl;

		for (d = st.dfirst[c]; d < st.dfirst[c+1]; d++) {
			tbl_dlist *dl = st.dls[d];

			if (st.pend[d] == ~0u) {
				TPL_free(dl);
				continue;
			}

			unsigned k2 = 0;

			for (unsigned k = 0; k < dl->n; k++) {
				if (tbl_delem_value(q, s, &dl->e[k]) < 0)
					dl->e[k2++] = dl->e[k];
			}

			dl->n = k2;
			*pp = dl;
			pp = &dl->next;
		}

		*pp = NULL;
	}

	// Unlink the dead; cond[] keeps them until the table is freed.

	for (j = 0; j < st.m; j++) {
		table *t = st.tabs[j];
		tbl_ans **pp = &t->first_ans;
		t->last_ans = NULL;

		while (*pp) {
			if ((*pp)->dead) {
				*pp = (*pp)->next;
				continue;
			}

			t->last_ans = *pp;
			pp = &(*pp)->next;
		}
	}

	TPL_free(st.tabs);
	TPL_free(st.dls);
	return true;
}

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
		&existed, &attvar, 0, &restrained, 0, NULL);

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
	tbl_answers_free(t);
	t->unproc_ans = NULL;
	t->update_head = t->update_tail = NULL;
	t->n_answers = 0;
	tbl_free_pending(t);
}

static bool tbl_deps_changed(query *q, tbl_state *s, table *t, unsigned depth)
{
	if (!t->depset)
		return false;

	if (t->depset->incomplete)
		return true;

	// A cycle in the table graph (mutual recursion across SCCs) would
	// otherwise recurse forever. Depth-capping is conservative in the
	// safe direction: treat it as changed and recompute.

	if (depth > 32)
		return true;

	for (tbl_dep *d = t->depset->list; d; d = d->next) {
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

			// Dropped since t read it, recomputed or not: its own deps
			// are then newer than its answers and can't show t changed.

			if ((dep->status != TBL_COMPLETE) || (dep->completed_at > t->completed_at))
				return true;

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
	tbl_susps_free(t->first_susp);
	tbl_susps_free(t->first_neg);
	t->first_susp = t->last_susp = t->unproc_susp = NULL;
	t->first_neg = t->last_neg = NULL;
	tbl_depset_release(t->depset);
	t->depset = NULL;
	t->in_wl = false;
	t->status = TBL_FRESH;
}

// --- item 3: updates while a table is computed ---
//
// A change to an incremental predicate that a table still being
// computed has called comes after the call, so the table completes on
// the old clauses yet is stamped after the change, and stays stale.
// Refuse the change instead, as SWI does.

// Only a call counts, as in SWI: a table may retract from, or look with clause/2 at, a predicate it then changes.

static bool tbl_dep_is(const tbl_dep *d, const predicate *pr)
{
	return !d->is_table && d->called && (d->m == pr->m)
		&& (d->functor == pr->key.val_off) && (d->arity == get_arity(&pr->key));
}

// Has scc called pr, itself or through the completed tables it read? Each table is visited once a walk, as a chain's tables keep edges to their own SCC.

static bool tbl_scc_called(query *q, tbl_state *s, const tscc *scc, const predicate *pr)
{
	for (const tbl_dep *d = scc->pdeps; d; d = d->next) {
		if (tbl_dep_is(d, pr))
			return true;
	}

	if (!++s->walks)
		s->walks = 1;

	table **stack = NULL;
	unsigned n = 0, cap = 0;
	const tbl_dep *list = scc->deps;
	bool found = false;

	for (;;) {
		for (const tbl_dep *d = list; d && !found; d = d->next) {
			if (!d->is_table) {
				found = tbl_dep_is(d, pr);
				continue;
			}

			table *dep = tbl_handle_from_value(s, d->handle);

			if (!dep || (dep->status != TBL_COMPLETE) || !dep->depset
				|| (dep->walk_stamp == s->walks))
				continue;

			dep->walk_stamp = s->walks;

			if (n == cap) {
				unsigned cap2 = cap ? cap * 2 : 16;
				table **tmp = TPL_realloc(stack, sizeof(*stack) * cap2);

				if (!tmp)
					continue;		// out of memory: let the update through, as before

				stack = tmp;
				cap = cap2;
			}

			stack[n++] = dep;
		}

		if (found || !n)
			break;

		list = stack[--n]->depset->list;
	}

	TPL_free(stack);
	return found;
}

// Raises error(permission_error(update, variant, Goal), Name/Arity) for t, the ball built here: throw_error() reported Goal as its Name/Arity.

static void tbl_raise_update(query *q, table *t)
{
	cell *img = tbl_reconstruct(q, t->leaf);
	const pl_idx n = img ? 7 + img->num_cells : 0;
	cell *g = img ? TPL_malloc(sizeof(cell) * n) : NULL;
	cell *ball = NULL;

	if (g) {
		make_struct(g, g_error_s, 2, n-1);
		make_struct(g+1, new_atom(q->pl, "permission_error"), 3, 2+img->num_cells);
		make_atom(g+2, new_atom(q->pl, "update"));
		make_atom(g+3, new_atom(q->pl, "variant"));
		dup_cells(g+4, img, img->num_cells);
		cell *pi = g + 4 + img->num_cells;
		make_struct(pi, g_slash_s, 2, 2);
		SET_OP(pi, OP_YFX);
		make_atom(pi+1, q->st.instr->val_off);
		make_int(pi+2, get_arity(q->st.instr));
		ball = import_term(q, g, q->st.cur_ctx);
		unshare_cells(g, n);
		TPL_free(g);
	}

	if (img) {
		unshare_cells(img, img->num_cells);
		TPL_free(img);
	}

	char *s = NULL;

	if (ball) {
		q->fullstop = q->nl = false;
		q->parens = q->numbervars = true;
		q->quoted = true;
		s = print_term_to_strbuf(q, ball, q->st.cur_ctx, 1);
		clear_write_options(q);
	}

	if (!s) {
		throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");
		return;
	}

	q->did_throw = true;
	find_exception_handler(q, s);
	TPL_free(s);
}

// Called by assert, retract and erase before they change an incremental pr: true, having raised, if a table being computed
// on this thread has called it. The engine looks at q->did_throw, so the caller just returns.

bool tbl_update_refused(query *q, predicate *pr)
{
	tbl_state *s = tbl_peek(q);

	if (!s || !s->scc_depth)
		return false;

	for (unsigned i = s->scc_depth; i--; ) {
		const tscc *scc = &s->scc[i];
		table *t = scc->fresh_head;

		while (t && !(t->is_incremental && t->leaf))
			t = t->fresh_next;

		if (t && tbl_scc_called(q, s, scc, pr)) {
			tbl_raise_update(q, t);
			return true;
		}
	}

	return false;
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
	unsigned num_vars = 0;
	tnode *leaf = trie_insert_(q, &s->variants, p1, p1_ctx, &existed, &attvar,
		q->pl->tbl_max_subgoal_size, &restrained, 0, &num_vars);

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
		t->ground = !num_vars;
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

	// Only an incremental table keeps its SCC's dependencies, so until
	// one is declared there is nothing to record.

	if (q->pl->tbl_incremental && s->scc_depth && !tbl_in_top_scc(s, t)
		&& (t->dep_stamp != tbl_scc_id(s))) {
		tbl_dep want = {0};
		want.is_table = true;
		want.handle = tbl_handle_value(s, t);
		t->dep_stamp = tbl_scc_id(s);
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

// '$tbl_set_moded'(+Handle, +Mask, +Op) - declares this table
// mode-directed: Mask has bit i set for each moded argument i+1, Op is
// how a new answer joins the stored one for its key (see agg_op). Called
// once, right after a FRESH table is created, by the
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

// '$tbl_incremental_declared' - an "as incremental" table exists, so SCCs
// start recording the dependencies only such tables keep.

static bool bif_tbl_incremental_declared_0(query *q)
{
	q->pl->tbl_incremental = true;
	return true;
}

static bool bif_tbl_has_specs_0(query *q)
{
	return q->pl->tbl_any_specs;
}

static bool bif_tbl_set_moded_3(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);
	GET_NEXT_ARG(p3,atom);
	table *t = tbl_handle(s, p1);

	if (!t)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	if (!is_smallint(p2) || (get_smallint(p2) <= 0))
		return throw_error(q, p2, p2_ctx, "domain_error", "table_mode_mask");

	tbl_intern_atoms(q);

	if (p3->val_off == s_min) t->agg_op = TBL_AGG_MIN;
	else if (p3->val_off == s_max) t->agg_op = TBL_AGG_MAX;
	else if (p3->val_off == s_first) t->agg_op = TBL_AGG_FIRST;
	else if (p3->val_off == s_last) t->agg_op = TBL_AGG_LAST;
	else if (p3->val_off == s_join) t->agg_op = TBL_AGG_JOIN;
	else return throw_error(q, p3, p3_ctx, "domain_error", "table_mode");

	t->agg_mask = (uint64_t)get_smallint(p2);
	t->agg_pos = 1;

	while (!((t->agg_mask >> (t->agg_pos - 1)) & 1))
		t->agg_pos++;

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

// '$tbl_add_answer'(+Handle, +Answer) adds Answer and then FAILS, as it
// does on a duplicate, for the producer to go on: except when the table
// is a ground call holding an unconditional answer, whose producers have
// nothing left to give. It succeeds then, and the driver cuts them
// (XSB's early completion, there by SWI's '$tbl_wkl_add_answer' too).

// A moded answer's new value for its key: replace the stored image and
// queue it, so every consumer that already read the old value runs again.

static bool tbl_answer_update(query *q, tbl_state *s, table *t, tbl_ans *old, cell *c, pl_ctx c_ctx)
{
	cell *new_image = tbl_image(q, c, c_ctx);

	if (!new_image)
		return false;

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

// The join for modes C cannot do itself (sum, lattice/1, po/1, or more
// than one moded argument): continue as '$tbl_moded_join'(T, New, Old),
// which calls '$tbl_moded_replace' if the join changes the stored answer.

static bool tbl_moded_join(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, cell *old)
{
	cell goal;
	make_instr(&goal, s_moded_join, NULL, 3, 0);
	cell *tmp = prepare_call(q, CALL_NOSKIP, &goal, q->st.cur_ctx, 1 + p1->num_cells + p2->num_cells + old->num_cells + 2);
	CHECKED(tmp);
	pl_idx num_cells = 1;
	num_cells += dup_cells_by_ref(tmp+num_cells, p1, p1_ctx, p1->num_cells);
	num_cells += dup_cells_by_ref(tmp+num_cells, p2, p2_ctx, p2->num_cells);
	num_cells += dup_cells_by_ref(tmp+num_cells, old, q->st.cur_ctx, old->num_cells);
	tmp->num_cells = num_cells;
	make_call(q, tmp+num_cells);
	q->st.instr = tmp;
	return true;
}

// '$tbl_moded_replace'(+Handle, +Answer): Answer, the join of a new answer
// with the one stored for its key, replaces it.

static bool bif_tbl_moded_replace_2(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,callable);
	table *t = tbl_handle(s, p1);

	if (!t || !t->agg_pos)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	twalk w;
	twalk_init(&w, q, &t->answers, false);
	w.skip_mask = t->agg_mask;
	bool ok = trie_walk(&w, p2, p2_ctx);
	tnode *leaf = w.node;
	twalk_done(&w);

	if (!ok || !leaf || !leaf->is_leaf || !leaf->value)
		return false;

	CHECKED(tbl_answer_update(q, s, t, leaf->value, p2, p2_ctx));
	return true;
}

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
	// trie is keyed on every argument EXCEPT the moded ones (trie_insert_'s
	// skip_mask), so two answers agreeing on the rest collide - existed
	// then means "this key already has a stored value", not "duplicate
	// answer", and is handled below instead of the plain dedup return.

	// Item 6 stage B: a derivation with delays gives a conditional answer.
	// Mode-directed tables refuse one: min/max over an answer that may
	// not hold has no meaning (SWI quietly treats it as unconditional).

	cell *dterm = tbl_delays_now(q, s);

	if (dterm && t->agg_pos)
		return throw_error(q, p2, p2_ctx, "permission_error", "aggregate,conditional_answer");

	bool existed = false, attvar = false, restrained = false;
	tnode *leaf = trie_insert_(q, &t->answers, p2, p2_ctx, &existed, &attvar,
		q->pl->tbl_max_answer_size, &restrained, t->agg_mask, NULL);

	if (!leaf) {
		if (attvar)
			return throw_error(q, p2, p2_ctx, "type_error", "free_variable");

		if (restrained)
			return throw_error(q, p2, p2_ctx, "resource_error", "max_table_answer_size");

		return throw_error(q, p2, p2_ctx, "representation_error", "tabled_answer");
	}

	tbl_delem *de = NULL;
	unsigned dn = 0, dcap = 0;

	if (dterm)
		CHECKED(tbl_delems_decode(dterm, &de, &dn, &dcap), TPL_free(de));

	// A repeat is not new to consumers either way, but it can change the
	// answer: an unconditional derivation settles a conditional answer,
	// and a conditional one adds an alternative delay list.

	if (existed && !t->agg_pos) {
		tbl_ans *old = leaf->value;

		if (old && old->dl) {
			if (!dn) {
				tbl_dlists_free(old->dl);
				old->dl = NULL;
				t->n_condlive--;
				t->n_uncond++;
			} else {
				tbl_dlist *dl = old->dl;

				while (dl && !tbl_dlist_same(dl, de, dn))
					dl = dl->next;

				if (!dl) {
					CHECKED(dl = tbl_dlist_new(de, dn), TPL_free(de));
					dl->next = old->dl;
					old->dl = dl;
				}
			}
		}

		TPL_free(de);
		return t->ground && old && !old->dl;
	}

	if (existed) {
		// Moded and this key has a stored answer already. first keeps
		// it; min, max and last compare the new value with the stored
		// one; join hands both to Prolog. An answer that changes nothing
		// is exactly like a plain duplicate - fail.

		tbl_ans *old = leaf->value;

		if (t->agg_op == TBL_AGG_FIRST)
			return false;

		cell *old_full = tbl_answer_term(q, old);
		CHECKED(old_full);

		if (t->agg_op == TBL_AGG_JOIN)
			return tbl_moded_join(q, p1, p1_ctx, p2, p2_ctx, old_full);

		pl_ctx new_ctx, old_ctx;
		cell *new_agg = tbl_nth_arg(q, p2, p2_ctx, t->agg_pos, &new_ctx);
		cell *old_agg = tbl_nth_arg(q, old_full, q->st.cur_ctx, t->agg_pos, &old_ctx);

		if (!new_agg || !old_agg)
			return throw_error(q, p2, p2_ctx, "type_error", "callable");

		int c = compare(q, new_agg, new_ctx, old_agg, old_ctx);
		bool better = t->agg_op == TBL_AGG_MAX ? (c > 0) : t->agg_op == TBL_AGG_MIN ? (c < 0) : (c != 0);

		if (better)
			CHECKED(tbl_answer_update(q, s, t, old, p2, p2_ctx));

		return false;
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

	if (q->pl->tbl_max_answers_for_subgoal && (t->n_answers >= q->pl->tbl_max_answers_for_subgoal)) {
		TPL_free(de);
		return throw_error(q, p2, p2_ctx, "resource_error", "max_answers_for_subgoal");
	}

	t->n_answers++;

	tbl_ans *a = TPL_calloc(1, sizeof(tbl_ans));
	CHECKED(a, TPL_free(de));
	a->leaf = leaf;

	if (dn) {
		if (t->n_cond == t->cond_cap) {
			unsigned ncap = t->cond_cap ? t->cond_cap * 2 : 8;
			tbl_ans **tmp = TPL_realloc(t->cond, sizeof(tbl_ans*) * ncap);
			CHECKED(tmp, TPL_free(a), TPL_free(de));
			t->cond = tmp;
			t->cond_cap = ncap;
		}

		CHECKED(a->dl = tbl_dlist_new(de, dn), TPL_free(a), TPL_free(de));
		a->aid = t->n_cond;
		t->cond[t->n_cond++] = a;
		t->n_condlive++;
	} else
		t->n_uncond++;

	TPL_free(de);

	// Item 5: the trie path IS the answer, so no second copy - except
	// for a subsumptive table, whose trie deliberately omits the
	// aggregated argument and so cannot reproduce it.

	if (t->agg_pos) {
		a->image = tbl_image(q, p2, p2_ctx);
		CHECKED(a->image);
	}

	a->is_new = true;

	// Subsumption finds the stored value through this, and item 6 the
	// answer a repeat derivation may settle.

	leaf->value = a;

	if (t->last_ans) t->last_ans->next = a; else t->first_ans = a;
	t->last_ans = a;

	if (!t->unproc_ans)
		t->unproc_ans = a;

	if (t->first_susp)
		tbl_enqueue(s, t);

	return t->ground && !dn && !t->agg_pos;
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

	// Item 6 stage B: reading a conditional answer makes the reader
	// conditional on it.

	if (a->dl) {
		tbl_delem d = { tbl_handle_value(s, t), a->aid };
		CHECKED(tbl_delays_build(q, tbl_delays_now(q, s), &d));
	}

	cell *tmp = tbl_answer_term(q, a);
	CHECKED(tmp);
	tbl_pin_answer_frame(q, tmp);
	return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
}

// '$tbl_add_suspension'(+Handle, +Dependency) records the suspension
// and FAILS, for the producer to go on, as '$tbl_add_answer' does.

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

	tbl_susp *sp = tbl_susp_new(q, s, p2, p2_ctx);
	CHECKED(sp);

	if (t->last_susp) t->last_susp->next = sp; else t->first_susp = sp;
	t->last_susp = sp;

	if (!t->unproc_susp)
		t->unproc_susp = sp;

	if (t->first_ans)
		tbl_enqueue(s, t);

	return false;
}

// '$tbl_add_neg_suspension'(+Handle, +Dependency) - item 6 stage B: a
// tnot/1 of this incomplete table, without an unconditional answer yet,
// waits here. Resumed only at the SCC's fixpoint, with the negation
// delayed, so it is not worklist work. Fails, as the above does.

static bool bif_tbl_add_neg_suspension_2(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);
	table *t = tbl_handle(s, p1);

	if (!t)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	if (s->scc_depth && t->scc && (t->scc < tbl_scc_id(s))) {
		tscc *top = &s->scc[s->scc_depth-1];

		if (!top->dep_min || (t->scc < top->dep_min))
			top->dep_min = t->scc;
	}

	tbl_susp *sp = tbl_susp_new(q, s, p2, p2_ctx);
	CHECKED(sp);
	sp->target = tbl_handle_value(s, t);

	if (t->last_neg) t->last_neg->next = sp; else t->first_neg = sp;
	t->last_neg = sp;
	s->wfs = true;
	return false;
}

static bool tbl_take_negatives(tbl_state *s, tscc *top);

// '$tbl_pop_worklist'(-Handle) - semidet; materializes the new work.

static bool bif_tbl_pop_worklist_1(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,any);

	// Only drain tables owned by the SCC we are completing; work for
	// outer SCCs is left for their own completion loops.

	table *t = s->wl_head, *prev = NULL;

	while (t && !tbl_in_top_scc(s, t)) {
		prev = t;
		t = t->wl_next;
	}

	// Item 6 stage B: with no positive work left, the tnot/1 calls still
	// waiting are the next work, delayed - unless the SCC escaped, whose
	// parent delays them, or a worker threw, which rolls it back. As work
	// rather than a step of completion/0, a program without tnot/1 runs
	// the same driver it did before.

	if (!t) {
		tscc *top = s->scc_depth ? &s->scc[s->scc_depth-1] : NULL;

		if (!s->wfs || !top || s->saw_exception || top->dep_min || !tbl_take_negatives(s, top))
			return false;

		tbl_intern_atoms(q);
		cell tmp;
		make_atom(&tmp, s_negations);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

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

static bool tbl_neg_work(query *q, tbl_state *s, cell *p3, pl_ctx p3_ctx);

static bool bif_tbl_wkl_work_3(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,atomic);
	GET_NEXT_ARG(p2,any);
	GET_NEXT_ARG(p3,any);

	if (is_atom(p1))
		return tbl_neg_work(q, s, p3, p3_ctx);

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

	// Item 6 stage B: the consumer resumes with the delays it suspended
	// with, plus this answer if it is conditional - never with the
	// leader's, which belong to whoever called the leader.

	if (s->wfs) {
		tbl_delem d = { tbl_handle_value(s, t), p->a->aid };
		const tbl_delem *dp = p->a->dl ? &d : NULL;

		if (p->s->delays || dp || tbl_delays_now(q, s))
			CHECKED(tbl_delays_build(q, p->s->delays, dp));
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

// Item 6 stage B: at the SCC's fixpoint, move its negative suspensions
// into this round's delaying. One whose table has an unconditional
// answer by now just fails, so it is dropped. True if any are left.

static bool tbl_take_negatives(tbl_state *s, tscc *top)
{
	tbl_susps_free(top->negs);
	top->negs = NULL;
	tbl_susp **tail = &top->negs;

	for (table *t = top->fresh_head; t; t = t->fresh_next) {
		tbl_susp *sp = t->first_neg;
		t->first_neg = t->last_neg = NULL;

		while (sp) {
			tbl_susp *next = sp->next;
			sp->next = NULL;

			if (t->n_uncond)
				tbl_susps_free(sp);
			else {
				*tail = sp;
				tail = &sp->next;
			}

			sp = next;
		}
	}

	return top->negs != NULL;
}

// '$tbl_wkl_work'(negations, _, -Dependency) - item 6 stage B, nondet
// over this round's negative suspensions: each resumes with its own
// delays plus the negation it waited on.

static bool tbl_neg_work(query *q, tbl_state *s, cell *p1, pl_ctx p1_ctx)
{
	if (!s->scc_depth)
		return false;

	if (q->retry && (q->st.v2 != s->generation))
		return false;

	tbl_susp *sp = q->retry ? (tbl_susp*)(size_t)q->st.v1 : s->scc[s->scc_depth-1].negs;

	if (!sp)
		return false;

	if (sp->next) {
		q->st.v1 = (uint64_t)(size_t)sp->next;
		q->st.v2 = s->generation;
		CHECKED(push_choice(q));
	}

	tbl_delem d = { sp->target, TBL_NEG };
	CHECKED(tbl_delays_build(q, sp->delays, &d));
	cell *td = import_term(q, sp->image, q->st.cur_ctx);
	CHECKED(td);
	tbl_pin_answer_frame(q, td);
	return unify(q, p1, p1_ctx, td, q->st.cur_ctx);
}

// '$tbl_answer_state'(+Handle, -State) - item 6 stage B: unconditional
// if any answer is, else conditional if any answer is, else none.

static bool bif_tbl_answer_state_2(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);
	table *t = tbl_handle(s, p1);

	if (!t)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	tbl_intern_atoms(q);
	cell tmp;
	make_atom(&tmp, t->n_uncond ? s_unconditional : t->n_condlive ? s_conditional : s_none);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

// '$tbl_delay_neg'(+Handle) - item 6 stage B: tnot/1 of a complete table
// whose answers are all conditional is undefined, so delay it.

static bool bif_tbl_delay_neg_1(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	table *t = tbl_handle(s, p1);

	if (!t)
		return throw_error(q, p1, p1_ctx, "type_error", "table_handle");

	s->wfs = true;
	tbl_delem d = { tbl_handle_value(s, t), TBL_NEG };
	CHECKED(tbl_delays_build(q, tbl_delays_now(q, s), &d));
	return true;
}

// '$tbl_delem_goal'(+Element, -Goal) - item 6 stage B: a delay element
// as the goal it stands for, tnot(Call) or the answer, for
// call_delays/2. One naming a table since abolished reads as undefined.

static bool bif_tbl_delem_goal_2(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,compound);
	GET_NEXT_ARG(p2,any);
	tbl_intern_atoms(q);
	cell *h = deref(q, p1+1, p1_ctx);
	table *t = is_integer(h) ? tbl_handle_from_value(s, h->val_uint) : NULL;
	unsigned aid = TBL_NEG;

	if (get_arity(p1) == 2) {
		cell *c = deref(q, p1+1+(p1+1)->num_cells, p1_ctx);
		aid = is_integer(c) ? (unsigned)c->val_uint : TBL_NEG;
	}

	cell *tmp = NULL;

	if (t && (aid == TBL_NEG) && t->leaf) {
		cell *img = tbl_reconstruct(q, t->leaf);
		CHECKED(img);
		cell *g = TPL_malloc(sizeof(cell) * (1 + img->num_cells));
		CHECKED(g, unshare_cells(img, img->num_cells), TPL_free(img));
		make_struct(g, s_tnot, 1, img->num_cells);
		dup_cells(g+1, img, img->num_cells);
		unshare_cells(img, img->num_cells);
		TPL_free(img);
		tmp = import_term(q, g, q->st.cur_ctx);
		unshare_cells(g, g->num_cells);
		TPL_free(g);
		CHECKED(tmp);
	} else if (t && (aid < t->n_cond) && !t->cond[aid]->dead) {
		tmp = tbl_answer_term(q, t->cond[aid]);
		CHECKED(tmp);
	}

	if (!tmp) {
		cell undef;
		make_atom(&undef, s_undefined);
		return unify(q, p2, p2_ctx, &undef, q->st.cur_ctx);
	}

	tbl_pin_answer_frame(q, tmp);
	return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
}

// Leader flag (the "scheduling component" of the Desouter driver).

// Item 6 stage D: the residual program, read from delay elements.

static tnode *trie_lookup(query *q, tnode **root, cell *c, pl_ctx ctx)
{
	twalk w;
	twalk_init(&w, q, root, false);
	bool ok = trie_walk(&w, c, ctx);
	tnode *leaf = w.node;
	twalk_done(&w);
	return ok && leaf && leaf->is_leaf ? leaf : NULL;
}

// The live conditional answer of t that is a variant of c, if any.

static const tbl_ans *tbl_cond_answer(query *q, table *t, cell *c, pl_ctx c_ctx)
{
	if (!t || (t->status != TBL_COMPLETE) || !t->n_condlive || t->agg_pos)
		return NULL;

	tnode *leaf = trie_lookup(q, &t->answers, c, c_ctx);
	const tbl_ans *a = leaf ? leaf->value : NULL;
	return a && a->dl && !a->dead ? a : NULL;
}

// '$tbl_goal_delem'(+Goal, -Elem): the delay element a delay goal names,
// neg(T) for tnot(G) and pos(T, A) for a conditional answer, looked up
// in Goal's own table and then in any other of its predicate. Fails if
// there is none, as for an answer that holds outright.

static bool bif_tbl_goal_delem_2(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,callable);
	GET_NEXT_ARG(p2,any);
	tbl_intern_atoms(q);
	bool neg = (p1->val_off == s_tnot) && (get_arity(p1) == 1);
	cell *g = neg ? deref(q, p1+1, p1_ctx) : p1;
	pl_ctx g_ctx = neg ? q->latest_ctx : p1_ctx;

	if (!is_callable(g))
		return false;

	tnode *leaf = trie_lookup(q, &s->variants, g, g_ctx);
	table *t = leaf ? leaf->value : NULL;
	tbl_delem d = { .aid = TBL_NEG };

	if (neg) {
		if (!t || (t->status != TBL_COMPLETE))
			return false;
	} else {
		const tbl_ans *a = tbl_cond_answer(q, t, g, g_ctx);
		pl_idx functor = is_interned(g) ? g->val_off : 0;

		for (table *t2 = s->all_tables; !a && t2; t2 = t2->all_next) {
			if ((t2 != t) && (t2->functor == functor) && (t2->arity == get_arity(g)) && (a = tbl_cond_answer(q, t2, g, g_ctx)))
				t = t2;
		}

		if (!a)
			return false;

		d.aid = a->aid;
	}

	d.th = tbl_handle_value(s, t);
	cell *tmp = alloc_heap(q, 3);
	CHECKED(tmp);
	tbl_make_delem(tmp, &d);
	return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
}

// '$tbl_delem_answers'(+Elem, -Answers): the conditional answers a delay
// element stands for, as pos(T, A): the one it names, or for neg(T)
// every one of T's still in doubt.

static bool bif_tbl_delem_answers_2(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,compound);
	GET_NEXT_ARG(p2,any);
	tbl_intern_atoms(q);
	cell *h = deref(q, p1+1, p1_ctx);
	table *t = is_integer(h) ? tbl_handle_from_value(s, h->val_uint) : NULL;
	bool pos = get_arity(p1) == 2;
	unsigned aid = TBL_NEG;

	if (pos) {
		cell *c = deref(q, p1+1+(p1+1)->num_cells, p1_ctx);
		aid = is_integer(c) ? (unsigned)c->val_uint : TBL_NEG;
	}

	CHECKED(init_tmp_heap(q));

	for (unsigned i = 0; t && (i < t->n_cond); i++) {
		const tbl_ans *a = t->cond[i];

		if ((pos && (i != aid)) || !a->dl || a->dead)
			continue;

		tbl_delem d = { .th = h->val_uint, .aid = i };
		cell tmp[3];
		tbl_make_delem(tmp, &d);
		CHECKED(append_list(q, tmp));
	}

	cell *l = end_list(q);
	CHECKED(l);
	return unify(q, p2, p2_ctx, l, q->st.cur_ctx);
}

// '$tbl_answer_delays'(+pos(T, A), +N, -Delays): the Nth (from 0, latest
// first, as SWI orders them) of the answer's alternative delay lists, as
// a list of elements.

static bool bif_tbl_answer_delays_3(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,compound);
	GET_NEXT_ARG(p2,integer);
	GET_NEXT_ARG(p3,any);
	cell *h = deref(q, p1+1, p1_ctx);
	table *t = is_integer(h) && (get_arity(p1) == 2) ? tbl_handle_from_value(s, h->val_uint) : NULL;
	cell *c = t ? deref(q, p1+2, p1_ctx) : NULL;
	unsigned aid = c && is_integer(c) ? (unsigned)c->val_uint : TBL_NEG;

	if (!t || (aid >= t->n_cond) || t->cond[aid]->dead || is_negative(p2))
		return false;

	const tbl_dlist *dl = t->cond[aid]->dl;

	for (pl_int i = get_smallint(p2); dl && i; i--)
		dl = dl->next;

	if (!dl)
		return false;

	CHECKED(init_tmp_heap(q));
	tbl_intern_atoms(q);

	for (unsigned k = 0; k < dl->n; k++) {
		cell tmp[3];
		tbl_make_delem(tmp, &dl->e[k]);
		CHECKED(append_list(q, tmp));
	}

	cell *l = end_list(q);
	CHECKED(l);
	return unify(q, p3, p3_ctx, l, q->st.cur_ctx);
}

// For the toplevel: is the answer just found conditional on delays?

bool tbl_answer_undefined(query *q)
{
	tbl_state *s = tbl_peek(q);
	return s && tbl_delays_now(q, s);
}

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
	top->fresh_head = top->fresh_tail = t;
	top->deps = top->deps_tail = NULL;	// item 3
	top->pdeps = NULL;
	top->deps_incomplete = false;
	top->negs = NULL;			// item 6
	top->caller_delays = NULL;
	t->scc = top->id;
	t->fresh_next = NULL;
	s->in_use++;

	// Item 6 stage B: the leader's workers start with no delays, whatever
	// its caller holds. Done here rather than per worker, where it would
	// be one more goal on every fresh call.

	const cell *d = tbl_delays_now(q, s);

	if (d) {
		CHECKED(top->caller_delays = TPL_malloc(sizeof(cell) * d->num_cells));
		dup_cells(top->caller_delays, d, d->num_cells);
		CHECKED(tbl_delays_build(q, NULL, NULL));
	}

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

	if (top->caller_delays) {
		cell *d = top->caller_delays;
		top->caller_delays = NULL;
		bool ok = tbl_delays_build(q, d, NULL);
		tbl_image_free(d);
		CHECKED(ok);
	}

	if (s->in_use)
		s->in_use--;

	bool escaped = top->dep_min != 0;

	if (escaped && s->scc_depth) {
		tscc *parent = &s->scc[s->scc_depth-1];

		// Merge: hand our tables to the parent. They keep their SCC id,
		// which tbl_in_top_scc() reads as the parent's from here on, so
		// this is a splice, not a walk - a chain of escaping SCCs n deep
		// used to re-tag n^2/2 tables.

		if (top->fresh_head) {
			top->fresh_tail->fresh_next = parent->fresh_head;
			parent->fresh_head = top->fresh_head;

			if (!parent->fresh_tail)
				parent->fresh_tail = top->fresh_tail;
		}

		// Still depending further out? Propagate.

		if (top->dep_min < parent->id) {
			if (!parent->dep_min || (top->dep_min < parent->dep_min))
				parent->dep_min = top->dep_min;
		}

		// Item 3: the tables went to the parent, so their dependencies
		// must go with them - the parent completes them and will flush
		// its dep set onto them. Table edges are spliced; predicate
		// edges, few, are merged distinct.

		if (top->deps) {
			top->deps_tail->next = parent->deps;
			parent->deps = top->deps;

			if (!parent->deps_tail)
				parent->deps_tail = top->deps_tail;
		}

		for (tbl_dep *d = top->pdeps; d; ) {
			tbl_dep *next = d->next;
			tbl_scc_add_dep(parent, d);
			TPL_free(d);
			d = next;
		}

		if (top->deps_incomplete)
			parent->deps_incomplete = true;

		top->deps = top->deps_tail = top->pdeps = NULL;
	}

	tbl_deps_free(top->deps);		// non-escaping: flushed at completion
	tbl_deps_free(top->pdeps);
	top->deps = top->deps_tail = top->pdeps = NULL;
	tbl_susps_free(top->negs);		// item 6: all resumed by now
	top->negs = NULL;

	top->fresh_head = top->fresh_tail = NULL;
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

	if (s->wfs)
		CHECKED(tbl_settle_answers(q, s, top));

	tbl_susps_free(top->negs);
	top->negs = NULL;

	// Item 3: the SCC's dependencies become one set for its incremental
	// tables to share, predicate edges first: the cheap and likely
	// reason for a table to have changed.

	tbl_depset *ds = NULL;

	for (table *t = top->fresh_head; t && !ds; t = t->fresh_next) {
		if (!t->is_incremental)
			continue;

		if (!(ds = TPL_calloc(1, sizeof(tbl_depset)))) {
			ds = &s_depset_lost;
			break;
		}

		ds->incomplete = top->deps_incomplete;
		ds->list = top->deps;

		if (top->pdeps) {
			tbl_dep *last = top->pdeps;

			while (last->next)
				last = last->next;

			last->next = top->deps;
			ds->list = top->pdeps;
		}

		top->deps = top->deps_tail = top->pdeps = NULL;
	}

	for (table *t = top->fresh_head; t; ) {
		table *next = t->fresh_next;
		t->status = TBL_COMPLETE;
		t->fresh_next = NULL;
		tbl_free_pending(t);

		// A completed table never resumes consumers again: its
		// suspensions (whole continuation images) are dead weight.
		// Freeing here is a large win for many-table workloads.

		tbl_susps_free(t->first_susp);
		tbl_susps_free(t->first_neg);
		t->first_susp = t->last_susp = t->unproc_susp = NULL;
		t->first_neg = t->last_neg = NULL;

		// Item 3: give each table it completed the SCC's dependency set.
		// Only tables of incremental predicates keep it - for anything
		// else the set is dead weight and the table is permanent as
		// before.

		if (t->is_incremental) {
			tbl_depset_release(t->depset);
			t->depset = ds;
			ds->refcnt++;
			t->completed_at = (uint64_t)q->pl->dbgen;
		}

		// Item 4: publish. The table is complete and will never be
		// written again, so hand it to the registry. Only a CLEAN
		// completion qualifies - saw_exception is checked by the
		// caller, and an incremental table is refused at declaration
		// time because invalidation would mutate it under readers.

		// Item 6: a table left holding conditional answers stays private:
		// its delays name this thread's tables by this thread's handles.

		if (t->wants_shared && !t->is_shared && !t->n_condlive)
			tbl_publish(q, s, t);

		t = next;
	}

	tbl_deps_free(top->deps);
	tbl_deps_free(top->pdeps);
	top->deps = top->deps_tail = top->pdeps = NULL;
	top->deps_incomplete = false;
	top->fresh_head = top->fresh_tail = NULL;
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

			tbl_susps_free(t->first_susp);
			tbl_susps_free(t->first_neg);
			t->first_susp = t->last_susp = t->unproc_susp = NULL;
			t->first_neg = t->last_neg = NULL;
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

	rtop->fresh_head = rtop->fresh_tail = NULL;
	tbl_susps_free(rtop->negs);		// item 6
	rtop->negs = NULL;

	// A rolled-back table is FRESH, but it was left linked in the
	// worklist: a later enqueue would link it twice and loop the list.

	for (table **pp = &s->wl_head; *pp; ) {
		if (!(*pp)->in_wl) {
			table *t = *pp;
			*pp = t->wl_next;
			t->wl_next = NULL;
		} else
			pp = &(*pp)->wl_next;
	}

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
			tbl_free_push(&sh->free, t->slot);
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
	TPL_free(s->free.idx);
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
		TPL_free(sh->free.idx);
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

// '$tbl_handles'(-List) snapshots this thread's tables plus published ones, for current_table/2.

static bool bif_tbl_handles_1(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,var);
	CHECKED(init_tmp_heap(q));
	cell tmp;

	for (table *t = s->all_tables; t; t = t->all_next) {
		if (!t->leaf)
			continue;

		make_tbl_handle(s, &tmp, t);
		append_list(q, &tmp);
	}

	tbl_shared *sh = tbl_shared_peek(q);

	if (sh) {
#if USE_THREADS
		acquire_lock(&sh->guard);
#endif

		for (table *t = sh->all; t; t = t->all_next) {
			if (!t->is_shared || !t->leaf)
				continue;

			make_tbl_handle_(s, sh, &tmp, t);
			append_list(q, &tmp);
		}

#if USE_THREADS
		release_lock(&sh->guard);
#endif
	}

	cell *l = end_list(q);
	CHECKED(l);
	return unify(q, p1, p1_ctx, l, q->st.cur_ctx);
}

// '$tbl_variant'(+Handle, -Variant) rebuilds a table's call variant from its path in the variant trie; fails on a stale handle.

static bool bif_tbl_variant_2(query *q)
{
	tbl_state *s = tbl(q);
	CHECKED(s);

	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);
	table *t = tbl_handle(s, p1);

	if (!t || !t->leaf)
		return false;

	cell *img = tbl_reconstruct(q, t->leaf);
	CHECKED(img);
	cell *tmp = import_term(q, img, q->st.cur_ctx);
	unshare_cells(img, img->num_cells);
	TPL_free(img);
	CHECKED(tmp);
	tbl_pin_answer_frame(q, tmp);
	return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
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
	{"$tbl_set_moded", 3, bif_tbl_set_moded_3, "+integer,+integer,+atom", false, false, BLAH},
	{"$tbl_moded_replace", 2, bif_tbl_moded_replace_2, "+integer,+callable", false, false, BLAH},
	{"$tbl_set_pred_incremental", 2, bif_tbl_set_pred_incremental_2, "+atom,+integer", false, false, BLAH},
	{"$tbl_set_incremental", 1, bif_tbl_set_incremental_1, "+integer", false, false, BLAH},
	{"$tbl_set_shared", 2, bif_tbl_set_shared_2, "+integer,+term", false, false, BLAH},
	{"$tbl_specs_declared", 0, bif_tbl_specs_declared_0, "", false, false, BLAH},
	{"$tbl_has_specs", 0, bif_tbl_has_specs_0, "", false, false, BLAH},
	{"$tbl_incremental_declared", 0, bif_tbl_incremental_declared_0, "", false, false, BLAH},
	{"$tbl_add_answer", 2, bif_tbl_add_answer_2, "+integer,+term", false, false, BLAH},
	{"$tbl_get_answer", 2, bif_tbl_get_answer_2, "+integer,?term", false, false, BLAH},
	{"$tbl_add_suspension", 2, bif_tbl_add_suspension_2, "+integer,+term", false, false, BLAH},
	{"$tbl_pop_worklist", 1, bif_tbl_pop_worklist_1, "-integer", false, false, BLAH},
	{"$tbl_wkl_work", 3, bif_tbl_wkl_work_3, "+atomic,-term,-term", false, false, BLAH},
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
	{"$tbl_handles", 1, bif_tbl_handles_1, "-list", false, false, BLAH},
	{"$tbl_variant", 2, bif_tbl_variant_2, "+integer,-term", false, false, BLAH},
	{"$tbl_add_neg_suspension", 2, bif_tbl_add_neg_suspension_2, "+integer,+term", false, false, BLAH},
	{"$tbl_answer_state", 2, bif_tbl_answer_state_2, "+integer,-atom", false, false, BLAH},
	{"$tbl_delay_neg", 1, bif_tbl_delay_neg_1, "+integer", false, false, BLAH},
	{"$tbl_delem_goal", 2, bif_tbl_delem_goal_2, "+compound,-term", false, false, BLAH},
	{"$tbl_goal_delem", 2, bif_tbl_goal_delem_2, "+callable,-compound", false, false, BLAH},
	{"$tbl_delem_answers", 2, bif_tbl_delem_answers_2, "+compound,-list", false, false, BLAH},
	{"$tbl_answer_delays", 3, bif_tbl_answer_delays_3, "+compound,+integer,-list", false, false, BLAH},


	{0}
};
