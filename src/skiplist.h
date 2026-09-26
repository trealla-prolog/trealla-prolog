#pragma once

#include <string.h>
#include <stdbool.h>

typedef struct skiplist_ skiplist;
typedef struct sliter_ sliter;

skiplist *sl_create(
	int (*cmpkey)(const void *k1, const void *k2, const void *p, void *l),
	void (*delkey)(void *k, void *v, const void *p),
	const void *p
	);

bool sl_get(skiplist *l, const void *k, const void **v);
bool sl_set(skiplist *l, const void *k, const void *v);		// at head
bool sl_app(skiplist *l, const void *k, const void *v);		// at tail
bool sl_rem(skiplist *l, const void *k, const void *v);
bool sl_replace(skiplist *l, const void *k, const void *v, const void *newk, const void *newv);
bool sl_del(skiplist *l, const void *k);

void sl_set_wild_card(void *ctx);
void sl_set_tmp(skiplist *l);
bool sl_is_find(void *ctx);

sliter *sl_find_key(skiplist *l, const void *k);
bool sl_next_key(sliter *i, void **v);

sliter *sl_first(skiplist *l);
bool sl_has_next(sliter *i, void **v);
bool sl_next(sliter *i, void **v);
void *sl_key(sliter *i);

void sl_done(sliter *i);
size_t sl_count(const skiplist *l);
void sl_destroy(skiplist *l);
