// Unit tests for src/skiplist.c
//
// Built and run by skiplist.sh; the output is diffed against
// skiplist.expected, so every check prints a fixed line.
//
// The skiplist is included as source rather than linked, for two
// reasons: it makes this a genuine unit test with no dependency on a
// built tree, and it lets the allocator be poisoned (see below).
//
// POISONING. Two of the bugs this file pins were reads of memory that
// was allocated but never initialised. Under a plain malloc those read
// back as zero often enough to look correct - the delete crash they
// caused reproduced on some runs and not others. Filling every fresh
// allocation with 0xAA removes that luck: an uninitialised slot is a
// wild pointer every time, not one run in ten.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static void *poison_malloc(size_t n)
{
	void *p = malloc(n);
	if (p) memset(p, 0xAA, n);
	return p;
}

// skiplist.c uses Trealla's allocator boundary. Keep this a standalone unit
// test, while preserving the deliberately poisoned non-zeroing allocations.
void *tpl_malloc(size_t n) { return poison_malloc(n); }
void *tpl_calloc(size_t n, size_t size) { return calloc(n, size); }
void tpl_free(void *p) { free(p); }

#include "../../src/skiplist.c"

// skiplist.c only takes the guard around its iterator freelist. These
// stand in for threads.c so the test links on its own.

void init_lock(lock *l) { (void)l; }
void deinit_lock(lock *l) { (void)l; }
bool try_lock(lock *l) { (void)l; return true; }
void acquire_lock(lock *l) { (void)l; }
void release_lock(lock *l) { (void)l; }

static int g_fails = 0;

static void ok(const char *name, bool cond)
{
	printf("%s: %s\n", name, cond ? "ok" : "FAILED");
	if (!cond) g_fails++;
}

static void okv(const char *name, long got, long want)
{
	if (got == want)
		printf("%s: ok\n", name);
	else {
		printf("%s: FAILED got %ld want %ld\n", name, got, want);
		g_fails++;
	}
}

// Keys are small integers cast to pointers, which is what the default
// comparator expects.

#define K(n) ((void*)(long)(n))

// ---------------------------------------------------------------- basics

static void test_empty(void)
{
	skiplist *l = sl_create(NULL, NULL, NULL);
	const void *v = NULL;
	okv("empty_count", (long)sl_count(l), 0);
	ok("empty_get", !sl_get(l, K(1), &v));
	ok("empty_del", !sl_del(l, K(1)));
	sl_destroy(l);
}

static void test_set_get(void)
{
	skiplist *l = sl_create(NULL, NULL, NULL);

	for (long i = 1; i <= 100; i++)
		sl_set(l, K(i), K(i * 10));

	okv("set_count", (long)sl_count(l), 100);

	const void *v = NULL;
	ok("get_first", sl_get(l, K(1), &v) && (long)v == 10);
	ok("get_middle", sl_get(l, K(50), &v) && (long)v == 500);
	ok("get_last", sl_get(l, K(100), &v) && (long)v == 1000);
	ok("get_missing_low", !sl_get(l, K(0), &v));
	ok("get_missing_high", !sl_get(l, K(101), &v));

	// A NULL value pointer must still report presence.
	ok("get_null_val", sl_get(l, K(50), NULL));
	sl_destroy(l);
}

// sl_set inserts BEFORE equal keys and sl_app AFTER them. That is not
// an implementation detail: module.c picks between them on the assert
// direction, so sl_set is asserta/1 and sl_app is assertz/1. sl_get
// returns the first match, so sl_set shadows and sl_app does not.

static void test_set_vs_app_ordering(void)
{
	skiplist *l = sl_create(NULL, NULL, NULL);
	const void *v = NULL;

	sl_app(l, K(1), K(100));
	sl_app(l, K(1), K(200));
	ok("app_keeps_first", sl_get(l, K(1), &v) && (long)v == 100);
	okv("app_count", (long)sl_count(l), 2);
	sl_destroy(l);

	l = sl_create(NULL, NULL, NULL);
	sl_set(l, K(1), K(100));
	sl_set(l, K(1), K(200));
	ok("set_shadows_with_last", sl_get(l, K(1), &v) && (long)v == 200);
	okv("set_count_duplicates", (long)sl_count(l), 2);
	sl_destroy(l);
}

// --------------------------------------------------------------- delete

static void test_del(void)
{
	skiplist *l = sl_create(NULL, NULL, NULL);

	for (long i = 1; i <= 100; i++)
		sl_set(l, K(i), K(i * 10));

	ok("del_present", sl_del(l, K(50)));
	okv("del_count", (long)sl_count(l), 99);

	const void *v = NULL;
	ok("del_gone", !sl_get(l, K(50), &v));
	ok("del_neighbour_low", sl_get(l, K(49), &v) && (long)v == 490);
	ok("del_neighbour_high", sl_get(l, K(51), &v) && (long)v == 510);
	ok("del_absent", !sl_del(l, K(50)));

	for (long i = 1; i <= 100; i++)
		sl_del(l, K(i));

	okv("del_all_count", (long)sl_count(l), 0);
	sl_destroy(l);
}

// sl_rem removes one specific key/value pair rather than the first
// entry for a key.

static void test_rem(void)
{
	skiplist *l = sl_create(NULL, NULL, NULL);
	sl_app(l, K(1), K(100));
	sl_app(l, K(1), K(200));
	sl_app(l, K(2), K(300));

	ok("rem_pair", sl_rem(l, K(1), K(200)));
	okv("rem_count", (long)sl_count(l), 2);

	const void *v = NULL;
	ok("rem_kept_other", sl_get(l, K(1), &v) && (long)v == 100);
	ok("rem_kept_key2", sl_get(l, K(2), &v) && (long)v == 300);
	sl_destroy(l);
}

// REGRESSION. sl_create allocates the header with MAX_LEVELS+1 forward
// slots but used to initialise only MAX_LEVELS of them, and sl_del and
// sl_rem began their descent at l->level instead of l->level-1. Once
// l->level reached the MAX_LEVELS cap - a few tens of thousands of
// entries in one list - a delete read that uninitialised top slot and
// followed it.
//
// Under the poisoning allocator the pre-fix code segfaults here.

static void test_max_level_delete(void)
{
	skiplist *l = sl_create(NULL, NULL, NULL);
	long i;

	for (i = 1; i <= 2000000 && l->level < MAX_LEVELS; i++)
		sl_set(l, K(i), K(i));

	// If this ever stops reaching the cap the test has gone stale and
	// is no longer exercising what it claims to.
	okv("max_level_reached", l->level, MAX_LEVELS);
	ok("max_level_header_top_slot_null", l->header->forward[MAX_LEVELS] == NULL);

	ok("max_level_del", sl_del(l, K(1234)));
	ok("max_level_rem", sl_rem(l, K(5678), K(5678)));

	const void *v = NULL;
	ok("max_level_del_gone", !sl_get(l, K(1234), &v));
	ok("max_level_survivor", sl_get(l, K(1235), &v) && (long)v == 1235);
	sl_destroy(l);
}

// ------------------------------------------------------------ iterators

static void test_iteration(void)
{
	skiplist *l = sl_create(NULL, NULL, NULL);

	for (long i = 1; i <= 1000; i++)
		sl_set(l, K(i), K(i * 3));

	long seen = 0, prev = 0, unordered = 0, badval = 0;
	void *val = NULL;
	sliter *iter = sl_first(l);

	while (sl_next(iter, &val)) {
		long k = (long)sl_key(iter);
		if (k <= prev) unordered++;
		if ((long)val != k * 3) badval++;
		prev = k;
		seen++;
	}

	sl_done(iter);
	okv("iter_count", seen, 1000);
	okv("iter_ordered", unordered, 0);
	okv("iter_values", badval, 0);
	sl_destroy(l);
}

static void test_find_key(void)
{
	skiplist *l = sl_create(NULL, NULL, NULL);
	sl_app(l, K(1), K(10));
	sl_app(l, K(2), K(20));
	sl_app(l, K(2), K(21));
	sl_app(l, K(2), K(22));
	sl_app(l, K(3), K(30));

	long seen = 0;
	void *val = NULL;
	sliter *iter = sl_find_key(l, K(2));

	while (sl_next_key(iter, &val))
		seen++;

	sl_done(iter);
	okv("find_key_matches", seen, 3);

	iter = sl_find_key(l, K(99));
	seen = 0;

	if (iter) {
		while (sl_next_key(iter, &val)) seen++;
		sl_done(iter);
	}

	okv("find_key_missing", seen, 0);
	sl_destroy(l);
}

// ------------------------------------------------------------- teardown

static long g_delkey_calls = 0;

static void counting_delkey(void *key, void *val, const void *p)
{
	(void)key; (void)val; (void)p;
	g_delkey_calls++;
}

static void test_destroy_calls_delkey(void)
{
	skiplist *l = sl_create(NULL, counting_delkey, NULL);
	g_delkey_calls = 0;

	for (long i = 1; i <= 500; i++)
		sl_set(l, K(i), K(i));

	sl_del(l, K(1));				// delkey on delete too
	okv("delkey_on_del", g_delkey_calls, 1);

	sl_destroy(l);
	okv("delkey_on_destroy", g_delkey_calls, 500);
}

// -------------------------------------------------------- model check

// Cross-check a long run of mixed operations against a plain array.

#define MODEL_N 100000

static void test_against_model(void)
{
	static char present[MODEL_N + 1];
	skiplist *l = sl_create(NULL, NULL, NULL);
	unsigned seed = 20260728;
	long live = 0;

	for (long i = 1; i <= MODEL_N; i++) {
		sl_set(l, K(i), K(i * 2));
		present[i] = 1;
		live++;
	}

	for (long i = 1; i <= MODEL_N; i++) {
		if (rand_r(&seed) & 1) {
			if (sl_del(l, K(i))) { present[i] = 0; live--; }
		}
	}

	okv("model_count", (long)sl_count(l), live);

	long bad = 0;

	for (long i = 1; i <= MODEL_N; i++) {
		const void *v = NULL;
		bool got = sl_get(l, K(i), &v);

		if (got != (present[i] != 0)) bad++;
		else if (got && ((long)v != i * 2)) bad++;
	}

	okv("model_lookups", bad, 0);

	long seen = 0, prev = 0, unordered = 0;
	void *val = NULL;
	sliter *iter = sl_first(l);

	while (sl_next(iter, &val)) {
		long k = (long)sl_key(iter);
		if (k <= prev) unordered++;
		prev = k;
		seen++;
	}

	sl_done(iter);
	okv("model_iteration", seen, live);
	okv("model_iter_ordered", unordered, 0);
	sl_destroy(l);
}

// random_level() used to convert log(0.0) - that is, -inf - to int when
// rand_r() returned 0, which is undefined and in practice INT_MIN. That
// asked new_node_of_level() for a nonsense size, the malloc failed and
// the insert was silently dropped. Levels must be in range for every
// seed, including the one that produces 0.

static void test_random_level_range(void)
{
	long out_of_range = 0;
	unsigned seed = 0;

	for (long i = 0; i < 200000; i++) {
		int lvl = random_level(&seed);
		if ((lvl < 0) || (lvl > MAX_LEVEL)) out_of_range++;
	}

	okv("random_level_in_range", out_of_range, 0);
}

int main(void)
{
	// Line-buffered: if a regression crashes the process mid-run the
	// checks that already passed still reach the diff, which is what
	// tells you where it went.

	setvbuf(stdout, NULL, _IOLBF, 0);

	test_empty();
	test_set_get();
	test_set_vs_app_ordering();
	test_del();
	test_rem();
	test_max_level_delete();
	test_iteration();
	test_find_key();
	test_destroy_calls_delkey();
	test_against_model();
	test_random_level_range();

	printf("failures: %d\n", g_fails);
	return 0;					// output is diffed; exit code stays 0
}
