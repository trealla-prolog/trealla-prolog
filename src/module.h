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
module *load_text(module *m, const char *src, const char *filename);
bool unload_file(module *m, const char *filename);
void set_unloaded(module *m, const char *filename);
const char *get_loaded(const module *m, const char *filename);
void set_parent(const module *m, const char *filename, const char *parent);
void convert_to_literal(module *m, cell *c);
unsigned search_op(module *m, const char *name, unsigned *specifier, bool prefer_unifix);
unsigned match_op(module *m, const char *name, unsigned *specifier, unsigned arity);
unsigned get_op(module *m, const char *name, unsigned specifier);
bool set_op(module *m, const char *name, unsigned specifier, unsigned priority);
predicate *find_functor(module *m, const char *name, unsigned arity);
predicate *find_predicate(module *m, cell *c);
predicate *search_predicate(module *m, cell *c);
predicate *create_predicate(module *m, cell *c, bool *created);
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
bool do_use_module_1(module *m, cell *p);
bool do_use_module_2(module *m, cell *p);
extern int g_no_jit_index;
uint64_t cell_signature(const cell *a);
void head_signatures(const cell *head, uint64_t *sig);
void build_predicate_index(predicate *pr);
void build_predicate_composite_index(predicate *pr);
int index_cmpkey2(const void *ptr1, const void *ptr2, const void *param, void *l);
rule *asserta_to_db(module *m, unsigned num_vars, cell *p1, bool consulting);
rule *assertz_to_db(module *m, unsigned num_vars, cell *p1, bool consulting);
rule *find_in_db(module *m, uint64_t ref);
rule *erase_from_db(module *m, uint64_t ref);
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

inline static builtins *get_builtin_term(module *m, cell *c, bool *found, bool *evaluable)
{
	return get_builtin_by_atom(m->pl, c->val_off, get_arity(c), found, evaluable);
}
