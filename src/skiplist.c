#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#if (__STDC_VERSION__ >= 201112L) && USE_THREADS
#include <stdatomic.h>
#define sl_atomic _Atomic

// Publishing a new node into a list another thread may be descending.
// Its key and val have to be visible before the forward pointer that
// reveals it, which on a weak memory model is not otherwise guaranteed:
// the store linking the node in can land before the stores that filled
// it, and a reader then compares against an uninitialised key.
//
// Only the writer needs a fence. A reader reaches key and val *through*
// the forward pointer it loaded, and that address dependency orders the
// loads on every architecture Trealla targets.
//
// This does not make the list lock-free. Concurrent sl_rem() still has
// to be excluded by the caller - the database does that with
// prolog_lock(). It closes the narrower window where insertion alone
// races with a descent, which nothing excluded.

#define sl_publish_barrier() atomic_thread_fence(memory_order_release)
#else
#define sl_atomic volatile
#define sl_publish_barrier() ((void)0)
#endif

#include "skiplist.h"
#include "threads.h"

typedef struct slnode_ slnode_t;

struct slnode_ {
	void *key;
	void *val;
	slnode_t *forward[];
};

// Traversal state: one per lookup, never shared between them.
typedef struct { bool wild_card, is_find; } slctx;

struct sliter_ {
	sliter *next;
	skiplist *l;
	slnode_t *p;
	void *key;
	slctx ctx;
};

struct skiplist_ {
	slnode_t *header;
	int (*cmpkey)(const void*, const void*, const void*, void *l);
	void (*delkey)(void*, void*, const void*);
	const void *p;
	sliter tmp_iter;
	sliter *iters;
	size_t count;
	lock guard;
	int level;
	unsigned seed;
	bool is_tmp_list, is_destroyed;
};

#define MAX_LEVELS 16
#define MAX_LEVEL (MAX_LEVELS - 1)

inline static slnode_t *new_node_of_level(unsigned x)
{
	return TPL_malloc(sizeof(slnode_t) + ((x+1) * sizeof(slnode_t*)));
}

static int default_cmpkey(const void *p1, const void *p2, __attribute__((unused)) const void *p, void *l)
{
	ptrdiff_t i1 = (ptrdiff_t)p1;
	ptrdiff_t i2 = (ptrdiff_t)p2;
	return i1 < i2 ? -1 : i1 > i2 ? 1 : 0;
}

static int g_sl_random = -1;

skiplist *sl_create(int (*cmpkey)(const void*, const void*, const void*, void *), void(*delkey)(void*, void*, const void*), const void *p)
{
	skiplist *l = (skiplist*)TPL_calloc(1, sizeof(struct skiplist_));
	if (!l) return NULL;

	init_lock(&l->guard);
	l->header = new_node_of_level(MAX_LEVELS);
	if (!l->header) {
		TPL_free(l);
		return NULL;
	}

	// Deterministic by default. The old seed mixed in the skiplist's own
	// heap address and clock(), so every list in every run got a different
	// level distribution - and any bug that depends on the shape of the
	// structure then shows up as flakiness rather than as a reproducible
	// failure. That is how a broken sl_rem() and a non-antisymmetric index
	// comparator both stayed hidden for so long: they only bit on the runs
	// where the dice fell a particular way.
	//
	// Seeded off a process-local counter instead, so the Nth skiplist
	// created always gets the same sequence. Set TPL_SKIPLIST_RANDOM to
	// bring the entropy back for soak and fuzz runs, where exploring many
	// different shapes is the whole point.

	static sl_atomic unsigned g_seq;
	unsigned n = ++g_seq;

	if (g_sl_random < 0)
		g_sl_random = getenv("TPL_SKIPLIST_RANDOM") ? 1 : 0;

	if (g_sl_random)
		n ^= (unsigned)((size_t)l + (size_t)clock());

	// Knuth multiplicative, then a mixing round so early lists do not
	// start from near-identical states.

	l->seed = n * 2654435761u;
	l->seed ^= l->seed >> 15;

	if (!l->seed)
		l->seed = 1;
	l->level = 1;

	// new_node_of_level(x) allocates x+1 forward slots, so the header
	// has MAX_LEVELS+1 of them. Initialising only MAX_LEVELS left the
	// top slot holding whatever the allocator last put there, and
	// sl_del/sl_rem read exactly that slot once l->level reaches the
	// MAX_LEVELS cap.

	for (int i = 0; i <= MAX_LEVELS; i++)
		l->header->forward[i] = NULL;

	l->header->key = NULL;
	l->header->val = NULL;
	l->cmpkey = cmpkey ? cmpkey : default_cmpkey;
	l->delkey = delkey;
	l->p = p;
	return l;
}

void sl_destroy(skiplist *l)
{
	if (!l)
		return;

	slnode_t *p, *q;
	p = l->header;
	q = p->forward[0];
	TPL_free(p);
	p = q;

	while (p) {
		q = p->forward[0];

		if (l->delkey) {
			l->delkey(p->key, p->val, l->p);
		}

		TPL_free(p);
		p = q;
	}

	while (l->iters) {
		sliter *iter = l->iters;
		l->iters = iter->next;
		TPL_free(iter);
	}

	l->is_destroyed = true;
	deinit_lock(&l->guard);
	TPL_free(l);
}

void sl_set_wild_card(void *ctx) { if (ctx) ((slctx*)ctx)->wild_card = true; }
bool sl_is_find(void *ctx) { return ctx ? ((slctx*)ctx)->is_find : true; }
size_t sl_count(const skiplist *l) { return l ? l->count : 0; }
void sl_set_tmp(skiplist *l) { l->is_tmp_list = true; }

// xorshift32 on the list's own seed. Replaces log(frand())/log(0.5),
// which needed a guard for rand_r() returning 0 (log(0.0) is -inf, and
// the conversion to int gave INT_MIN, which asked new_node_of_level()
// for a nonsense size and silently dropped the insert).
//
// It also removes a platform split: on Windows rand_r was #defined to
// rand(), discarding the per-list seed for shared global state, which is
// neither reproducible nor safe to call from several threads at once.
// This is the same generator everywhere.

static inline unsigned sl_rand(unsigned *seedp)
{
	unsigned x = *seedp;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return *seedp = x;
}

// Geometric with P=0.5, straight off the low bits - no floating point,
// and no zero case to special-case.

static int random_level(unsigned *seedp)
{
	int lvl = 0;

	while ((sl_rand(seedp) & 1u) && (lvl < MAX_LEVEL))
		lvl++;

	return lvl;
}

bool sl_get(skiplist *l, const void *key, const void **val)
{
	if (!l || l->is_destroyed)
		return false;

	slnode_t *p = l->header, *q = NULL;
	slctx ctx = {false, true};

	for (int k = l->level - 1; k >= 0; k--) {
		while ((q = p->forward[k]) && (l->cmpkey(q->key, key, l->p, &ctx) < 0))
			p = q;
	}

	if (!(q = p->forward[0]))
		return false;

	if (l->cmpkey(q->key, key, l->p, &ctx) != 0)
		return false;

	if (val)
		*val = q->val;

	return true;
}

bool sl_set(skiplist *l, const void *key, const void *val)
{
	if (!l || l->is_destroyed)
		return false;

	slnode_t *update[MAX_LEVELS+1], *p = l->header, *q = NULL;
	slctx ctx = {false, true};
	int k;

	for (k = l->level - 1; k >= 0; k--) {
		while ((q = p->forward[k]) && (l->cmpkey(q->key, key, l->p, &ctx) < 0))
			p = q;

		update[k] = p;
	}

	k = random_level(&l->seed);

	if (k >= l->level) {
		l->level++;
		k = l->level - 1;
		update[k] = l->header;
	}

	q = new_node_of_level(k + 1);
	if (!q) return false;
	q->key = (void *)key;
	q->val = (void*)val;

	// Before any forward pointer reveals q.

	sl_publish_barrier();

	for (; k >= 0; k--) {
		p = update[k];
		q->forward[k] = p->forward[k];
		p->forward[k] = q;
	}

	l->count++;
	return true;
}

bool sl_app(skiplist *l, const void *key, const void *val)
{
	if (!l || l->is_destroyed)
		return false;

	slnode_t *update[MAX_LEVELS+1], *p = l->header, *q = NULL;
	slctx ctx = {false, true};
	int k;

	for (k = l->level - 1; k >= 0; k--) {
		while ((q = p->forward[k]) && (l->cmpkey(q->key, key, l->p, &ctx) <= 0))
			p = q;

		update[k] = p;
	}

	k = random_level(&l->seed);

	if (k >= l->level) {
		l->level++;
		k = l->level - 1;
		update[k] = l->header;
	}

	q = new_node_of_level(k + 1);
	if (!q) return false;
	q->key = (void *)key;
	q->val = (void*)val;

	// Before any forward pointer reveals q.

	sl_publish_barrier();

	for (; k >= 0; k--) {
		p = update[k];
		q->forward[k] = p->forward[k];
		p->forward[k] = q;
	}

	l->count++;
	return true;
}

// Give the entry holding val under key a new key and value in place, the new key comparing equal
// to the old. A reader finds the entry throughout, and either key or value it sees is usable.

bool sl_replace(skiplist *l, const void *key, const void *val, const void *newkey, const void *newval)
{
	if (!l || l->is_destroyed)
		return false;

	slnode_t *p = l->header, *q = NULL;
	slctx ctx = {false, true};

	for (int k = l->level - 1; k >= 0; k--) {
		while ((q = p->forward[k]) && (l->cmpkey(q->key, key, l->p, &ctx) < 0))
			p = q;
	}

	for (q = p->forward[0]; q && !l->cmpkey(q->key, key, l->p, &ctx); q = q->forward[0]) {
		if (q->val != val)
			continue;

		sl_publish_barrier();
		q->val = (void*)newval;
		q->key = (void*)newkey;
		return true;
	}

	return false;
}

bool sl_rem(skiplist *l, const void *key, const void *val)
{
	if (!l || l->is_destroyed || !key)
		return false;

	slnode_t *update[MAX_LEVELS+1], *p = l->header, *q = NULL;
	slctx ctx = {false, true};
	int k;

	// Descend on a STRICT less-than so update[] lands on the last node
	// ahead of the run of equal keys, at every level.
	//
	// The old loop advanced while cmp <= 0 and broke only on
	// q->val == val. At a level where the target node is absent - most
	// levels, since heights are random - that walked p past the whole
	// equal-key run and so past the target's position. The descent then
	// finished beyond the node it wanted and either unlinked a
	// neighbour or reported failure. With duplicate keys it failed on
	// 394 of 400 removals, and first-argument indexing is nothing but
	// duplicate keys: every clause sharing a principal functor lands on
	// the same key. Index entries therefore outlived the clauses whose
	// cells they borrow for a key.

	for (k = l->level - 1; k >= 0; k--) {
		while ((q = p->forward[k]) && (l->cmpkey(q->key, key, l->p, &ctx) < 0))
			p = q;

		update[k] = p;
	}

	// Walk the equal-key run at level 0 for the exact pair, carrying
	// update[] forward as nodes tall enough to matter go by. Level 0
	// included - leaving update[0] behind strands the node in the
	// bottom chain after it has been freed.

	slnode_t *t = update[0]->forward[0];

	while (t && (l->cmpkey(t->key, key, l->p, &ctx) == 0) && (t->val != val)) {
		for (k = 0; k < l->level; k++) {
			if (update[k]->forward[k] == t)
				update[k] = t;
		}

		t = t->forward[0];
	}

	if (!t || (l->cmpkey(t->key, key, l->p, &ctx) != 0) || (t->val != val))
		return false;

	if (l->delkey)
		l->delkey(t->key, t->val, l->p);

	for (k = 0; k < l->level; k++) {
		if (update[k]->forward[k] == t)
			update[k]->forward[k] = t->forward[k];
	}

	int m = l->level - 1;

	while (!l->header->forward[m] && (m > 0))
		m--;

	l->level = m + 1;
	l->count--;
	TPL_free(t);
	return true;
}

bool sl_del(skiplist *l, const void *key)
{
	if (!l || l->is_destroyed || !key)
		return false;

	slnode_t *update[MAX_LEVELS+1], *p = l->header, *q = NULL;
	slctx ctx = {false, true};
	int k;

	for (k = l->level - 1; k >= 0; k--) {
		while ((q = p->forward[k]) && (l->cmpkey(q->key, key, l->p, &ctx) < 0))
			p = q;

		update[k] = p;
	}

	if (!(q = p->forward[0]))
		return false;

	if (l->cmpkey(q->key, key, l->p, &ctx) != 0)
		return false;

	if (l->delkey)
		l->delkey(q->key, q->val, l->p);

	int m = l->level - 1;

	for (k = 0; k <= m; k++) {
		p = update[k];

		if (!p || (p->forward[k] != q))
			break;

		p->forward[k] = q->forward[k];
	}

	m = l->level - 1;

	while (!l->header->forward[m] && (m > 0))
		m--;

	l->level = m + 1;
	l->count--;
	TPL_free(q);
	return true;
}

sliter *sl_first(skiplist *l)
{
	if (!l || l->is_destroyed)
		return NULL;

	sliter *iter;

	if (l->is_tmp_list)
		iter = &l->tmp_iter;
	else {
		acquire_lock(&l->guard);

		if (!l->iters) {
			iter = TPL_malloc(sizeof(sliter));
			if (!iter) { release_lock(&l->guard); return NULL; }
		} else {
			iter = l->iters;
			l->iters = iter->next;
		}

		release_lock(&l->guard);
	}

	iter->key = NULL;
	iter->l = l;
	iter->p = l->header->forward[0];
	return iter;
}

bool sl_has_next(sliter *iter, void **val)
{
	if (!iter)
		return false;

	if (!iter->p)
		return false;

	if (val)
		*val = iter->p->val;

	iter->key = iter->p->key;
	return true;
}

bool sl_next(sliter *iter, void **val)
{
	if (!iter)
		return false;

	if (!iter->p)
		return false;

	if (val)
		*val = iter->p->val;

	iter->key = iter->p->key;
	iter->p = iter->p->forward[0];
	return true;
}

void *sl_key(sliter *iter)
{
	if (!iter)
		return NULL;

	return (void*)iter->key;
}

sliter *sl_find_key(skiplist *l, const void *key)
{
	if (!l || l->is_destroyed)
		return NULL;

	slnode_t *p = l->header, *q = NULL;
	slctx ctx = {false, true};

	for (int k = l->level - 1; k >= 0; k--) {
		while ((q = p->forward[k]) && (l->cmpkey(q->key, key, l->p, &ctx) < 0))
			p = q;
	}

	if (!p || !(q = p->forward[0]))
		return false;

	sliter *iter;

	if (l->is_tmp_list)
		iter = &l->tmp_iter;
	else {
		acquire_lock(&l->guard);

		if (!l->iters) {
			iter = TPL_malloc(sizeof(sliter));
			if (!iter) { release_lock(&l->guard); return NULL; }
		} else {
			iter = l->iters;
			l->iters = iter->next;
		}

		release_lock(&l->guard);
	}

	iter->key = (void*)key;
	iter->l = l;
	iter->p = q;
	return iter;
}

bool sl_next_key(sliter *iter, void **val)
{
	if (!iter)
		return false;

	iter->ctx.is_find = false;

	if (!iter->p)
		return false;

	iter->ctx.wild_card = false;
	int ok = iter->l->cmpkey(iter->p->key, iter->key, iter->l->p, &iter->ctx);

	if (!iter->ctx.wild_card && (ok != 0))
		return false;

	if (val)
		*val = (void*)iter->p->val;

	iter->p = iter->p->forward[0];
	return true;
}

void sl_done(sliter *iter)
{
	if (!iter)
		return;

	skiplist *l = iter->l;
	acquire_lock(&l->guard);

	if (!l->is_tmp_list) {
		iter->next = l->iters;
		l->iters = iter;
		release_lock(&l->guard);
	} else {
		release_lock(&l->guard);
		sl_destroy(l);
	}
}
