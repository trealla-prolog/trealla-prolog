#pragma once

#include "internal.h"

#define DOUBLE_BAR "$||"

extern const char *g_escapes;
extern const char *g_anti_escapes;

parser *parser_create(module *m);
parser *parser_create_sized(module *m, pl_idx num_cells);
void parser_destroy(parser *p);

unsigned tokenize(parser *p, bool args, bool consing);
void parser_reset(parser *p);
void quad_reset(module *m);
void term_to_body(parser *p);
cell *check_body_callable(cell *c);
bool run(parser *p, const char *src, bool dump, query **subq, unsigned int yield_time_in_ms);
char *eat_space(parser *p);
bool virtual_term(parser *p, const char *src);
bool get_token(parser *p, bool last_op, bool was_postfix);
void read_integer(parser *p, mp_int v2, int base, const char **srcptr);
void share_cells(cell *src, pl_idx num_cells);
void unshare_cells(cell *src, pl_idx num_cells);

void compile_clause(predicate *pr, clause *cl, cell *body);

// bif_dcgs.c. The translator keeps its structs static; this is the one
// entry point parser.c needs. p->cl->cells must be a (H --> B) term; on
// success p->cl is replaced with the translated clause.

bool dcg_expand_clause(parser *p);

void make_uint(cell *tmp, pl_uint v);
void make_int(cell *tmp, pl_int v);
void make_float(cell *tmp, pl_flt v);
void make_ptr(cell *tmp, void *v);
void make_var(cell *tmp, pl_idx off, unsigned var_num);
void make_end(cell *tmp);
void make_atom(cell *tmp, pl_idx offset);
void make_smalln(cell *tmp, const char *s, size_t n);
bool make_cstring(cell *d, const char *s);
bool make_string(cell *d, const char *s);
bool make_cstringn(cell *tmp, const char *s, size_t n);
bool make_stringn(cell *tmp, const char *s, size_t n);
void make_blob(cell *tmp, void *ptr);
cell *make_nil(void);

void clear_clause(clause *cl);
void release_token_value(parser *p);
void do_reduce(cell *n);
cell *get_logical_body(cell *c);

#if USE_FFI
void *do_dlopen(const char *filename, int flag);
const char *do_dlopen_error(void);
bool do_register_predicate(module *m, query *q, void *handle, const char *symbol, cell *l, pl_ctx l_ctx, const char *ret);
bool do_register_struct(module *m, query *q, void *handle, const char *symbol, cell *l, pl_ctx l_ctx, const char *ret);
int do_dlclose(void *handle);
#endif

void make_struct(cell *tmp, pl_idx offset, unsigned arity, pl_idx extra_cells);

#define make_instr(tmp, offset, fn, arity, extra_cells) { \
	cell *tmp_make = tmp; \
	make_struct(tmp_make, offset, arity, extra_cells); \
	\
	if ((void*)fn != NULL) { \
		static builtins *s_fn_ptr_##fn = NULL; \
		if (!s_fn_ptr_##fn) \
			s_fn_ptr_##fn = get_fn_ptr(fn); \
		\
		tmp_make->bif_ptr = s_fn_ptr_##fn; \
		tmp_make->flags = FLAG_INTERNED_BUILTIN; \
	} \
}

inline static void make_ref(cell *tmp, unsigned var_num, pl_ctx ctx)
{
	tmp->tag = TAG_VAR;
	tmp->num_cells = 1;
	tmp->flags = FLAG_VAR_REF;
	set_arity(tmp, 0);
	tmp->var_num = var_num;
	tmp->val_ctx = ctx;
	tmp->val_attrs = NULL;
}

inline static void make_indirect(cell *tmp, cell *v, pl_ctx v_ctx)
{
	tmp->tag = TAG_INDIRECT;
	tmp->num_cells = 1;
	tmp->flags = 0;
	set_arity(tmp, 0);
	tmp->val_ptr = v;
	tmp->val_ctx = v_ctx;
}

extern const char *g_solo;
