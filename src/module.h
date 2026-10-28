#pragma once

#include "internal.h"
#include "prolog.h"

int index_cmpkey(const void *ptr1, const void *ptr2, const void *param, void *l);
module *module_create(prolog *pl, const char *name);
void module_duplicate(prolog *pl, module *m, const char *name, unsigned arity);
void module_destroy(module *m);

// Records that `m` uses `used`, growing the list. False if it cannot, which
// is the MAX_MODULES ceiling the flat array used to run straight past.
bool module_add_use(module *m, module *used);
bool module_dump_term(module* m, cell *p1);

bool save_file(module *m, const char *filename);
module *load_file(module *m, const char *filename, bool including, bool init);
module *load_fp(module *m, FILE *fp, const char *filename, bool including, bool init);
module *reload_file(module *m, const char *filename);
module *load_text(module *m, const char *src, const char *filename);
bool unload_file(module *m, const char *filename);
bool unload_for_reload(module *m, const char *filename);
void forget_file(module *m, const char *filename);
const char *set_known(module *m, const char *filename);
bool is_known(const module *m, const char *filename);
void module_load_done(parser *p);
void module_import_new_exports(module *into, module *from);
void set_unloaded(module *m, const char *filename);
const char *get_loaded(const module *m, const char *filename);
void set_parent(const module *m, const char *filename, const char *parent);
void convert_to_literal(module *m, cell *c);
unsigned search_op(module *m, const char *name, unsigned *specifier, bool prefer_unifix);
unsigned match_op(module *m, const char *name, unsigned *specifier, unsigned arity);
unsigned get_op(module *m, const char *name, unsigned specifier);
bool set_op(module *m, const char *name, unsigned specifier, unsigned priority);
void export_op(module *m, const char *name, unsigned specifier);
void visit_ops(module *m, const char *name, void (*fn)(void*, const op_table*), void *ctx);
predicate *find_functor(module *m, const char *name, unsigned arity);
predicate *find_predicate(module *m, cell *c);
predicate *search_predicate(module *m, cell *c);
predicate *create_predicate(module *m, cell *c, bool *created);
predicate *thread_local_shadow(predicate *pr);
void module_clear_tables(module *m);
void module_bury_predicates(module *m);
bool find_goal_expansion(module *m, cell *c);
bool find_goal_expansion_specific(module *m, cell *c);
bool search_goal_expansion(module *m, cell *c);
void create_goal_expansion(module *m, cell *c);
bool needs_quoting(module *m, const char *src, int srclen);
void process_clause(module *m, clause *cl);
void process_module(module *m);
void recheck_var_in_indexed_args(predicate *pr);
void index_remove_clause(predicate *pr, rule *r);
builtins *get_module_help(module *m, const char *name, unsigned arity, bool *found, bool *evaluable);
void format_property(module *m, char *tmpbuf, size_t buflen, const char *name, unsigned arity, const char *type, bool function);
void format_template(module *m, char *tmpbuf, size_t buflen, const char *name, unsigned arity, const builtins *ptr, bool function, bool alt);
void push_property(module *m, const char *name, unsigned arity, const char *type);
void push_template(module *m, const char *name, unsigned arity, const builtins *ptr);
void retract_from_db(module *m, rule *r);
void clear_property(module *m, const char *name, unsigned arity);
void detach_clauses(predicate *pr);
bool do_use_module_1(module *m, cell *p);
bool do_use_module_2(module *m, cell *p);
extern int g_no_jit_index;
uint64_t cell_signature(const cell *a);
void head_signatures(const predicate *pr, const cell *head, uint64_t *sig);
void choose_sig_args(predicate *pr, bool mt);
void build_predicate_index(predicate *pr);
void build_predicate_composite_index(predicate *pr);
void index_free(predicate *pr);
int index_cmpkey2(const void *ptr1, const void *ptr2, const void *param, void *l);
rule *asserta_to_db(module *m, unsigned num_vars, cell *p1, bool consulting);
rule *assertz_to_db(module *m, unsigned num_vars, cell *p1, bool consulting);
rule *find_in_db(module *m, uint64_t ref);
void set_meta_predicate_in_db(module *m, cell *c);
void set_discontiguous_in_db(module *m, const char *name, unsigned arity);
void set_dynamic_in_db(module *m, const char *name, unsigned arity);
void set_multifile_in_db(module *m, const char *name, pl_idx arity);
void set_det_in_db(module *m, const char *name, pl_idx arity);
void make(module *m);

#if USE_FFI
bool do_foreign_struct(module *m, cell *p);
bool do_use_foreign_module(module *m, cell *p);
#endif

bool is_bif_overridden(module *m, cell *c);

inline static builtins *get_builtin_term(module *m, cell *c, bool *found, bool *evaluable)
{
	builtins *b = get_builtin_by_atom(m->pl, c->val_off, get_arity(c), found, evaluable);

	if (b && !b->evaluable && m->bif_overrides && is_bif_overridden(m, c)) {
		if (found) *found = false;
		if (evaluable) *evaluable = false;
		return NULL;
	}

	return b;
}

// Key chains (docs/DESIGN-key-chains.md). An idx1/idx2 entry holds one chainable key's clauses, in
// database order through rule->kprev/knext. For a single clause the value is that rule, tagged in its
// low bit, and the entry's key is in the clause's cells; for more it is a keyhead with its own copy of
// the key. A single-clause entry whose rule has left the chains stays until the rule is reclaimed or
// the next clause with its key takes it over; an emptied keyhead stays until its last rule is reclaimed.

typedef struct keyhead_ {
	cell key;
	rule *first, *last;
	unsigned count;						// clauses on the chain, to pick the shorter of two
} keyhead;

bool key_chainable(const cell *c);
bool key_unorderable(const cell *c);

static inline bool kval_single(const void *v) { return (uintptr_t)v & 1; }
static inline rule *kval_rule(const void *v) { return (rule*)((uintptr_t)v & ~(uintptr_t)1); }
static inline void *kval_tag(const rule *r) { return (void*)((uintptr_t)r | 1); }
static inline rule *kchain_first(const void *v) { return kval_single(v) ? kval_rule(v) : ((const keyhead*)v)->first; }
static inline unsigned kchain_count(const void *v) { return kval_single(v) ? 1 : ((const keyhead*)v)->count; }
