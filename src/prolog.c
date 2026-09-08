#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tpl_features.h"
#include "files.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#if !TPL_FREESTANDING && !defined(_WIN32) && !defined(__wasi__) && !defined(__ANDROID__)
#include <sys/resource.h>
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/sysctl.h>
#endif

#include "library.h"
#include "module.h"
#include "parser.h"
#include "prolog.h"
#include "query.h"

static lock g_symtab_guard;
static skiplist *g_symtab = NULL;
static size_t s_global_atoms_size = 64000, s_global_atoms_offset = 0;
pl_atomic int g_tpl_count = 0;

#define MAX_PROLOGS 64

prolog *g_prologs[MAX_PROLOGS] = {0};

pl_idx g_empty_s, g_dot_s, g_cut_s, g_nil_s, g_true_s, g_fail_s;
pl_idx g_anon_s, g_neck_s, g_eof_s, g_lt_s, g_gt_s, g_eq_s, g_false_s;
pl_idx g_sys_elapsed_s, g_sys_queue_s, g_braces_s, g_call_s, g_braces_s;
pl_idx g_sys_stream_property_s, g_unify_s, g_on_s, g_off_s, g_sys_var_s;
pl_idx g_plus_s, g_minus_s, g_once_s, g_post_unify_hook_s, g_sys_record_key_s;
pl_idx g_conjunction_s, g_disjunction_s, g_at_s, g_sys_ne_s, g_sys_incr_s;
pl_idx g_dcg_s, g_throw_s, g_sys_block_catcher_s, g_sys_drop_barrier_s;
pl_idx g_if_then_s, g_soft_cut_s, g_negation_s, g_none_s;
pl_idx g_error_s, g_slash_s, g_sys_cleanup_if_det_s;
pl_idx g_goal_expansion_s, g_term_expansion_s, g_tm_s, g_float_s;
pl_idx g_sys_cut_if_det_s, g_as_s, g_colon_s, g_member_s;
pl_idx g_caret_s, g_sys_counter_s, g_catch_s, g_memberchk_s;
pl_idx g_cont_s, g_sys_set_if_var_s, g_is_s, g_maplist_s;
pl_idx g_sys_succeed_on_retry_s, g_sys_fail_on_retry_s;
pl_idx g_quad_s, g_sys_quad_s;
pl_idx g_sys_call_check_s, g_ignore_s, g_sys_reset_handler_s;
pl_idx g_reset_s, g_sys_get_level_s, g_sys_jump_s, g_if_s;
pl_idx g_sys_call_s, g_sys_cut_s, g_notunify_s, g_sys_module_s;
pl_idx g_sys_reunify_s, g_sys_undo_s, g_sys_jump_if_nil_s;
pl_idx g_sys_loop_s, g_sys_end_s, g_sys_create_var_s;
pl_idx g_sys_match_s, g_double_bar_s, g_sys_list_s, g_ge_s;
pl_idx g_sys_abort_s, g_count_s, g_exit_s, g_killed_s;
pl_idx g_dummy_s;

char *g_global_atoms = NULL;
char *g_tpl_lib = NULL;
int g_ac = 0, g_avc = 1, g_argvc = 0;
char **g_av = NULL, **g_argv = NULL, *g_argv0 = NULL;
unsigned g_max_depth = 6000;			// default recursion limit (Linux)
unsigned g_cpu_count = 1;				// real value probed by g_init()
unsigned g_max_os_threads = 0;			// ditto; 0 means "none known"

bool is_multifile_in_db(prolog *pl, const char *mod, const char *name, unsigned arity)
{
	module *m = find_module(pl, mod);
	if (!m) return false;

	cell tmp = (cell){0};
	tmp.tag = TAG_INTERNED;
	tmp.val_off = new_atom(m->pl, name);
	if (tmp.val_off == ERR_IDX) return false;
	set_arity(&tmp, arity);
	predicate *pr = find_predicate(m, &tmp);
	if (!pr) return false;
	return pr->is_multifile ? true : false;
}

static pl_idx add_to_global_atoms(const char *name)
{
	size_t offset = s_global_atoms_offset, len = strlen(name);

	while ((offset+len+1+1) >= s_global_atoms_size) {
		size_t nbytes = (size_t)s_global_atoms_size * 3 / 2;
		void *tmp = TPL_realloc(g_global_atoms, nbytes);
		if (!tmp) return ERR_IDX;
		g_global_atoms = tmp;
		memset(g_global_atoms + s_global_atoms_size, 0, nbytes - s_global_atoms_size);
		s_global_atoms_size = nbytes;
	}

	const size_t s_lim = 1024*1024*1024;
	assert((offset + len + 1) < s_lim);

	memcpy(g_global_atoms + offset, name, len+1);
	s_global_atoms_offset += len + 1;
	const char *key = TPL_strdup(name);
	sl_app(g_symtab, key, (void*)(size_t)offset);
	return (pl_idx)offset;
}

pl_idx new_atom(prolog *pl, const char *name)
{
	acquire_lock(&g_symtab_guard);
	const void *val;

	if (sl_get(g_symtab, name, &val)) {
		release_lock(&g_symtab_guard);
		return (pl_idx)(size_t)val;
	}

	pl_idx off = add_to_global_atoms(name);
	release_lock(&g_symtab_guard);
	return off;
}

module *find_module(prolog *pl, const char *name)
{
	for (module *m = list_front(&pl->modules);
		m; m = list_next(m)) {
		if (!strcmp(m->name, name)) {
			if (m->orig)
				return m->orig;
			else
				return m;
		}
	}

	return NULL;
}

bool get_halt(prolog *pl) { return pl->halt; }
bool get_error(prolog *pl) { return pl->error; }
bool get_status(prolog *pl) { return pl->status; }
bool get_redo(prolog *pl) { return pl->is_redo; }
bool did_dump_vars(prolog *pl) { return pl->did_dump_vars; }
int get_halt_code(prolog *pl) { return pl->halt_code; }

void set_trace(prolog *pl) { pl->trace = true; }
void set_autofail(prolog *pl) { pl->autofail = true; }
void set_dump_vars(prolog *pl, int onoff) { pl->no_dump_vars = !onoff; }

void set_quiet(prolog *pl) { pl->quiet = true; }
void set_opt(prolog *pl, int level) { pl->opt = level; }
void set_limit(prolog *pl, int level) { pl->limit = level; }

bool pl_isatty(prolog* pl) { return isatty(fileno(pl->streams[0].fp)); }
FILE *pl_stdin(prolog *pl) { return pl->streams[0].fp; }

bool pl_eval(prolog *pl, const char *s, bool interactive)
{
	if (!*s)
		return false;

	pl->p = parser_create(pl->m);
	if (!pl->p) return false;

	if (interactive && isatty(fileno(stdin)))
		pl->p->fp = stdin;

	pl->p->interactive = interactive;
	bool ok = run(pl->p, s, true, NULL, 0);
	if (get_status(pl)) pl->m = pl->p->m;
	parser_destroy(pl->p);
	pl->p = NULL;
	return ok;
}

bool pl_query(prolog *pl, const char *s, pl_sub_query **subq, unsigned int yield_time_in_ms)
{
	if (!pl || !*s || !subq)
		return false;

	pl->p = parser_create(pl->m);
	if (!pl->p) return false;
	pl->is_query = true;
	*subq = NULL;
	bool ok = run(pl->p, s, !pl->no_dump_vars, (query**)subq, yield_time_in_ms);
	if (get_status(pl)) pl->m = pl->p->m;

	// Only ours to free if no query took it - see run(). Destroying it
	// here regardless freed the goal's strings while the query was still
	// reading them, so the first solution was fine and later ones read
	// freed memory.

	if (!*subq)
		parser_destroy(pl->p);

	pl->p = NULL;
	return ok;
}

bool pl_redo(pl_sub_query *subq)
{
	if (!subq)
		return false;

	query *q = (query*)subq;
	release_pl_terms(q);				// the previous answer's views die here

	if (query_redo(q))
		return true;

	query_destroy(q);
	return false;
}

		 /*******************************
		 *   INSPECTING AN ANSWER       *
		 *******************************/

// Handles are arena-allocated on the query and released with it, so an
// embedder never frees one and a leak is impossible. The arena resets on
// pl_redo, which is what makes "valid until the next redo" true rather
// than merely advisory.

// Each handle is allocated on its own and only the INDEX of pointers
// grows. Handing out interior pointers into one growable block looks
// tidier and is wrong: realloc moves it, and every handle the caller is
// still holding dangles. That only bites once the block outgrows its
// first capacity, so it survives every shallow term and crashes on a
// nested one - which is exactly how it was found.

static pl_term *new_pl_term(query *q, cell *c, pl_ctx c_ctx)
{
	if (!c)
		return NULL;

	if (q->terms_used == q->terms_cap) {
		unsigned cap = q->terms_cap ? q->terms_cap * 2 : 16;
		struct pl_term_ **tmp = TPL_realloc(q->terms, cap * sizeof(*tmp));
		if (!tmp) return NULL;
		q->terms = tmp;
		q->terms_cap = cap;
	}

	pl_term *t = TPL_malloc(sizeof(struct pl_term_));

	if (!t)
		return NULL;

	t->q = q;
	t->c = c;
	t->ctx = c_ctx;
	q->terms[q->terms_used++] = t;
	return t;
}

void release_pl_terms(query *q)
{
	for (unsigned i = 0; i < q->terms_used; i++)
		TPL_free(q->terms[i]);

	q->terms_used = 0;
}

int pl_term_type(pl_term *t)
{
	if (!t) return PL_TYPE_VAR;
	cell *c = t->c;
	if (is_var(c)) return PL_TYPE_VAR;
	if (is_integer(c)) return PL_TYPE_INTEGER;
	if (is_float(c)) return PL_TYPE_FLOAT;
	if (is_string(c)) return PL_TYPE_STRING;
	if (is_compound(c)) return PL_TYPE_COMPOUND;
	if (is_atom(c)) return PL_TYPE_ATOM;
	return PL_TYPE_VAR;
}

const char *pl_atom_text(pl_term *t)
{
	if (!t || !(is_atom(t->c) || is_string(t->c)))
		return NULL;

	return C_STR(t->q, t->c);
}

size_t pl_atom_len(pl_term *t)
{
	if (!t || !(is_atom(t->c) || is_string(t->c)))
		return 0;

	return C_STRLEN(t->q, t->c);
}

// Only a smallint fits. A bignum is not clamped or rounded here - it
// says no, and pl_term_text is how it is read. Trealla's integers are
// unbounded and an embedder that silently truncated them would be worse
// than one that made the caller ask.

bool pl_get_int64(pl_term *t, int64_t *v)
{
	if (!t || !is_smallint(t->c))
		return false;

	*v = get_smallint(t->c);
	return true;
}

bool pl_get_float(pl_term *t, double *v)
{
	if (!t || !is_float(t->c))
		return false;

	*v = get_float(t->c);
	return true;
}

char *pl_term_text(pl_term *t)
{
	if (!t)
		return NULL;

	return print_term_to_strbuf(t->q, t->c, t->ctx, 1);
}

// Radix matters more than it looks. CPython refuses to parse a decimal
// integer over sys.get_int_max_str_digits() - 4300 by default - so a host
// reading an unbounded Prolog integer through decimal text hits a wall
// that base 16 does not have. The same asymmetry the Prolog side of Janus
// has to route around going the other way.

char *pl_int_text(pl_term *t, int radix)
{
	if (!t || !is_integer(t->c) || (radix < 2) || (radix > 36))
		return NULL;

	if (is_smallint(t->c)) {
		char buf[80];
		int64_t v = get_smallint(t->c), n = v;
		bool neg = v < 0;
		int i = 0;

		if (!n)
			buf[i++] = '0';

		while (n) {
			int d = (int)(neg ? -(n % radix) : (n % radix));
			buf[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
			n /= radix;
		}

		if (neg)
			buf[i++] = '-';

		char *out = TPL_malloc(i + 1);

		if (!out)
			return NULL;

		for (int j = 0; j < i; j++)
			out[j] = buf[i - 1 - j];

		out[i] = '\0';
		return out;
	}

	mp_result len = mp_int_string_len(&t->c->val_bigint->ival, radix);

	if (len <= 0)
		return NULL;

	char *out = TPL_malloc(len);

	if (!out)
		return NULL;

	if (mp_int_to_string(&t->c->val_bigint->ival, radix, out, len) != MP_OK) {
		TPL_free(out);
		return NULL;
	}

	return out;
}

const char *pl_functor(pl_term *t)
{
	if (!t || !is_interned(t->c))
		return NULL;

	return C_STR(t->q, t->c);
}

unsigned pl_arity(pl_term *t)
{
	return t && is_compound(t->c) ? get_arity(t->c) : 0;
}

pl_term *pl_arg(pl_term *t, unsigned n)
{
	if (!t || !is_compound(t->c) || (n >= get_arity(t->c)))
		return NULL;

	cell *c = t->c + 1;

	for (unsigned i = 0; i < n; i++)
		c += c->num_cells;

	c = deref(t->q, c, t->ctx);
	return new_pl_term(t->q, c, t->q->latest_ctx);
}

		 /*******************************
		 *   BINDINGS OF THE ANSWER     *
		 *******************************/

// The names come from the parser that read the goal, which the query now
// owns (see run()), and the values from frame 0's slots - the same pair
// dump_vars/2 walks to print an answer at the toplevel.

unsigned pl_num_bindings(pl_sub_query *subq)
{
	if (!subq)
		return 0;

	query *q = (query*)subq;
	return q->top ? q->top->num_vars : 0;
}

const char *pl_binding_name(pl_sub_query *subq, unsigned i)
{
	if (!subq)
		return NULL;

	query *q = (query*)subq;

	if (!q->top || (i >= q->top->num_vars))
		return NULL;

	return GET_POOL(q, vartab_off(q->top, i));
}

pl_term *pl_binding_value(pl_sub_query *subq, unsigned i)
{
	if (!subq)
		return NULL;

	query *q = (query*)subq;

	if (!q->top || (i >= q->top->num_vars))
		return NULL;

	const frame *f = GET_FRAME(0);
	slot *e = get_slot(q, f, i);

	if (is_empty(&e->c))
		return NULL;					// never bound

	cell *c = deref(q, &e->c, 0);
	return new_pl_term(q, c, q->latest_ctx);
}

pl_term *pl_binding(pl_sub_query *subq, const char *name)
{
	unsigned n = pl_num_bindings(subq);

	for (unsigned i = 0; i < n; i++) {
		const char *s = pl_binding_name(subq, i);

		if (s && !strcmp(s, name))
			return pl_binding_value(subq, i);
	}

	return NULL;
}

bool pl_yield_at(pl_sub_query *subq, unsigned int time_in_ms)
{
	if (!subq)
		return false;

	query *q = (query*)subq;
	do_yield_at(q, time_in_ms);
	return true;
}

bool pl_did_yield(pl_sub_query *subq)
{
	if (!subq)
		return false;

	query *q = (query*)subq;
	return q->yielded;
}

bool pl_done(pl_sub_query *subq)
{
	if (!subq)
		return false;

	query *q = (query*)subq;
	query_destroy(q);
	return true;
}

bool pl_consult_fp(prolog *pl, FILE *fp, const char *filename)
{
	return load_fp(pl->user_m, fp, filename, false, true) != NULL;
}

bool pl_consult(prolog *pl, const char *filename)
{
	return load_file(pl->user_m, filename, false, true);
}

bool pl_consult_text(prolog *pl, const char *source, size_t source_len, const char *source_name)
{
	if (!pl || !source || !source_name || (source_len == SIZE_MAX)
		|| memchr(source, '\0', source_len))
		return false;

	char *copy = TPL_malloc(source_len + 1);

	if (!copy)
		return false;

	memcpy(copy, source, source_len);
	copy[source_len] = '\0';
	module *m = load_text(pl->user_m, copy, source_name);
	TPL_free(copy);
	return m != NULL;
}

bool pl_logging(prolog *pl, const char *filename)
{
	pl->logfp = fopen(filename, "a");
	return pl->logfp ? true : false;
}

bool pl_restore(prolog *pl, const char *filename)
{
	return restore_log(pl->user_m, filename);
}

static void g_destroy()
{
	sl_destroy(g_symtab);
	TPL_free(g_global_atoms);
	TPL_free(g_tpl_lib);

	// A later pl_create() checks these, so leaving them dangling made
	// create/destroy/create read freed memory.

	g_symtab = NULL;
	g_global_atoms = NULL;
	g_tpl_lib = NULL;
	deinit_lock(&g_symtab_guard);
}

void ptrfree(const void *key, const void *val, const void *p)
{
	builtins *ptr = (void*)val;

	if (ptr->via_directive) {
		if (ptr->help2) TPL_free((void*)ptr->help2);
		if (ptr->desc) TPL_free((void*)ptr->desc);
		if (ptr->name) TPL_free((void*)ptr->name);
		TPL_free((void*)ptr);
	}
}

void keyfree(const void *key, const void *val, const void *p)
{
	TPL_free((void*)key);
}

void fake_free(const void *key, const void *val, const void *p)
{
	TPL_free((void*)key);
	TPL_free((void*)val);
}

builtins *get_help(prolog *pl, const char *name, unsigned arity, bool *found, bool *evaluable)
{
	sliter *iter = sl_find_key(pl->help, name);
	builtins *ptr;

	while (sl_next_key(iter, (void**)&ptr)) {
		if (ptr->arity == arity) {
			if (found) *found = true;
			if (evaluable) *evaluable = ptr->evaluable;
			sl_done(iter);
			return ptr;
		}
	}

	if (found) *found = false;
	if (evaluable) *evaluable = false;
	sl_done(iter);
	return NULL;
}

builtins *get_builtin(prolog *pl, const char *name, size_t len, unsigned arity, bool *found, bool *evaluable)
{
	// TODO: use 'len' in comparison
	sliter *iter = sl_find_key(pl->biftab, name);
	builtins *ptr;

	while (sl_next_key(iter, (void**)&ptr)) {
		if (ptr->arity == arity) {
			if (found) *found = true;
			if (evaluable) *evaluable = ptr->evaluable;
			sl_done(iter);
			return ptr;
		}
	}

	if (found) *found = false;
	if (evaluable) *evaluable = false;
	sl_done(iter);
	return NULL;
}

builtins *get_fn_ptr(void *fn)
{
	for (builtins *ptr = g_iso_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_evaluable_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_os_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins **tab = g_port_bif_tables; *tab; tab++) {
		for (builtins *ptr = *tab; ptr->name; ptr++) {
			if (ptr->fn == fn)
				return ptr;
		}
	}

	for (builtins *ptr = g_other_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_control_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_atts_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_bboard_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_tabling_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_dcgs_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_database_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_csv_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_sregex_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_sort_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_tasks_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_threads_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_streams_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_misc_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_net_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_uri_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_ffi_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	for (builtins *ptr = g_posix_bifs; ptr->name; ptr++) {
		if (ptr->fn == fn)
			return ptr;
	}

	return NULL;
}

void load_builtins(prolog *pl)
{
	for (const builtins *ptr = g_atts_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_bboard_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_tabling_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_dcgs_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_csv_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_database_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_evaluable_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_ffi_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_format_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_iso_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_misc_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (builtins **tab = g_port_bif_tables; *tab; tab++) {
		for (const builtins *ptr = *tab; ptr->name; ptr++) {
			sl_app(pl->biftab, ptr->name, ptr);
			if (ptr->name[0] == '$') continue;
			sl_app(pl->help, ptr->name, ptr);
		}
	}

	for (const builtins *ptr = g_net_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_uri_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_os_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_other_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_control_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_posix_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_sort_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_sregex_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_streams_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_tasks_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}

	for (const builtins *ptr = g_threads_bifs; ptr->name; ptr++) {
		sl_app(pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		sl_app(pl->help, ptr->name, ptr);
	}
}

// How many logical CPUs we can actually run on, which is not the same
// question as how many the box has: _SC_NPROCESSORS_ONLN follows CPUs
// being taken offline, where _SC_NPROCESSORS_CONF would not.
//
// Every path that cannot answer falls back to 1 rather than to a guess.
// Too high oversubscribes whatever sizes itself off this; 1 is merely
// conservative. Testing _SC_NPROCESSORS_ONLN with defined() rather than
// per-platform ifdefs means a host that lacks it - wasi has no threads
// at all - degrades to that fallback instead of failing to build.

static unsigned detect_cpu_count(void)
{
// TPL_FREESTANDING is always defined - as 0 in a hosted build - so this
// has to test its value, not its existence, or every build takes the
// freestanding branch and reports a single CPU.

#if TPL_FREESTANDING
	return 1;
#elif defined(_WIN32)
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors > 0 ? (unsigned)si.dwNumberOfProcessors : 1;
#elif defined(_SC_NPROCESSORS_ONLN)
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	return n > 0 ? (unsigned)n : 1;
#else
	return 1;
#endif
}

// How many POSIX threads the O/S will let this process have, which is a
// different question again from how many CPUs there are (detect_cpu_count)
// and from how many thread slots we ourselves have (MAX_ACTUAL_THREADS).
//
// Returns 0 for "nothing here knows", which the caller reports as our own
// cap - true enough, since then nothing below it constrains you.
//
// sysctlbyname() resolves names at runtime, so a name this kernel does not
// have just fails and we fall through to the next. That is what lets one
// list cover several BSDs without a per-platform ifdef for each, and means
// a wrong guess degrades to the fallback rather than breaking the build.
//
// OpenBSD is the exception: it has no sysctlbyname() at all, only the
// numeric-MIB sysctl(), so there a name that resolves at runtime is not
// on offer and the one MIB we want has to be named at compile time.

static unsigned detect_max_os_threads(void)
{
#if !TPL_FEATURE_THREADS || defined(_WIN32) || defined(__wasi__)
	return 0;					// no fixed per-process limit to report
#else
#if defined(__OpenBSD__)
	{
		static const int s_mib[] = { CTL_KERN, KERN_MAXTHREAD };	// system-wide
		int val = 0;
		size_t len = sizeof(val);

		if (!sysctl(s_mib, sizeof(s_mib) / sizeof(s_mib[0]), &val, &len, NULL, 0)
			&& (val > 0))
			return (unsigned)val;
	}
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
	static const char *s_names[] = {
		"kern.num_taskthreads",					// macOS, per-task
		"kern.threads.max_threads_per_proc",	// FreeBSD, per-process
		"kern.maxlwp",							// NetBSD, system-wide
		NULL
	};

	for (const char **name = s_names; *name; name++) {
		int val = 0;
		size_t len = sizeof(val);

		if (!sysctlbyname(*name, &val, &len, NULL, 0) && (val > 0))
			return (unsigned)val;
	}
#endif

#if defined(RLIMIT_NPROC)
	// Linux counts threads against NPROC, so there this is the real
	// ceiling. On the BSDs it counts processes and is the wrong number
	// entirely - hence it goes after the sysctls, never before.
	struct rlimit rlp;

	if (!getrlimit(RLIMIT_NPROC, &rlp) && rlp.rlim_cur
		&& (rlp.rlim_cur != RLIM_INFINITY))
		return rlp.rlim_cur > UINT_MAX ? UINT_MAX : (unsigned)rlp.rlim_cur;
#endif

	return 0;
#endif
}

static bool g_init(prolog *pl)
{
	bool error = false;

	init_lock(&g_symtab_guard);
	g_global_atoms = TPL_calloc(s_global_atoms_size, 1);
	s_global_atoms_offset = 0;

	CHECK_SENTINEL(g_symtab = sl_create((void*)fake_strcmp, (void*)keyfree, NULL), NULL);
	CHECK_SENTINEL(g_dummy_s = new_atom(pl, "dummy"), ERR_IDX);
	CHECK_SENTINEL(g_sys_match_s = new_atom(pl, "$match"), ERR_IDX);
	CHECK_SENTINEL(g_false_s = new_atom(pl, "false"), ERR_IDX);
	CHECK_SENTINEL(g_true_s = new_atom(pl, "true"), ERR_IDX);
	CHECK_SENTINEL(g_at_s = new_atom(pl, "@"), ERR_IDX);
	CHECK_SENTINEL(g_conjunction_s = new_atom(pl, ","), ERR_IDX);
	CHECK_SENTINEL(g_disjunction_s = new_atom(pl, ";"), ERR_IDX);
	CHECK_SENTINEL(g_if_then_s = new_atom(pl, "->"), ERR_IDX);
	CHECK_SENTINEL(g_soft_cut_s = new_atom(pl, "*->"), ERR_IDX);
	CHECK_SENTINEL(g_negation_s = new_atom(pl, "\\+"), ERR_IDX);
	CHECK_SENTINEL(g_dot_s = new_atom(pl, "."), ERR_IDX);
	CHECK_SENTINEL(g_plus_s = new_atom(pl, "+"), ERR_IDX);
	CHECK_SENTINEL(g_minus_s = new_atom(pl, "-"), ERR_IDX);
	CHECK_SENTINEL(g_empty_s = new_atom(pl, ""), ERR_IDX);
	CHECK_SENTINEL(g_anon_s = new_atom(pl, "_"), ERR_IDX);
	CHECK_SENTINEL(g_dcg_s = new_atom(pl, "-->"), ERR_IDX);
	CHECK_SENTINEL(g_maplist_s = new_atom(pl, "maplist"), ERR_IDX);
	CHECK_SENTINEL(g_call_s = new_atom(pl, "call"), ERR_IDX);
	CHECK_SENTINEL(g_catch_s = new_atom(pl, "catch"), ERR_IDX);
	CHECK_SENTINEL(g_member_s = new_atom(pl, "member"), ERR_IDX);
	CHECK_SENTINEL(g_memberchk_s = new_atom(pl, "memberchk"), ERR_IDX);
	CHECK_SENTINEL(g_sys_counter_s = new_atom(pl, "$counter"), ERR_IDX);
	CHECK_SENTINEL(g_braces_s = new_atom(pl, "braces"), ERR_IDX);
	CHECK_SENTINEL(g_unify_s = new_atom(pl, "="), ERR_IDX);
	CHECK_SENTINEL(g_notunify_s = new_atom(pl, "\\="), ERR_IDX);
	CHECK_SENTINEL(g_on_s = new_atom(pl, "on"), ERR_IDX);
	CHECK_SENTINEL(g_off_s = new_atom(pl, "off"), ERR_IDX);
	CHECK_SENTINEL(g_cut_s = new_atom(pl, "!"), ERR_IDX);
	CHECK_SENTINEL(g_nil_s = new_atom(pl, "[]"), ERR_IDX);
	CHECK_SENTINEL(g_braces_s = new_atom(pl, "{}"), ERR_IDX);
	CHECK_SENTINEL(g_fail_s = new_atom(pl, "fail"), ERR_IDX);
	CHECK_SENTINEL(g_neck_s = new_atom(pl, ":-"), ERR_IDX);
	CHECK_SENTINEL(g_quad_s = new_atom(pl, "?-"), ERR_IDX);
	CHECK_SENTINEL(g_sys_quad_s = new_atom(pl, "$quad"), ERR_IDX);
	CHECK_SENTINEL(g_eof_s = new_atom(pl, "end_of_file"), ERR_IDX);
	CHECK_SENTINEL(g_lt_s = new_atom(pl, "<"), ERR_IDX);
	CHECK_SENTINEL(g_gt_s = new_atom(pl, ">"), ERR_IDX);
	CHECK_SENTINEL(g_ge_s = new_atom(pl, ">="), ERR_IDX);
	CHECK_SENTINEL(g_eq_s = new_atom(pl, "="), ERR_IDX);
	CHECK_SENTINEL(g_sys_reunify_s = new_atom(pl, "$reunify"), ERR_IDX);
	CHECK_SENTINEL(g_sys_undo_s = new_atom(pl, "$undo"), ERR_IDX);
	CHECK_SENTINEL(g_sys_jump_if_nil_s = new_atom(pl, "$jump_if_nil"), ERR_IDX);
	CHECK_SENTINEL(g_once_s = new_atom(pl, "once"), ERR_IDX);
	CHECK_SENTINEL(g_throw_s = new_atom(pl, "throw"), ERR_IDX);
	CHECK_SENTINEL(g_error_s = new_atom(pl, "error"), ERR_IDX);
	CHECK_SENTINEL(g_slash_s = new_atom(pl, "/"), ERR_IDX);
	CHECK_SENTINEL(g_goal_expansion_s = new_atom(pl, "goal_expansion"), ERR_IDX);
	CHECK_SENTINEL(g_term_expansion_s = new_atom(pl, "term_expansion"), ERR_IDX);
	CHECK_SENTINEL(g_tm_s = new_atom(pl, "tm"), ERR_IDX);
	CHECK_SENTINEL(g_float_s = new_atom(pl, "float"), ERR_IDX);
	CHECK_SENTINEL(g_sys_elapsed_s = new_atom(pl, "$elapsed"), ERR_IDX);
	CHECK_SENTINEL(g_sys_queue_s = new_atom(pl, "$queue"), ERR_IDX);
	CHECK_SENTINEL(g_sys_var_s = new_atom(pl, "$VAR"), ERR_IDX);
	CHECK_SENTINEL(g_sys_stream_property_s = new_atom(pl, "$stream_property"), ERR_IDX);
	CHECK_SENTINEL(g_post_unify_hook_s = new_atom(pl, "$post_unify_hook"), ERR_IDX);
	CHECK_SENTINEL(g_sys_record_key_s = new_atom(pl, "$record_key"), ERR_IDX);
	CHECK_SENTINEL(g_sys_ne_s = new_atom(pl, "$ne"), ERR_IDX);
	CHECK_SENTINEL(g_sys_incr_s = new_atom(pl, "$incr"), ERR_IDX);
	CHECK_SENTINEL(g_sys_block_catcher_s = new_atom(pl, "$block_catcher"), ERR_IDX);
	CHECK_SENTINEL(g_sys_drop_barrier_s = new_atom(pl, "$drop_barrier"), ERR_IDX);
	CHECK_SENTINEL(g_sys_cleanup_if_det_s = new_atom(pl, "$cleanup_if_det"), ERR_IDX);
	CHECK_SENTINEL(g_sys_cut_if_det_s = new_atom(pl, "$cut_if_det"), ERR_IDX);
	CHECK_SENTINEL(g_as_s = new_atom(pl, "as"), ERR_IDX);
	CHECK_SENTINEL(g_colon_s = new_atom(pl, ":"), ERR_IDX);
	CHECK_SENTINEL(g_caret_s = new_atom(pl, "^"), ERR_IDX);
	CHECK_SENTINEL(g_none_s = new_atom(pl, "none"), ERR_IDX);
	CHECK_SENTINEL(g_cont_s = new_atom(pl, "cont"), ERR_IDX);
	CHECK_SENTINEL(g_sys_set_if_var_s = new_atom(pl, "$set_if_var"), ERR_IDX);
	CHECK_SENTINEL(g_is_s = new_atom(pl, "is"), ERR_IDX);
	CHECK_SENTINEL(g_sys_succeed_on_retry_s = new_atom(pl, "$succeed_on_retry"), ERR_IDX);
	CHECK_SENTINEL(g_sys_fail_on_retry_s = new_atom(pl, "$fail_on_retry"), ERR_IDX);
	CHECK_SENTINEL(g_sys_call_check_s = new_atom(pl, "$call_check"), ERR_IDX);
	CHECK_SENTINEL(g_sys_reset_handler_s = new_atom(pl, "$reset_handler"), ERR_IDX);
	CHECK_SENTINEL(g_sys_get_level_s = new_atom(pl, "$get_level"), ERR_IDX);
	CHECK_SENTINEL(g_sys_jump_s = new_atom(pl, "$jump"), ERR_IDX);
	CHECK_SENTINEL(g_reset_s = new_atom(pl, "reset"), ERR_IDX);
	CHECK_SENTINEL(g_ignore_s = new_atom(pl, "ignore"), ERR_IDX);
	CHECK_SENTINEL(g_if_s = new_atom(pl, "if"), ERR_IDX);
	CHECK_SENTINEL(g_count_s = new_atom(pl, "count"), ERR_IDX);
	CHECK_SENTINEL(g_sys_call_s = new_atom(pl, "$call"), ERR_IDX);
	CHECK_SENTINEL(g_sys_cut_s = new_atom(pl, "$cut"), ERR_IDX);
	CHECK_SENTINEL(g_sys_module_s = new_atom(pl, "$module"), ERR_IDX);
	CHECK_SENTINEL(g_sys_loop_s = new_atom(pl, "$LOOP:"), ERR_IDX);
	CHECK_SENTINEL(g_sys_end_s = new_atom(pl, "$:END"), ERR_IDX);
	CHECK_SENTINEL(g_sys_create_var_s = new_atom(pl, "$create_var"), ERR_IDX);
	CHECK_SENTINEL(g_sys_list_s = new_atom(pl, "$list"), ERR_IDX);
	CHECK_SENTINEL(g_sys_abort_s = new_atom(pl, "$abort"), ERR_IDX);
	CHECK_SENTINEL(g_double_bar_s = new_atom(pl, DOUBLE_BAR), ERR_IDX);
	CHECK_SENTINEL(g_exit_s = new_atom(pl, "exit"), ERR_IDX);
	CHECK_SENTINEL(g_killed_s = new_atom(pl, "killed"), ERR_IDX);

	char *ptr = getenv("TPL_LIBRARY_PATH");

	if (ptr)
		g_tpl_lib = TPL_strdup(ptr);

#if !TPL_FREESTANDING && !defined(_WIN32) && !defined(__wasi__) && !defined(__ANDROID__)
	struct rlimit rlp;
	getrlimit(RLIMIT_STACK, &rlp);
	g_max_depth = rlp.rlim_cur / 1024;
#endif

	g_cpu_count = detect_cpu_count();
	g_max_os_threads = detect_max_os_threads();

	return error;
}

void pl_destroy(prolog *pl)
{
	if (!pl) return;

#if USE_THREADS
	if (pl->q_cnt)
		thread_cancel_all(pl);
#endif

	thread_deinitialize(pl);

	if (pl->logfp)
		fclose(pl->logfp);

	// Before the modules: tables hold cells referencing module data.

	tabling_destroy(pl);

	// After tabling_destroy(), which sweeps every thread's tables.

	threads_destroy(pl);

	module_destroy(pl->system_m);
	module_destroy(pl->user_m);
	sl_destroy(pl->biftab);
	module *m;

	while ((m = list_front(&pl->modules)) != NULL)
		module_destroy(m);

	TPL_free(pl->modmap);
	sl_destroy(pl->fortab);
	sl_destroy(pl->help);
	sl_destroy(pl->alias);
	sl_destroy(pl->tasks);		// NULL-safe: never created if no task ever spawned

	for (int i = 0; i < MAX_STREAMS; i++) {
		stream *str = &pl->streams[i];

		if (str->fp) {
			if ((str->fp != stdin)
				&& (str->fp != stdout)
				&& (str->fp != stderr)
			) {
				if (str->is_alias)
					;
				else if (str->is_map)
					sl_destroy(str->keyval);
				else if (str->is_engine) {
					query_destroy(str->engine);
					free_detached_term(str->cur_yield);
					str->cur_yield = NULL;
				}
				else if (str->fp && (i > 2)) {
					fclose(str->fp_in);

					if (str->fp_in != str->fp_out)
						fclose(str->fp_out);
				}
			}

			parser_destroy(str->p);
			sl_destroy(str->alias);
			TPL_free(str->filename);
			TPL_free(str->mode);
			TPL_free(str->data);
		}
	}

	parser_destroy(pl->p);

	if (!--g_tpl_count)
		g_destroy();

	TPL_free(pl);
}

// Defined here rather than in tpl.c so that the engine links without a
// front end: src/prolog.h declares it and src/toplevel.c installs it, so
// tpl.o was the odd file out. Same reason for g_envp in src/bif_os.c.

void g_sigfn(int s)
{
	g_tpl_interrupt = s;
}

prolog *pl_create()
{
	//printf("*** sizeof(cell) = %u bytes\n", (unsigned)sizeof(cell));
	//assert(sizeof(cell) == 24);

	prolog *pl = TPL_calloc(1, sizeof(prolog));
	if (!pl) return NULL;
	bool error = false;
	pl->opt = 1;

	g_prologs[g_tpl_count] = pl;

	if (!g_tpl_count++)
		g_init(pl);


	if (!g_tpl_lib) {
#ifdef DEFAULT_LIBRARY_PATH
		g_tpl_lib = TPL_strdup(DEFAULT_LIBRARY_PATH);
#else
		g_tpl_lib = tpl_realpath(g_argv0);

		if (g_tpl_lib) {
			char *src = g_tpl_lib + strlen(g_tpl_lib) - 1;

			while ((src != g_tpl_lib) && (*src != '/'))
				src--;

			*src = '\0';
			g_tpl_lib = TPL_realloc(g_tpl_lib, strlen(g_tpl_lib)+40);
			strcat(g_tpl_lib, "/library");
		} else
			g_tpl_lib = TPL_strdup("../library");
#endif
	}

	pl->streams[0].fp_in = stdin;
	pl->streams[0].fp_out = stdin;
	CHECK_SENTINEL(pl->streams[0].alias = sl_create((void*)fake_strcmp, (void*)keyfree, NULL), NULL);
	CHECK_SENTINEL(pl->streams[0].filename = TPL_strdup("stdin"), NULL);
	CHECK_SENTINEL(pl->streams[0].mode = TPL_strdup("read"), NULL);
	sl_app(pl->streams[0].alias, TPL_strdup("user_input"), NULL);
	pl->streams[0].eof_action = eof_action_reset;

	pl->streams[1].fp_in = stdout;
	pl->streams[1].fp_out = stdout;
	CHECK_SENTINEL(pl->streams[1].alias = sl_create((void*)fake_strcmp, (void*)keyfree, NULL), NULL);
	CHECK_SENTINEL(pl->streams[1].filename = TPL_strdup("stdout"), NULL);
	CHECK_SENTINEL(pl->streams[1].mode = TPL_strdup("append"), NULL);
	sl_app(pl->streams[1].alias, TPL_strdup("user_output"), NULL);
	pl->streams[1].eof_action = eof_action_reset;

	pl->streams[2].fp_in = stderr;
	pl->streams[2].fp_out = stderr;
	CHECK_SENTINEL(pl->streams[2].alias = sl_create((void*)fake_strcmp, (void*)keyfree, NULL), NULL);
	CHECK_SENTINEL(pl->streams[2].filename = TPL_strdup("stderr"), NULL);
	CHECK_SENTINEL(pl->streams[2].mode = TPL_strdup("append"), NULL);
	sl_app(pl->streams[2].alias, TPL_strdup("user_error"), NULL);
	pl->streams[2].eof_action = eof_action_reset;

	init_lock(&pl->guard);

	// Unconditional: the thread table holds the main thread, and
	// q->pl->main_thread is dereferenced by interrupt_pending() whether
	// or not this build has threads. It used to be threads[0] in a fixed
	// array, so it existed for free; now it has to be made.

	thread_initialize(pl);

	pl->help = sl_create((void*)fake_strcmp, (void*)ptrfree, NULL);
	pl->fortab = sl_create((void*)fake_strcmp, NULL, NULL);
	pl->biftab = sl_create((void*)fake_strcmp, NULL, NULL);
	pl->alias = sl_create((void*)fake_strcmp, NULL, NULL);

	if (pl->biftab)
		load_builtins(pl);

	//printf("Library: %s\n", g_tpl_lib);

	pl->system_m = module_create(pl, "system");

	if (!pl->system_m || pl->system_m->error) {
		pl_destroy(pl);
		return NULL;
	}

	pl->user_m = module_create(pl, "user");

	if (!pl->user_m || pl->user_m->error) {
		pl_destroy(pl);
		return NULL;
	}

	pl->user_m->flags.strict_iso = false;
	pl->m = pl->user_m;
	pl->limit = 1;
	pl->current_input = 0;		// STDIN
	pl->current_output = 1;		// STDOUT
	pl->current_error = 2;		// STDERR
	pl->def_max_depth = 0;
	pl->def_quoted = true;
	pl->def_double_quotes = true;
	pl->rnd_first_time = 1;
	pl->global_bb = true;		// Tabling seems to need it
	pl->tabling = true;			// (:- table)/1 memoizes; false = plain calls

	// In user space...

	set_discontiguous_in_db(pl->user_m, "$predicate_property", 3);

	set_multifile_in_db(pl->user_m, "portray", 1);
	set_multifile_in_db(pl->user_m, "$predicate_property", 3);

	set_dynamic_in_db(pl->user_m, "portray", 1);
	set_dynamic_in_db(pl->user_m, "$op", 3);
	set_dynamic_in_db(pl->user_m, "$predicate_property", 3);
	set_dynamic_in_db(pl->user_m, "$current_prolog_flag", 2);
	set_dynamic_in_db(pl->user_m, "$stream_property", 2);

	pl->user_m->prebuilt = true;
	const char *save_filename = pl->user_m->filename;

	// Load some common libraries...

	const char *bootstrap[] = {"builtins", NULL};

	for (int i = 0; bootstrap[i]; i++) {
		bool found = false;

		for (library *lib = g_libs; lib->name; lib++) {
			if (!strcmp(lib->name, bootstrap[i])) {
				size_t len = *lib->len;
				char *src = TPL_malloc(len+1);
				check_error(src, pl_destroy(pl));
				memcpy(src, lib->start, len);
				src[len] = '\0';
				SB(s1);
				SB_sprintf(s1, "library/%s", lib->name);
				module *m = load_text(pl->user_m, src, SB_cstr(s1));
				m->prebuilt = true;
				SB_free(s1);
				TPL_free(src);
				check_error(m, pl_destroy(pl));
				found = true;
				break;
			}
		}

		if (!found) {
#if TPL_FREESTANDING
			fprintf(stderr, "Error: freestanding build is missing embedded library(%s)\n", bootstrap[i]);
			pl_destroy(pl);
			return NULL;
#else
			SB(s1);
			SB_sprintf(s1, "%s/%s.pl", g_tpl_lib, bootstrap[i]);
			module *m = load_file(pl->user_m, SB_cstr(s1), false, true);

			if (!m || m->error) {
				fprintf(stderr, "Error: could not find library(%s) at %s\n", bootstrap[i], SB_cstr(s1));
				SB_free(s1);
				pl_destroy(pl);
				return NULL;
			}

			m->prebuilt = true;
			SB_free(s1);
#endif
		}
	}

	pl->user_m->filename = save_filename;
	pl->user_m->prebuilt = false;
	return pl;
}
