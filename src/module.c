#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "history.h"
#include "files.h"
#include "library.h"
#include "module.h"
#include "network.h"
#include "parser.h"
#include "prolog.h"
#include "query.h"

struct loaded_file_ {
	loaded_file *next;
	char *filename;
	char *orig_filename;
	const char *parent;
	time_t when_loaded;
	bool is_loaded:1;
};

static const op_table g_ops[] =
{
	{":-", OP_XFX, 1200},
	{":-", OP_FX, 1200},
	{"-->", OP_XFX, 1200},
	{"?-", OP_FX, 1200},
	{"?-", OP_XFX, 1200},				// a quad labelled by a ground term
	{"|", OP_XFY, 1105},
	{";", OP_XFY, 1100},
	{"->", OP_XFY, 1050},
	{"*->", OP_XFY, 1050},
	{",", OP_XFY, 1000},

	{"public", OP_FX, 1150},
	{"discontiguous", OP_FX, 1150},
	{"multifile", OP_FX, 1150},
	{"dynamic", OP_FX, 1150},
	{"ensure_loaded", OP_FX, 1150},
	{"meta_predicate", OP_FX, 1150},

	//{"op", OP_FX, 1150},
	//{"initialization", OP_FX, 1150},
	//{"set_prolog_flag", OP_FX, 1150},
	//{"module", OP_FX, 1150},
	//{"use_module", OP_FX, 1150},

	{"\\+", OP_FY, 900},
	{"as", OP_XFX, 700},
	{"is", OP_XFX, 700},
	{"=", OP_XFX, 700},
	{"\\=", OP_XFX, 700},
	{"==", OP_XFX, 700},
	{"\\==", OP_XFX, 700},
	{"=:=", OP_XFX, 700},
	{"=\\=", OP_XFX, 700},
	{"<", OP_XFX, 700},
	{"=<", OP_XFX, 700},
	{">", OP_XFX, 700},
	{">=", OP_XFX, 700},
	{"@<", OP_XFX, 700},
	{"@=<", OP_XFX, 700},
	{"@>", OP_XFX, 700},
	{"@>=", OP_XFX, 700},
	{"=..", OP_XFX, 700},
	{":", OP_XFY, 600},
	{"+", OP_YFX, 500},
	{"-", OP_YFX, 500},
	{"?", OP_FX, 500},
	{"/\\", OP_YFX, 500},
	{"\\/", OP_YFX, 500},
	{"*", OP_YFX, 400},
	{"/", OP_YFX, 400},
	{"//", OP_YFX, 400},
	{"div", OP_YFX, 400},
	{"rdiv", OP_YFX, 400},
	{"rem", OP_YFX, 400},
	{"mod", OP_YFX, 400},
	{"<<", OP_YFX, 400},
	{">>", OP_YFX, 400},
	{"**", OP_XFX, 200},
	{"^", OP_XFY, 200},
	{"\\", OP_FY, 200},
	{"-", OP_FY, 200},
	{"+", OP_FY, 200},

	{"++", OP_FY, 100},			// used in mode declarations
	{"--", OP_FY, 100},			// used in mode declarations
	{"@", OP_FY, 100},			// used in mode declarations
	{":", OP_FY, 100},			// used in mode declarations

	{DOUBLE_BAR, OP_XFY, 1},

	//{"$", OP_FX, 1},

	{0,0,0}
};

builtins *get_module_help(module *m, const char *name, unsigned arity, bool *found, bool *evaluable)
{
	sliter *iter = sl_find_key(m->pl->help, name);
	builtins *ptr;

	while (sl_next_key(iter, (void**)&ptr)) {
		if (ptr->m != m)
			continue;

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

static const char *set_known(module *m, const char *filename)
{
	loaded_file *ptr = m->loaded_files;

	while (ptr) {
		if (!strcmp(ptr->filename, filename))
			return ptr->filename;

		ptr = ptr->next;
	}

	ptr = TPL_malloc(sizeof(loaded_file));
	ENSURE(ptr);
	ptr->next = m->loaded_files;
	ptr->orig_filename = TPL_strdup(filename);
	ptr->filename = TPL_strdup(filename);
	ptr->is_loaded = false;
	ptr->parent = NULL;
	m->loaded_files = ptr;
	return ptr->filename;
}

static const char *set_loaded(module *m, const char *filename, const char *orig_filename)
{
	loaded_file *ptr = m->loaded_files;

	while (ptr) {
		if (!strcmp(ptr->filename, filename)) {
			ptr->is_loaded = true;
			return ptr->filename;
		}

		ptr = ptr->next;
	}

	ptr = TPL_malloc(sizeof(loaded_file));
	ENSURE(ptr);
	ptr->next = m->loaded_files;
	ptr->orig_filename = TPL_strdup(orig_filename);
	ptr->filename = TPL_strdup(filename);
	ptr->when_loaded = time(0);
	ptr->is_loaded = true;
	ptr->parent = NULL;
	m->loaded_files = ptr;
	return ptr->filename;
}

void set_unloaded(module *m, const char *filename)
{
	loaded_file *ptr = m->loaded_files;

	while (ptr) {
		if (!strcmp(ptr->filename, filename)) {
			ptr->is_loaded = false;
			return;
		}

		ptr = ptr->next;
	}
}

static bool is_loaded(const module *m, const char *filename)
{
	loaded_file *ptr = m->loaded_files;

	while (ptr) {
		if (ptr->is_loaded && !strcmp(ptr->filename, filename))
			return true;

		ptr = ptr->next;
	}

	return false;
}

const char *get_loaded(const module *m, const char *filename)
{
	loaded_file *ptr = m->loaded_files;

	while (ptr) {
		if (ptr->is_loaded && !strcmp(ptr->filename, filename))
			return ptr->orig_filename;

		ptr = ptr->next;
	}

	return filename;
}

const char *get_parent(const module *m, const char *filename)
{
	loaded_file *ptr = m->loaded_files;

	while (ptr) {
		if (ptr->is_loaded && !strcmp(ptr->filename, filename))
			return ptr->parent ? ptr->parent : filename;

		ptr = ptr->next;
	}

	return filename;
}

void set_parent(const module *m, const char *filename, const char *parent)
{
	loaded_file *ptr = m->loaded_files;

	while (ptr) {
		if (ptr->is_loaded && !strcmp(ptr->filename, filename)) {
			ptr->parent = parent;
			return;
		}

		ptr = ptr->next;
	}
}

static void clear_loaded(const module *m)
{
	loaded_file *ptr = m->loaded_files;

	while (ptr) {
		loaded_file *save = ptr;
		ptr = ptr->next;
		if (save->orig_filename) TPL_free(save->orig_filename);
		if (save->filename) TPL_free(save->filename);
		TPL_free(save);
	}
}

void make(module *m)
{
	m->make = true;
	loaded_file *ptr = m->loaded_files;

	while (ptr) {
		struct stat st = {0};
		loaded_file *save = ptr->next;

		if (stat(ptr->filename, &st) == 0) {
			if (st.st_mtime > ptr->when_loaded) {
				char *parent_filename = TPL_strdup(get_parent(m, ptr->filename));
				printf("%% %s changed\n", ptr->filename);

				if (strcmp(parent_filename, ptr->filename))
					unload_file(m, ptr->filename);

				unload_file(m, parent_filename);
				load_file(m, parent_filename, false, true);
				TPL_free(parent_filename);
			}
		}

		ptr = save;
	}

	m->make = false;
}

predicate *find_predicate(module *m, cell *c)
{
	uint32_t arity = get_arity(c);
	cell tmp = *c;
	tmp.tag = TAG_INTERNED;
	tmp.flags = 0;
	tmp.num_cells = 1;
	tmp.match = NULL;

	if (is_cstring(c)) {
		tmp.val_off = new_atom(m->pl, C_STR(m, c));
	}

	set_arity(&tmp, arity);

	sliter *iter = sl_find_key(m->index, &tmp);
	predicate *pr = NULL;

	while (sl_next_key(iter, (void*)&pr)) {
		if (!pr || pr->is_abolished)
			continue;

		sl_done(iter);
		return pr;
	}

	sl_done(iter);
	return NULL;
}

predicate *search_predicate(module *m, cell *c)
{
	predicate *pr = find_predicate(m, c);

	if (pr)
		return pr;

	if (m->pl->user_m) {
		pr = find_predicate(m->pl->user_m, c);

		if (pr)
			return pr;
	}

	return NULL;
}

predicate *create_predicate(module *m, cell *c, bool *created)
{
	if (created) *created = false;
	bool found, evaluable;

	if ((c->val_off == g_neck_s) || is_var(c) || (get_arity(c) > MAX_PROCEDURE_ARITY))
		return NULL;

	builtins *b;

	if (b = get_builtin_term(m, c, &found, &evaluable),
		!evaluable && found && b->iso) {
		if (m->p->is_consulting)
			fprintf(stderr, "Error: permission error modifying %s/%u\n", C_STR(m, c), get_arity(c));

		return NULL;
	}

	predicate *pr = TPL_calloc(1, sizeof(predicate));

	if (!pr)
		return NULL;

	list_push_back(&m->predicates, pr);

	if (created)
		*created = true;

	pr->filename = m->filename;
	pr->m = m;
	pr->key = *c;
	pr->key.tag = TAG_INTERNED;
	pr->key.num_cells = 1;
	pr->key.match = NULL;
	set_arity(&pr->key, get_arity(c));
	pr->is_noindex = m->pl->noindex || !get_arity(&pr->key);
	sl_app(m->index, &pr->key, pr);
	return pr;
}

static void abolish_predicate(predicate *pr)
{
	while (pr->head) {
		rule *tmp = pr->head;
		pr->head = pr->head->next;
		clear_clause(&tmp->cl);
		TPL_free(tmp);
		pr->cnt--;
	}

	pr->head = pr->tail = NULL;
	sl_destroy(pr->idx0);
	sl_destroy(pr->idx2);
	sl_destroy(pr->idx1);
	pr->idx0 = pr->idx1 = pr->idx2 = NULL;
	pr->is_var_in_head = false;
	pr->is_var_in_first_arg = false;
	pr->is_var_in_idx2_arg = false;
	pr->idx2_arg = 0;

	if (pr->meta_args) {
		unshare_cells(pr->meta_args, pr->meta_args->num_cells);
		TPL_free(pr->meta_args);
		pr->meta_args = NULL;
	}
}

static void destroy_predicate(module *m, predicate *pr)
{
	sl_del(m->index, &pr->key);

	while (pr->head) {
		rule *tmp = pr->head;
		pr->head = pr->head->next;
		clear_clause(&tmp->cl);
		TPL_free(tmp);
	}

	pr->head = pr->tail = NULL;
	sl_destroy(pr->idx0);
	sl_destroy(pr->idx2);
	sl_destroy(pr->idx1);
	pr->idx0 = pr->idx1 = pr->idx2 = NULL;
	pr->is_var_in_head = false;
	pr->is_var_in_first_arg = false;
	pr->is_var_in_idx2_arg = false;
	pr->idx2_arg = 0;

	if (pr->meta_args) {
		unshare_cells(pr->meta_args, pr->meta_args->num_cells);
		TPL_free(pr->meta_args);
	}

	list_remove(&m->predicates, pr);
	TPL_free(pr);
}

bool find_goal_expansion(module *m, cell *c)
{
	if (m->wild_goal_expansion)
		return true;

	for (pi *g = m->gex_head; g; g = g->next) {
		if ((g->key.val_off == c->val_off) && (get_arity(&g->key) == get_arity(c)))
			return true;
	}

	return false;
}

// Like find_goal_expansion but ignores a module's wildcard (var-headed)
// goal_expansion. Used to decide whether to run the expansion query in
// user_m: only redirect when user has a SPECIFIC hook for this functor,
// so a wildcard user:goal_expansion (eg. clpz) does not hijack every goal.

bool find_goal_expansion_specific(module *m, cell *c)
{
	for (pi *g = m->gex_head; g; g = g->next) {
		if ((g->key.val_off == c->val_off) && (get_arity(&g->key) == get_arity(c)))
			return true;
	}

	return false;
}

bool search_goal_expansion(module *m, cell *c)
{
	if (find_goal_expansion(m, c))
		return true;

	if (m->pl->user_m) {
		if (find_goal_expansion(m->pl->user_m, c))
			return true;
	}

	for (unsigned i = 0; i < m->idx_used; i++) {
		module *tmp_m = m->used[i];

		if (find_goal_expansion(tmp_m, c))
			return true;
	}


	for (module *tmp_m = list_front(&m->pl->modules);
		tmp_m; tmp_m = list_next(tmp_m)) {
		if (m == tmp_m)
			continue;

		if (find_goal_expansion(tmp_m, c))
			return true;
	}

	return false;
}

void create_goal_expansion(module *m, cell *c)
{
	if (find_goal_expansion(m, c))
		return;

	pi *g = TPL_calloc(1, sizeof(pi));
	ENSURE(g);
	g->prev = m->gex_tail;

	if (m->gex_tail)
		m->gex_tail->next = g;

	m->gex_tail = g;

	if (!m->gex_head)
		m->gex_head = g;

	g->key = *c;
}

static int predicate_cmpkey(const void *ptr1, const void *ptr2, const void *param, void *l)
{
	const cell *p1 = (const cell*)ptr1;
	const cell *p2 = (const cell*)ptr2;

	if (get_arity(p1) < get_arity(p2))
		return -1;

	if (get_arity(p1) > get_arity(p2))
		return 1;

	if (p1->val_off == p2->val_off)
		return 0;

	return strcmp(g_global_atoms+p1->val_off, g_global_atoms+p2->val_off);
}

static int index_cmpkey_(const void *ptr1, const void *ptr2, const void *param, void *l)
{
	cell *p1 = (cell*)ptr1;
	cell *p2 = (cell*)ptr2;

	if (is_var(p1) || is_var(p2))
		return 0;

	if (is_smallint(p1)) {
		if (is_smallint(p2)) {
			if (get_smallint(p1) < get_smallint(p2))
				return -1;
			else if (get_smallint(p1) > get_smallint(p2))
				return 1;
			else
				return 0;
		} else if (is_bigint(p2)) {
			return -mp_int_compare_value(&p2->val_bigint->ival, p1->val_int);
		} else if (is_rational(p2)) {
			return -mp_rat_compare_value(&p2->val_bigint->irat, p1->val_int, 1);
		} else
			return -1;
	} else if (is_bigint(p1)) {
		if (is_bigint(p2)) {
			return mp_int_compare(&p1->val_bigint->ival, &p2->val_bigint->ival);
		} else if (is_smallint(p2)) {
			return mp_int_compare_value(&p1->val_bigint->ival, p2->val_int);
		} else
			return -1;
	} else if (is_rational(p1)) {
		if (is_rational(p2)) {
			return mp_rat_compare(&p1->val_bigint->irat, &p2->val_bigint->irat);
		} else if (is_bigint(p2)) {
			mpq_t tmp;
			mp_int_init_copy(&tmp.num, &p2->val_bigint->ival);
			mp_int_init_value(&tmp.den, 1);
			int ok = mp_rat_compare(&p1->val_bigint->irat, &tmp);
			mp_rat_clear(&tmp);
			return ok;
		} else if (is_smallint(p2)) {
			return mp_rat_compare_value(&p1->val_bigint->irat, p2->val_int, 1);
		} else
			return -1;
	} else if (is_float(p1)) {
		if (is_float(p2)) {
			if (get_float(p1) < get_float(p2))
				return -1;
			else if (get_float(p1) > get_float(p2))
				return 1;
			else
				return 0;
		} else if (is_integer(p2))
			return 1;
		else
			return -1;
	} else if (is_string(p1) && is_string(p2)) {
		return strcmp(C_STR(m, p1), C_STR(m, p2));
	} else if (is_list(p1) && is_list(p2)) {
		PROLOG_LIST_HANDLER(p1);
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p1) && is_list(p2)) {
			cell *h1 = PROLOG_LIST_HEAD(p1);
			cell *h2 = PROLOG_LIST_HEAD(p2);

			if (l && (is_var(h1) || is_var(h2))) {
				if (sl_is_find(l))
					return 0;

				sl_set_wild_card(l);
			} else {
				int ok = index_cmpkey_(h1, h2, param, l);

				if (ok != 0)
					return ok;
			}

			p1 = PROLOG_LIST_TAIL(p1);
			p2 = PROLOG_LIST_TAIL(p2);
		}

		if (l && (is_var(p1) || is_var(p2))) {
			if (!sl_is_find(l))
				sl_set_wild_card(l);

			return 0;
		}

		return index_cmpkey_(p1, p2, param, l);
	} else if (is_interned(p1) && !get_arity(p1)) {
		if (is_interned(p2) && !get_arity(p2)) {
			if (p1->val_off == p2->val_off)
				return 0;

			return strcmp(C_STR(m, p1), C_STR(m, p2));
		} else if (is_atom(p2))
			return strcmp(C_STR(m, p1), C_STR(m, p2));
		else if (is_number(p2))
			return 1;
		else
			return -1;
	} else if (is_atom(p1)) {
		if (is_atom(p2))
			return strcmp(C_STR(m, p1), C_STR(m, p2));
		else if (is_number(p2))
			return 1;
		else
			return -1;
	} else if (is_compound(p1)) {
		if (is_compound(p2)) {
			if (get_arity(p1) < get_arity(p2))
				return -1;

			if (get_arity(p1) > get_arity(p2))
				return 1;

			if (p1->val_off != p2->val_off)
				return strcmp(C_STR(m, p1), C_STR(m, p2));

			uint32_t arity = get_arity(p1);
			p1++; p2++;

			while (arity--) {
				if (l && (is_var(p1) || is_var(p2))) {
					if (sl_is_find(l))
						break;

					sl_set_wild_card(l);
					p1 += p1->num_cells;
					p2 += p2->num_cells;
					continue;
				}

				int ok = index_cmpkey_(p1, p2, param, l);

				if (ok != 0)
					return ok;

				p1 += p1->num_cells;
				p2 += p2->num_cells;
			}

			return 0;
		} else
			return 1;
	}

	return 0;
}

int index_cmpkey(const void *ptr1, const void *ptr2, const void *param, void *l)
{
	return index_cmpkey_(ptr1, ptr2, param, l);
}

rule *find_in_db(module *m, uuid *ref)
{
	for (module *tmp_m = list_front(&m->pl->modules);
		tmp_m; tmp_m = list_next(tmp_m)) {
		for (predicate *pr = list_front(&m->predicates);
			pr; pr = list_next(pr)) {
			if (!pr->is_dynamic)
				continue;

			for (rule *r = pr->head ; r; r = r->next) {
				if (r->dbgen_retracted)
					continue;

				if (!memcmp(&r->u, ref, sizeof(uuid)))
					return r;
			}
		}
	}

	return NULL;
}

// Drop a clause's index entries. The index keys are borrowed pointers
// into the clause's own cells, so an entry that outlives its clause
// turns the next skiplist descent into a use-after-free.
//
// Callers must withdraw every doomed clause BEFORE freeing any of them:
// sl_rem() descends by comparing against other nodes' keys, and those
// point into other clauses, so freeing as you go walks the comparator
// into memory released on an earlier iteration.

void index_remove_clause(predicate *pr, rule *r)
{
	if (!pr || !pr->idx1)
		return;

	cell *c = get_head(r->cl.cells);
	cell *k1 = get_arity(c) ? FIRST_ARG(c) : c;
	bool ground = !is_var(c) && (!is_compound(c) || is_ground(c));

	if (pr->idx0 && ground)
		sl_rem(pr->idx0, c, r);

	if (pr->idx2)
		sl_rem(pr->idx2, get_nth_arg(c, pr->idx2_arg), r);

	sl_rem(pr->idx1, k1, r);
}

static void purge_properties(predicate *pr)
{
	cell tmp;
	make_atom(&tmp, new_atom(pr->m->pl, "$predicate_property"));
	set_arity(&tmp, 3);
	predicate *pr2 = find_predicate(pr->m, &tmp);
	if (!pr2) return;

	// Pass 1: mark, and withdraw index entries while the whole chain is
	// still live.

	for (rule *r = pr2->head; r; r = r->next) {
		cell *f = r->cl.cells;
		cell *p1 = f + 1;
		cell *p2 = p1 + p1->num_cells;

		if ((get_arity(&pr->key) != get_arity(p2)) || (pr->key.val_off != p2->val_off))
			continue;

		r->dbgen_retracted = ++pr->m->pl->dbgen;
		pr2->last_modified = r->dbgen_retracted;	// incremental tabling (item 3)
		pr2->cnt--;
		index_remove_clause(pr2, r);
	}

	if (pr2->refcnt)
		return;

	for (rule *r = pr2->head; r; ) {
		cell *f = r->cl.cells;
		cell *p1 = f + 1;
		cell *p2 = p1 + p1->num_cells;
		rule *save = r;
		r = r->next;

		if ((get_arity(&pr->key) != get_arity(p2)) || (pr->key.val_off != p2->val_off))
			continue;

		predicate_delink(pr2, save);
		clear_clause(&save->cl);
		TPL_free(save);
	}
}

void push_property(module *m, const char *name, unsigned arity, const char *type)
{
	//printf("*** PUSH %s/%u\n", name, arity);
	char tmpbuf[1024];
	format_property(m, tmpbuf, sizeof(tmpbuf), name, arity, type, false);
	parser *p = parser_create(m);

	if (!p)
		return;

	p->srcptr = tmpbuf;
	p->is_consulting = true;
	p->internal = true;
	tokenize(p, false, false);
	parser_destroy(p);
}

static bool property_matches(module *m, const rule *r, const char *name, unsigned arity)
{
	const cell *p0 = r->cl.cells;
	const cell *p1 = p0 + 1;
	const cell *p2 = p1 + p1->num_cells;

	if (strcmp(C_STR(m, p2), name))
		return false;

	return get_arity(p2) == arity;
}

void clear_property(module *m, const char *name, unsigned arity)
{
	cell tmp;
	make_atom(&tmp, new_atom(m->pl, "$predicate_property"));
	set_arity(&tmp, 3);
	predicate *pr = find_predicate(m, &tmp);
	if (!pr) return;

	if (pr->idx1 && !pr->refcnt) {
		for (rule *r = pr->head; r; r = r->next) {
			if (property_matches(m, r, name, arity))
				index_remove_clause(pr, r);
		}
	}

	bool removed = false;

	for (rule *r = pr->head; r;) {
		if (!property_matches(m, r, name, arity)) {
			r = r->next;
			continue;
		}

		rule *save = r;
		r = r->next;

		if (pr->refcnt)
			retract_from_db(m, save);
		else {
			predicate_delink(pr, save);
			clear_clause(&save->cl);
			TPL_free(save);
			removed = true;
		}
	}

	if (removed && (pr->is_var_in_head || pr->is_var_in_first_arg || pr->is_var_in_idx2_arg))
		recheck_var_in_indexed_args(pr);
}

void push_template(module *m, const char *name, unsigned arity, const builtins *ptr)
{
	char tmpbuf[1024];
	format_template(m, tmpbuf, sizeof(tmpbuf), name, arity, ptr, false, NULL);
	parser *p = parser_create(m);

	if (!p)
		return;

	p->srcptr = tmpbuf;
	p->is_consulting = true;
	p->internal = true;
	tokenize(p, false, false);
	parser_destroy(p);
}

void set_discontiguous_in_db(module *m, const char *name, unsigned arity)
{
	if (arity > MAX_PROCEDURE_ARITY) {
		m->error = true;
		return;
	}

	cell tmp = (cell){0};
	tmp.tag = TAG_INTERNED;
	tmp.val_off = new_atom(m->pl, name);
	ENSURE(tmp.val_off != ERR_IDX);
	set_arity(&tmp, arity);
	predicate *pr = find_predicate(m, &tmp);
	if (!pr) pr = create_predicate(m, &tmp, NULL);

	if (pr && !pr->is_discontiguous) {
		push_property(m, name, arity, "discontiguous");
		pr->is_discontiguous = true;
	} else if (!pr)
		m->error = true;
}

void set_multifile_in_db(module *m, const char *name, pl_idx arity)
{
	if (arity > MAX_PROCEDURE_ARITY) {
		m->error = true;
		return;
	}

	cell tmp = (cell){0};
	tmp.tag = TAG_INTERNED;
	tmp.val_off = new_atom(m->pl, name);
	ENSURE(tmp.val_off != ERR_IDX);
	set_arity(&tmp, arity);
	predicate *pr = find_predicate(m, &tmp);
	if (!pr) pr = create_predicate(m, &tmp, NULL);

	if (pr && !pr->is_multifile) {
		push_property(m, name, arity, "multifile");
		pr->is_multifile = true;
	} else if (!pr)
		m->error = true;
}

void set_dynamic_in_db(module *m, const char *name, unsigned arity)
{
	if (arity > MAX_PROCEDURE_ARITY) {
		m->error = true;
		return;
	}

	cell tmp = (cell){0};
	tmp.tag = TAG_INTERNED;
	tmp.val_off = new_atom(m->pl, name);
	ENSURE(tmp.val_off != ERR_IDX);
	set_arity(&tmp, arity);
	predicate *pr = find_predicate(m, &tmp);
	if (!pr) pr = create_predicate(m, &tmp, NULL);

	if (pr && !pr->is_dynamic) {
		push_property(m, name, arity, "dynamic");
		pr->is_dynamic = true;
	} else if (!pr)
		m->error = true;
}

void set_meta_predicate_in_db(module *m, cell *c)
{
	const char *name = C_STR(m, c);
	unsigned arity = get_arity(c);

	if (arity > MAX_PROCEDURE_ARITY) {
		m->error = true;
		return;
	}

	cell tmp = (cell){0};
	tmp.tag = TAG_INTERNED;
	tmp.val_off = new_atom(m->pl, name);
	ENSURE(tmp.val_off != ERR_IDX);
	set_arity(&tmp, arity);
	predicate *pr = find_predicate(m, &tmp);
	if (!pr) pr = create_predicate(m, &tmp, NULL);

	if (pr && !pr->is_meta_predicate) {
		query q = (query){0};
		q.pl = m->pl;
		q.st.m = m;
		char *dst = print_canonical_to_strbuf(&q, c, 0, 0);
		char tmpbuf[1024];
		snprintf(tmpbuf, sizeof(tmpbuf), "meta_predicate(%s)", dst);
		TPL_free(dst);
		push_property(m, name, arity, tmpbuf);

		if (pr->meta_args) {
			unshare_cells(pr->meta_args, pr->meta_args->num_cells);
			TPL_free(pr->meta_args);
		}

		pr->is_meta_predicate = true;
		pr->meta_args = TPL_malloc(sizeof(cell)*c->num_cells);
		dup_cells(pr->meta_args, c, c->num_cells);
	} else if (!pr)
		m->error = true;
}

static bool is_check_directive(const cell *c)
{
	if ((c->val_off == g_neck_s) && (get_arity(c) == 1))
		return true;

	return false;
}

static bool do_use_module(module *cur_m, cell *c, module **mptr)
{
	*mptr = NULL;
	cell *p1 = c + 1;
	const char *name = C_STR(cur_m, p1);
	char dstbuf[1024*4];
	bool is_library = false;

	if (is_compound(p1) && !strcmp(name, "library")) {
		is_library = true;
		p1 = p1 + 1;
		if (!is_interned(p1)) return false;
		snprintf(dstbuf, sizeof(dstbuf), "%s", g_tpl_lib);
		name = C_STR(cur_m, p1);
		int cnt = 1;

		while ((get_arity(p1) == 2) && !strcmp(name, "/")) {
			cnt++;
			p1++;
		}

		while (cnt-- && is_interned(p1) && !get_arity(p1) && (p1->val_off != g_nil_s)) {
			name = C_STR(cur_m, p1);
			strcat(dstbuf, "/");
			strcat(dstbuf, name);
			p1++;
		}

		module *m;

		if ((m = find_module(cur_m->pl, name)) != NULL) {
			if (m != cur_m) {
				bool found = false;

				for (unsigned i = 0; i < cur_m->idx_used; i++) {
					if (cur_m->used[i] == m) {
						found = true;
						break;
					}
				}

				if (!found)
					module_add_use(cur_m, m);
			}

			*mptr = m;
			return true;
		}

		// These are some modules that don't exist in Trealla
		// but the predicates probably pop up somewhere else.

		if (!strcmp(name, "between")
		    || !strcmp(name, "cont")
		    || !strcmp(name, "crypto")
		    || !strcmp(name, "dcgs")
		    || !strcmp(name, "ffi")
		    || !strcmp(name, "files")
		    || !strcmp(name, "loader")
		    || !strcmp(name, "process")
		    || !strcmp(name, "terms")
		    )
			return true;

		for (library *lib = g_libs; lib->name; lib++) {
			if (strcmp(lib->name, name))
				continue;

			char *src = TPL_malloc(*lib->len+1);

			if (!src)
				return false;

			memcpy(src, lib->start, *lib->len);
			src[*lib->len] = '\0';
			SB(s1);
			SB_sprintf(s1, "library%c%s", '/', lib->name);
			m = load_text(cur_m, src, SB_cstr(s1));
			SB_free(s1);
			TPL_free(src);

			if (!m)
				return false;

			if (m != cur_m)
				module_add_use(cur_m, m);

			*mptr = m;
			return !m->error;
		}
	}

	module *m;

	if ((m = find_module(cur_m->pl, name)) != NULL) {
		if (m != cur_m)
			module_add_use(cur_m, m);

		*mptr = m;
		return true;
	}

	for (library *lib = g_libs; lib->name; lib++) {
		if (strcmp(lib->name, name))
			continue;

		char *src = TPL_malloc(*lib->len+1);
		ENSURE(src);
		memcpy(src, lib->start, *lib->len);
		src[*lib->len] = '\0';
		SB(s1);
		SB_sprintf(s1, "library/%s", lib->name);
		m = load_text(cur_m, src, SB_cstr(s1));
		SB_free(s1);
		TPL_free(src);

		if (m != cur_m)
			module_add_use(cur_m, m);

		*mptr = m;
		return !m->error;
	}

	char *filename = relative_to(cur_m->filename, is_library?dstbuf:name);

	if (!(m = load_file(cur_m, filename, false, true))) {
		fprintf(stderr, "Warning: module file not found: %s\n", filename);
		TPL_free(filename);
		return false;
	}

	TPL_free(filename);

	if (m != cur_m)
		module_add_use(cur_m, m);

	*mptr = m;
	return !m->error;
}

static bool do_import_predicate(module *cur_m, module *m, predicate *pr, cell *as)
{
	clear_property(cur_m, C_STR(m, &pr->key), get_arity(&pr->key));
	predicate *pr2 = find_predicate(cur_m, as);
	//if (pr2) printf("*** %s:%s/%u => %s\n", cur_m->name, C_STR(q,&pr2->key), get_arity(&pr2->key), m->name);
	if (!pr2) pr2 = create_predicate(cur_m, as, NULL);

	if (!pr2)
		return false;

	pr2->alias = pr->alias ? pr->alias : pr;
	char tmpbuf[1024];
	snprintf(tmpbuf, sizeof(tmpbuf), "imported_from(%s)", m->name);
	push_property(cur_m, C_STR(m, &pr->key), get_arity(&pr->key), tmpbuf);

	if (pr->is_dynamic)
		push_property(cur_m, C_STR(m, as), get_arity(as), "dynamic");

	if (!pr->meta_args)
		return true;

	SB(pr);
	SB_sprintf(pr, "meta_predicate(%s(", C_STR(m, as));
	cell *key = pr->meta_args + 1;

	for (unsigned i = 0; i < get_arity(&pr->key); i++, key++) {
		if (i != 0)
			SB_strcat(pr, ",");

		if (is_smallint(key)) {
			SB_sprintf(pr, "%d", (int)get_smallint(key));
		} else {
			SB_strcat(pr, C_STR(cur_m, key));
		}
	}

	SB_strcat(pr, "))");
	push_property(cur_m, C_STR(m, as), get_arity(as), SB_cstr(pr));
	return true;
}

// Import lists may write a DCG non-terminal as Name//Arity, meaning
// Name/(Arity+2), just as module/2 export lists do.

static bool get_import_pi(module *m, const cell *pi, cell *tmp)
{
	if (!is_compound(pi) || (get_arity(pi) != 2))
		return false;

	bool dcg = !strcmp(C_STR(m, pi), "//");

	if (!dcg && strcmp(C_STR(m, pi), "/"))
		return false;

	const cell *c_name = pi + 1, *c_arity = c_name + c_name->num_cells;

	if (!is_atom(c_name) || !is_smallint(c_arity))
		return false;

	*tmp = *c_name;
	set_arity(tmp, get_smalluint(c_arity) + (dcg ? 2 : 0));
	return true;
}

bool do_use_module_1(module *cur_m, cell *c)
{
	module *m;

	if (!do_use_module(cur_m, c, &m))
		return false;

	if (!m)
		return true;

	for (predicate *pr = list_front(&m->predicates);
		pr; pr = list_next(pr)) {
		if (!pr->is_public)
			continue;

		if (!do_import_predicate(cur_m, m, pr, &pr->key))
			return false;
	}

	return true;
}

bool do_use_module_2(module *cur_m, cell *c)
{
	module *m;

	if (!do_use_module(cur_m, c, &m))
		return false;

	if (!m)
		return true;

	cell *p1 = c + 1;
	cell *p2 = p1 + p1->num_cells;
	PROLOG_LIST_HANDLER(p2);

	while (is_iso_list(p2)) {
		cell *head = PROLOG_LIST_HEAD(p2);

		if (is_interned(head) && (get_arity(head) == 2)
			&& ((head->val_off == g_as_s) || (head->val_off == g_colon_s))) {
			cell *lhs = head + 1;
			cell *rhs = lhs + lhs->num_cells;

			cell tmp;

			if (get_import_pi(m, lhs, &tmp) && is_atom(rhs)) {
				predicate *pr = find_predicate(m, &tmp);
				if (!pr) return false;
				tmp.val_off = rhs->val_off;
				do_import_predicate(cur_m, m, pr, &tmp);
			} else if (get_import_pi(m, lhs, &tmp)
				&& is_structure(rhs) && (get_arity(rhs) == 2)) {
				predicate *pr = find_predicate(m, &tmp);
				if (!pr) return false;
				tmp.val_off = (rhs+1)->val_off;
				do_import_predicate(cur_m, m, pr, &tmp);
			}
		} else {
			cell tmp;

			if (get_import_pi(m, head, &tmp)) {
				predicate *pr = find_predicate(m, &tmp);
				if (!pr) return false;
				do_import_predicate(cur_m, m, pr, &pr->key);
			}
		}

		p2 = PROLOG_LIST_TAIL(p2);
	}

	return true;
}

#if USE_FFI
bool do_foreign_struct(module *m, cell *p)
{
	cell *p1 = p + 1;
	cell *p2 = p1 + p1->num_cells;
	const char *symbol = C_STR(m, p1);
	cell *l = p2;

	void *handle = NULL;
	do_register_struct(m, NULL, handle, symbol, l, 0, "invalid");
	return true;
}

bool do_use_foreign_module(module *m, cell *p)
{
	cell *p1 = p + 1;
	cell *p2 = p1 + p1->num_cells;
	PROLOG_LIST_HANDLER(p2);

	const char *name = C_STR(m, p1);
	void *handle = do_dlopen(name, 0);

	if (!handle) {
		fprintf(stderr, "Error: foreign module creation failed: %s, %s\n", name, get_loaded(m, m->filename));
		m->error = true;
		return false;
	}

	while (is_iso_list(p2)) {
		cell *h = PROLOG_LIST_HEAD(p2);
		const char *symbol = C_STR(m, h);
		cell *l = h + 1;
		cell *r = l + l->num_cells;
		const char *ret_type = C_STR(m, r);
		do_register_predicate(m, NULL, handle, symbol, l, 0, ret_type);
		p2 = PROLOG_LIST_TAIL(p2);
	}

	return true;
}
#endif

void convert_to_literal(module *m, cell *c)
{
	uint32_t arity = is_string(c) ? 0 : get_arity(c);
	pl_idx off = new_atom(m->pl, C_STR(m, c));
	c->tag = TAG_INTERNED;
	c->val_off = off;
	c->match = NULL;
	c->flags = 0;
	set_arity(c, arity);
}

predicate *find_functor(module *m, const char *name, unsigned arity)
{
	cell tmp = (cell){0};
	tmp.tag = TAG_INTERNED;
	tmp.val_off = new_atom(m->pl, name);
	set_arity(&tmp, arity);
	return find_predicate(m, &tmp);
}

#define DUMP_KEYS 0

#if DUMP_KEYS
static const char *dump_key(const void *k, const void *v, const void *p)
{
	(void)p; (void)k;
	const op_table *op = (const op_table*)v;
	static char tmpbuf[1024];
	snprintf(tmpbuf, sizeof(tmpbuf), "'%s:%u:%u'", op->name, op->specifier, op->priority);
	return tmpbuf;
}
#endif

static bool set_op_internal(module *m, const char *name, unsigned specifier, unsigned priority)
{
	sliter *iter = sl_find_key(m->ops, name);
	op_table *ptr;

	while (sl_next_key(iter, (void**)&ptr)) {
		if (IS_INFIX(ptr->specifier) != IS_INFIX(specifier))
			continue;

		if (IS_POSTFIX(ptr->specifier) != IS_POSTFIX(specifier))
			continue;

		if (!ptr->priority)
			continue;

		if (!priority) {
			ptr->specifier = 0;
			ptr->priority = 0;
			sl_done(iter);
			return true;
		}

		ptr->priority = priority;
		ptr->specifier = specifier;
		sl_done(iter);
		m->user_ops = true;
		return true;
	}

	sl_done(iter);
	iter = sl_find_key(m->defops, name);

	while (sl_next_key(iter, (void**)&ptr)) {
		if (IS_INFIX(ptr->specifier) != IS_INFIX(specifier))
			continue;

		if (IS_POSTFIX(ptr->specifier) != IS_POSTFIX(specifier))
			continue;

		if (!ptr->priority)
			continue;

		if (!priority) {
			ptr->specifier = 0;
			ptr->priority = 0;
			sl_done(iter);
			return true;
		}

		ptr->priority = priority;
		ptr->specifier = specifier;
		sl_done(iter);
		m->user_ops = true;
		return true;
	}

	sl_done(iter);
	op_table *tmp = TPL_malloc(sizeof(op_table));
	ENSURE(tmp);
	tmp->name = set_known(m, name);
	tmp->priority = priority;
	tmp->specifier = specifier;
	m->user_ops = true;
	sl_app(m->ops, tmp->name, tmp);
	return true;
}

bool set_op(module *m, const char *name, unsigned specifier, unsigned priority)
{
	set_op_internal(m, name, specifier, priority);

	for (unsigned i = 0; i < m->idx_used; i++) {
		module *tmp_m = m->used[i];

		if ((m == tmp_m) || !tmp_m->user_ops)
			continue;

		set_op_internal(tmp_m, name, specifier, priority);
	}

	return true;
}

static unsigned search_op_internal(const module *m, const char *name, unsigned *specifier, bool prefer_unifix)
{
	const op_table *ptr;
	sliter *iter = sl_find_key(m->defops, name);

	while (sl_next_key(iter, (void**)&ptr)) {
		if (!ptr->priority)
			continue;

		if (!IS_INFIX(ptr->specifier))
			continue;

		if (prefer_unifix)
			continue;

		if (specifier) *specifier = ptr->specifier;
		unsigned n = ptr->priority;
		sl_done(iter);
		return n;
	}

	sl_done(iter);
	iter = sl_find_key(m->ops, name);

	while (sl_next_key(iter, (void**)&ptr)) {
		if (!ptr->priority)
			continue;

		if (!IS_INFIX(ptr->specifier))
			continue;

		if (prefer_unifix)
			continue;

		if (specifier) *specifier = ptr->specifier;
		unsigned n = ptr->priority;
		sl_done(iter);
		return n;
	}

	sl_done(iter);
	iter = sl_find_key(m->defops, name);

	while (sl_next_key(iter, (void**)&ptr)) {
		if (!ptr->priority)
			continue;

		if (IS_INFIX(ptr->specifier))
			continue;

		if (prefer_unifix && !IS_PREFIX(ptr->specifier) && !IS_POSTFIX(ptr->specifier))
			continue;

		if (specifier) *specifier = ptr->specifier;
		unsigned n = ptr->priority;
		sl_done(iter);
		return n;
	}

	sl_done(iter);
	iter = sl_find_key(m->ops, name);

	while (sl_next_key(iter, (void**)&ptr)) {
		if (!ptr->priority)
			continue;

		if (IS_INFIX(ptr->specifier))
			continue;

		if (prefer_unifix && !IS_PREFIX(ptr->specifier) && !IS_POSTFIX(ptr->specifier))
			continue;

		if (specifier) *specifier = ptr->specifier;
		unsigned n = ptr->priority;
		sl_done(iter);
		return n;
	}

	sl_done(iter);

	if (prefer_unifix)
		return search_op_internal(m, name, specifier, false);

	return 0;
}

unsigned search_op(module *m, const char *name, unsigned *specifier, bool prefer_unifix)
{
	unsigned priority = search_op_internal(m, name, specifier, prefer_unifix);

	if (priority) {
		//printf(", priority=%u, spec=%u\n", priority, specifier?specifier:0);
		return priority;
	}

	//printf("\n");

	for (unsigned i = 0; i < m->idx_used; i++) {
		module *tmp_m = m->used[i];

		if ((m == tmp_m) || !tmp_m->user_ops)
			continue;

		priority = search_op_internal(tmp_m, name, specifier, prefer_unifix);

		if (priority)
			return priority;
	}

	if (specifier)
		*specifier = 0;

	return 0;
}

static unsigned match_op_internal(const module *m, const char *name, unsigned *specifier, unsigned arity)
{
	const op_table *ptr;
	sliter *iter = sl_find_key(m->defops, name);

	while (sl_next_key(iter, (void**)&ptr)) {
		if (!ptr->priority)
			continue;

		if ((arity == 2) && !IS_INFIX(ptr->specifier))
			continue;

		if ((arity == 1) && IS_INFIX(ptr->specifier))
			continue;

		if (specifier) *specifier = ptr->specifier;
		unsigned n = ptr->priority;
		sl_done(iter);
		return n;
	}

	sl_done(iter);
	iter = sl_find_key(m->ops, name);

	while (sl_next_key(iter, (void**)&ptr)) {
		if (!ptr->priority)
			continue;

		if ((arity == 2) && !IS_INFIX(ptr->specifier))
			continue;

		if ((arity == 1) && IS_INFIX(ptr->specifier))
			continue;

		if (specifier) *specifier = ptr->specifier;
		unsigned n = ptr->priority;
		sl_done(iter);
		return n;
	}

	sl_done(iter);
	iter = sl_find_key(m->defops, name);

	while (sl_next_key(iter, (void**)&ptr)) {
		if (!ptr->priority)
			continue;

		if ((arity == 2) && !IS_INFIX(ptr->specifier))
			continue;

		if ((arity == 1) && IS_INFIX(ptr->specifier))
			continue;

		if (specifier) *specifier = ptr->specifier;
		unsigned n = ptr->priority;
		sl_done(iter);
		return n;
	}

	sl_done(iter);
	iter = sl_find_key(m->ops, name);

	while (sl_next_key(iter, (void**)&ptr)) {
		if (!ptr->priority)
			continue;

		if ((arity == 2) && !IS_INFIX(ptr->specifier))
			continue;

		if ((arity == 1) && IS_INFIX(ptr->specifier))
			continue;

		if (specifier) *specifier = ptr->specifier;
		unsigned n = ptr->priority;
		sl_done(iter);
		return n;
	}

	sl_done(iter);
	return 0;
}

unsigned match_op(module *m, const char *name, unsigned *specifier, unsigned arity)
{
	unsigned priority = match_op_internal(m, name, specifier, arity);

	if (priority)
		return priority;

	for (unsigned i = 0; i < m->idx_used; i++) {
		module *tmp_m = m->used[i];

		if ((m == tmp_m) || !tmp_m->user_ops)
			continue;

		priority = match_op_internal(tmp_m, name, specifier, arity);

		if (priority)
			return priority;
	}

	if (specifier)
		*specifier = 0;

	return 0;
}

static unsigned get_op_internal(const module *m, const char *name, unsigned specifier)
{
	const op_table *ptr;
	sliter *iter = sl_find_key(m->defops, name);

	while (sl_next_key(iter, (void**)&ptr)) {
		if (!ptr->priority)
			continue;

		if (specifier != ptr->specifier)
			continue;

		unsigned n = ptr->priority;
		sl_done(iter);
		return n;
	}

	sl_done(iter);
	iter = sl_find_key(m->ops, name);

	while (sl_next_key(iter, (void**)&ptr)) {
		if (!ptr->priority)
			continue;

		if (specifier != ptr->specifier)
			continue;

		unsigned n = ptr->priority;
		sl_done(iter);
		return n;
	}

	sl_done(iter);
	return 0;
}

unsigned get_op(module *m, const char *name, unsigned specifier)
{
	unsigned priority = get_op_internal(m, name, specifier);

	if (priority)
		return priority;

	for (unsigned i = 0; i < m->idx_used; i++) {
		module *tmp_m = m->used[i];

		if ((m == tmp_m) || !tmp_m->user_ops)
			continue;

		priority = get_op_internal(tmp_m, name, specifier);

		if (priority)
			return priority;
	}

	return 0;
}

static bool check_not_multifile(module *m, predicate *pr, rule *r)
{
	if (pr->head
		&& !pr->is_multifile && !pr->is_dynamic
		&& (C_STR(m, &pr->key)[0] != '$')
		) {
		if ((r->filename != pr->head->filename) || pr->is_reload) {
			if (pr->head->filename)
				fprintf(stderr, "Warning: overwriting '%s'/%u\n", C_STR(m, &pr->key), get_arity(&pr->key));

			while (pr->head) {
				rule *tmp = pr->head;
				pr->head = pr->head->next;
				clear_clause(&tmp->cl);
				TPL_free(tmp);
			}

			pr->head = pr->tail = NULL;
			pr->is_processed = false;
			pr->is_reload = false;
			pr->meta_args = NULL;
			pr->alias = NULL;
			pr->cnt = 0;
			sl_destroy(pr->idx0);
			sl_destroy(pr->idx2);
			sl_destroy(pr->idx1);
			pr->idx0 = pr->idx2 = pr->idx1 = NULL;
			pr->is_var_in_head = false;
			pr->is_var_in_first_arg = false;
			pr->is_var_in_idx2_arg = false;
			pr->idx2_arg = 0;
			TPL_free(r);
			return false;
		}
	}

	if (pr->alias && (m == m->pl->user_m)) {
		fprintf(stderr, "Warning: overwriting %s:'%s'/%u\n", pr->m->name, C_STR(m, &pr->key), get_arity(&pr->key));
		pr->meta_args = NULL;
		pr->alias = NULL;
	}

	return true;
}

static void check_goal_expansion(module *m, cell *p1)
{
	cell *h = get_head(p1);

	if (h->val_off != g_goal_expansion_s)
		return;

	cell *arg1 = h + 1;

	if (is_var(arg1)) {
		m->wild_goal_expansion = true;
		return;
	}

	create_goal_expansion(m, arg1);
}

static void check_unique(module *m, rule *r_orig)
{
	cell *head = get_head(r_orig->cl.cells);
	bool matched = false;
	r_orig->cl.is_unique = false;

	for (rule *r = r_orig->next; r; r = r->next) {
		if (r->dbgen_retracted)
			continue;

		cell *head2 = get_head(r->cl.cells);

		if (!index_cmpkey(head, head2, m, NULL)) {
			matched = true;
			break;
		}
	}

	if (!matched)
		r_orig->cl.is_unique = true;
}

static void process_cell(module *m, clause *cl, cell *c, predicate *parent, int last_was_colon, bool is_directive)
{
	cell *body = cl->cells;
	unsigned specifier;

	if ((get_arity(c) == 2)
		&& !GET_OP(c)
		&& (c->val_off != g_braces_s)
		&& search_op(m, C_STR(m, c), &specifier, false)) {
		if (IS_INFIX(specifier))
			SET_OP(c, specifier);
	}

	bool found = false, evaluable = false;
	c->bif_ptr = get_builtin_term(m, c, &found, &evaluable);

	if (found) {
		if (evaluable)
			c->flags |= FLAG_INTERNED_EVALUABLE;
		else
			c->flags |= FLAG_INTERNED_BUILTIN;

		if (c->val_off != g_call_s)
			return;
	} else {
		if (last_was_colon < 1)
			c->match = search_predicate(m, c);
	}

	if (!is_directive
		&& ((c+c->num_cells) >= (body + cl->cidx-1))
		) {
			c->flags |= FLAG_INTERNED_TAIL_CALL;

			if (parent
				&& (parent->key.val_off == c->val_off)
				&& (get_arity(&parent->key) == get_arity(c))) {
				c->flags |= FLAG_INTERNED_RECURSIVE_CALL;
			}
	}

	bool any_vars = false;

	for (unsigned i = 1; i < c->num_cells; i++) {
		if (is_var(c+i)) {
			any_vars = true;
			break;
		}
	}

	if (!any_vars && is_compound(c))
		c->flags |= FLAG_INTERNED_GROUND;
}

// Mark a goal that ends the clause. Mirrors the tail of process_cell():
// builtins are left alone, only a goal whose functor matches the
// predicate being loaded is a recursive call.

static void mark_tail_call(cell *c, predicate *parent)
{
	if (!is_interned(c) || is_builtin(c) || is_evaluable(c))
		return;

	c->flags |= FLAG_INTERNED_TAIL_CALL;

	if (parent
		&& (parent->key.val_off == c->val_off)
		&& (get_arity(&parent->key) == get_arity(c)))
		c->flags |= FLAG_INTERNED_RECURSIVE_CALL;
}

static void mark_tail_positions(cell *body, predicate *parent)
{
	if (!is_interned(body) || !get_arity(body)) {
		mark_tail_call(body, parent);
		return;
	}

	cell *arg1 = body + 1;
	cell *arg2 = arg1 + arg1->num_cells;

	if ((body->val_off == g_conjunction_s) && (get_arity(body) == 2)) {
		mark_tail_positions(arg2, parent);				// (_ , Tail)
		return;
	}

	if ((body->val_off == g_disjunction_s) && (get_arity(body) == 2)) {
		mark_tail_positions(arg1, parent);				// (Tail ; _)
		mark_tail_positions(arg2, parent);				// (_ ; Tail)
		return;
	}

	if (((body->val_off == g_if_then_s) || (body->val_off == g_soft_cut_s))
		&& (get_arity(body) == 2)) {
		mark_tail_positions(arg2, parent);				// (_ -> Tail)
		return;
	}

	if ((body->val_off == g_if_s) && (get_arity(body) == 3)) {
		cell *arg3 = arg2 + arg2->num_cells;
		mark_tail_positions(arg2, parent);				// if(_, Tail, _)
		mark_tail_positions(arg3, parent);				// if(_, _, Tail)
		return;
	}

	mark_tail_call(body, parent);
}

void process_clause(module *m, clause *cl, predicate *parent)
{
	cl->is_unique = false;
	cell *c = cl->cells;

	if (c->val_off == g_sys_record_key_s)
		return;

	bool is_directive = is_check_directive(c);
	int last_was_colon = 0;

	for (pl_idx i = 0; i < cl->cidx; i++, c++) {
		if (!is_interned(c))
			continue;

		// Don't want to match on module qualified predicates

		if (c->val_off == g_colon_s) {
			process_cell(m, cl, c, parent, 0, is_directive);
			last_was_colon = 3;
		} else {
			last_was_colon--;
			process_cell(m, cl, c, parent, last_was_colon, is_directive);
		}
	}

	if (!is_directive) {
		cell *body = get_body(cl->cells);

		if (body)
			mark_tail_positions(body, parent);
	}
}

static void process_predicate(predicate *pr)
{
	if (pr->is_processed)
		return;

	pr->is_processed = true;

	for (rule *r = pr->head; r; r = r->next) {
		process_clause(pr->m, &r->cl, pr);
	}

	if (pr->idx1)
		return;

	for (rule *r = pr->head; r; r = r->next) {
		check_unique(pr->m, r);
		cell *body = get_body(r->cl.cells);

		if (body)
			compile_clause(pr, &r->cl, body);
	}
}

void process_module(module *m)
{
	for (predicate *pr = list_front(&m->predicates);
		pr; pr = list_next(pr)) {
		process_predicate(pr);
	}
}

bool module_dump_term(module* m, cell *p1)
{
	cell *tmp = p1;

	for (unsigned i = 0; i <p1->num_cells; i++, tmp++) {
		printf("[%02u] tag=%10s, num_cells=%u, arity=%u",
			i,
			(
				(tmp->tag == TAG_VAR && is_ref(tmp))? "var_ref" :
				tmp->tag == TAG_VAR ? "var" :
				tmp->tag == TAG_INTERNED ? "interned" :
				tmp->tag == TAG_CSTR ? "cstr" :
				tmp->tag == TAG_INT ? "integer" :
				tmp->tag == TAG_FLOAT ? "float" :
				tmp->tag == TAG_RATIONAL ? "rational" :
				tmp->tag == TAG_INDIRECT ? "indirect" :
				tmp->tag == TAG_BLOB ? "blob" :
				"other"
			),
			tmp->num_cells, get_arity(tmp));

		if ((tmp->tag == TAG_INT) && !is_managed(tmp))
			printf(", %lld", (long long)tmp->val_int);

		if (tmp->tag == TAG_INTERNED)
			printf(", '%s'", C_STR(q, tmp));

		if (is_var(tmp))
			printf(", local=%d, temp=%d, anon=%d", is_local(tmp), is_temporary(tmp), is_anon(tmp));

		if (is_ref(tmp))
			printf(", slot=%u, ctx=%u", tmp->var_num, tmp->val_ctx);
		else if (is_var(tmp))
			printf(", slot=%u, %s", tmp->var_num, C_STR(q, tmp));

		printf("\n");
	}

	return true;
}

static rule *assert_begin(module *m, unsigned num_vars, cell *p1, bool consulting)
{
	bool is_dirty = false;
	cell *c = p1;

	if (!is_check_directive(c)) {
		c = get_head(p1);

		if ((c->val_off == g_neck_s) && (get_arity(c) == 1)) {
			if (consulting)
				fprintf(stderr, "Error: permission error modifying %s:(%s)/%u\n", m->name, C_STR(m, c), get_arity(c));

			return NULL;
		}

		// Remove module from head if present...

		if ((p1->val_off == g_neck_s) && (c->val_off == g_colon_s) && (get_arity(c) == 2) && is_atom(FIRST_ARG(c))) {
			const char *name = C_STR(m, FIRST_ARG(c));
			module *tmp_m = find_module(m->pl, name), *save_m = m;

			if (!tmp_m) {
				if (consulting)
					fprintf(stderr, "Error: existence error module %s:(%s)/%u\n", name, C_STR(m, c), get_arity(c));

				return NULL;
			} else
				m = tmp_m;

			cell *head = p1 + 3;
			pl_idx head_num_cells = head->num_cells;
			cell *body = head + head_num_cells;
			move_cells(p1+1, head, head_num_cells);
			cell *new_body = p1 + 1 + head_num_cells;
			make_instr(new_body, g_colon_s, bif_iso_qualify_2, 2, 1+body->num_cells);
			SET_OP(new_body, OP_XFY);
			make_atom(new_body+1, new_atom(m->pl, save_m->name));
			is_dirty = true;
			//module_dump_term(save_m, p1);
			c = get_head(p1);

			if (!is_callable(c) || is_iso_list(c)) {
				if (consulting)
					fprintf(stderr, "Error: not callable %s:(%s)/%u\n", m->name, C_STR(m, c), get_arity(c));

				return NULL;
			}

		} else if ((c->val_off == g_colon_s) && (get_arity(c) == 2) && is_atom(FIRST_ARG(c))) {
			const char *name = C_STR(m, FIRST_ARG(c));
			module *tmp_m = find_module(m->pl, name);

			if (!tmp_m) {
				if (consulting)
					fprintf(stderr, "Error: extistence error module %s:(%s)/%u\n", name, C_STR(m, c), get_arity(c));

				return NULL;
			} else
				m = tmp_m;

			if (!is_callable(p1+2) || is_iso_list(p1+2)) {
				if (consulting)
					fprintf(stderr, "Error: not callable %s:(%s)/%u\n", m->name, C_STR(m, c), get_arity(c));

				return NULL;
			}


			move_cells(p1, p1+2, p1->num_cells-2);
			c = get_head(p1);
		}
	}

	if (!c || !m)
		return NULL;

	if (is_cstring(c))
		convert_to_literal(m, c);

	predicate *pr = find_predicate(m, c);

	if (pr && !consulting && !pr->is_dynamic)
		return NULL;

	check_goal_expansion(m, c);

	if (!pr) {
		bool created = false;
		pr = create_predicate(m, c, &created);

		if (!pr && consulting)
			fprintf(stderr, "Error: permission error modifying %s:(%s)/%u\n", m->name, C_STR(m, c), get_arity(c));

		check_error(pr);

		if (created) {
			if (is_check_directive(p1))
				pr->is_check_directive = true;

			clear_property(m, C_STR(m, c), get_arity(c));

			if (!consulting) {
				push_property(m, C_STR(m, c), get_arity(c), "dynamic");
				pr->is_dynamic = true;
			} else {
				if (m->prebuilt)
					push_property(m, C_STR(m, c), get_arity(c), "built_in");

				push_property(m, C_STR(m, c), get_arity(c), "static");
			}

			if (consulting && m->make_public) {
				push_property(m, C_STR(m, c), get_arity(c), "public");
				pr->is_public = true;
			}
		}
	}

	if (is_dirty)
		pr->is_dirty = true;

	if (m->prebuilt)
		pr->is_builtin = true;

	if (num_vars > pr->max_vars)
		pr->max_vars = num_vars;

	size_t dbe_size = sizeof(rule) + (sizeof(cell) * (p1->num_cells+1));
	rule *r = TPL_calloc(1, dbe_size);

	if (!r)
		return NULL;

	copy_cells(r->cl.cells, p1, p1->num_cells);
	r->cl.cells[p1->num_cells] = (cell){0};
	r->cl.cells[p1->num_cells].tag = TAG_END;
	r->cl.num_vars = num_vars;
	r->cl.num_allocated_cells = p1->num_cells;
	r->cl.cidx = p1->num_cells+1;
	r->dbgen_created = ++m->pl->dbgen;
	pr->last_modified = r->dbgen_created;	// incremental tabling (item 3)
	r->filename = m->filename;
	r->owner = pr;
	return r;
}

// Recompute the indexed-argument variable flags from the live clause chain.
// assert_commit() only ever sets them, so retracting the last var-headed
// clause would otherwise leave the predicate permanently barred from its
// own index. Call after clauses have been delinked, never while a query
// holds the predicate.

void recheck_var_in_indexed_args(predicate *pr)
{
	pr->is_var_in_head = false;
	pr->is_var_in_first_arg = false;
	pr->is_var_in_idx2_arg = false;

	for (rule *r = pr->head; r; r = r->next) {
		if (r->dbgen_retracted || r->cl.is_deleted)
			continue;

		cell *c = get_head(r->cl.cells);
		bool ground = !is_var(c) && (!is_compound(c) || is_ground(c));

		if (!ground)
			pr->is_var_in_head = true;

		if (!get_arity(c))
			return;

		cell *arg1 = FIRST_ARG(c);

		if (is_var(arg1))
			pr->is_var_in_first_arg = true;

		if (pr->idx2 && is_var(get_nth_arg(c, pr->idx2_arg)))
			pr->is_var_in_idx2_arg = true;

		if (pr->is_var_in_head && pr->is_var_in_first_arg && pr->is_var_in_idx2_arg)
			return;
	}
}

static void assert_commit(module *m, rule *r, predicate *pr, bool append)
{
	if (pr->db_id)
		r->db_id = append ? pr->db_id : -pr->db_id;

	pr->db_id++;
	pr->cnt++;
	uuid_gen(m->pl, &r->u);

	// Note: indexing here refers to the dynamic index...

	if (pr->is_noindex)
		return;

	if (!pr->idx1) {
		unsigned INDEX_THRESHOLD = 500;

		if (pr->cnt < INDEX_THRESHOLD)
			return;

		// Indexing is an optimisation, so a failure here leaves the
		// predicate unindexed rather than killing the process.

		pr->idx1 = sl_create(index_cmpkey, NULL, m);

		if (!pr->idx1)
			return;

		pr->idx0 = sl_create(index_cmpkey, NULL, m);

		if (!pr->idx0) {
			sl_destroy(pr->idx1);
			pr->idx1 = NULL;
			return;
		}

		pr->is_var_in_head = false;
		pr->is_var_in_first_arg = false;
		pr->is_var_in_idx2_arg = false;
		pr->idx2_arg = 0;

		// Pick the first later argument with no variable-headed clauses.
		// A variable key cannot be ordered in the skiplist, so an index on
		// that argument would force every lookup back to the clause walk.
		for (unsigned n = 1; n < get_arity(&pr->key); n++) {
			bool has_var = false;

			for (rule *cl2 = pr->head; cl2; cl2 = cl2->next) {
				if (!cl2->dbgen_retracted && is_var(get_nth_arg(get_head(cl2->cl.cells), n))) {
					has_var = true;
					break;
				}
			}

			if (!has_var) {
				pr->idx2 = sl_create(index_cmpkey, NULL, m);

				// The secondary index is optional; without it lookups
				// just fall back to idx1.

				if (pr->idx2)
					pr->idx2_arg = n;

				break;
			}
		}

		for (rule *cl2 = pr->head; cl2; cl2 = cl2->next) {
			cell *c = get_head(cl2->cl.cells);

			if (cl2->dbgen_retracted)
				continue;

			bool ground = !is_var(c) && (!is_compound(c) || is_ground(c));

			if (!ground)
				pr->is_var_in_head = true;
			else
				sl_app(pr->idx0, c, cl2);

			if (get_arity(c) && is_var(FIRST_ARG(c)))
				pr->is_var_in_first_arg = true;

			cell *k1 = get_arity(c) ? FIRST_ARG(c) : c;
			sl_app(pr->idx1, k1, cl2);

			if (pr->idx2)
				sl_app(pr->idx2, get_nth_arg(c, pr->idx2_arg), cl2);
		}

		return;
	}

	cell *c = get_head(r->cl.cells);
	cell *arg1 = get_arity(c) ? FIRST_ARG(c) : NULL;
	cell *k1 = arg1 ? arg1 : c;
	bool ground = !is_var(c) && (!is_compound(c) || is_ground(c));

	if (!ground)
		pr->is_var_in_head = true;

	if (arg1 && is_var(arg1))
		pr->is_var_in_first_arg = true;

	if (pr->idx2 && is_var(get_nth_arg(c, pr->idx2_arg)))
		pr->is_var_in_idx2_arg = true;

	if (!append) {
		if (ground)
			sl_set(pr->idx0, c, r);

		sl_set(pr->idx1, k1, r);

		if (pr->idx2)
			sl_set(pr->idx2, get_nth_arg(c, pr->idx2_arg), r);
	} else {
		if (ground)
			sl_app(pr->idx0, c, r);

		sl_app(pr->idx1, k1, r);

		if (pr->idx2)
			sl_app(pr->idx2, get_nth_arg(c, pr->idx2_arg), r);
	}
}

rule *asserta_to_db(module *m, unsigned num_vars, cell *p1, bool consulting)
{
	predicate *pr;
	rule *r;

	do {
		r = assert_begin(m, num_vars, p1, consulting);

		if (!r)
			return NULL;

		pr = r->owner;
	}
	 while (!check_not_multifile(m, pr, r));

	if (pr->head)
		pr->head->prev = r;

	r->next = pr->head;

	pl_publish_barrier();		// before r is reachable
	pr->head = r;

	if (!pr->tail)
		pr->tail = r;

	assert_commit(m, r, pr, false);

	if (!consulting && !pr->idx1)
		pr->is_processed = false;

	if (pr->is_multifile && !pr->is_dynamic) {
		pr->is_processed = false;

		if (pr->is_dirty)
			process_predicate(pr);

		pr->is_dirty = false;
	}

	return r;
}

rule *assertz_to_db(module *m, unsigned num_vars, cell *p1, bool consulting)
{
	predicate *pr;
	rule *r;

	do {
		r = assert_begin(m, num_vars, p1, consulting);

		if (!r)
			return NULL;

		pr = r->owner;
	}
	 while (!check_not_multifile(m, pr, r));

	r->prev = pr->tail;

	pl_publish_barrier();		// before r is reachable

	if (pr->tail)
		pr->tail->next = r;

	pr->tail = r;

	if (!pr->head)
		pr->head = r;

	assert_commit(m, r, pr, true);

	if (!consulting && !pr->idx1)
		pr->is_processed = false;

	if (pr->is_multifile && !pr->is_dynamic) {
		pr->is_processed = false;

		if (pr->is_dirty)
			process_predicate(pr);

		pr->is_dirty = false;
	}

	return r;
}

static bool remove_from_predicate(module *m, predicate *pr, rule *r)
{
	if (r->dbgen_retracted)
		return false;

	r->dbgen_retracted = ++m->pl->dbgen;
	pr->last_modified = r->dbgen_retracted;	// incremental tabling (item 3)
	r->filename = NULL;
	pr->cnt--;
	return true;
}

void retract_from_db(module *m, rule *r)
{
	predicate *pr = r->owner;

	if (remove_from_predicate(m, pr, r))
		list_push_back(&pr->dirty, r);
}

rule *erase_from_db(module *m, uuid *ref)
{
	rule *r = find_in_db(m, ref);
	if (!r) return 0;
	retract_from_db(m, r);
	return r;
}


// Retract and run the initialization goals recorded while loading, in
// the order they were seen. Shared by the two load paths below.
//
// run_init lives on the module, and library(builtins) and
// library(iso_ext) have no module directive of their own, so they load
// into whatever module is consulting them. A file that recorded a goal
// and then used one of those would have it run at the end of *that*
// nested load - before the rest of the file defining it had been read:
//
//     :- initialization(main).
//     :- use_module(library(iso_ext)).
//     main :- write(ran), nl.
//
//         Warning: Initialization goal exception:
//             error(existence_error(procedure,main/0),main/0).
//
// So the callers ask the parser as well: a load runs the goals only if
// it recorded one itself.

static void run_initialization_goals(parser *p)
{
	p->is_consulting = false;
	p->is_command = true;
	SB(src);
	SB_sprintf(src, "sys_forall(%s:retract(('$directive'(initialization(__G_)))), (once(__G_); format('Error: ~w~n', [__G_])))", p->m->name);

	if (run(p, SB_cstr(src), false, NULL, 0))
		p->pl->status = false;

	SB_free(src);
	p->m->run_init = false;
}

module *load_text(module *m, const char *src, const char *filename)
{
	parser *p = parser_create(m);
	check_error(p);
	const char *save_filename = p->m->filename;
	p->m->filename = set_known(m, filename);
	p->is_consulting = true;
	p->srcptr = (char*)src;
	tokenize(p, false, false);

	if (!p->error && !p->already_loaded_error && !p->end_of_term && p->cl->cidx) {
		if (!p->do_read_term)
			fprintf(stderr, "Error: syntax error, incomplete statement, %s:%d\n", filename, p->line_num);

		p->error = true;
	}

	if (!p->error) {
		process_module(p->m);
		int save = p->pl->quiet;
		//p->pl->quiet = true;
		p->pl->halt = false;
		p->is_directive = true;

		if (p->m->run_init && p->saw_initialization) {
			run_initialization_goals(p);
		}

		p->is_command = p->is_directive = false;
		p->pl->quiet = save;
	}

	module *save_m = p->m;
	m->filename = save_filename;
	parser_destroy(p);
	return save_m;
}

static bool unload_realfile(module *m, const char *filename)
{
	for (predicate *pr = list_front(&m->predicates); pr; ) {
		if (pr->filename && strcmp(pr->filename, filename)) {
			pr = list_next(pr);
			continue;
		}

		purge_properties(pr);

		if (!pr->refcnt)
			abolish_predicate(pr);
		else
			pr->is_abolished = true;

		pr = list_next(pr);
	}

	set_unloaded(m, filename);
	return true;
}

// Search order for a consulted name that may or may not carry an
// extension. ".pl" leads and the bare name follows it.

static const char *g_src_suffixes[] = {
	".pl", "", ".pro", ".prolog", ".p", ".P"
};

bool unload_file(module *m, const char *filename)
{
#if !TPL_FEATURE_FILESYSTEM
	(void)m;
	(void)filename;
	return false;
#else
	size_t len = strlen(filename);
	char *tmpbuf = TPL_malloc(len + 20);
	ENSURE(tmpbuf);
	memcpy(tmpbuf, filename, len+1);

	if (tmpbuf[0] == '~') {
		const char *ptr = getenv("HOME");

		if (ptr) {
			tmpbuf = TPL_realloc(tmpbuf, strlen(ptr) + 10 + strlen(filename) + 20);
			ENSURE(tmpbuf);
			strcpy(tmpbuf, ptr);
			strcat(tmpbuf, filename+1);
		}
	}

	char *savebuf = TPL_strdup(tmpbuf);
	char *realbuf = NULL;

	// Same order as load_file, or unloading `f` would not find the
	// `f.pl` that consulting `f` actually loaded.

	for (unsigned i = 0; i < (sizeof(g_src_suffixes) / sizeof(g_src_suffixes[0])); i++) {
		strcpy(tmpbuf, savebuf);
		strcat(tmpbuf, g_src_suffixes[i]);

		if ((realbuf = tpl_realpath(tmpbuf)))
			break;
	}

	if (!realbuf) {
		TPL_free(savebuf);
		TPL_free(tmpbuf);
		return false;
	}

	TPL_free(savebuf);
	TPL_free(tmpbuf);
	filename = realbuf;
	bool ok = unload_realfile(m, filename);
	TPL_free(realbuf);
	return ok;
#endif
}

module *load_fp(module *m, FILE *fp, const char *filename, bool including, bool init)
{
	parser *p = parser_create(m);
	if (!p) return NULL;
	const char *save_filename = m->filename;
	if (!including) m->filename = set_known(m, filename);
	p->is_consulting = true;
	p->fp = fp;
	bool ok = false;

	virtual_term(p, "begin_of_file.");
	tokenize(p, false, false);

	do {
		if (tpl_getline_fp(&p->save_line, &p->n_line, p->fp) == -1) {
			virtual_term(p, "end_of_file.");
			break;
		}

		p->srcptr = p->save_line;

		if (!tokenize(p, false, false))
			break;

		ok = !p->error;
	}
	 while (ok && !p->already_loaded_error && !g_tpl_interrupt);

	if (g_tpl_interrupt)
		return NULL;

	if (!p->error && !p->already_loaded_error && !p->end_of_term && p->cl->cidx) {
		if (!p->do_read_term)
			fprintf(stderr, "Error: syntax error, incomplete statement, %s:%d\n", filename, p->line_num);

		p->error = true;
	}

	module *save_m = p->m;

	if (!p->error && !p->already_loaded_error) {
		process_module(p->m);
		int save = p->pl->quiet;
		p->is_directive = true;

		if (p->m->run_init && p->saw_initialization && init) {
			run_initialization_goals(p);
		}

		p->is_command = p->is_directive = false;
		p->pl->quiet = save;
	}

	ok = !p->error;
	parser_destroy(p);
	m->filename = save_filename;

	if (!ok)
		unload_realfile(m, filename);

	return save_m;
}

bool restore_log(module *m, const char *filename)
{
	FILE *fp = fopen(filename, "r");
	char *line = NULL;

	if (!fp)
		return false;

	FILE *save = m->pl->logfp;
	m->pl->logfp = NULL;

	for (;;) {
		size_t n = 0;

		if (tpl_getline_fp(&line, &n, fp) < 0) {
			TPL_free(line);
			break;
		}

		pl_eval(m->pl, line, false);
	}

	fclose(fp);
	m->pl->logfp = save;
	return true;
}

module *load_file(module *m, const char *filename, bool including, bool init)
{
#if !TPL_FEATURE_FILESYSTEM
	(void)m;
	(void)filename;
	(void)including;
	(void)init;
	return NULL;
#else
	const char *orig_filename = filename;

	if (!strcmp(filename, "user")) {
		m = m->pl->user_m;

		for (int i = 0; i < MAX_STREAMS; i++) {
			stream *str = &m->pl->streams[i];
			char tmpbuf[256];
			snprintf(tmpbuf, sizeof(tmpbuf), "user");
			filename = set_loaded(m, tmpbuf, orig_filename);

			if (!sl_get(str->alias, "user_input", NULL))
				continue;

			for (predicate *pr = list_front(&m->predicates); pr; pr = list_next(pr))
				pr->is_reload = true;

			// Process extra input line text...

			while (m->pl->p && m->pl->p->srcptr && *m->pl->p->srcptr) {
				m->filename = filename;
				parser *p = parser_create(m);
				if (!p) return NULL;
				p->srcptr = m->pl->p->srcptr;
				p->is_consulting = true;
				p->m = m;

				if (!tokenize(p, false, false)) {
					if (p->end_of_file) {
						m->pl->p->srcptr = p->srcptr;
						parser_destroy(p);
						return m;
					}

					if (p->error) {
						m->pl->p->srcptr = NULL;
						parser_destroy(p);
						return m;
					}
				}

				m->pl->p->srcptr = p->srcptr;
				parser_destroy(p);
			}

			module *save_m = load_fp(m, str->fp, filename, including, init);
			clearerr(str->fp);
			return save_m;
		}
	}

	size_t len = strlen(filename);
	char *tmpbuf = TPL_malloc(len + 20);
	check_error(tmpbuf);
	memcpy(tmpbuf, filename, len+1);

	if (tmpbuf[0] == '~') {
		const char *ptr = getenv("HOME");

		if (ptr) {
			tmpbuf = TPL_realloc(tmpbuf, strlen(ptr) + 10 + strlen(filename) + 20);
			check_error(tmpbuf);
			strcpy(tmpbuf, ptr);
			strcat(tmpbuf, filename+1);
		}
	}

	char *savebuf = TPL_strdup(tmpbuf);
	char *realbuf = NULL;

	// ".pl" is tried before the bare name. Quintus and SICStus both
	// specified it that way and every system since has followed, so
	// consulting `f` where both `f.pl` and an executable `f` exist must
	// pick the source file - see issue #1114.
	//
	// The empty suffix in the middle is the bare name; the rest are ours.

	for (unsigned i = 0; i < (sizeof(g_src_suffixes) / sizeof(g_src_suffixes[0])); i++) {
		strcpy(tmpbuf, savebuf);
		strcat(tmpbuf, g_src_suffixes[i]);

		if ((realbuf = tpl_realpath(tmpbuf)))
			break;
	}

	TPL_free(savebuf);
	TPL_free(tmpbuf);

	if (!realbuf)
		return NULL;

	if (including)
		set_unloaded(m, realbuf);

	else if (is_loaded(m, realbuf)) {
		TPL_free(realbuf);
		return m;
	}

	struct stat st = {0};
	stat(filename, &st);

	if ((st.st_mode & S_IFMT) == S_IFDIR) {
		char *tmpbuf = TPL_malloc(strlen(orig_filename)+20);
		ENSURE(tmpbuf);
		strcpy(tmpbuf, orig_filename);
		strcat(tmpbuf, ".pl");
		m = load_file(m, tmpbuf, including, init);
		TPL_free(tmpbuf);
		TPL_free(realbuf);
		return m;
	}

	filename = set_loaded(m, realbuf, orig_filename);
	FILE *fp = fopen(filename, "r");

	if (!fp) {
		TPL_free(realbuf);
		return NULL;
	}

	m->actual_filename = filename;

	// Check for a BOM

	int ch = getc_utf8(fp);

	if ((unsigned)ch != 0xFEFF)
		fseek(fp, 0, SEEK_SET);

	clearerr(fp);
	module *save_m = load_fp(m, fp, filename, including, init);
	fclose(fp);
	TPL_free(realbuf);
	return save_m;
#endif
}

static void module_save_fp(module *m, FILE *fp, int canonical, int dq)
{
	(void) dq;
	pl_ctx ctx = 0;
	query q = (query){0};
	q.pl = m->pl;
	q.st.m = m;

	for (predicate *pr = list_front(&m->predicates);
		pr; pr = list_next(pr)) {
		if (pr->is_builtin)
			continue;

		for (rule *r = pr->head; r; r = r->next) {
			if (r->dbgen_retracted)
				continue;

			if (canonical)
				print_canonical(&q, fp, r->cl.cells, ctx, 0);
			else
				print_term(&q, fp, r->cl.cells, ctx, 0);

			fprintf(fp, "\n");
		}
	}
}

bool save_file(module *m, const char *filename)
{
	FILE *fp = fopen(filename, "w");

	if (!fp) {
		fprintf(stderr, "Error: file '%s' cannot be created\n", filename);
		return false;
	}

	module_save_fp(m, fp, 0, 0);
	fclose(fp);
	return true;
}

void module_destroy(module *m)
{
	sliter *iter = sl_first(m->defops);
	op_table *opptr;

	while (sl_next(iter, (void**)&opptr))
		TPL_free(opptr);

	sl_done(iter);
	sl_destroy(m->defops);
	iter = sl_first(m->ops);

	while (sl_next(iter, (void**)&opptr))
		TPL_free(opptr);

	sl_done(iter);
	sl_destroy(m->ops);
	predicate *pr;

	while ((pr = list_front(&m->predicates)) != NULL)
		destroy_predicate(m, pr);

	while (m->gex_head) {
		pi *save = m->gex_head;
		m->gex_head = m->gex_head->next;
		TPL_free(save);
	}

	if (m->fp)
		fclose(m->fp);

	sl_destroy(m->index);
	sl_destroy(m->keyval);
	quad_reset(m);
	parser_destroy(m->p);
	TPL_free(m->used);
	clear_loaded(m);

	// module_create() links the module only once it is fully built, so a
	// failure part-way through leaves an unlinked node here.

	if ((m->pl->modules.front == &m->hdr) || m->hdr.prev || m->hdr.next)
		list_remove(&m->pl->modules, m);

	deinit_lock(&m->guard);
	TPL_free(m);
}

void module_duplicate(prolog *pl, module *m, const char *name, unsigned arity)
{
	module *tmp_m = module_create(pl, name);

	if (!tmp_m)
		return;

	tmp_m->orig = m;
	tmp_m->arity = arity;
}

static void keyval_free(const void *key, const void *val, const void *p)
{
	TPL_free((void*)key);
	cell *c = (cell*)val;
	unshare_cells(c, c->num_cells);
	TPL_free((void*)val);
}

// A module uses a handful of others, so this doubles from small rather than
// reserving room for every module that could ever exist. Refusing at
// MAX_MODULES is new: the flat array was appended to with no check.

bool module_add_use(module *m, module *used)
{
	if (m->idx_used >= m->used_alloc) {
		if (m->idx_used >= MAX_MODULES)
			return false;

		unsigned wanted = m->used_alloc ? m->used_alloc * 2 : 8;

		if (wanted > MAX_MODULES)
			wanted = MAX_MODULES;

		module **tmp = TPL_realloc(m->used, wanted * sizeof(module*));

		if (!tmp)
			return false;

		m->used = tmp;
		m->used_alloc = wanted;
	}

	m->used[m->idx_used++] = used;
	return true;
}

// Ids are handed out monotonically, so the map only ever grows at the end.

static bool modmap_set(prolog *pl, unsigned id, module *m)
{
	if (id >= pl->modmap_alloc) {
		if (id >= MAX_MODULES)
			return false;

		unsigned wanted = pl->modmap_alloc ? pl->modmap_alloc * 2 : 16;

		while (wanted <= id)
			wanted *= 2;

		if (wanted > MAX_MODULES)
			wanted = MAX_MODULES;

		module **tmp = TPL_realloc(pl->modmap, wanted * sizeof(module*));

		if (!tmp)
			return false;

		memset(tmp + pl->modmap_alloc, 0,
			(wanted - pl->modmap_alloc) * sizeof(module*));
		pl->modmap = tmp;
		pl->modmap_alloc = wanted;
	}

	pl->modmap[id] = m;
	return true;
}

module *module_create(prolog *pl, const char *name)
{
	module *m = TPL_calloc(1, sizeof(module));

	if (!m)
		return NULL;

	init_lock(&m->guard);
	m->pl = pl;
	m->filename = set_known(m, name);
	m->name = set_known(m, name);
	m->flags.unknown = UNK_ERROR;
	m->flags.syntax_error = UNK_ERROR;
	m->flags.double_quote_chars = true;
	m->flags.character_escapes = true;
	m->flags.occurs_check = false;
	m->error = false;
	m->id = ++pl->next_mod_id;
	m->defops = sl_create((void*)fake_strcmp, NULL, NULL);
	m->keyval = sl_create((void*)fake_strcmp, (void*)keyval_free, NULL);
	modmap_set(pl, m->id, m);

	if (strcmp(name, "system")) {
		for (const op_table *ptr = g_ops; ptr->name; ptr++) {
			op_table *tmp = TPL_malloc(sizeof(op_table));

			if (!tmp) {
				module_destroy(m);
				return NULL;
			}

			memcpy(tmp, ptr, sizeof(op_table));
			sl_app(m->defops, tmp->name, tmp);
		}
	}

	m->ops = sl_create((void*)fake_strcmp, NULL, NULL);
	m->index = sl_create(predicate_cmpkey, NULL, m);
	m->p = parser_create(m);
	check_error(m->p);
	set_multifile_in_db(m, "$predicate_property", 3);

	parser *p = parser_create(m);
	if (p) {
		p->is_consulting = true;
		process_module(p->m);
		parser_destroy(p);
	}

	if (!m->name || !m->p || m->error || !p) {
		module_destroy(m);
		m = NULL;
		return m;
	}

	set_discontiguous_in_db(m, "term_expansion", 2);
	set_discontiguous_in_db(m, "goal_expansion", 2);
	set_discontiguous_in_db(m, "$directive", 1);

	set_multifile_in_db(m, "term_expansion", 2);
	set_multifile_in_db(m, "goal_expansion", 2);
	set_multifile_in_db(m, "$directive", 1);

	set_dynamic_in_db(m, "term_expansion", 2);
	set_dynamic_in_db(m, "goal_expansion", 2);
	set_dynamic_in_db(m, "$directive", 1);

	list_push_back(&pl->modules, m);
	return m;
}
