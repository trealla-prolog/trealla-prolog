#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#if !defined(_WIN32) && !defined(__wasi__)
#include <fcntl.h>
#endif

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
	time_t mtime;						// the file's when last loaded, 0 if never loaded into this module
	bool is_loaded:1;
};

static const op_table g_ops[] =
{
	{":-", OP_XFX, 1200, false, false},
	{":-", OP_FX, 1200, false, false},
	{"-->", OP_XFX, 1200, false, false},
	{"?-", OP_FX, 1200, false, false},
	{"?-", OP_XFX, 1200, false, false},				// a quad labelled by a ground term
	{"|", OP_XFY, 1105, false, false},
	{";", OP_XFY, 1100, false, false},
	{"->", OP_XFY, 1050, false, false},
	{"*->", OP_XFY, 1050, false, false},
	{",", OP_XFY, 1000, false, false},

	{"public", OP_FX, 1150, false, false},
	{"discontiguous", OP_FX, 1150, false, false},
	{"multifile", OP_FX, 1150, false, false},
	{"dynamic", OP_FX, 1150, false, false},
	{"ensure_loaded", OP_FX, 1150, false, false},
	{"meta_predicate", OP_FX, 1150, false, false},

	//{"op", OP_FX, 1150},
	//{"initialization", OP_FX, 1150},
	//{"set_prolog_flag", OP_FX, 1150},
	//{"module", OP_FX, 1150},
	//{"use_module", OP_FX, 1150},

	{"\\+", OP_FY, 900, false, false},
	{"as", OP_XFX, 700, false, false},
	{"is", OP_XFX, 700, false, false},
	{"=", OP_XFX, 700, false, false},
	{"\\=", OP_XFX, 700, false, false},
	{"==", OP_XFX, 700, false, false},
	{"\\==", OP_XFX, 700, false, false},
	{"=@=", OP_XFX, 700, false, false},
	{"\\=@=", OP_XFX, 700, false, false},
	{"=:=", OP_XFX, 700, false, false},
	{"=\\=", OP_XFX, 700, false, false},
	{"<", OP_XFX, 700, false, false},
	{"=<", OP_XFX, 700, false, false},
	{">", OP_XFX, 700, false, false},
	{">=", OP_XFX, 700, false, false},
	{"@<", OP_XFX, 700, false, false},
	{"@=<", OP_XFX, 700, false, false},
	{"@>", OP_XFX, 700, false, false},
	{"@>=", OP_XFX, 700, false, false},
	{"=..", OP_XFX, 700, false, false},
	{":", OP_XFY, 600, false, false},
	{"+", OP_YFX, 500, false, false},
	{"-", OP_YFX, 500, false, false},
	{"?", OP_FX, 500, false, false},
	{"/\\", OP_YFX, 500, false, false},
	{"\\/", OP_YFX, 500, false, false},
	{"*", OP_YFX, 400, false, false},
	{"/", OP_YFX, 400, false, false},
	{"//", OP_YFX, 400, false, false},
	{"div", OP_YFX, 400, false, false},
	{"rdiv", OP_YFX, 400, false, false},
	{"rem", OP_YFX, 400, false, false},
	{"mod", OP_YFX, 400, false, false},
	{"<<", OP_YFX, 400, false, false},
	{">>", OP_YFX, 400, false, false},
	{"**", OP_XFX, 200, false, false},
	{"^", OP_XFY, 200, false, false},
	{"\\", OP_FY, 200, false, false},
	{"-", OP_FY, 200, false, false},
	{"+", OP_FY, 200, false, false},

	{"++", OP_FY, 100, false, false},			// used in mode declarations
	{"--", OP_FY, 100, false, false},			// used in mode declarations
	{"@", OP_FY, 100, false, false},			// used in mode declarations
	{":", OP_FY, 100, false, false},			// used in mode declarations

	{DOUBLE_BAR, OP_XFY, 1, false, false},

	//{"$", OP_FX, 1},

	{0,0,0,false,false}
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

const char *set_known(module *m, const char *filename)
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
	ptr->mtime = 0;
	ptr->is_loaded = false;
	ptr->parent = NULL;
	m->loaded_files = ptr;
	return ptr->filename;
}

bool is_known(const module *m, const char *filename)
{
	for (const loaded_file *ptr = m->loaded_files; ptr; ptr = ptr->next) {
		if (!strcmp(ptr->filename, filename))
			return true;
	}

	return false;
}

static const char *set_loaded(module *m, const char *filename, const char *orig_filename, time_t mtime)
{
	loaded_file *ptr = m->loaded_files;

	while (ptr) {
		if (!strcmp(ptr->filename, filename)) {
			ptr->mtime = mtime;				// a reload, or make/0 sees it as changed ever after
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
	ptr->mtime = mtime;
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
	loaded_file *ptr = m->loaded_files;

	while (ptr) {
		struct stat st = {0};
		loaded_file *save = ptr->next;

		// One only known, as a module's own source is, was never loaded into m. One whose load
		// failed is retried, and any change counts: a file put back to an older copy changed too.

		if (ptr->mtime && (stat(ptr->filename, &st) == 0)) {
			if (st.st_mtime != ptr->mtime) {
				char *parent_filename = TPL_strdup(get_parent(m, ptr->filename));
				printf("%% %s changed\n", ptr->filename);

				if (strcmp(parent_filename, ptr->filename))
					unload_for_reload(m, ptr->filename);

				unload_for_reload(m, parent_filename);
				reload_file(m, parent_filename);
				TPL_free(parent_filename);
			}
		}

		ptr = save;
	}
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
	predicate *pr = NULL;

	// The first entry under the key is nearly always live, and sl_get() takes no iterator lock.

	if (!sl_get(m->index, &tmp, (const void**)&pr))
		return NULL;

	if (pr && !pr->is_abolished)
		return pr;

	sliter *iter = sl_find_key(m->index, &tmp);

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

	if (m->pl->user_m && (m != m->pl->user_m)) {
		pr = find_predicate(m->pl->user_m, c);

		if (pr)
			return pr;
	}

	return NULL;
}

// The current thread's own predicate behind a thread_local/1 one: dynamic,
// named for the thread and the original, made on first use. Its clauses
// keep the original head, so they match the goal as it is.

predicate *thread_local_shadow(predicate *pr)
{
	if (!pr->is_thread_local)
		return pr;

	module *m = pr->m;
	thread *t = current_thread(m->pl);

	// Its own, seen before and not since abolished: only this thread
	// touches its list.

	for (unsigned i = 0; i < t->tl_shadows_n; i++) {
		if ((t->tl_origs[i] == pr) && !t->tl_shadows[i]->is_abolished)
			return t->tl_shadows[i];
	}

	char name[1024];
	snprintf(name, sizeof(name), "$tl:%u:%s", (unsigned)t->chan, C_STR(m, &pr->key));
	cell key = (cell){0};
	key.tag = TAG_INTERNED;
	key.val_off = new_atom(m->pl, name);

	if (key.val_off == ERR_IDX)
		return NULL;

	set_arity(&key, get_arity(&pr->key));
	prolog_lock_mod(m->pl, m);
	predicate *sh = find_predicate(m, &key);

	if (!sh && (sh = create_predicate(m, &key, NULL)) != NULL)
		sh->is_dynamic = true;

	if (sh) {
		if (t->tl_shadows_n == t->tl_shadows_cap) {
			unsigned cap = t->tl_shadows_cap ? t->tl_shadows_cap * 2 : 8;
			predicate **a = TPL_realloc(t->tl_shadows, sizeof(predicate*) * cap);
			predicate **b = a ? TPL_realloc(t->tl_origs, sizeof(predicate*) * cap) : NULL;

			if (a) t->tl_shadows = a;
			if (b) t->tl_origs = b;

			if (!a || !b) {
				prolog_unlock_mod(m->pl, m);
				return NULL;
			}

			t->tl_shadows_cap = cap;
		}

		t->tl_origs[t->tl_shadows_n] = pr;
		t->tl_shadows[t->tl_shadows_n++] = sh;
	}

	prolog_unlock_mod(m->pl, m);
	return sh;
}

static predicate *revive_buried(module *m, const cell *c);
static void free_buried(module *m);

// An abolished header for c made like new once nothing reads or waits in it, so lookups don't walk past one per abolish; last_modified and db_id carry on.

static predicate *revive_abolished(module *m, const cell *c)
{
	cell tmp = *c;
	tmp.tag = TAG_INTERNED;
	tmp.flags = 0;
	tmp.num_cells = 1;
	tmp.match = NULL;
	set_arity(&tmp, get_arity(c));
	sliter *iter = sl_find_key(m->index, &tmp);
	predicate *pr = NULL;

	while (sl_next_key(iter, (void*)&pr)) {
		if (pr && pr->is_abolished && !pr->refcnt && !pr->head
			&& !list_count(&pr->dirty) && !list_count(&pr->delinked))
			break;

		pr = NULL;
	}

	sl_done(iter);

	if (!pr)
		return NULL;

	index_free(pr);

	if (pr->meta_args) {
		unshare_cells(pr->meta_args, pr->meta_args->num_cells);
		TPL_free(pr->meta_args);
	}

	const bool is_revived = pr->is_revived;
	pr->alias = NULL;
	pr->meta_args = NULL;
	pr->cnt = 0;
	pr->max_vars = pr->idx2_arg = pr->idx3_want = 0;
	pr->sig_args[0] = pr->sig_args[1] = pr->sig_args[2] = 0;
	pr->drain_gen = 0;
	pr->drain_old = 0;
	pr->is_reload = pr->is_builtin = pr->is_public = pr->is_dynamic = pr->is_thread_local = false;
	pr->is_meta_predicate = pr->is_multifile = pr->is_discontiguous = false;
	pr->needs_index = pr->no_idx3 = pr->is_check_directive = pr->is_processed = false;
	pr->is_var_in_head = pr->is_var_in_first_arg = pr->is_open_first_arg = pr->is_var_in_idx2_arg = false;
	pr->is_iso = pr->is_dirty = pr->is_incremental = pr->sig_custom = pr->is_static_noted = pr->is_bif_override = false;
	pr->sig_chosen = false;
	pr->is_revived = is_revived;
	pr->filename = m->filename;
	pr->is_noindex = m->pl->noindex || !get_arity(&pr->key);
	list_remove(&m->predicates, pr);
	list_push_back(&m->predicates, pr);		// where a new one would go
	pl_publish_barrier();
	pr->is_abolished = false;
	m->pl->pred_gen++;
	return pr;
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

	predicate *pr = revive_abolished(m, c);

	if (pr) {
		if (created)
			*created = true;

		return pr;
	}

	pr = is_interned(c) ? revive_buried(m, c) : NULL;

	if (!pr && !(pr = TPL_calloc(1, sizeof(predicate))))
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
	m->pl->pred_gen++;
	return pr;
}

bool is_bif_overridden(module *m, cell *c)
{
	predicate *pr = find_predicate(m, c);
	return pr && pr->is_bif_override;
}

// Every clause out of pr: a dynamic one goes unless a live query touched it, a static one, whose readers nothing counts, waits for every live query.

void detach_clauses(predicate *pr)
{
	prolog *pl = pr->m->pl;
	list gone;
	list_init(&gone);
	rule *r;

	list_init(&pr->dirty);			// its clauses are still in the chain

	while ((r = list_pop_front(&pr->delinked)) != NULL)
		list_push_back(&gone, r);

	for (r = pr->head; r; r = r->next) {
		if (!r->dbgen_retracted)
			pr->last_modified = r->dbgen_retracted = ++pl->dbgen;

		list_push_back(&gone, r);
	}

	pr->head = pr->tail = NULL;
	pr->cnt = 0;
	index_free(pr);
	pr->needs_index = false;
	pr->no_idx3 = false;
	pr->idx3_want = 0;
	pr->is_var_in_head = false;
	pr->is_var_in_first_arg = false;
	pr->is_open_first_arg = false;
	pr->is_var_in_idx2_arg = false;
	pr->idx2_arg = 0;

	if (pr->meta_args) {
		unshare_cells(pr->meta_args, pr->meta_args->num_cells);
		TPL_free(pr->meta_args);
		pr->meta_args = NULL;
	}

	pr->is_meta_predicate = false;			// so a reload can declare it again

	// Only now the index has gone, as it borrows keys from the clauses' cells.

	while ((r = list_pop_front(&gone)) != NULL) {
		if (pr->is_dynamic)
			release_rule(pl, r);
		else
			retain_rule(pl, r);
	}
}

static void destroy_predicate(module *m, predicate *pr)
{
	// This header's own entry: an abolished one can share its key, and its entry would outlive it

	sl_rem(m->index, &pr->key, pr);
	m->pl->pred_gen++;

	while (pr->head) {
		rule *tmp = pr->head;
		pr->head = pr->head->next;
		clear_clause(&tmp->cl);
		TPL_free(tmp);
	}

	// Out of the chain by a drain, so not reached above.

	rule *r;

	while ((r = list_pop_front(&pr->delinked)) != NULL) {
		clear_clause(&r->cl);
		TPL_free(r);
	}

	pr->head = pr->tail = NULL;
	index_free(pr);
	pr->needs_index = false;
	pr->is_var_in_head = false;
	pr->is_var_in_first_arg = false;
	pr->is_open_first_arg = false;
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

	// Atoms are interned, so unequal offsets are already unequal names: the skiplist wants a
	// consistent order, not a lexicographic one, and comparing offsets skips the strcmp.

	return p1->val_off < p2->val_off ? -1 : 1;
}

// How '.'/2 orders against c, a term that is not a list, as the compound branch below would.

static int index_cmpdot(cell *c)
{
	if (!is_compound(c))
		return 1;

	if (get_arity(c) != 2)
		return get_arity(c) > 2 ? -1 : 1;

	return strcmp(".", C_STR(m, c));
}

static int index_cmpkey_(const void *ptr1, const void *ptr2, const void *param, void *l)
{
	cell *p1 = (cell*)ptr1;
	cell *p2 = (cell*)ptr2;

	if (is_var(p1) || is_var(p2))
		return 0;

	// A string is a list, so it orders as one and never as the atom of the same text.

	if (is_string(p1) && !is_list(p2))
		return index_cmpdot(p2);

	if (is_string(p2) && !is_list(p1))
		return -index_cmpdot(p1);

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
		} else if (is_rational(p2)) {
			return -index_cmpkey_(p2, p1, param, l);	// by value, as rational against bigint is
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
		} else if (is_integer(p2) || is_rational(p2))
			return 1;					// floats order after every exact number, as those branches say
		else
			return -1;
	} else if (is_string(p1) && is_string(p2)) {
		return _CMP_SLICES(m, p1, p2);		// slices are not NUL-terminated
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

// Orders whole clause heads by the first argument and idx2_arg together, so a goal with both
// bound lands on the few clauses that match both rather than every clause sharing the first.
// The keys are whole heads, but only these two components are compared: everything else
// is left to the candidate filter. param is the predicate, which carries idx2_arg.

int index_cmpkey2(const void *ptr1, const void *ptr2, const void *param, void *l)
{
	const predicate *pr = (const predicate*)param;
	cell *p1 = (cell*)ptr1, *p2 = (cell*)ptr2;

	if (!get_arity(p1) || !get_arity(p2))
		return index_cmpkey_(ptr1, ptr2, pr->m, l);

	int ok = index_cmpkey_(FIRST_ARG(p1), FIRST_ARG(p2), pr->m, l);

	if (ok != 0)
		return ok;

	return index_cmpkey_(get_nth_arg(p1, pr->idx2_arg), get_nth_arg(p2, pr->idx2_arg), pr->m, l);
}

// Build the composite index from the clauses the predicate has now. Called from the read path
// once enough lookups have wanted one, so a predicate nobody queries on two arguments never
// pays for it. Publishes only when complete, like build_predicate_index(); callers hold the lock.

void build_predicate_composite_index(predicate *pr)
{
	if (pr->idx3 || pr->no_idx3 || !pr->idx1 || !pr->idx2_arg)
		return;

	if (pr->is_var_in_first_arg || pr->is_open_first_arg || pr->is_var_in_idx2_arg)
		return;

	skiplist *idx3 = sl_create(index_cmpkey2, NULL, pr);

	if (!idx3)
		return;

	for (rule *r = pr->head; r; r = r->next) {
		if (r->dbgen_retracted)
			continue;

		sl_app(idx3, get_head(r->cl.cells), r);
	}

	pl_publish_barrier();				// complete before any reader can see it

	pr->idx3 = idx3;
}

// A clause is identified by the generation it was created in: dbgen is a global atomic counter
// bumped once per assert, so that stamp is unique and monotonic. It used to carry a uuid as well,
// which cost a global lock and a clock read on every assert for an id this already provides.

rule *find_in_db(module *m, uint64_t ref)
{
	if (!ref)
		return NULL;

	for (module *tmp_m = list_front(&m->pl->modules);
		tmp_m; tmp_m = list_next(tmp_m)) {
		for (predicate *pr = list_front(&tmp_m->predicates);
			pr; pr = list_next(pr)) {
			if (!pr->is_dynamic)
				continue;

			for (rule *r = pr->head ; r; r = r->next) {
				if (r->dbgen_retracted)
					continue;

				if (r->dbgen_created == ref)
					return r;
			}
		}
	}

	return NULL;
}

// --- key chains (docs/DESIGN-key-chains.md, and module.h) ---

// Keys for which "equal under index_cmpkey()" is an equivalence. A compound (f(X) is equal to f(a) and
// to f(b)) and a string (it unifies with a list) keep one overflow entry per clause instead.

bool key_chainable(const cell *c)
{
	return is_atomic(c) && !is_string(c);
}

// A key with a variable in it (f(_) equal to both f(1) and f(2)) has no place in a skiplist order.

bool key_unorderable(const cell *c)
{
	for (pl_idx i = 0; i < c->num_cells; i++) {
		if (is_var(c+i))
			return true;
	}

	return false;
}

static void keyhead_del(void *k, void *v, const void *p)
{
	if (kval_single(v))
		return;

	keyhead *kh = v;
	unshare_cell(&kh->key);
	TPL_free(kh);
}

// The key a clause head files under in idx1 (k = 0) or idx2 (k = 1).

static cell *chain_key(const predicate *pr, cell *head, unsigned k)
{
	if (k)
		return get_nth_arg(head, pr->idx2_arg);

	return get_arity(head) ? FIRST_ARG(head) : head;
}

// File a clause under key, at the head or tail of its chain or in the overflow. As on the main chain,
// a rule's own links are set before anything that reaches it is.

static bool chain_link(skiplist *idx, skiplist *ovf, cell *key, rule *r, unsigned k, bool append)
{
	// An unorderable key would leave its neighbours out of order even once retracted, so it stays out:
	// lookups that could match it walk the clauses instead.

	if (key_unorderable(key))
		return true;

	if (!key_chainable(key))
		return append ? sl_app(ovf, key, r) : sl_set(ovf, key, r);

	void *v = NULL;

	if (!sl_get(idx, key, (const void**)&v)) {
		r->kprev[k] = r->knext[k] = NULL;
		return sl_set(idx, key, kval_tag(r));
	}

	keyhead *kh = v, *made = NULL;

	if (kval_single(v)) {
		rule *r0 = kval_rule(v);

		if (!r0->cl.is_indexed) {
			r->kprev[k] = r->knext[k] = NULL;
			return sl_replace(idx, key, v, key, kval_tag(r));
		}

		// A second clause: a keyhead, swapped into the entry once this one is linked on.

		if (!(kh = made = TPL_calloc(1, sizeof(keyhead))))
			return false;

		kh->key = *key;
		kh->key.num_cells = 1;
		share_cell(&kh->key);
		kh->first = kh->last = r0;
		kh->count = 1;
	}

	kh->count++;

	if (append) {
		r->knext[k] = NULL;
		r->kprev[k] = kh->last;
		pl_publish_barrier();

		if (kh->last)
			kh->last->knext[k] = r;
		else
			kh->first = r;

		kh->last = r;
	} else {
		r->kprev[k] = NULL;
		r->knext[k] = kh->first;
		pl_publish_barrier();

		if (kh->first)
			kh->first->kprev[k] = r;
		else
			kh->last = r;

		kh->first = r;
	}

	if (made && !sl_replace(idx, key, v, &made->key, made)) {
		keyhead_del(NULL, made, NULL);
		return false;
	}

	return true;
}

// Link a clause into both indexes. It is marked first, so whatever part a failure leaves linked is
// unlinked with it later. idx1 is passed apart because a build publishes it last.

static bool index_link_rule(predicate *pr, skiplist *idx1, rule *r, bool append)
{
	cell *head = get_head(r->cl.cells);
	r->cl.is_indexed = true;

	return chain_link(idx1, pr->ovf1, chain_key(pr, head, 0), r, 0, append)
		&& (!pr->idx2 || chain_link(pr->idx2, pr->ovf2, chain_key(pr, head, 1), r, 1, append));
}

// Off its key chains, from predicate_delink(). Its own knext stays, as its next does, so a reader
// parked on it walks on from there.

void index_unlink_rule(predicate *pr, rule *r)
{
	cell *head = get_head(r->cl.cells);

	for (unsigned k = 0; k < 2; k++) {
		skiplist *idx = k ? pr->idx2 : pr->idx1;

		if (!idx)
			continue;

		cell *key = chain_key(pr, head, k);

		if (!key_chainable(key))
			continue;

		rule *prev = r->kprev[k], *next = r->knext[k];

		if (prev)
			prev->knext[k] = next;

		if (next)
			next->kprev[k] = prev;

		void *v = NULL;

		if (sl_get(idx, key, (const void**)&v) && !kval_single(v)) {
			keyhead *kh = v;

			if (kh->first == r)
				kh->first = next;

			if (kh->last == r)
				kh->last = prev;

			kh->count--;
		}
	}

	r->cl.is_indexed = false;
}

// Withdraw a reclaimed clause from the index: off its chains if it is still on them, out of the
// overflows and the composite index, and its key's entry freed once that holds nothing. Reclaiming
// happens with no reader inside the predicate, which is what makes freeing an entry safe.
//
// Overflow and single-clause entries borrow their keys from the clause's own cells, so callers must
// withdraw every doomed clause BEFORE freeing any of them: sl_rem() descends by comparing against
// other entries' keys, and freeing as you go walks the comparator into released memory.

void index_remove_clause(predicate *pr, rule *r)
{
	if (!pr || !pr->idx1)
		return;

	if (r->cl.is_indexed)
		index_unlink_rule(pr, r);

	cell *c = get_head(r->cl.cells);

	for (unsigned k = 0; k < 2; k++) {
		skiplist *idx = k ? pr->idx2 : pr->idx1;

		if (!idx)
			continue;

		cell *key = chain_key(pr, c, k);
		void *v = NULL;

		if (key_unorderable(key))
			continue;

		if (!key_chainable(key))
			sl_rem(k ? pr->ovf2 : pr->ovf1, key, r);
		else if (sl_get(idx, key, (const void**)&v)
			&& (kval_single(v) ? (kval_rule(v) == r) : !((keyhead*)v)->first))
			sl_rem(idx, key, v);	// a single clause's entry goes with it, a keyhead once empty
	}

	if (pr->idx3)
		sl_rem(pr->idx3, c, r);
}

// $predicate_property holds a clause per property per predicate, so asking it whether a
// predicate has any properties by walking it made creating the Nth predicate cost O(N) and
// consulting a file quadratic in the predicates it defines - 4.6s for 40,000 of them where
// SWI took 0.33s. Almost every one of those walks finds nothing, because the predicate is
// being created for the first time. This set answers that question directly.

static uint64_t property_key(module *m, const char *name, unsigned arity)
{
	return ((uint64_t)new_atom(m->pl, name) << 32) | arity;
}

static void note_property(module *m, const char *name, unsigned arity)
{
	if (!m->props) {
		m->props = sl_create(NULL, NULL, NULL);

		if (!m->props)
			return;
	}

	sl_set(m->props, (void*)(size_t)property_key(m, name, arity), NULL);
}

static bool any_property(module *m, const char *name, unsigned arity)
{
	const void *v;

	if (!m->props)
		return false;

	return sl_get(m->props, (void*)(size_t)property_key(m, name, arity), &v);
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
	note_property(m, name, arity);
}

static bool property_matches(module *m, const rule *r, pl_idx name_off, const char *name, unsigned arity)
{
	const cell *key = r->cl.cells + 1;		// Name/Arity, see format_property()

	if (!is_compound(key) || (get_arity(key) != 2))
		return false;

	const cell *n = key + 1;
	const cell *a = n + n->num_cells;

	if (!is_smallint(a) || ((unsigned)get_smallint(a) != arity))
		return false;

	return is_interned(n) ? (n->val_off == name_off) : !strcmp(C_STR(m, n), name);
}

void clear_property(module *m, const char *name, unsigned arity)
{
	if (!any_property(m, name, arity))
		return;

	cell tmp;
	make_atom(&tmp, new_atom(m->pl, "$predicate_property"));
	set_arity(&tmp, 3);
	predicate *pr = find_predicate(m, &tmp);
	if (!pr) return;
	const pl_idx name_off = new_atom(m->pl, name);	// a compare per clause, not a strcmp

	// One already retracted is on dirty, for the last reader out to reclaim.

	if (pr->idx1 && !pr->refcnt) {
		for (rule *r = pr->head; r; r = r->next) {
			if (!r->dbgen_retracted && property_matches(m, r, name_off, name, arity))
				index_remove_clause(pr, r);
		}
	}

	bool removed = false;

	for (rule *r = pr->head; r;) {
		if (r->dbgen_retracted || !property_matches(m, r, name_off, name, arity)) {
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
			pr->cnt--;
			removed = true;
		}
	}

	if (removed && (pr->is_var_in_head || pr->is_var_in_first_arg || pr->is_var_in_idx2_arg))
		recheck_var_in_indexed_args(pr);

	sl_del(m->props, (void*)(size_t)property_key(m, name, arity));
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
	note_property(m, name, arity);
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
		q.depth_limit = g_max_depth;
		char *dst = print_canonical_to_strbuf(&q, c, 0, 0);
		ENSURE(dst);
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

static pl_thread_local unsigned g_library_loads;		// library code being loaded on this thread

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
			g_library_loads++;
			m = load_text(cur_m, src, SB_cstr(s1));
			g_library_loads--;
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
		g_library_loads++;
		m = load_text(cur_m, src, SB_cstr(s1));
		g_library_loads--;
		SB_free(s1);
		TPL_free(src);

		if (m != cur_m)
			module_add_use(cur_m, m);

		*mptr = m;
		return !m->error;
	}

	char *filename = is_library ? TPL_strdup(dstbuf) : relative_to(loading_base(cur_m), name);
	ENSURE(filename);

	g_library_loads += is_library;
	m = load_file(cur_m, filename, false, true);
	g_library_loads -= is_library;

	if (!m) {
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

	if (pr->is_bif_override)
		pr2->is_bif_override = cur_m->bif_overrides = true;
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

// After a module's file is read again, what it now exports and the importer neither imports nor defines

void module_import_new_exports(module *into, module *from)
{
	for (predicate *pr = list_front(&from->predicates); pr; pr = list_next(pr)) {
		if (pr->is_public && !find_predicate(into, &pr->key))
			do_import_predicate(into, from, pr, &pr->key);
	}
}

// A module a module/2 directive in this parse switched to goes back to its name once its file is read

void module_load_done(parser *p)
{
	module *m = p->m;

	if (!m || !p->switched_module)
		return;

	m->filename = set_known(m, m->name);
	m->reading_file = false;
	p->switched_module = false;
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

// Only ever added to, so it can answer yes too often but never a wrong no.

static void op_name_bits(const char *name, unsigned *h1, unsigned *h2)
{
	unsigned a = 5381, b = 0;

	for (const unsigned char *s = (const unsigned char*)name; *s; s++) {
		a = (a * 33) ^ *s;
		b = (b * 31) + *s;
	}

	*h1 = a % OP_NAMES_BITS;
	*h2 = b % OP_NAMES_BITS;
}

static void note_op_name(prolog *pl, const char *name)
{
	unsigned h1, h2;
	op_name_bits(name, &h1, &h2);
	pl->op_names[h1/8] |= 1 << (h1%8);
	pl->op_names[h2/8] |= 1 << (h2%8);
	pl_publish_barrier();		// the bits before the entry they describe
}

// The tokenizer asks this of nearly every token, and nearly no token is an operator.

static bool any_op_name(const prolog *pl, const char *name)
{
	unsigned h1, h2;
	op_name_bits(name, &h1, &h2);

	if (!(pl->op_names[h1/8] & (1 << (h1%8))))
		return false;

	return pl->op_names[h2/8] & (1 << (h2%8));
}

// A module sees its own operators first, those exported to it having been
// copied in when it imported them, then, as in SWI, those of the user
// module, then the standard ones. Each module has its own copy of those,
// so its own, and user's, are the ones op/3 has changed there.

typedef enum { OPS_ALL, OPS_CHANGED, OPS_DEFAULTS } op_mode;

static bool op_visible(const op_table *ptr, op_mode mode, bool is_default)
{
	if (mode == OPS_ALL)
		return true;

	bool changed = !is_default || ptr->changed;
	return mode == OPS_CHANGED ? changed : !changed;
}

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

		ptr->changed = true;
		m->user_ops = true;

		if (!priority) {
			ptr->specifier = 0;
			ptr->priority = 0;
			sl_done(iter);
			return true;
		}

		ptr->priority = priority;
		ptr->specifier = specifier;
		sl_done(iter);
		return true;
	}

	sl_done(iter);
	op_table *tmp = TPL_malloc(sizeof(op_table));
	ENSURE(tmp);
	tmp->name = set_known(m, name);
	tmp->priority = priority;
	tmp->specifier = specifier;
	tmp->exported = tmp->changed = false;
	m->user_ops = true;
	note_op_name(m->pl, tmp->name);
	sl_app(m->ops, tmp->name, tmp);
	return true;
}

bool set_op(module *m, const char *name, unsigned specifier, unsigned priority)
{
	return set_op_internal(m, name, specifier, priority);
}

// Calls fn for each operator m sees, or only those called name if given,
// in the order a lookup tries them, a name in the same class as one
// already seen being shadowed by it.

static void visit_ops_table(skiplist *seen, skiplist *table, op_mode mode, bool is_default, const char *name, void (*fn)(void*, const op_table*), void *ctx)
{
	sliter *iter = name ? sl_find_key(table, name) : sl_first(table);
	op_table *ptr;

	while (name ? sl_next_key(iter, (void**)&ptr) : sl_next(iter, (void**)&ptr)) {
		if (!ptr->priority || !ptr->specifier || !op_visible(ptr, mode, is_default))
			continue;

		char key[1024];
		snprintf(key, sizeof(key), "%c%s", IS_INFIX(ptr->specifier) ? 'i' : IS_POSTFIX(ptr->specifier) ? 's' : 'p', ptr->name);
		const void *v;

		if (sl_get(seen, key, &v))
			continue;

		sl_set(seen, TPL_strdup(key), NULL);
		fn(ctx, ptr);
	}

	sl_done(iter);
}

static void seen_free(const void *key, const void *val, const void *p)
{
	TPL_free((void*)key);
}

void visit_ops(module *m, const char *name, void (*fn)(void*, const op_table*), void *ctx)
{
	skiplist *seen = sl_create((void*)fake_strcmp, (void*)seen_free, NULL);

	if (!seen)
		return;

	module *user_m = m->pl->user_m;

	if (!user_m || (m == user_m)) {
		visit_ops_table(seen, m->defops, OPS_ALL, true, name, fn, ctx);
		visit_ops_table(seen, m->ops, OPS_ALL, false, name, fn, ctx);
	} else {
		visit_ops_table(seen, m->defops, OPS_CHANGED, true, name, fn, ctx);
		visit_ops_table(seen, m->ops, OPS_CHANGED, false, name, fn, ctx);
		visit_ops_table(seen, user_m->defops, OPS_CHANGED, true, name, fn, ctx);
		visit_ops_table(seen, user_m->ops, OPS_CHANGED, false, name, fn, ctx);
		visit_ops_table(seen, m->defops, OPS_DEFAULTS, true, name, fn, ctx);
	}

	sl_destroy(seen);
}

// An op(P,T,Name) in a module's export list: marked, for a module that
// imports this one later, and copied to those that already have, as SWI
// does, overriding even a standard operator there.

void export_op(module *m, const char *name, unsigned specifier)
{
	unsigned priority = 0;

	for (unsigned k = 0; k < 2; k++) {
		sliter *iter = sl_find_key(k ? m->defops : m->ops, name);
		op_table *ptr;

		while (sl_next_key(iter, (void**)&ptr)) {
			if (ptr->priority && (IS_INFIX(ptr->specifier) == IS_INFIX(specifier))
				&& (IS_POSTFIX(ptr->specifier) == IS_POSTFIX(specifier))) {
				ptr->exported = true;
				priority = ptr->priority;
			}
		}

		sl_done(iter);
	}

	if (!priority)
		return;

	for (module *tmp = list_front(&m->pl->modules); tmp; tmp = list_next(tmp)) {
		for (unsigned i = 0; (i < tmp->idx_used) && (tmp != m); i++) {
			if (tmp->used[i] == m) {
				set_op_internal(tmp, name, specifier, priority);
				break;
			}
		}
	}
}

// What used exports, into m, which has just imported it.

static void import_ops(module *m, module *used)
{
	for (unsigned k = 0; k < 2; k++) {
		sliter *iter = sl_first(k ? used->defops : used->ops);
		op_table *ptr;

		while (sl_next(iter, (void**)&ptr)) {
			if (ptr->exported && ptr->priority)
				set_op_internal(m, ptr->name, ptr->specifier, ptr->priority);
		}

		sl_done(iter);
	}
}

// One class of operator, infix, prefix or postfix, in one module's tables.

enum { OP_CLASS_INFIX, OP_CLASS_PREFIX, OP_CLASS_POSTFIX };

static bool op_in_class(unsigned specifier, int cls)
{
	return cls == OP_CLASS_INFIX ? IS_INFIX(specifier) : cls == OP_CLASS_PREFIX ? IS_PREFIX(specifier) : IS_POSTFIX(specifier);
}

static unsigned search_op_internal(const module *m, const char *name, unsigned *specifier, int cls, op_mode mode)
{
	for (unsigned k = 0; k < 2; k++) {
		sliter *iter = sl_find_key(k ? m->ops : m->defops, name);
		const op_table *ptr;

		while (sl_next_key(iter, (void**)&ptr)) {
			if (!ptr->priority || !op_visible(ptr, mode, !k))
				continue;

			if (!op_in_class(ptr->specifier, cls))
				continue;

			if (specifier) *specifier = ptr->specifier;
			unsigned n = ptr->priority;
			sl_done(iter);
			return n;
		}

		sl_done(iter);
	}

	return 0;
}

unsigned search_op(module *m, const char *name, unsigned *specifier, bool prefer_unifix)
{
	if (!any_op_name(m->pl, name)) {
		if (specifier) *specifier = 0;
		return 0;
	}

	// The preferred class across every table before the others, so a prefix
	// op of the module's own, say, doesn't hide the standard infix one. Where
	// an operand is expected a prefix op comes first, and after one an infix
	// then a postfix op, so a name that is both prefix and postfix is read
	// as whichever fits.

	module *user_m = m->pl->user_m;
	bool inherit = user_m && (m != user_m);
	static const int unifix_order[] = { OP_CLASS_PREFIX, OP_CLASS_POSTFIX, OP_CLASS_INFIX };
	static const int infix_order[] = { OP_CLASS_INFIX, OP_CLASS_POSTFIX, OP_CLASS_PREFIX };

	for (unsigned pass = 0; pass < 3; pass++) {
		int cls = prefer_unifix ? unifix_order[pass] : infix_order[pass];
		unsigned priority;

		if (!inherit)
			priority = search_op_internal(m, name, specifier, cls, OPS_ALL);
		else if (!(priority = search_op_internal(m, name, specifier, cls, OPS_CHANGED))
			&& !(user_m->user_ops && (priority = search_op_internal(user_m, name, specifier, cls, OPS_CHANGED))))
			priority = search_op_internal(m, name, specifier, cls, OPS_DEFAULTS);

		if (priority)
			return priority;
	}

	if (specifier)
		*specifier = 0;

	return 0;
}

static unsigned match_op_internal(const module *m, const char *name, unsigned *specifier, unsigned arity, op_mode mode)
{
	const op_table *ptr;
	sliter *iter = sl_find_key(m->defops, name);

	while (sl_next_key(iter, (void**)&ptr)) {
		if (!ptr->priority)
			continue;

		if (!op_visible(ptr, mode, true))
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

		if (!op_visible(ptr, mode, false))
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

		if (!op_visible(ptr, mode, true))
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

		if (!op_visible(ptr, mode, false))
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
	module *user_m = m->pl->user_m;

	if (!user_m || (m == user_m))
		return match_op_internal(m, name, specifier, arity, OPS_ALL);

	unsigned priority = match_op_internal(m, name, specifier, arity, OPS_CHANGED);

	if (!priority && user_m->user_ops)
		priority = match_op_internal(user_m, name, specifier, arity, OPS_CHANGED);

	if (!priority)
		priority = match_op_internal(m, name, specifier, arity, OPS_DEFAULTS);

	if (priority)
		return priority;

	if (specifier)
		*specifier = 0;

	return 0;
}

static unsigned get_op_internal(const module *m, const char *name, unsigned specifier, op_mode mode)
{
	const op_table *ptr;
	sliter *iter = sl_find_key(m->defops, name);

	while (sl_next_key(iter, (void**)&ptr)) {
		if (!ptr->priority)
			continue;

		if (!op_visible(ptr, mode, true))
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

		if (!op_visible(ptr, mode, false))
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
	module *user_m = m->pl->user_m;

	if (!user_m || (m == user_m))
		return get_op_internal(m, name, specifier, OPS_ALL);

	unsigned priority = get_op_internal(m, name, specifier, OPS_CHANGED);

	if (!priority && user_m->user_ops)
		priority = get_op_internal(user_m, name, specifier, OPS_CHANGED);

	if (!priority)
		priority = get_op_internal(m, name, specifier, OPS_DEFAULTS);

	if (priority)
		return priority;

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

			detach_clauses(pr);			// code from the old file may still be running
			pr->is_processed = false;
			pr->is_reload = false;
			pr->alias = NULL;
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

static void process_cell(module *m, clause *cl, cell *c, int last_was_colon, bool is_directive, pl_idx *next_var)
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
		c->flags &= ~FLAG_INTERNED_BUILTIN;		// bound before a user definition overrode it

		if (last_was_colon < 1)
			c->match = search_predicate(m, c);
	}

	if (!is_directive && ((c+c->num_cells) >= (body + cl->cidx-1)))
		c->flags |= FLAG_INTERNED_TAIL_CALL;

	if (!is_compound(c))
		return;

	// next_var only moves forward, so the ground checks over a clause are linear

	pl_idx idx = c - cl->cells;

	if (*next_var <= idx) {
		for (*next_var = idx + 1; (*next_var < cl->cidx) && !is_var(cl->cells + *next_var); (*next_var)++)
			;
	}

	if (*next_var >= (idx + c->num_cells))
		c->flags |= FLAG_INTERNED_GROUND;
}

// Mark a goal that ends the clause. Mirrors the tail of process_cell():
// builtins are left alone.

static void mark_tail_call(cell *c)
{
	if (!is_interned(c) || is_builtin(c) || is_evaluable(c))
		return;

	c->flags |= FLAG_INTERNED_TAIL_CALL;
}

static void mark_tail_positions(cell *body)
{
	// Each construct's last tail is looped rather than recursed, so a long conjunction can't exhaust the C stack

	for (;;) {
		if (!is_interned(body) || !get_arity(body)) {
			mark_tail_call(body);
			return;
		}

		cell *arg1 = body + 1;
		cell *arg2 = arg1 + arg1->num_cells;

		if ((body->val_off == g_conjunction_s) && (get_arity(body) == 2)) {
			body = arg2;							// (_ , Tail)
			continue;
		}

		if ((body->val_off == g_disjunction_s) && (get_arity(body) == 2)) {
			mark_tail_positions(arg1);				// (Tail ; _)
			body = arg2;							// (_ ; Tail)
			continue;
		}

		if (((body->val_off == g_if_then_s) || (body->val_off == g_soft_cut_s))
			&& (get_arity(body) == 2)) {
			body = arg2;							// (_ -> Tail)
			continue;
		}

		if ((body->val_off == g_if_s) && (get_arity(body) == 3)) {
			cell *arg3 = arg2 + arg2->num_cells;
			mark_tail_positions(arg2);				// if(_, Tail, _)
			body = arg3;							// if(_, _, Tail)
			continue;
		}

		mark_tail_call(body);
		return;
	}
}

void process_clause(module *m, clause *cl)
{
	cl->is_unique = false;
	cell *c = cl->cells;

	if (c->val_off == g_sys_record_key_s)
		return;

	bool is_directive = is_check_directive(c);
	int last_was_colon = 0;
	pl_idx next_var = 0;	// first var after the cell last checked for groundness

	for (pl_idx i = 0; i < cl->cidx; i++, c++) {
		if (!is_interned(c))
			continue;

		// Don't want to match on module qualified predicates

		if (c->val_off == g_colon_s) {
			process_cell(m, cl, c, 0, is_directive, &next_var);
			last_was_colon = 3;
		} else {
			last_was_colon--;
			process_cell(m, cl, c, last_was_colon, is_directive, &next_var);
		}
	}

	if (!is_directive) {
		cell *body = get_body(cl->cells);

		if (body)
			mark_tail_positions(body);
	}
}

static void process_predicate(predicate *pr)
{
	if (pr->is_processed)
		return;

	pr->is_processed = true;

	for (rule *r = pr->head; r; r = r->next) {
		process_clause(pr->m, &r->cl);
	}

	// A deferred index counts as indexed: check_unique() below is quadratic, and the
	// predicates that want an index are exactly the ones big enough for that to hurt.

	if (pr->idx1 || pr->needs_index)
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

// Whether a retracted copy of this clause could be freed without waiting for the query to end.
// Two things can reach a clause that nothing is iterating: a variable bound into its cells, and
// code running out of them. A head argument that is a compound can be bound to, by retract/1 or
// by matching, and the binding outlives the call - so every argument has to be atomic, and then
// nothing can point in. A body means both a cell range to run and a compiled block (cl->alt)
// whose length is not recorded, so only a fact qualifies; what remains to check is whether any
// live instruction pointer is inside it, which purge_reclaimed() does.

static bool clause_is_purgeable(cell *cells)
{
	if (get_logical_body(cells))
		return false;

	const cell *head = cells;
	const unsigned n = head->num_cells;

	for (unsigned i = 1; i <= n; i++) {
		const cell *c = head + i;

		if (is_compound(c) || is_managed(c) || is_var(c))
			return false;
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

			// As in SWI, a clause for a module that doesn't exist yet creates it.

			if (!tmp_m && !(tmp_m = module_create(m->pl, name)))
				return NULL;

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

			if (!tmp_m && !(tmp_m = module_create(m->pl, name)))
				return NULL;

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

	if (pr && pr->is_thread_local && !(pr = thread_local_shadow(pr)))
		return NULL;

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
				pr->is_static_noted = true;
			}

			if (consulting && m->make_public) {
				push_property(m, C_STR(m, c), get_arity(c), "public");
				pr->is_public = true;
			}
		}
	}

	if (is_dirty)
		pr->is_dirty = true;

	// Consulted clauses for a non-ISO builtin take it over, as in SWI and Scryer, but not in library code

	if (consulting && !pr->head && !pr->is_bif_override
		&& !m->prebuilt && m->pl->user_m && !m->pl->user_m->prebuilt && !g_library_loads) {
		bool found, evaluable;
		get_builtin_by_atom(m->pl, pr->key.val_off, get_arity(&pr->key), &found, &evaluable);

		if (found && !evaluable) {
			fprintf(stderr, "Warning: overwriting '%s'/%u\n", C_STR(m, &pr->key), get_arity(&pr->key));
			pr->is_bif_override = m->bif_overrides = true;
		}
	}

	// Declared before its first clause, or kept through a reconsult, it was never noted static.

	if (consulting && !pr->is_dynamic && !pr->is_static_noted && (C_STR(m, &pr->key)[0] != '$')) {
		pr->is_static_noted = true;			// first: push_property() consults a clause too
		char name[1024];
		snprintf(name, sizeof(name), "%s", C_STR(m, &pr->key));
		push_property(m, name, get_arity(&pr->key), "static");
	}

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
	head_signatures(pr, get_head(r->cl.cells), r->cl.arg_sig);
	r->cl.is_purgeable = clause_is_purgeable(r->cl.cells);
	r->cl.num_allocated_cells = p1->num_cells;
	r->cl.cidx = p1->num_cells+1;
	r->dbgen_created = ++m->pl->dbgen;
	pr->last_modified = r->dbgen_created;	// incremental tabling (item 3)
	r->filename = m->filename;
	r->owner = pr;
	return r;
}

// A cheap summary of a head's first argument, for throwing out a clause without unifying it.
// Zero means "could be anything" - an unbound argument, or a kind not summarised here - and
// always matches, so a clause whose summary was never computed is simply always tried.
//
// Equal arguments must always give equal summaries, or a clause that could match would be
// skipped. The reverse does not matter: two different arguments sharing a summary only costs
// a unification that was going to fail. Strings are left at zero because an interned atom and
// a cstring holding the same text unify.

uint64_t cell_signature(const cell *a)
{
	if (is_interned(a))
		return ((uint64_t)get_arity(a) << 48) | ((uint64_t)a->val_off << 2) | 1;

	if (is_smallint(a))
		return ((uint64_t)a->val_int << 2) | 2;

	return 0;
}

void head_signatures(const predicate *pr, const cell *head, uint64_t *sig)
{
	const uint32_t arity = get_arity(head);

	if (pr->sig_custom) {
		for (unsigned i = 0; i < 3; i++)
			sig[i] = pr->sig_args[i] < arity ? cell_signature(get_nth_arg((cell*)head, pr->sig_args[i])) : 0;

		return;
	}

	const cell *a = head + 1;

	for (unsigned i = 0; i < 3; i++) {
		if (i >= arity) {
			sig[i] = 0;
			continue;
		}

		sig[i] = cell_signature(a);
		a += a->num_cells;
	}
}

#define SIG_SCAN_ARGS 16				// arguments considered
#define SIG_SAMPLE 256					// clauses looked at
#define SIG_DISTINCT 32					// distinct summaries counted per argument

// At a predicate's first call, summarise the three head arguments that best tell its clauses apart; fixed from then on.
// Dynamic predicates, and any first called once threads exist, keep the first three: rewriting summaries would race.

void choose_sig_args(predicate *pr, bool mt)
{
	pr->sig_chosen = true;
	const unsigned arity = get_arity(&pr->key);

	if ((arity <= 3) || pr->is_dynamic || mt)
		return;

	const unsigned num_args = arity < SIG_SCAN_ARGS ? arity : SIG_SCAN_ARGS;
	unsigned score[SIG_SCAN_ARGS] = {0};

	for (unsigned n = 0; n < num_args; n++) {
		uint64_t seen[SIG_DISTINCT];
		unsigned num_seen = 0, sampled = 0;

		for (rule *r = pr->head; r && (sampled < SIG_SAMPLE) && (num_seen < SIG_DISTINCT); r = r->next, sampled++) {
			const uint64_t s = cell_signature(get_nth_arg(get_head(r->cl.cells), n));

			if (!s)
				continue;

			unsigned j = 0;

			while ((j < num_seen) && (seen[j] != s))
				j++;

			if (j == num_seen)
				seen[num_seen++] = s;
		}

		score[n] = num_seen;
	}

	// The three highest scores, the lower argument winning a tie, then back into argument order.

	bool taken[SIG_SCAN_ARGS] = {0};
	unsigned best[3];

	for (unsigned i = 0; i < 3; i++) {
		unsigned b = num_args;

		for (unsigned n = 0; n < num_args; n++) {
			if (!taken[n] && ((b == num_args) || (score[n] > score[b])))
				b = n;
		}

		taken[b] = true;
		best[i] = b;
	}

	unsigned k = 0;

	for (unsigned n = 0; n < num_args; n++) {
		if (taken[n])
			best[k++] = n;
	}

	if ((best[0] == 0) && (best[1] == 1) && (best[2] == 2))
		return;

	for (unsigned i = 0; i < 3; i++)
		pr->sig_args[i] = best[i];

	pr->sig_custom = true;

	for (rule *r = pr->head; r; r = r->next)
		head_signatures(pr, get_head(r->cl.cells), r->cl.arg_sig);
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
	pr->is_open_first_arg = false;
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
		else if (key_unorderable(arg1))
			pr->is_open_first_arg = true;

		if (pr->idx2 && key_unorderable(get_nth_arg(c, pr->idx2_arg)))
			pr->is_var_in_idx2_arg = true;

		if (pr->is_var_in_head && pr->is_var_in_first_arg && pr->is_open_first_arg && pr->is_var_in_idx2_arg)
			return;
	}
}

// Build the index a predicate has been flagged as wanting, from the clauses it has now. Called
// from the first lookup that would use it, so a load that asserts and never queries pays nothing,
// and one pass over the chain replaces clause-by-clause maintenance.
//
// Readers that are already walking the chain must see either no index or a complete one, so
// nothing is published until it is fully built. Callers hold the module lock.

int g_no_jit_index = 0;

// Drop a predicate's indexes. Rules keep their links, since a reader parked on one may still walk
// on, but are no longer on any index's chains; a later build links them afresh.

void index_free(predicate *pr)
{
	for (rule *r = pr->head; r; r = r->next)
		r->cl.is_indexed = false;

	sl_destroy(pr->idx2);
	sl_destroy(pr->idx1);
	sl_destroy(pr->ovf1);
	sl_destroy(pr->ovf2);
	sl_destroy(pr->idx3);
	sl_destroy(pr->idx3_old);
	pr->idx1 = pr->idx2 = pr->idx3 = pr->idx3_old = pr->ovf1 = pr->ovf2 = NULL;
}

void build_predicate_index(predicate *pr)
{
	module *m = pr->m;

	if (pr->idx1 || pr->is_noindex) {
		pr->needs_index = false;
		return;
	}

	// Indexing is an optimisation, so a failure here leaves the
	// predicate unindexed rather than killing the process.

	skiplist *idx1 = sl_create(index_cmpkey, keyhead_del, m), *ovf1 = sl_create(index_cmpkey, NULL, m);

	if (!idx1 || !ovf1) {
		sl_destroy(idx1);
		sl_destroy(ovf1);
		return;
	}

	skiplist *idx2 = NULL, *ovf2 = NULL;
	unsigned idx2_arg = 0;

	// Pick the first later argument with no variable-headed clauses.
	// A variable key cannot be ordered in the skiplist, so an index on
	// that argument would force every lookup back to the clause walk.
	for (unsigned n = 1; n < get_arity(&pr->key); n++) {
		bool has_var = false;

		for (rule *cl2 = pr->head; cl2; cl2 = cl2->next) {
			if (!cl2->dbgen_retracted && key_unorderable(get_nth_arg(get_head(cl2->cl.cells), n))) {
				has_var = true;
				break;
			}
		}

		if (!has_var) {
			idx2 = sl_create(index_cmpkey, keyhead_del, m);
			ovf2 = sl_create(index_cmpkey, NULL, m);

			// The secondary index is optional; without it lookups
			// just fall back to idx1.

			if (!idx2 || !ovf2) {
				sl_destroy(idx2);
				sl_destroy(ovf2);
				idx2 = ovf2 = NULL;
			} else
				idx2_arg = n;

			break;
		}
	}

	// Everything but idx1 can go in place now: readers test idx1, published last.

	pr->idx2_arg = idx2_arg;
	pr->idx2 = idx2;
	pr->ovf1 = ovf1;
	pr->ovf2 = ovf2;

	bool var_in_head = false, var_in_first_arg = false, open_first_arg = false;

	for (rule *cl2 = pr->head; cl2; cl2 = cl2->next) {
		if (cl2->dbgen_retracted)
			continue;

		cell *c = get_head(cl2->cl.cells);
		bool ground = !is_var(c) && (!is_compound(c) || is_ground(c));

		if (!ground)
			var_in_head = true;

		if (get_arity(c) && is_var(FIRST_ARG(c)))
			var_in_first_arg = true;
		else if (get_arity(c) && key_unorderable(FIRST_ARG(c)))
			open_first_arg = true;

		// An index missing a clause would lose answers, so a failure leaves none at all.

		if (!index_link_rule(pr, idx1, cl2, true)) {
			sl_destroy(idx1);
			index_free(pr);
			pr->idx2_arg = 0;
			return;
		}
	}

	pr->is_var_in_head = var_in_head;
	pr->is_var_in_first_arg = var_in_first_arg;
	pr->is_open_first_arg = open_first_arg;
	pr->is_var_in_idx2_arg = false;		// idx2_arg was picked to have none

	pl_publish_barrier();				// idx1 is the gate readers test, so it goes last

	pr->idx1 = idx1;
	pr->needs_index = false;
}

static void assert_commit(module *m, rule *r, predicate *pr, bool append)
{
	if (pr->db_id)
		r->db_id = append ? pr->db_id : -pr->db_id;

	pr->db_id++;
	pr->cnt++;

	// Note: indexing here refers to the dynamic index...

	if (pr->is_noindex)
		return;

	if (!pr->idx1) {
		unsigned INDEX_THRESHOLD = 500;

		if (pr->cnt < INDEX_THRESHOLD)
			return;

		// Past the threshold this predicate wants an index, but building it here charges
		// every assert for one nobody may ever look up. find_key() builds it on demand,
		// unless --nojitindex asks for the old eager behaviour.

		pr->needs_index = true;

		if (g_no_jit_index)
			build_predicate_index(pr);

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
	else if (arg1 && key_unorderable(arg1))
		pr->is_open_first_arg = true;

	if (pr->idx2 && key_unorderable(get_nth_arg(c, pr->idx2_arg)))
		pr->is_var_in_idx2_arg = true;

	// A variable in either key argument cannot be ordered, so the composite index goes and
	// does not come back: the shape it answers no longer holds for this predicate.

	if (pr->idx3 && (key_unorderable(k1) || (pr->idx2_arg && key_unorderable(get_nth_arg(c, pr->idx2_arg))))) {
		if (pr->refcnt)
			pr->idx3_old = pr->idx3;	// a reader inside may be iterating it
		else
			sl_destroy(pr->idx3);

		pr->idx3 = NULL;
		pr->no_idx3 = true;
	}

	// A failed link (out of memory) leaves the clause out of indexed lookups, as a failed sl_set() did.

	index_link_rule(pr, pr->idx1, r, append);

	if (pr->idx3) {
		if (append)
			sl_app(pr->idx3, c, r);
		else
			sl_set(pr->idx3, c, r);
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

	if (!consulting && !pr->idx1 && !pr->needs_index)
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

	if (!consulting && !pr->idx1 && !pr->needs_index)
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
	const char *save_base = g_load_base;
	g_load_base = p->m->filename = set_known(m, filename);
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
	module_load_done(p);
	m->filename = save_filename;
	g_load_base = save_base;
	parser_destroy(p);
	return save_m;
}

// The recorded properties of every predicate in keys, in one walk of the table: one walk each made
// reconsulting a file quadratic in the predicates it defines. Freed only once all are out of the index.

static void purge_properties(module *m, skiplist *keys)
{
	cell tmp;
	make_atom(&tmp, new_atom(m->pl, "$predicate_property"));
	set_arity(&tmp, 3);
	predicate *pr = find_predicate(m, &tmp);

	if (!pr)
		return;

	list doomed;
	list_init(&doomed);

	for (rule *r = pr->head; r; r = r->next) {
		const cell *key = r->cl.cells + 1;		// Name/Arity, see format_property()

		if (r->dbgen_retracted || !is_compound(key) || (get_arity(key) != 2))
			continue;

		const cell *n = key + 1;
		const cell *a = n + n->num_cells;

		if (!is_smallint(a))
			continue;

		const unsigned arity = get_smallint(a);
		uint64_t k = is_interned(n) ? (((uint64_t)n->val_off << 32) | arity) : property_key(m, C_STR(m, n), arity);

		if (!sl_get(keys, (void*)(size_t)k, NULL))
			continue;

		if (pr->refcnt)
			retract_from_db(m, r);			// on dirty, for the last reader out to reclaim
		else {
			if (pr->idx1)
				index_remove_clause(pr, r);

			list_push_back(&doomed, r);
		}
	}

	rule *r;
	bool removed = false;

	while ((r = list_pop_front(&doomed)) != NULL) {
		predicate_delink(pr, r);
		clear_clause(&r->cl);
		TPL_free(r);
		pr->cnt--;
		removed = true;
	}

	if (removed && (pr->is_var_in_head || pr->is_var_in_first_arg || pr->is_var_in_idx2_arg))
		recheck_var_in_indexed_args(pr);
}

// Those its declarations still imply come back, as declarations outlive a reconsult (as in Scryer);
// static is noted again with its next clause.

static void rerecord_properties(predicate *pr)
{
	module *m = pr->m;
	char name[1024];
	snprintf(name, sizeof(name), "%s", C_STR(m, &pr->key));	// push_property() can move the atom table
	const unsigned arity = get_arity(&pr->key);

	if (pr->is_dynamic) push_property(m, name, arity, "dynamic");
	if (pr->is_thread_local) push_property(m, name, arity, "thread_local");
	if (pr->is_discontiguous) push_property(m, name, arity, "discontiguous");
	if (pr->is_multifile) push_property(m, name, arity, "multifile");
	if (pr->is_public) push_property(m, name, arity, "public");
}

struct kept_clause_ {
	kept_clause *next;
	const char *filename;
	unsigned num_vars;
	cell cells[];
};

// A copy of each live clause of pr that filename did not put there, asserted or from another file

static void keep_clauses(module *m, predicate *pr, const char *filename)
{
	for (rule *r = pr->head; r; r = r->next) {
		if (r->dbgen_retracted || (r->filename && !strcmp(r->filename, filename)))
			continue;

		pl_idx num_cells = r->cl.cells->num_cells;
		kept_clause *k = TPL_malloc(sizeof(kept_clause) + (sizeof(cell) * num_cells));

		if (!k)
			continue;

		dup_cells(k->cells, r->cl.cells, num_cells);
		k->filename = r->filename;
		k->num_vars = r->cl.num_vars;
		k->next = NULL;

		if (m->kept_tail)
			m->kept_tail->next = k;
		else
			m->kept_head = k;

		m->kept_tail = k;
	}
}

static void drop_kept(module *m)
{
	kept_clause *k;

	while ((k = m->kept_head) != NULL) {
		m->kept_head = k->next;
		unshare_cells(k->cells, k->cells->num_cells);
		TPL_free(k);
	}

	m->kept_tail = NULL;
}

// After the file's own, as SWI's reload leaves them

static void restore_kept(module *m)
{
	kept_clause *k;

	while ((k = m->kept_head) != NULL) {
		m->kept_head = k->next;
		rule *r = assertz_to_db(m, k->num_vars, k->cells, false);

		if (r)
			r->filename = k->filename;
		else
			unshare_cells(k->cells, k->cells->num_cells);

		TPL_free(k);
	}

	m->kept_tail = NULL;
}

static bool unload_realfile(module *m, const char *filename, bool keep)
{
	skiplist *keys = sl_create(NULL, NULL, NULL);
	predicate **kept = NULL;
	unsigned num_kept = 0, max_kept = 0;

	for (predicate *pr = list_front(&m->predicates); pr; pr = list_next(pr)) {
		if (pr->filename && strcmp(pr->filename, filename))
			continue;

		if (keep && pr->is_dynamic && !pr->is_thread_local && !pr->is_abolished)
			keep_clauses(m, pr, filename);

		const uint64_t k = property_key(m, C_STR(m, &pr->key), get_arity(&pr->key));

		if (keys)
			sl_set(keys, (void*)(size_t)k, NULL);

		if (m->props)
			sl_del(m->props, (void*)(size_t)k);

		pr->is_static_noted = false;

		// A query still inside it keeps its clauses until the last such query leaves.

		if (pr->refcnt) {
			for (rule *r = pr->head; r; r = r->next)
				retract_from_db(m, r);

			pr->is_abolished = true;
			m->pl->pred_gen++;
			continue;
		}

		detach_clauses(pr);

		if (num_kept == max_kept) {
			const unsigned want = max_kept ? max_kept * 2 : 64;
			predicate **tmp = TPL_realloc(kept, want * sizeof(*kept));

			if (!tmp)
				continue;

			kept = tmp;
			max_kept = want;
		}

		kept[num_kept++] = pr;
	}

	if (keys) {
		purge_properties(m, keys);
		sl_destroy(keys);
	}

	for (unsigned i = 0; i < num_kept; i++)
		rerecord_properties(kept[i]);

	TPL_free(kept);
	set_unloaded(m, filename);
	return true;
}

// Search order for a consulted name that may or may not carry an
// extension. ".pl" leads and the bare name follows it.

static const char *g_src_suffixes[] = {
	".pl", "", ".pro", ".prolog", ".p", ".P"
};

pl_thread_local const char *g_load_base;

#if TPL_FEATURE_FILESYSTEM
// The real path of the source file a consulted name means, ~ expanded and the suffixes tried in order, or NULL

static char *find_source(const char *filename)
{
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
	ENSURE(savebuf);
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
	return realbuf;
}
#endif

bool source_exists(const char *filename)
{
#if !TPL_FEATURE_FILESYSTEM
	(void)filename;
	return false;
#else
	char *realbuf = find_source(filename);
	TPL_free(realbuf);
	return realbuf != NULL;
#endif
}

// Taken out by unload_files/1: make/0 is not to bring it back, as SWI's does not, until it is loaded again.

void forget_file(module *m, const char *filename)
{
#if !TPL_FEATURE_FILESYSTEM
	(void)m;
	(void)filename;
#else
	char *realbuf = find_source(filename);

	if (!realbuf)
		return;

	for (loaded_file *ptr = m->loaded_files; ptr; ptr = ptr->next) {
		if (!strcmp(ptr->filename, realbuf))
			ptr->mtime = 0;
	}

	TPL_free(realbuf);
#endif
}

static bool unload_source(module *m, const char *filename, bool keep)
{
#if !TPL_FEATURE_FILESYSTEM
	(void)m;
	(void)filename;
	(void)keep;
	return false;
#else
	// Found as load_file finds it, or unloading `f` would miss the `f.pl` that consulting `f` loaded

	char *realbuf = find_source(filename);

	if (!realbuf)
		return false;

	filename = realbuf;
	bool ok = unload_realfile(m, filename, keep);
	TPL_free(realbuf);
	return ok;
#endif
}

bool unload_file(module *m, const char *filename)
{
	return unload_source(m, filename, false);
}

// Before reload_file(), which puts back what was asserted into the file's dynamic predicates since

bool unload_for_reload(module *m, const char *filename)
{
	return unload_source(m, filename, true);
}

static module *load_source_fp(module *m, FILE *fp, const char *filename, bool including, bool init, bool reload)
{
	parser *p = parser_create(m);
	if (!p) return NULL;
	p->is_reconsult = reload;
	const char *save_filename = m->filename;
	const char *save_base = g_load_base;
	if (!including) m->filename = set_known(m, filename);
	g_load_base = filename;
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

	if (g_tpl_interrupt) {
		module_load_done(p);
		m->filename = save_filename;
		g_load_base = save_base;
		return NULL;
	}

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
	module_load_done(p);
	parser_destroy(p);
	m->filename = save_filename;
	g_load_base = save_base;

	if (!ok)
		unload_realfile(m, filename, false);

	return save_m;
}

module *load_fp(module *m, FILE *fp, const char *filename, bool including, bool init)
{
	return load_source_fp(m, fp, filename, including, init, false);
}

static module *load_source(module *m, const char *filename, bool including, bool init, bool reload)
{
#if !TPL_FEATURE_FILESYSTEM
	(void)m;
	(void)filename;
	(void)including;
	(void)init;
	(void)reload;
	return NULL;
#else
	const char *orig_filename = filename;

	if (!strcmp(filename, "user")) {
		m = m->pl->user_m;

		for (int i = 0; i < MAX_STREAMS; i++) {
			stream *str = &m->pl->streams[i];
			char tmpbuf[256];
			snprintf(tmpbuf, sizeof(tmpbuf), "user");
			filename = set_loaded(m, tmpbuf, orig_filename, 0);

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

			module *save_m = load_source_fp(m, str->fp, filename, including, init, reload);
			clearerr(str->fp);
			return save_m;
		}
	}

	char *realbuf = find_source(filename);

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
		m = load_source(m, tmpbuf, including, init, reload);
		TPL_free(tmpbuf);
		TPL_free(realbuf);
		return m;
	}

	struct stat rst = {0};
	stat(realbuf, &rst);
	filename = set_loaded(m, realbuf, orig_filename, rst.st_mtime);
	FILE *fp = fopen(filename, "r");

	if (!fp) {
		TPL_free(realbuf);
		return NULL;
	}

#if !defined(_WIN32) && !defined(__wasi__)
	// This stays open for directives/initialization goals run while
	// loading, so without this a process_create/3 child spawned from
	// one of them would inherit it too, for no reason (#1153).
	fcntl(fileno(fp), F_SETFD, FD_CLOEXEC);
#endif

	m->actual_filename = filename;

	// Check for a BOM

	int ch = getc_utf8(fp);

	if ((unsigned)ch != 0xFEFF)
		fseek(fp, 0, SEEK_SET);

	clearerr(fp);
	module *save_m = load_source_fp(m, fp, filename, including, init, reload);
	fclose(fp);
	TPL_free(realbuf);
	return save_m;
#endif
}

module *load_file(module *m, const char *filename, bool including, bool init)
{
	return load_source(m, filename, including, init, false);
}

// As consult/1 and make/0 do, re-reading a module's own file into it

module *reload_file(module *m, const char *filename)
{
	module *save_m = load_source(m, filename, false, true, true);
	restore_kept(m);

	if (save_m && (save_m != m))
		restore_kept(save_m);			// a module file's, unloaded by its module/2 directive

	return save_m;
}

static void module_save_fp(module *m, FILE *fp, int canonical, int dq)
{
	(void) dq;
	pl_ctx ctx = 0;
	query q = (query){0};
	q.pl = m->pl;
	q.st.m = m;
	q.depth_limit = g_max_depth;

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
	sl_destroy(m->props);
	sl_destroy(m->defops);
	iter = sl_first(m->ops);

	while (sl_next(iter, (void**)&opptr))
		TPL_free(opptr);

	sl_done(iter);
	sl_destroy(m->ops);
	predicate *pr;

	while ((pr = list_front(&m->predicates)) != NULL)
		destroy_predicate(m, pr);

	free_buried(m);

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
	drop_kept(m);

	// module_create() links the module only once it is fully built, so a
	// failure part-way through leaves an unlinked node here.

	prolog_lock(m->pl);					// as module_create() links and maps, or a grow could copy the slot back

	if ((m->pl->modules.front == &m->hdr) || m->hdr.prev || m->hdr.next)
		list_remove(&m->pl->modules, m);

	modmap *mm = m->pl->modmap;

	if (mm && (m->id < mm->alloc) && (mm->slots[m->id] == m))
		mm->slots[m->id] = NULL;

	prolog_unlock(m->pl);
	deinit_lock(&m->guard);
	TPL_free(m);
}

static void keyval_free(const void *key, const void *val, const void *p);

// For destroy_module/1: its own operators and blackboard go, as code with
// a stale pointer to the module may still look. Operators are looked up
// without a lock, so they are zeroed, not freed (module_destroy() frees
// them at exit), and the blackboard is swapped under the lock it is
// always used under.

void module_clear_tables(module *m)
{
	sliter *iter = sl_first(m->ops);
	op_table *opptr;

	while (sl_next(iter, (void**)&opptr)) {
		opptr->priority = 0;
		opptr->exported = false;
	}

	sl_done(iter);
	skiplist *keyval = sl_create((void*)fake_strcmp, (void*)keyval_free, NULL);
	prolog_lock(m->pl);
	sl_destroy(m->keyval);
	m->keyval = keyval;
	prolog_unlock(m->pl);
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

	if (used != m)
		import_ops(m, used);

	return true;
}

// Ids are handed out monotonically, a reused module keeping its own, so
// the map only ever grows at the end, as far as the modules made need.
// Called under the prolog lock; see module_by_id() for why it never
// reallocs in place.

static bool modmap_set(prolog *pl, unsigned id, module *m)
{
	modmap *mm = pl->modmap;

	if (!mm || (id >= mm->alloc)) {
		unsigned wanted = mm ? mm->alloc * 2 : 16;

		while (wanted <= id)
			wanted *= 2;

		modmap *tmp = TPL_calloc(1, sizeof(modmap) + wanted * sizeof(module*));

		if (!tmp)
			return false;

		if (mm)
			memcpy(tmp->slots, mm->slots, mm->alloc * sizeof(module*));

		tmp->prev = mm;
		tmp->alloc = wanted;
		tmp->slots[id] = m;
		pl_publish_barrier();
		pl->modmap = tmp;
		return true;
	}

	mm->slots[id] = m;
	return true;
}

// A module destroy_module/1 emptied, taken again rather than a new one
// made, so a program that makes and destroys modules, one a request say,
// holds as many as it has at once, not as many as it has ever made. It
// keeps its id and its buried predicate headers (see
// module_bury_predicates()); the rest is freed here, which assumes nothing
// still runs in the old module.

static module *module_reuse(prolog *pl)
{
	prolog_lock(pl);
	module *m = pl->dead_modules;

	if (m)
		pl->dead_modules = m->dead_next;

	prolog_unlock(pl);

	if (!m)
		return NULL;

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
	sl_destroy(m->props);

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
	clear_loaded(m);
	drop_kept(m);

	m->dead_next = NULL;
	m->orig = NULL;
	m->gex_head = m->gex_tail = NULL;
	m->p = NULL;
	m->fp = NULL;
	m->filename = m->name = m->actual_filename = NULL;
	m->index = m->ops = m->defops = m->keyval = m->props = NULL;
	m->loaded_files = NULL;
	memset(&m->flags, 0, sizeof(m->flags));
	m->idx_used = m->arity = 0;
	m->if_depth = 0;
	memset(m->ifs_blocked, 0, sizeof(m->ifs_blocked));
	memset(m->ifs_done, 0, sizeof(m->ifs_done));
	m->quad_query = m->quad_name = NULL;
	m->quad_num_vars = m->quad_line_num = 0;
	m->in_quad = m->quad_recorded = m->user_ops = m->prebuilt = m->from_file = false;
	m->make_public = m->loaded_properties = m->loading = m->error = false;
	m->ignore_vars = m->wild_goal_expansion = m->run_init = m->reading_file = false;
	return m;
}

// For destroy_module/1: m's predicate headers, abolished, out of its index
// and list onto m->buried. A pointer still cached to one finds it
// abolished, and so searches again (see match_head()). When m is taken
// again, create_predicate() revives the one of the same name and arity
// rather than making another, so the headers a reused module holds are
// bounded by the predicates it has had, not the times it was reused. A
// revived one is in m again, which a pointer cached for m's old name
// isn't, and match_head() checks that too.

void module_bury_predicates(module *m)
{
	predicate *pr;

	while ((pr = list_pop_front(&m->predicates)) != NULL) {
		sl_rem(m->index, &pr->key, pr);
		pr->is_abolished = true;
		m->pl->pred_gen++;
		pr->filename = NULL;			// m->filename goes when m is reused
		list_push_back(&m->buried, pr);
	}
}

// A buried header of m's for c, made like new.

static predicate *revive_buried(module *m, const cell *c)
{
	for (predicate *pr = list_front(&m->buried); pr; pr = list_next(pr)) {
		if ((pr->key.val_off != c->val_off) || (get_arity(&pr->key) != get_arity(c)))
			continue;

		// Still read, or holding clauses for the last reader to reclaim.

		if (pr->refcnt || pr->head || list_count(&pr->delinked))
			continue;

		list_remove(&m->buried, pr);

		if (pr->meta_args) {
			unshare_cells(pr->meta_args, pr->meta_args->num_cells);
			TPL_free(pr->meta_args);
		}

		memset(pr, 0, sizeof(predicate));
		pr->is_revived = true;
		return pr;
	}

	return NULL;
}

static void free_buried(module *m)
{
	predicate *pr;

	while ((pr = list_pop_front(&m->buried)) != NULL) {
		if (pr->meta_args) {
			unshare_cells(pr->meta_args, pr->meta_args->num_cells);
			TPL_free(pr->meta_args);
		}

		TPL_free(pr);
	}
}

module *module_create(prolog *pl, const char *name)
{
	module *m = module_reuse(pl);

	if (!m) {
		if (!(m = TPL_calloc(1, sizeof(module))))
			return NULL;

		init_lock(&m->guard);
		m->pl = pl;
		prolog_lock(pl);
		m->id = ++pl->next_mod_id;
		bool mapped = modmap_set(pl, m->id, m);
		prolog_unlock(pl);

		if (!mapped) {					// unmapped, its code would find no module on return
			deinit_lock(&m->guard);
			TPL_free(m);
			return NULL;
		}
	}

	m->filename = set_known(m, name);
	m->name = set_known(m, name);
	m->flags.unknown = UNK_ERROR;
	m->flags.syntax_error = UNK_ERROR;
	m->flags.double_quote_chars = true;
	m->flags.character_escapes = true;
	m->flags.occurs_check = false;
	m->error = false;
	m->defops = sl_create((void*)fake_strcmp, NULL, NULL);
	m->keyval = sl_create((void*)fake_strcmp, (void*)keyval_free, NULL);

	if (strcmp(name, "system")) {
		for (const op_table *ptr = g_ops; ptr->name; ptr++) {
			op_table *tmp = TPL_malloc(sizeof(op_table));

			if (!tmp) {
				module_destroy(m);
				return NULL;
			}

			memcpy(tmp, ptr, sizeof(op_table));
			note_op_name(pl, tmp->name);
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

	pl_publish_barrier();				// find_module() walks the list unlocked
	prolog_lock(pl);
	list_push_back(&pl->modules, m);
	prolog_unlock(pl);
	return m;
}
