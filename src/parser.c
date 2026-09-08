#include <ctype.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "module.h"
#include "network.h"
#include "parser.h"
#include "prolog.h"
#include "query.h"

static const unsigned INITIAL_NBR_CELLS = 1000;
const char *g_solo = "!(){}[]|,;`'\"";

bool is_graphic(int ch)
{
	return (ch == '#') || (ch == '$') || (ch == '&')
		|| (ch == '*') || (ch == '+') || (ch == '-')
		|| (ch == '.') || (ch == '/') || (ch == ':')
		|| (ch == '<') || (ch == '=') || (ch == '>')
		|| (ch == '?') || (ch == '@') || (ch == '^')
		|| (ch == '~');
}

char *slicedup(const char *s, size_t n)
{
	char *ptr = TPL_malloc(n+1);
	ENSURE (ptr);
	memcpy(ptr, s, n);
	ptr[n] = '\0';
	return ptr;
}

int slicecmp(const char *s1, size_t len1, const char *s2, size_t len2)
{
	size_t min_len = len1 < len2 ? len1 : len2;
	int val = memcmp(s1, s2, min_len);
	if (val) return val > 0 ? 1 : -1;
	return len1 < len2 ? -1 : len1 > len2 ? 1 : 0;
}

cell *list_head(cell *l, cell *tmp)
{
	if (!is_string(l))
		return l + 1;

	const char *src = is_slice(l) ? l->val_str : is_strbuf(l) ? (char*)l->val_strb->cstr + l->strb_off : (char*)l->val_chr + l->val_off;
	tmp->num_cells = 1;
	tmp->flags = 0;

	if (is_codes(l)) {
		tmp->tag = TAG_INT;
		tmp->val_int = peek_char_utf8(src);
	} else {
		size_t char_len = len_char_utf8(src);

		if (char_len <= MAX_SMALL_STRING) {
			tmp->tag = TAG_CSTR;
			memcpy(tmp->val_chr, src, char_len);
			tmp->val_chr[char_len] = '\0';
			tmp->chr_len = char_len;
		} else {
			tmp->tag = TAG_INTERNED;
			tmp->val_off = g_nil_s;
		}
	}

	set_arity(tmp, 0);

	return tmp;
}

cell *list_tail(cell *l, cell *tmp)
{
	if (!is_string(l)) {
		cell *h = l + 1;
		return h + h->num_cells;
	}

	const char *src = is_slice(l) ? l->val_str : is_strbuf(l) ? (char*)l->val_strb->cstr + l->strb_off : (char*)l->val_chr;
	size_t char_len = len_char_utf8(src);
	size_t str_len = is_slice(l) ? (size_t)l->str_len : is_strbuf(l) ? (size_t)l->val_strb->len - l->strb_off : (unsigned)l->chr_len;

	if (str_len == char_len) {
		tmp->tag = TAG_INTERNED;
		tmp->num_cells = 1;
		tmp->flags = 0;
		set_arity(tmp, 0);
		tmp->val_off = g_nil_s;
		return tmp;
	}

	if (is_slice(l)) {
		*tmp = *l;
		tmp->val_str = l->val_str + char_len;
		tmp->str_len = l->str_len - char_len;
		return tmp;
	}

	if (is_strbuf(l)) {
		*tmp = *l;
		tmp->strb_off = l->strb_off + char_len;
		tmp->strb_len = l->strb_len - char_len;
		return tmp;
	}

	*tmp = *l;
	memcpy(tmp->val_chr, l->val_chr + char_len, l->chr_len - char_len);
	tmp->val_chr[l->chr_len - char_len] = '\0';
	tmp->chr_len = l->chr_len - char_len;
	return tmp;
}

cell *get_logical_body(cell *c)
{
	cell *body = get_body(c);

	if (!body)
		return NULL;

	// A body of just 'true' is equivalent to no body at all,

	if (!get_arity(body) && is_interned(body) && (body->val_off == g_true_s))
		return NULL;

	return body;
}

size_t slicecpy(char *dst, size_t dstlen, const char *src, size_t len)
{
	char *save = dst;

	while ((dstlen-1) && len) {
		*dst++ = *src++;
		dstlen--;
		len--;
	}

	*dst = '\0';
	return dst - save;
}

static void *make_string_internal(cell *c, const char *s, size_t n, size_t off)
{
	strbuf *strb = TPL_malloc(sizeof(strbuf) + n + 1);
	if (!strb) return NULL;
	memcpy(strb->cstr, s, n);
	strb->cstr[n] = 0;
	strb->len = n;
	strb->refcnt = 1;
	c->val_strb = strb;
	c->strb_off = off;
	c->strb_len = n;
	c->flags |= (FLAG_MANAGED | FLAG_CSTR_BLOB);
	return strb;
}

bool make_cstring(cell *d, const char *s)
{
	return make_cstringn(d, s, strlen(s));
}

bool make_string(cell *d, const char *s)
{
	return make_stringn(d, s, strlen(s));
}

bool make_cstringn(cell *d, const char *s, size_t n)
{
	if (!n) {
		make_atom(d, g_empty_s);
		return true;
	}

	if (n < MAX_SMALL_STRING) {
		make_smalln(d, s, n);
		return true;
	}

	*d = (cell){0};
	d->tag = TAG_CSTR;
	d->num_cells = 1;
	make_string_internal(d, s, n, 0);
	return true;
}

bool make_stringn(cell *d, const char *s, size_t n)
{
	if (!n) {
		make_atom(d, g_empty_s);
		return true;
	}

	*d = (cell){0};
	d->tag = TAG_CSTR;
	d->flags = FLAG_CSTR_STRING;
	d->num_cells = 1;
	set_arity(d, 2);
	make_string_internal(d, s, n, 0);
	return true;
}

void make_atom(cell *tmp, pl_idx offset)
{
	*tmp = (cell){0};
	tmp->tag = TAG_INTERNED;
	tmp->num_cells = 1;
	tmp->val_off = offset;
}

cell *make_nil(void)
{
	static cell tmp = {
		.tag = TAG_INTERNED,
		.num_cells = 1,
		.flags = 0,
		.small_arity = 0,
		.arity = 0,
		.val_off = 0
	};

	if (!tmp.val_off)
		tmp.val_off = g_nil_s;

	return &tmp;
}

void make_smalln(cell *tmp, const char *s, size_t n)
{
	*tmp = (cell){0};
	tmp->tag = TAG_CSTR;
	tmp->num_cells = 1;
	memcpy(tmp->val_chr, s, n);
	tmp->val_chr[n] = '\0';
	tmp->chr_len = n;
}

void make_var(cell *tmp, pl_idx off, unsigned var_num)
{
	*tmp = (cell){0};
	tmp->tag = TAG_VAR;
	tmp->flags = FLAG_VAR_LOCAL;
	tmp->num_cells = 1;
	tmp->var_num = var_num;
	tmp->val_off = off;

	if (off == g_anon_s)
		tmp->flags |= FLAG_VAR_ANON;
}

void make_float(cell *tmp, pl_flt v)
{
	*tmp = (cell){0};
	tmp->tag = TAG_FLOAT;
	tmp->num_cells = 1;
	tmp->val_float = v;
}

void make_int(cell *tmp, pl_int v)
{
	*tmp = (cell){0};
	tmp->tag = TAG_INT;
	tmp->num_cells = 1;
	set_smallint(tmp, v);
}

void make_uint(cell *tmp, pl_uint v)
{
	*tmp = (cell){0};
	tmp->tag = TAG_INT;
	tmp->num_cells = 1;
	set_smalluint(tmp, v);
}

void make_ptr(cell *tmp, void *v)
{
	*tmp = (cell){0};
	tmp->tag = TAG_INT;
	tmp->num_cells = 1;
	tmp->val_ptr = v;
}

void make_struct(cell *tmp, pl_idx offset, unsigned arity, pl_idx extra_cells)
{
	*tmp = (cell){0};
	tmp->tag = TAG_INTERNED;
	tmp->num_cells = 1 + extra_cells;
	tmp->arity = arity;
	tmp->val_off = offset;
}

void make_end(cell *tmp)
{
	*tmp = (cell){0};
	tmp->tag = TAG_END;
	tmp->num_cells = 1;
}

void make_blob(cell *tmp, void *ptr)
{
	*tmp = (cell){0};
	tmp->tag = TAG_BLOB;
	tmp->flags = FLAG_MANAGED;
	tmp->num_cells = 1;
	tmp->val_blob = ptr;
	tmp->val_blob->refcnt = 0;
}

void share_cells(cell *src, pl_idx num_cells)
{
	for (pl_idx i = 0; i < num_cells; i++, src++)
		share_cell(src);
}

void unshare_cells(cell *src, pl_idx num_cells)
{
	for (pl_idx i = 0; i < num_cells; i++, src++)
		unshare_cell(src);
}

void clear_clause(clause *cl)
{
	unshare_cells(cl->cells, cl->cidx);
	TPL_free(cl->alt);
	cl->alt = NULL;
	cl->num_vars = 0;
	cl->cidx = 0;
}

static bool make_room(parser *p, unsigned num)
{
	if ((p->cl->cidx+num) >= p->cl->num_allocated_cells) {
		pl_idx num_cells = (p->cl->num_allocated_cells + num) * 3 / 2;

		clause *cl = TPL_realloc(p->cl, sizeof(clause)+(sizeof(cell)*num_cells));
		ENSURE(cl);
		p->cl = cl;
		p->cl->num_allocated_cells = num_cells;
	}

	return true;
}

static cell *make_a_cell(parser *p)
{
	make_room(p, 1);
	cell *ret = p->cl->cells + p->cl->cidx++;
	*ret = (cell){0};
	return ret;
}

void parser_reset(parser *p)
{
	p->was_consing = p->was_string = p->was_partial = p->did_getline = false;
	p->already_loaded_error = p->do_read_term = p->internal = p->one_shot = false;
	p->start_term = p->end_of_term = p->end_of_file = p->is_directive  = false;
	p->is_command = p->is_comment = p->is_consulting = p->is_symbol = false;
	p->is_string = p->is_quoted = p->is_var = p->is_op = p->skip = p->last_close = false;
	p->last_neg = p->no_fp = p->reuse= p->in_body = false;
	p->is_number_chars = p->expand_dcg = false;

	SB_free(p->token);
	p->nesting_parens = p->nesting_brackets = p->nesting_braces = 0;
	p->num_vars = 0;
	p->start_term = true;
	p->error = false;
	p->error_desc = NULL;
	p->error_type = NULL;
	p->cl->cidx = 0;
	p->flags = p->m->flags;
}

static void vartab_free(parser *p);

void parser_destroy(parser *p)
{
	if (!p) return;
	SB_free(p->token);
	TPL_free(p->save_line);

	if (p->cl) {
		clear_clause(p->cl);
		TPL_free(p->cl);
	}

	p->save_line = NULL;
	p->cl = NULL;
	vartab_free(p);
	TPL_free(p);
}

// Returns NULL when out of memory rather than aborting. Exception
// delivery reparses the ball through parse_to_heap(), which needs a
// parser, so aborting here turned a catchable resource_error(memory)
// into a crash -- the tail of issue #801. Every caller checks.

parser *parser_create(module *m)
{
	parser *p = TPL_calloc(1, sizeof(parser));

	if (!p)
		return NULL;

	p->pl = m->pl;
	p->m = m;
	pl_idx num_cells = INITIAL_NBR_CELLS;
	p->cl = TPL_calloc(1, sizeof(clause)+(sizeof(cell)*num_cells));

	if (!p->cl) {
		TPL_free(p);
		return NULL;
	}

	p->cl->num_allocated_cells = num_cells;
	p->start_term = true;
	p->flags = m->flags;
	p->line_num = 1;
	return p;
}

static void consultall(parser *p, cell *l)
{
	PROLOG_LIST_HANDLER(l);

	while (is_list(l)) {
		cell *h = PROLOG_LIST_HEAD(l);

		if (is_iso_list(h))
			consultall(p, h);
		else {
			char *s = C_STR(p, h);

			if (!load_file(p->m, s, false, true))
				fprintf(stderr, "Error: file not found: '%s'\n", s);
		}

		l = PROLOG_LIST_TAIL(l);
	}
}

static bool is_dir_sep(int ch)
{
#ifdef _WIN32
	// Backslash only counts on Windows: on POSIX it is an ordinary
	// character and a file may legitimately be named with one.

	return (ch == '/') || (ch == '\\');
#else
	return ch == '/';
#endif
}

static const char *last_dir_sep(const char *s)
{
	const char *last = NULL;

	for (const char *p = s; *p; p++) {
		if (is_dir_sep(*p))
			last = p;
	}

	return last;
}

// A name that already carries a directory is taken as given; a bare one
// - or an explicitly upward one - is taken relative to the file doing
// the consulting.
//
// basefile is whatever realpath() handed back, and on Windows that is
// backslash-separated. Scanning for '/' alone found no separator there
// at all, so the directory was truncated away and the name was left to
// resolve against the working directory instead of against the
// consulting file. Consulting a bare name from a file in a
// subdirectory could only work if the two happened to coincide.

char *relative_to(const char *basefile, const char *relfile)
{
	char *tmpbuf = TPL_malloc(strlen(basefile) + strlen(relfile) + 256);
	ENSURE(tmpbuf);
	char *ptr = tmpbuf;
	bool upward = !strncmp(relfile, "../", 3);

#ifdef _WIN32
	upward = upward || !strncmp(relfile, "..\\", 3);
#endif

	if (upward || !last_dir_sep(relfile)) {
		strcpy(tmpbuf, basefile);
		const char *sep = last_dir_sep(tmpbuf);

		// Keep the separator the base was written with, so a Windows
		// path stays a Windows path.

		ptr = sep ? tmpbuf + (sep - tmpbuf) + 1 : tmpbuf;
		*ptr = '\0';
	}

	strcpy(ptr, relfile);
	return tmpbuf;
}

static void do_op(parser *p, cell *c, bool make_public)
{
	cell *p1 = c + 1, *p2 = c + 2, *p3 = c + 3;

	if (!is_integer(p1) || !is_interned(p2) || (!is_atom(p3) && !is_list(p3))) {
		if (!p->do_read_term)
			fprintf(stderr, "Error: unknown op, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		p->error = true;
		return;
	}

	unsigned specifier;
	char *spec = DUP_STRING(p, p2);

	if (!strcmp(spec, "fx"))
		specifier = OP_FX;
	else if (!strcmp(spec, "fy"))
		specifier = OP_FY;
	else if (!strcmp(spec, "xf"))
		specifier = OP_XF;
	else if (!strcmp(spec, "yf"))
		specifier = OP_YF;
	else if (!strcmp(spec, "xfx"))
		specifier = OP_XFX;
	else if (!strcmp(spec, "xfy"))
		specifier = OP_XFY;
	else if (!strcmp(spec, "yfx"))
		specifier = OP_YFX;
	else {
		if (!p->do_read_term)
			fprintf(stderr, "Error: unknown op spec tag, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		TPL_free(spec);
		return;
	}

	TPL_free(spec);
	PROLOG_LIST_HANDLER(p3);

	while (is_list(p3)) {
		cell *h = PROLOG_LIST_HEAD(p3);

		if (is_atom(h)) {
			char *name = DUP_STRING(p, h);

			unsigned tmp_optype = 0;
			unsigned tmp_pri = search_op(p->m, name, &tmp_optype, get_arity(p3));

			if (IS_INFIX(specifier) && IS_POSTFIX(tmp_optype) && (true || p->m->flags.strict_iso)) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: permission error set op, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				TPL_free(name);
				return;
			}

			if (IS_POSTFIX(specifier) && IS_INFIX(tmp_optype) && (true || p->m->flags.strict_iso)) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: permission error set op, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				TPL_free(name);
				return;
			}

			if (!set_op(p->m, name, specifier, get_smallint(p1))) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: could not set op, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				TPL_free(name);
				continue;
			}

			if (make_public) {
				if (!set_op(p->pl->user_m, name, specifier, get_smallint(p1))) {
					if (!p->do_read_term)
						fprintf(stderr, "Error: could not set op, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

					TPL_free(name);
					continue;
				}
			}

			TPL_free(name);
		}

		p3 = PROLOG_LIST_TAIL(p3);
	}

	if (is_atom(p3) && !is_nil(p3)) {
		char *name = DUP_STRING(p, p3);
		unsigned tmp_optype = 0;
		unsigned tmp_pri = search_op(p->m, name, &tmp_optype, get_arity(p3));

		if (IS_INFIX(specifier) && IS_POSTFIX(tmp_optype) && (true || p->m->flags.strict_iso)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: permission error set op, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			TPL_free(name);
			return;
		}

		if (IS_POSTFIX(specifier) && IS_INFIX(tmp_optype) && (true || p->m->flags.strict_iso)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: permission error set op, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			TPL_free(name);
			return;
		}

		if (!set_op(p->m, name, specifier, get_smallint(p1))) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: could not set op, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			TPL_free(name);
			return;
		}

		if (make_public) {
			if (!set_op(p->pl->user_m, name, specifier, get_smallint(p1))) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: could not set op, %s:%u\n", get_loaded(p->m, p->m->filename), p->line_num);

				TPL_free(name);
				return;
			}
		}

		TPL_free(name);
	}
}

// One place to report a directive the loader could not make sense of.
// The wording is kept as it was: some callers treat it as an error and
// stop the load, others merely warn and carry on.

static void report_unknown_directive(parser *p, const char *severity, const char *dirname, unsigned arity)
{
	if (p->do_read_term || p->pl->quiet)
		return;

	fflush(stdout);
	fprintf(stderr, "%s: unknown directive: %s/%u\n", severity, dirname, arity);
}

// Runs a directive's goal, distinguishing a goal that raised from one
// that merely failed so the two can be reported differently...

static bool goal_run_reporting(parser *p, cell *goal, bool *raised)
{
	*raised = false;

	if (p->error || p->internal || !is_interned(goal))
		return false;

	query *q = query_create(p->m);
	execute(q, goal, p->cl->num_vars);
	bool ok = (q->retry == QUERY_OK);

	if (!ok)
		*raised = q->did_throw || q->error;

	query_destroy(q);
	return ok;
}

static bool goal_run(parser *p, cell *goal)
{
	if (p->error || p->internal || !is_interned(goal))
		return false;

	if ((goal->val_off == g_goal_expansion_s) && (get_arity(goal) == 2))
		return false;

	if (goal->val_off == g_cut_s)
		return false;

	query *q = query_create(p->m);
	execute(q, goal, p->cl->num_vars);

	if (q->retry != QUERY_OK) {
		query_destroy(q);
		return false;
	}

	query_destroy(q);
	return true;
}

static bool conditionals(parser *p, cell *d)
{
	p->skip = false;

	if (!is_interned(d))
		return false;

	if (strcmp(C_STR(p, d), ":-"))
		return false;

	cell *c = d + 1;

	if (!is_interned(c))
		return false;

	const char *dirname = C_STR(p, c);

	if (!strcmp(dirname, "if") && (get_arity(c) == 1) && !p->m->ifs_blocked[p->m->if_depth]) {
		bool ok = goal_run(p, FIRST_ARG(c));
		p->m->ifs_blocked[++p->m->if_depth] = !ok;
		p->m->ifs_done[p->m->if_depth] = ok;
		return true;
	}

	if (!strcmp(dirname, "if") && (get_arity(c) == 1)) {
		bool save1 = p->m->ifs_blocked[p->m->if_depth];
		p->m->ifs_blocked[++p->m->if_depth] = save1;
		p->m->ifs_done[p->m->if_depth] = true;
		return true;
	}

	if (!strcmp(dirname, "elif") && (get_arity(c) == 1) && !p->m->ifs_done[p->m->if_depth] && p->m->ifs_blocked[p->m->if_depth]) {
		bool ok = goal_run(p, FIRST_ARG(c));
		p->m->ifs_blocked[p->m->if_depth] = !ok;
		p->m->ifs_done[p->m->if_depth] = ok;
		return true;
	}

	if (!strcmp(dirname, "elif") && (get_arity(c) == 1)) {
		p->m->ifs_blocked[p->m->if_depth] = true;
		return true;
	}

	if (!strcmp(dirname, "else") && (get_arity(c) == 0) && !p->m->ifs_done[p->m->if_depth] && p->m->ifs_blocked[p->m->if_depth]) {
		p->m->ifs_blocked[p->m->if_depth] = false;
		p->m->ifs_done[p->m->if_depth] = true;
		return true;
	}

	if (!strcmp(dirname, "else") && (get_arity(c) == 0)) {
		p->m->ifs_blocked[p->m->if_depth] = true;
		return true;
	}

	if (!strcmp(dirname, "endif") && (get_arity(c) == 0)) {
		--p->m->if_depth;
		return true;
	}

	return false;
}

// Quads are queries using answer descriptions: a '?- Query.' term
// followed by terms describing the expected toplevel answers. See
// github.com/trealla-prolog/trealla issue #1063. Each quad is recorded
// as a '$quad'(Query, VarNames, AnswerDescription, File, Line) fact
// so that library(quads) can interpret them as tests at run time.
// Nothing is executed at load time.

void quad_reset(module *m)
{
	if (m->quad_query) {
		cell *c = m->quad_query;
		pl_idx num_cells = c->num_cells;

		for (pl_idx i = 0; i < num_cells; i++, c++)
			unshare_cell(c);

		TPL_free(m->quad_query);
		m->quad_query = NULL;
	}

	if (m->quad_name) {
		cell *c = m->quad_name;
		pl_idx num_cells = c->num_cells;

		for (pl_idx i = 0; i < num_cells; i++, c++)
			unshare_cell(c);

		TPL_free(m->quad_name);
		m->quad_name = NULL;
	}

	m->quad_recorded = false;

	m->quad_num_vars = 0;
	m->in_quad = false;
}

// A quad may be labelled: 'Name ?- Query.' names the query with a
// ground term, so a report can say which quad it is (issue #1071).
// The label carries no variables to share with the answer.

static bool is_ground_term(const cell *c)
{
	pl_idx num_cells = c->num_cells;

	for (pl_idx i = 0; i < num_cells; i++, c++) {
		if (is_var(c))
			return false;
	}

	return true;
}

// The shape of a toplevel answer, per the grammar in issue #1063
// plus the annotations used by existing quad suites (Flowlog), and
// outputs/1 for captured current-output (issue #1082).
//
// A toplevel answer reports an answer *substitution*, so every equation
// in it binds a variable: it has a variable on the left, and no variable
// is bound twice within one answer. '1 = X' and 'X = 1, X = 2' are
// therefore not answer descriptions (issue #1074).
//
// A substitution is also idempotent: no variable it binds occurs in what
// another equation binds. 'X = f(Y), Y = 1' is therefore not one either,
// the answer being 'X = f(1), Y = 1' (issue #1081). An answer annotated
// 'sto' is exempt, a cyclic term being what such a description states.
//
// Such a term is told apart from an ordinary clause that merely follows
// a quad, because nothing else could be meant by an equation directly
// after a query. ANSWER_BAD says so, and the caller reports it instead
// of quietly loading it (which would surface as a permission error on
// (=)/2, or, worse, as a quad that passes).
//
// The same applies once the principal functor is already one that marks
// an answer description — ','/2, ';'/2, '|'/2 (issue #1087). An unknown
// conjunct such as 'Y = 2, some_unknown_stuff' is still an answer
// description (malformed), not "not an answer description": returning
// ANSWER_NO would warn that the query has no description and fall
// through to a permission error on (',')/2.

enum answer_kind { ANSWER_NO = 0, ANSWER_OK, ANSWER_BAD };

// Inside a known answer-description constructor, a subterm that is not
// itself a description is a malformation of this one, not evidence that
// the outer term is an ordinary clause.

static enum answer_kind answer_as_part(enum answer_kind k)
{
	return (k == ANSWER_NO) ? ANSWER_BAD : k;
}

// The variables already bound by the answer being checked. A new one
// starts at each alternative, since ';' and '|' separate answers.

typedef struct {
	unsigned num_vars;
	unsigned var_num[MAX_VARS];
	bool sto;
} answer_vars;

static bool answer_vars_add(answer_vars *seen, unsigned var_num)
{
	for (unsigned i = 0; i < seen->num_vars; i++) {
		if (seen->var_num[i] == var_num)
			return false;
	}

	if (seen->num_vars < MAX_VARS)
		seen->var_num[seen->num_vars++] = var_num;

	return true;
}

static enum answer_kind answer_one(parser *p, const cell *c, answer_vars *seen);

static enum answer_kind answer_description(parser *p, const cell *c, answer_vars *seen)
{
	if (!is_interned(c))
		return ANSWER_NO;

	const char *name = C_STR(p, c);

	if (get_arity(c) == 0) {
		if (!strcmp(name, "sto"))
			seen->sto = true;

		return (!strcmp(name, "true") || !strcmp(name, "false")
			|| !strcmp(name, "...") || !strcmp(name, "loops")
			|| !strcmp(name, "instantiation_error")
			|| !strcmp(name, "system_error")
			|| !strcmp(name, "ad_infinitum")
			|| !strcmp(name, "sto")
			|| !strcmp(name, "unexpected")
			|| !strcmp(name, "inattendue")
			|| !strcmp(name, "other_answer_sequence")
			|| !strcmp(name, "maybe")
			|| !strcmp(name, "waits"))
			? ANSWER_OK : ANSWER_NO;
	}

	if (get_arity(c) == 2) {
		const cell *lhs = c + 1;
		const cell *rhs = lhs + lhs->num_cells;

		// 'V ~~ Spec' binds V to a float only approximately, to the
		// precision Spec is written to (issue #1145). It binds like an
		// equation does, so it is one for every check made here. The
		// operator is exported by library(quads), not global, so this
		// is only ever reached in a file that imported it.

		if (!strcmp(name, "=") || !strcmp(name, "~~")) {
			if (!is_var(lhs))
				return ANSWER_BAD;

			return answer_vars_add(seen, lhs->var_num) ? ANSWER_OK : ANSWER_BAD;
		}

		if (!strcmp(name, ",")) {
			enum answer_kind k = answer_as_part(answer_description(p, lhs, seen));

			if (k != ANSWER_OK)
				return k;

			return answer_as_part(answer_description(p, rhs, seen));
		}

		if (!strcmp(name, ";") || !strcmp(name, "|")) {
			answer_vars lhs_seen = {0}, rhs_seen = {0};
			enum answer_kind k = answer_as_part(answer_one(p, lhs, &lhs_seen));

			if (k != ANSWER_OK)
				return k;

			return answer_as_part(answer_one(p, rhs, &rhs_seen));
		}

		if (!strcmp(name, "error")
			|| !strcmp(name, "type_error")
			|| !strcmp(name, "domain_error")
			|| !strcmp(name, "existence_error"))
			return ANSWER_OK;
	}

	if (get_arity(c) == 1)
		return (!strcmp(name, "throw")
			|| !strcmp(name, "syntax_error")
			|| !strcmp(name, "representation_error")
			|| !strcmp(name, "resource_error")
			|| !strcmp(name, "evaluation_error")
			|| !strcmp(name, "uninstantiation_error")
			|| !strcmp(name, "outputs")
			|| !strcmp(name, "inputs")
			|| !strcmp(name, "peeks"))
			? ANSWER_OK : ANSWER_NO;

	if (get_arity(c) == 3)
		return !strcmp(name, "permission_error") ? ANSWER_OK : ANSWER_NO;

	return ANSWER_NO;
}

// Walk the equations of one answer again, now that every variable it
// binds is known, and check that none of them occurs on a right-hand
// side. Alternatives are not entered: each is an answer of its own and
// has been checked against its own set.

static bool answer_is_substitution(parser *p, const cell *c, const answer_vars *seen)
{
	if (!is_interned(c) || (get_arity(c) != 2))
		return true;

	const char *name = C_STR(p, c);
	const cell *lhs = c + 1;
	const cell *rhs = lhs + lhs->num_cells;

	if (!strcmp(name, ","))
		return answer_is_substitution(p, lhs, seen)
			&& answer_is_substitution(p, rhs, seen);

	if (strcmp(name, "=") && strcmp(name, "~~"))
		return true;

	pl_idx num_cells = rhs->num_cells;

	for (pl_idx i = 0; i < num_cells; i++, rhs++) {
		if (!is_var(rhs))
			continue;

		for (unsigned j = 0; j < seen->num_vars; j++) {
			if (seen->var_num[j] == rhs->var_num)
				return false;
		}
	}

	return true;
}

// One answer: its shape, then the substitution property over the whole
// of it, which is only decidable once all its equations have been seen.

static enum answer_kind answer_one(parser *p, const cell *c, answer_vars *seen)
{
	enum answer_kind k = answer_description(p, c, seen);

	if ((k != ANSWER_OK) || seen->sto)
		return k;

	return answer_is_substitution(p, c, seen) ? ANSWER_OK : ANSWER_BAD;
}

// Build and assert
// '$quad'(Id, Query, VarNames, AnswerDescription, File, Line).
// The query was read as one term and the answer description as another,
// so their variables can only be related by name: VarNames is a list of
// Name=Var pairs covering the named variables of both terms, and
// library(quads) unifies same-named entries.
//
// Id is the label of 'Name ?- Query.' and an unbound variable when the
// quad is written with the prefix operator, so 'this quad has no name'
// needs no reserved atom.

static void quad_record(parser *p, cell *ad)
{
	module *m = p->m;
	cell *q = m->quad_query;
	cell *id = m->quad_name;
	pl_idx q_num_cells = q->num_cells;
	pl_idx ad_num_cells = ad->num_cells;
	pl_idx id_num_cells = id ? id->num_cells : 1;
	unsigned q_num_vars = m->quad_num_vars;
	unsigned ad_num_vars = p->cl->num_vars;
	unsigned total_vars = q_num_vars + ad_num_vars;
	unsigned id_var_num = total_vars;
	if (!id) total_vars++;

	// Collect the named variables of both terms (first occurrence
	// each). Answer-description vars are renumbered +q_num_vars.

	struct { unsigned var_num; pl_idx off; uint16_t flags; } *vt;
	vt = TPL_malloc(sizeof(*vt) * (total_vars ? total_vars : 1));
	if (!vt) { p->error = true; return; }
	bool *seen = TPL_calloc(total_vars ? total_vars : 1, sizeof(bool));
	if (!seen) { TPL_free(vt); p->error = true; return; }
	unsigned num_named = 0;

	for (int pass = 0; pass < 2; pass++) {
		const cell *c = pass ? ad : q;
		pl_idx num_cells = pass ? ad_num_cells : q_num_cells;
		unsigned offset = pass ? q_num_vars : 0;

		for (pl_idx i = 0; i < num_cells; i++, c++) {
			if (!is_var(c))
				continue;

			unsigned var_num = c->var_num + offset;

			if (seen[var_num] || is_anon(c) || !strcmp(C_STR(p, c), "_"))
				continue;

			seen[var_num] = true;
			vt[num_named].var_num = var_num;
			vt[num_named].off = c->val_off;
			vt[num_named].flags = c->flags;
			num_named++;
		}
	}

	// '$quad'/5 + Query + VarNames list + AnswerDescription + File + Line

	pl_idx vn_num_cells = (4 * num_named) + 1;
	pl_idx total = 1 + id_num_cells + q_num_cells + vn_num_cells + ad_num_cells + 1 + 1;
	cell *tmp = TPL_calloc(total, sizeof(cell));

	if (!tmp) {
		TPL_free(vt);
		TPL_free(seen);
		p->error = true;
		return;
	}

	cell *dst = tmp;
	dst->tag = TAG_INTERNED;
	dst->val_off = g_sys_quad_s;
	dst->flags = 0;
	set_arity(dst, 6);
	dst->num_cells = total;
	dst++;

	if (id) {
		dup_cells(dst, id, id_num_cells);
	} else {
		dst->tag = TAG_VAR;
		dst->var_num = id_var_num;
		dst->val_off = g_anon_s;
		dst->flags = FLAG_VAR_ANON;
		dst->num_cells = 1;
	}

	dst += id_num_cells;

	dup_cells(dst, q, q_num_cells);
	dst += q_num_cells;

	for (unsigned i = 0; i < num_named; i++) {
		dst->tag = TAG_INTERNED;			// list cell
		dst->val_off = g_dot_s;
		dst->flags = 0;
		set_arity(dst, 2);
		dst->num_cells = (4 * (num_named - i)) + 1;
		dst++;
		dst->tag = TAG_INTERNED;			// Name = Var
		dst->val_off = g_eq_s;
		dst->flags = 0;
		set_arity(dst, 2);
		dst->num_cells = 3;
		dst++;
		dst->tag = TAG_INTERNED;			// Name
		dst->val_off = vt[i].off;
		dst->num_cells = 1;
		dst++;
		dst->tag = TAG_VAR;					// Var
		dst->var_num = vt[i].var_num;
		dst->val_off = vt[i].off;
		dst->flags = vt[i].flags;
		dst->num_cells = 1;
		dst++;
	}

	dst->tag = TAG_INTERNED;
	dst->val_off = g_nil_s;
	dst->num_cells = 1;
	dst++;

	dup_cells(dst, ad, ad_num_cells);

	for (pl_idx i = 0; i < ad_num_cells; i++) {
		if (is_var(dst+i))
			(dst+i)->var_num += q_num_vars;
	}

	dst += ad_num_cells;

	dst->tag = TAG_INTERNED;
	dst->val_off = new_atom(p->pl, get_loaded(m, m->filename));
	dst->num_cells = 1;
	dst++;

	dst->tag = TAG_INT;
	set_smallint(dst, m->quad_line_num);
	dst->num_cells = 1;

	TPL_free(vt);
	TPL_free(seen);

	// Quads are recorded as data, so a program can add or retract them
	// the way it can any other data. Without this the first recorded
	// quad would make '$quad'/5 static and a later assertz/1 would raise
	// a permission error.

	set_dynamic_in_db(m, "$quad", 6);

	if (assertz_to_db(m, total_vars, tmp, true) == NULL) {
		fprintf(stderr, "Warning: could not record quad, %s:%d\n", get_loaded(m, m->filename), m->quad_line_num);
		cell *c = tmp;

		for (pl_idx i = 0; i < total; i++, c++)
			unshare_cell(c);
	}

	// On success the asserted copy takes over the cell references

	TPL_free(tmp);
}

static bool quads(parser *p, cell *d)
{
	module *m = p->m;

	if (p->internal || p->error)
		return false;

	if (!is_interned(d))
		return false;

	// '?- Query.' or, labelled with a ground term, 'Name ?- Query.'

	if ((d->val_off == g_quad_s) && ((get_arity(d) == 1) || (get_arity(d) == 2))) {
		cell *id = (get_arity(d) == 2) ? d + 1 : NULL;
		cell *q = id ? id + id->num_cells : d + 1;

		// A non-ground label is recorded like a malformed answer
		// description (issue #1078): do not set p->error, or the rest
		// of the file is abandoned and later quads never run. The
		// following answer terms must still be consumed, and
		// library(quads) reports the bad identifier as failed.

		if (m->quad_query && !m->quad_recorded)
			fprintf(stderr, "Warning: quad query without answer description, %s:%d\n", get_loaded(m, m->filename), m->quad_line_num);

		quad_reset(m);
		m->quad_query = TPL_malloc(sizeof(cell) * q->num_cells);

		if (!m->quad_query) {
			p->error = true;
			return true;
		}

		dup_cells(m->quad_query, q, q->num_cells);

		if (id) {
			// A non-ground label cannot be stored as-is: a variable
			// label is indistinguishable from an unlabelled quad once
			// retrieved. Record a ground sentinel so the load can
			// continue (issue #1078) and library(quads) reports it.
			if (!is_ground_term(id)) {
				m->quad_name = TPL_malloc(sizeof(cell));

				if (!m->quad_name) {
					p->error = true;
					return true;
				}

				make_atom(m->quad_name, new_atom(p->pl, "$bad_quad_identifier"));
			} else {
				m->quad_name = TPL_malloc(sizeof(cell) * id->num_cells);

				if (!m->quad_name) {
					p->error = true;
					return true;
				}

				dup_cells(m->quad_name, id, id->num_cells);
			}
		}

		m->quad_num_vars = p->cl->num_vars;
		m->quad_line_num = p->line_num_start ? p->line_num_start : p->line_num;
		m->in_quad = true;
		p->line_num_start = 0;

		// Release the parser's own reference on the consumed term.
		// Ordinary clause loading hands these cells to assertz_to_db(),
		// which takes the reference over - which is why the caller can
		// reset cl->cidx without unsharing. A quad instead dup_cells()
		// its own copy, so the parser's reference is still outstanding
		// and nothing else will ever drop it.

		unshare_cells(d, d->num_cells);
		return true;
	}

	if (!m->in_quad)
		return false;

	answer_vars seen = {0};
	enum answer_kind kind = answer_one(p, d, &seen);

	if (kind == ANSWER_NO) {
		if (m->quad_query && !m->quad_recorded)
			fprintf(stderr, "Warning: quad query without answer description, %s:%d\n", get_loaded(m, m->filename), m->quad_line_num);

		quad_reset(m);
		return false;
	}

	// A quad may carry more than one answer description, and all of
	// them have to hold. Keep the query so each following description
	// is recorded against it, rather than discarding all but the first.
	//
	// A malformed one (ANSWER_BAD) is recorded too, so the load carries
	// on and run_quads reports it as failed, rather than each such case
	// needing a file of its own (issue #1078). It must still be consumed
	// here: left to ordinary clause loading, an equation would surface
	// as a permission error on (=)/2. library(quads) repeats the shape
	// check, so nothing is lost by not reporting it now.

	if (m->quad_query) {
		quad_record(p, d);
		m->quad_recorded = true;
		m->in_quad = true;		// keep consuming answer-shaped terms
	}

	p->line_num_start = 0;

	// As above: quad_record() dup_cells() into the '$quad'/6 it asserts,
	// so the answer description's own cells are still held by the parser
	// here. Every string literal in an answer description leaked one
	// strbuf without this.

	unshare_cells(d, d->num_cells);
	return true;
}

// A directive is a *declaration* if it tells the loader something about
// how to read or organise the remaining terms. Anything else is, per ISO
// 7.4.2, simply a goal to be executed at this point in the load.

static bool is_declaration_name(const char *name)
{
	static const char *s_names[] = {
		"attribute", "autoload", "create_prolog_flag", "discontiguous",
		"dynamic", "elif", "else", "encoding", "endif", "ensure_loaded",
		"export", "foreign_struct", "help", "if", "include", "info",
		"initialization", "meta_predicate", "module", "multifile", "op",
		"pragma", "public", "reexport", "set_prolog_flag",
		"use_foreign_module", "use_module",
		NULL
	};

	for (const char **n = s_names; *n; n++) {
		if (!strcmp(*n, name))
			return true;
	}

	return false;
}

// Note: a conjunction counts as a declaration only if every conjunct is
// one, so that ':- dynamic(a/1), dynamic(b/1).' is still handled by the
// loader while ':- write(hello), nl.' is run as an ordinary goal...

static bool is_declaration(parser *p, cell *c)
{
	if (!is_interned(c))
		return false;

	if ((c->val_off == g_conjunction_s) && (get_arity(c) == 2)) {
		cell *lhs = c + 1;
		cell *rhs = lhs + lhs->num_cells;
		return is_declaration(p, lhs) && is_declaration(p, rhs);
	}

	return is_declaration_name(C_STR(p, c));
}

// Does this directive (or any conjunct of it) declare an
// initialization goal? Those are recorded whole for the end-of-load
// runner, so they are not split apart.

static bool has_initialization(parser *p, cell *c)
{
	if (!is_interned(c))
		return false;

	if ((c->val_off == g_conjunction_s) && (get_arity(c) == 2)) {
		cell *lhs = c + 1;
		cell *rhs = lhs + lhs->num_cells;
		return has_initialization(p, lhs) || has_initialization(p, rhs);
	}

	return !strcmp(C_STR(p, c), "initialization") && (get_arity(c) == 1);
}

static bool directive_term(parser *p, cell *c);

static bool directives(parser *p, cell *d)
{
	p->skip = false;

	if (!is_interned(d))
		return false;

	if (is_list(d) && p->is_command) {
		consultall(p, d);
		p->skip = true;
		return false;
	}

	if (strcmp(C_STR(p, d), ":-"))
		return false;

	if (get_arity(d) != 1)
		return false;

	cell *c = d + 1;

	if (!is_interned(c))
		return false;

	const char *dirname = C_STR(p, c);

	if (is_list(c)) {
		printf("WARNING: directive to load '%s' not allowed\n", C_STR(p, c+1));
		p->error = true;
		return false;
	}

	cell *arg = c + 1;

	d->val_off = new_atom(p->pl, "$directive");
	CLR_OP(d);

	return directive_term(p, c);
}

// Handles one directive. A conjunction of declarations is the same as
// giving them separately, so ':- dynamic(a/1), dynamic(b/1).' recurses
// into each conjunct rather than trying to read ',' as a declaration.
// initialization/1 is excluded: its goal is recorded as a whole
// '$directive'(initialization(G)) fact for the end-of-load runner to
// retract, which a conjunction would not match.

static bool directive_term(parser *p, cell *c)
{
	module *m = p->m;
	const char *dirname = C_STR(p, c);

	if ((c->val_off == g_conjunction_s) && (get_arity(c) == 2)
		&& is_declaration(p, c) && !has_initialization(p, c)) {
		cell *lhs = c + 1;
		cell *rhs = lhs + lhs->num_cells;

		if (!directive_term(p, lhs) || p->error)
			return false;

		return directive_term(p, rhs);
	}

	// run_init lives on the module, and a load whose file has no module
	// directive of its own shares one with whatever consulted it, so
	// the module flag alone cannot say whose goals are pending. The
	// parser flag says this load has some of its own; include/1 and
	// ensure_loaded/1 hand theirs up, since they load with init false
	// and leave their goals for the load still going on to run.

	if (!strcmp(dirname, "initialization") && (get_arity(c) == 1)) {
		p->m->run_init = true;
		p->saw_initialization = true;
		return false;
	}

	// Not a declaration? Then it's a goal: run it here, at its position
	// in the load, rather than silently discarding it...

	if (!is_declaration(p, c)) {
		bool raised = false;

		if (!goal_run_reporting(p, c, &raised)
			&& !p->internal && !p->do_read_term && !p->pl->quiet) {
			fflush(stdout);
			fprintf(stderr, "Warning: directive %s: %s/%u, %s:%d\n",
				raised ? "raised an exception" : "failed",
				dirname, (unsigned)get_arity(c),
				get_loaded(p->m, p->m->filename), p->line_num);
		}

		return true;
	}

	if (!strcmp(dirname, "info") && (get_arity(c) == 1)) {
		printf("INFO: %s\n", C_STR(p, FIRST_ARG(c)));
		return true;
	}

	cell *p1 = c + 1;

	if (!strcmp(dirname, "help") && (get_arity(c) == 2)) {
		// An atom here is a zero-arity predicate. This used to bail out,
		// so ':- help(foo, [...])' was accepted and silently did nothing
		// - which is why nothing in library/ documents a 0-arity
		// predicate.
		if (!is_compound(p1) && !is_atom(p1)) return true;
		cell *p2 = p1 + p1->num_cells;
		if (!is_iso_list_or_nil(p2)) return true;
		PROLOG_LIST_HANDLER(p2);
		char *desc = NULL;
		bool iso = false;

		while (is_iso_list(p2)) {
			cell *h = PROLOG_LIST_HEAD(p2);

			if (is_compound(h) && is_atom(h+1) && !strcmp(C_STR(p, h), "iso")) {
				cell *arg = h + 1;
				iso = !strcmp(C_STR(p, arg), "true");

				if (iso) {
					predicate *pr = find_predicate(p->m, p1);

					if (pr)
						pr->is_iso = true;
				}
			}

			if (is_compound(h) && is_atom(h+1) && !strcmp(C_STR(p, h), "desc")) {
				cell *arg = h + 1;
				desc = DUP_STRING(p, arg);
			}

			p2 = PROLOG_LIST_TAIL(p2);
		}

		pl_ctx p1_ctx = 0;
		query q = (query){0};
		q.pl = p->pl;
		q.st.m = p->m;
		char *dst = print_term_to_strbuf(&q, p1, p1_ctx, 0);
		builtins *ptr = TPL_calloc(1, sizeof(builtins));
		ENSURE(ptr);
		ptr->name = TPL_strdup(C_STR(p, p1));
		ptr->arity = get_arity(p1);
		ptr->m = p->m;
		ptr->desc = desc;
		char *src = dst;

		while (*src && (*src != '('))
			src++;

		if (*src == '(')
			src++;

		// Strip the closing paren of the argument list. With no arguments
		// there is no paren, and chopping the last character would eat a
		// letter off the name.

		if (get_arity(p1)) {
			char *end = dst + strlen(dst) - 1;
			*end = '\0';
		}

		ptr->help = *src ? src : dst;
		ptr->help2 = dst;
		ptr->iso = iso;
		ptr->via_directive = true;
		sl_app(p->pl->help, ptr->name, ptr);

		if (ptr->iso)
			push_property(p->m, ptr->name, ptr->arity, "iso");

		push_template(p->m, ptr->name, ptr->arity, ptr);
		return true;
	}

	if (!strcmp(dirname, "include") && (get_arity(c) == 1)) {
		if (!is_atom(p1)) return true;
		unsigned save_line_nbr = p->line_num;
		const char *name = C_STR(p, p1);
		char *filename = relative_to(p->m->filename, name);

		if (!load_file(p->m, filename, true, false)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: not found: %s:%d\n", filename, p->line_num);

			TPL_free(filename);
			p->line_num = save_line_nbr;
			p->error = true;
			return true;
		}

		set_parent(p->m, p->m->actual_filename, p->m->filename);
		TPL_free(filename);
		p->line_num = save_line_nbr;
		p->saw_initialization |= p->m->run_init;
		return true;
	}

	if (!strcmp(dirname, "ensure_loaded") && (get_arity(c) == 1)) {
		if (!is_atom(p1)) return true;
		unsigned save_line_nbr = p->line_num;
		const char *name = C_STR(p, p1);
		char *filename = relative_to(p->m->filename, name);

		if (!load_file(p->m, filename, false, false)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: not found: %s:%d\n", filename, p->line_num);

			TPL_free(filename);
			p->line_num = save_line_nbr;
			p->error = true;
			return true;
		}

		TPL_free(filename);
		p->line_num = save_line_nbr;
		p->saw_initialization |= p->m->run_init;
		return true;
	}

	if (!strcmp(dirname, "pragma") && (get_arity(c) == 2)) {
		cell *p2 = c + 2;
		const char *name = "";
		char tmpbuf[1024];

		if (is_var(p1)) {
			snprintf(tmpbuf, sizeof(tmpbuf), "%s", p->m->filename);
			char *ptr = tmpbuf + strlen(tmpbuf) - 1;

			while (*ptr && (*ptr != '.') && (ptr != tmpbuf))
				ptr--;

			if (*ptr == '.')
				*ptr = '\0';

			name = tmpbuf;
		} else if (!is_atom(p1)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: pragma name not an atom, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error = true;
			return true;
		} else
			name = C_STR(p, p1);

		module *tmp_m;

		if ((tmp_m = find_module(p->pl, name)) != NULL) {
			//if (!p->do_read_term)
			//	fprintf(stderr, "Error: module already loaded: %s, %s:%d\n", name, get_loaded(p->m, p->m->filename), p->line_num);
			//
			p->already_loaded_error = true;
			p->m = tmp_m;
			return true;
		}

		tmp_m = module_create(p->pl, name);
		if (!tmp_m) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: module creation failed: %s, %s:%d\n", name, get_loaded(p->m, p->m->filename), p->line_num);

			p->error = true;
			return true;
		}

		if (tmp_m != p->m)
			module_add_use(p->m, tmp_m);

		PROLOG_LIST_HANDLER(p2);

		while (is_iso_list(p2)) {
			PROLOG_LIST_HEAD(p2);
			p2 = PROLOG_LIST_TAIL(p2);
		}

		return true;
	}

	if (!strcmp(dirname, "attribute") && (get_arity(c) == 1)) {
		cell *arg = c + 1;

		if (arg->val_off == g_slash_s) {
			cell *f = arg;
			char *name = C_STR(p->m, f+1);
			unsigned arity = get_smallint(f+2);
			module_duplicate(p->pl, p->m, name, arity);
			return true;
		}

		while (arg->val_off == g_conjunction_s) {
			cell *f = arg + 1;

			if ((!is_compound(f)) || (f->val_off != g_slash_s))
				break;

			char *name = C_STR(p->m, f+1);
			unsigned arity = get_smallint(f+2);
			module_duplicate(p->pl, p->m, name, arity);
			arg += 4;
		}

		cell *f = arg;

		if ((!is_compound(f)) || (f->val_off != g_slash_s))
			return true;

		char *name = C_STR(p->m, f+1);
		unsigned arity = get_smallint(f+2);
		module_duplicate(p->pl, p->m, name, arity);
		return true;
	}

	if (!strcmp(dirname, "module") && (get_arity(c) >= 1)) {
		module *save_m = p->m;
		const char *name = "";
		char tmpbuf[1024];

		if (is_var(p1)) {
			snprintf(tmpbuf, sizeof(tmpbuf), "%s", p->m->filename);
			char *ptr = tmpbuf + strlen(tmpbuf) - 1;

			while (*ptr && (*ptr != '.') && (ptr != tmpbuf))
				ptr--;

			if (*ptr == '.')
				*ptr = '\0';

			name = tmpbuf;
		} else if (!is_atom(p1)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: module name not an atom, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error = true;
			return true;
		} else
			name = C_STR(p, p1);

		if (!p->m->make) {
			module *tmp_m;

			if ((tmp_m = find_module(p->pl, name)) != NULL) {
				p->already_loaded_error = true;
				p->m = tmp_m;
				return true;
			}

			tmp_m = module_create(p->pl, name);

			if (!tmp_m) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: module creation failed: %s, %s:%d\n", name, get_loaded(p->m, p->m->filename), p->line_num);

				p->error = true;
				return true;
			}

			if (tmp_m != p->m)
				module_add_use(p->m, tmp_m);

			p->m = tmp_m;
		}

		if (get_arity(c) == 1)
			return true;

		cell *p2 = c + 2;
		PROLOG_LIST_HANDLER(p2);

		while (is_iso_list(p2)) {
			cell *head = PROLOG_LIST_HEAD(p2);

			if (is_compound(head)) {
				if (!strcmp(C_STR(p, head), "/")
					|| !strcmp(C_STR(p, head), "//")) {
					cell *f = head+1, *a = f+1;
					if (!is_interned(f)) return true;
					if (!is_integer(a)) return true;
					cell tmp = *f;
	set_arity(&tmp, get_smallint(a));

					if (!strcmp(C_STR(p, head), "//"))
						set_arity(&tmp, get_arity(&tmp) + 2);

					predicate *pr = find_predicate(p->m, &tmp);
					if (!pr) pr = create_predicate(p->m, &tmp, NULL);

					if (!pr) {
						module_destroy(p->m);
						p->m = NULL;
						if (!p->do_read_term)
							fprintf(stderr, "Error: predicate creation failed, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

						p->error = true;
						return true;
					}

					pr->is_public = true;
				} else if (!strcmp(C_STR(p, head), "op") && (get_arity(head) == 3)) {
					// Keep exported operators in their declaring module. Importers
					// find them through search_op() after use_module/1.
					do_op(p, head, false);
				} else {
					if (!p->do_read_term)
						fprintf(stderr, "Error: predicate export failed, '%s' in %s:%d\n", C_STR(p, head), get_loaded(p->m, p->m->filename), p->line_num);

					p->error = true;
					return true;
				}
			}

			p2 = PROLOG_LIST_TAIL(p2);
		}

		return true;
	}

	if ((!strcmp(dirname, "use_module") || !strcmp(dirname, "autoload") || !strcmp(dirname, "reexport")) && (get_arity(c) >= 1)) {
		if (!is_callable(p1))
			return true;

		get_arity(c) == 1 ? do_use_module_1(p->m, c) : do_use_module_2(p->m, c);
		return true;
	}

#if USE_FFI
	if (!strcmp(dirname, "foreign_struct") && (get_arity(c) == 2)) {
		if (!is_iso_atom(p1)) {
			p->error = true;
			return true;
		}

		do_foreign_struct(p->m, c);
		return true;
	}

	if (!strcmp(dirname, "use_foreign_module") && (get_arity(c) == 2)) {
		if (!is_atom(p1)) {
			p->error = true;
			return true;
		}

		if (!do_use_foreign_module(p->m, c)) {
			p->error = true;
			return true;
		}

		return true;
	}
#endif

	if (!strcmp(dirname, "meta_predicate") && (get_arity(c) == 1)) {
		if (!is_compound(p1))
			return true;
	}

	if (!strcmp(dirname, "set_prolog_flag") && (get_arity(c) == 2)) {
		cell *p2 = c + 2;

		// The five flags below affect parsing itself (eg. double_quotes
		// changes how the REST of this file reads), so they need to
		// take effect right here rather than waiting for the directive
		// to run as a goal. Their values are always atoms. Anything
		// else - an unrecognized flag name, or a non-atom value like
		// the tabling restraint flags' integers - has no parse-time
		// meaning and falls through to the generic goal path below.

		if (is_interned(p2) && !strcmp(C_STR(p, p1), "double_quotes")) {
			if (!strcmp(C_STR(p, p2), "atom")) {
				p->m->flags.double_quote_chars = p->m->flags.double_quote_codes = false;
				p->m->flags.double_quote_atom = true;
			} else if (!strcmp(C_STR(p, p2), "codes")) {
				p->m->flags.double_quote_chars = p->m->flags.double_quote_atom = false;
				p->m->flags.double_quote_codes = true;
			} else if (!strcmp(C_STR(p, p2), "chars")) {
				p->m->flags.double_quote_atom = p->m->flags.double_quote_codes = false;
				p->m->flags.double_quote_chars = true;
			} else {
				if (!p->do_read_term)
					fprintf(stderr, "Error: unknown value, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error = true;
				return true;
			}
		} else if (is_interned(p2) && !strcmp(C_STR(p, p1), "character_escapes")) {
			if (!strcmp(C_STR(p, p2), "true") || !strcmp(C_STR(p, p2), "on"))
				p->m->flags.character_escapes = true;
			else if (!strcmp(C_STR(p, p2), "false") || !strcmp(C_STR(p, p2), "off"))
				p->m->flags.character_escapes = false;
		} else if (is_interned(p2) && !strcmp(C_STR(p, p1), "occurs_check")) {
			if (!strcmp(C_STR(p, p2), "true") || !strcmp(C_STR(p, p2), "on"))
				p->m->flags.occurs_check = true;
			else if (!strcmp(C_STR(p, p2), "false") || !strcmp(C_STR(p, p2), "off"))
				p->m->flags.occurs_check = false;
		} else if (is_interned(p2) && !strcmp(C_STR(p, p1), "strict_iso")) {
			if (!strcmp(C_STR(p, p2), "true") || !strcmp(C_STR(p, p2), "on"))
				p->m->flags.strict_iso = true;
			else if (!strcmp(C_STR(p, p2), "false") || !strcmp(C_STR(p, p2), "off"))
				p->m->flags.strict_iso = false;
		} else if (is_interned(p2) && !strcmp(C_STR(p, p1), "empty_args")) {
			if (!strcmp(C_STR(p, p2), "true") || !strcmp(C_STR(p, p2), "on"))
				p->m->flags.empty_args = true;
			else if (!strcmp(C_STR(p, p2), "false") || !strcmp(C_STR(p, p2), "off"))
				p->m->flags.empty_args = false;
		} else {
			// Anything else - tabling, global_bb, the tabling restraint
			// flags (integer-valued, so is_interned(p2) alone routes
			// them here) - has no parse-time meaning. Run the directive
			// as an ordinary goal instead of discarding it: without
			// this a directive-form set_prolog_flag/2 for any flag not
			// in the list above was a silent no-op, never forwarded to
			// the runtime set_prolog_flag/2 that actually implements it.

			bool raised = false;

			if (!goal_run_reporting(p, c, &raised)
				&& !p->internal && !p->do_read_term && !p->pl->quiet) {
				fflush(stdout);
				fprintf(stderr, "Warning: directive %s: %s/%u, %s:%d\n",
					raised ? "raised an exception" : "failed",
					dirname, (unsigned)get_arity(c),
					get_loaded(p->m, p->m->filename), p->line_num);
			}
		}

		p->flags = p->m->flags;
		return true;
	}

	if (!strcmp(dirname, "op") && (get_arity(c) == 3)) {
		do_op(p, c, false);
		return true;
	}

	if (is_iso_list(p1)) {
		PROLOG_LIST_HANDLER(p1);

		while (is_list(p1)) {
			cell *h = PROLOG_LIST_HEAD(p1);

			if (is_interned(h) && (!strcmp(C_STR(p, h), "/") || !strcmp(C_STR(p, h), "//")) && (get_arity(h) == 2)) {
				cell *c_name = h + 1;

				if (is_var(c_name)) {
					if (((!p->do_read_term)) && !p->pl->quiet)
						fprintf(stderr, "Error: uninstantiated: %s/%d\n", dirname, get_arity(c));

					p->error = true;
					return true;
				}

				if (!is_atom(c_name)) {
					fprintf(stderr, "Error: predicate-indicator %s, %s:%d\n", p->m->name, get_loaded(p->m, p->m->filename), p->line_num);
					p->error = true;
					return true;
				}

				cell *c_arity = h + 2;

				if (!is_integer(c_arity)) {
					fprintf(stderr, "Error: predicate-indicator %s, %s:%d\n", p->m->name, get_loaded(p->m, p->m->filename), p->line_num);
					p->error = true;
					return true;
				}

				unsigned arity = get_smallint(c_arity);

				if (!strcmp(C_STR(p, h), "//"))
					arity += 2;

				cell tmp = *c_name;
				set_arity(&tmp, arity);

				if (!strcmp(dirname, "dynamic")) {
					predicate * pr = find_predicate(p->m, &tmp);

					if (pr && !pr->is_dynamic && pr->head) {
						if (!p->do_read_term)
							fprintf(stderr, "Error: no permission to modify static predicate %s:%s/%u, %s:%d\n", p->m->name, C_STR(p->m, c_name), arity, get_loaded(p->m, p->m->filename), p->line_num);

						p->error = true;
						return true;
					}

					set_dynamic_in_db(p->m, C_STR(p, c_name), arity);
					p->error = p->m->error;
				} else if (!strcmp(dirname, "encoding")) {
				} else if (!strcmp(dirname, "public")) {
				} else if (!strcmp(dirname, "export")) {
				} else if (!strcmp(dirname, "discontiguous")) {
					set_discontiguous_in_db(p->m, C_STR(p, c_name), arity);
					p->error = p->m->error;
				} else if (!strcmp(dirname, "multifile")) {
					const char *src = C_STR(p, c_name);

					if (strcmp(src, ":")) {
						set_multifile_in_db(p->m, src, arity);
						p->error = p->m->error;
					} else {
						// multifile(:(mod,/(name,arity)))
						cell *c_mod = c_name + 1;				// FIXME: verify
						cell *c_slash = c_name + 2;				// FIXME: verify
						cell *c_functor = c_slash + 1;			// FIXME: verify
						cell *c_arity = c_slash + 2;			// FIXME: verify
						const char *mod = C_STR(p, c_mod);
						const char *name = C_STR(p, c_functor);
						arity = get_smalluint(c_arity);

						if (!strcmp(C_STR(p, c_slash), "//"))
							arity += 2;

						if (!is_multifile_in_db(p->pl, mod, name, arity)) {
							if (!p->do_read_term)
								fprintf(stderr, "Error: not multifile %s:%s/%u\n", mod, name, arity);

							p->error = true;
							return true;
						}
					}
				} else {
					report_unknown_directive(p, "Error", dirname, get_arity(c));
					p->error = true;
					return true;
				}
			}

			p1 = PROLOG_LIST_TAIL(p1);
		}
	}

#if 0
	if (is_nil(p1))
		return true;
#endif

	if (is_var(p1)) {
		if (((!p->do_read_term)) && !p->pl->quiet)
			fprintf(stderr, "Error: uninstantiated: %s/%d\n", dirname, get_arity(c));

		p->error = true;
		return true;
	}

	// Bound the walk by the extent of this directive. Without it the
	// loop runs on into whatever cells happen to follow, which is how
	// a conjunct came to be read as a predicate indicator of the
	// conjunct before it...

	const cell *c_end = c + c->num_cells;

	while ((p1 < c_end) && is_interned(p1) && !is_nil(p1) && (p1->val_off != g_dot_s)) {
		module *m = p->m;
		cell *c_id = p1;

		if (!strcmp(C_STR(p, p1), ":") && (get_arity(p1) == 2)) {
			cell *c_mod = p1 + 1;

			if (!is_atom(c_mod))
				return true;

			m = find_module(p->pl, C_STR(p, c_mod));

			if (!m)
				m = module_create(p->pl, C_STR(p, c_mod));

			c_id = p1 + 2;
		}

		if ((!strcmp(C_STR(p, c_id), "/") || !strcmp(C_STR(p, c_id), "//"))
			&& (get_arity(p1) == 2)) {
			cell *c_name = c_id + 1;

			if (is_var(c_name)) {
				if (((!p->do_read_term)) && !p->pl->quiet)
					fprintf(stderr, "Error: uninstantiated: %s/%d\n", dirname, get_arity(c));

				p->error = true;
				return true;
			}

			if (!is_atom(c_name)) {
				fprintf(stderr, "Error: predicate-indicator %s, %s:%d\n", p->m->name, get_loaded(p->m, p->m->filename), p->line_num);
				p->error = true;
				return true;
			}

			cell *c_arity = c_id + 2;

			if (!is_integer(c_arity)) {
				fprintf(stderr, "Error: predicate-indicator %s, %s:%d\n", p->m->name, get_loaded(p->m, p->m->filename), p->line_num);
				p->error = true;
				return true;
			}

			unsigned arity = get_smallint(c_arity);
			cell tmp = *c_name;
			set_arity(&tmp, arity);


			if (!strcmp(C_STR(p, c_id), "//"))
				arity += 2;

			if (!strcmp(dirname, "multifile")) {
				set_multifile_in_db(m, C_STR(p, c_name), arity);
				p->error = m->error;
			} else if (!strcmp(dirname, "discontiguous")) {
				set_discontiguous_in_db(m, C_STR(p, c_name), arity);
				p->error = m->error;
			} else if (!strcmp(dirname, "public"))
				;
			else if (!strcmp(dirname, "export"))
				;
			else if (!strcmp(dirname, "dynamic")) {
				predicate * pr = find_predicate(p->m, &tmp);

				if (pr && !pr->is_dynamic && pr->head) {
					if (!p->do_read_term)
						fprintf(stderr, "Error: no permission to modify static predicate %s:%s/%u, %s:%d\n", m->name, C_STR(p->m, c_name), arity, get_loaded(p->m, p->m->filename), p->line_num);

					p->error = true;
					return true;
				}

				set_dynamic_in_db(m, C_STR(p, c_name), arity);
				p->error = m->error;
			} else {
				report_unknown_directive(p, "Error", dirname, get_arity(c));
				p->error = true;
				return true;
			}

			p1 += p1->num_cells;
		} else if (!strcmp(dirname, "create_prolog_flag")) {
			p1 += 1;
		} else if (!strcmp(dirname, "encoding")) {
			p1 += 1;
		} else if (!strcmp(dirname, "meta_predicate")) {
			if (p1->val_off == g_conjunction_s)
				p1 += 1;

			set_meta_predicate_in_db(m, p1);
			p->error = m->error;
			p1 += p1->num_cells;
		} else if (!strcmp(C_STR(p, p1), ",") && (get_arity(p1) == 2))
			p1 += 1;
		else {
			report_unknown_directive(p, "Warning", dirname, get_arity(c));
			return true;
		}
	}

	if (!strcmp(dirname, "meta_predicate") && (get_arity(c) == 1))
		return true;

	if (!strcmp(dirname, "dynamic") && (get_arity(c) == 1))
		return true;

	if (!strcmp(dirname, "discontiguous") && (get_arity(c) == 1))
		return true;

	if (!strcmp(dirname, "multifile") && (get_arity(c) == 1))
		return true;

	report_unknown_directive(p, "Warning", dirname, get_arity(c));
	return true;
}

// Mark terms immediately followed by ! in a ,/2 chain. Conjunction is
// xfy, so bodies nest as (A, (B, !)) — tag each goal whose next is cut.
// Also walk ;/2, ->/2, *->/2 and if/3 so cuts inside those arms are seen.

static bool starts_with_cut(const cell *c)
{
	if (!is_interned(c))
		return false;

	if (c->val_off == g_cut_s)
		return true;

	if ((get_arity(c) == 2) && (c->val_off == g_conjunction_s))
		return starts_with_cut(c + 1);

	return false;
}

static void mark_next_cut(cell *c)
{
	if (!is_interned(c) || !get_arity(c))
		return;

	cell *arg1 = c + 1;
	cell *arg2 = arg1 + arg1->num_cells;

	if ((c->val_off == g_conjunction_s) && (get_arity(c) == 2)) {
		if (starts_with_cut(arg2))
			arg1->flags |= FLAG_INTERNED_NEXT_CUT;

		mark_next_cut(arg1);
		mark_next_cut(arg2);
		return;
	}

	if ((c->val_off == g_disjunction_s) && (get_arity(c) == 2)) {
		mark_next_cut(arg1);
		mark_next_cut(arg2);
		return;
	}

	if (((c->val_off == g_if_then_s) || (c->val_off == g_soft_cut_s))
		&& (get_arity(c) == 2)) {
		mark_next_cut(arg1);
		mark_next_cut(arg2);
		return;
	}

	if ((c->val_off == g_if_s) && (get_arity(c) == 3)) {
		cell *arg3 = arg2 + arg2->num_cells;
		mark_next_cut(arg1);
		mark_next_cut(arg2);
		mark_next_cut(arg3);
		return;
	}
}

static void check_first_cut(clause *cl)
{
	cell *c = get_body(cl->cells);

	if (!c)
		return;

	mark_next_cut(c);

	if (c->val_off == g_cut_s) {
		cl->is_first_cut = true;
		return;
	}

	if (c->val_off == g_conjunction_s) {
		c += 1;

		if (c->val_off == g_cut_s) {
			cl->is_first_cut = true;
			return;
		}
	}
}

// Room for entry i, and for `need` bytes of name pool. Both double from
// small and stop at the limits the errors below already enforce, so a clause
// with four variables carries four entries rather than a thousand.

static bool vartab_entry_room(parser *p, unsigned i)
{
	if (i < p->vartab.alloc)
		return true;

	if (i >= MAX_VARS)
		return false;

	unsigned wanted = p->vartab.alloc ? p->vartab.alloc : 16;

	while (wanted <= i)
		wanted *= 2;

	if (wanted > MAX_VARS)
		wanted = MAX_VARS;

	var_entry *v = TPL_realloc(p->vartab.v, wanted * sizeof(var_entry));

	if (!v)
		return false;

	memset(v + p->vartab.alloc, 0,
		(wanted - p->vartab.alloc) * sizeof(var_entry));
	p->vartab.v = v;
	p->vartab.alloc = wanted;
	return true;
}

// The pool is scanned until a NUL, so anything new has to arrive zeroed.

static bool vartab_pool_room(parser *p, size_t need)
{
	if (need <= p->vartab.pool_size)
		return true;

	if (need > MAX_VAR_POOL_SIZE)
		return false;

	size_t wanted = p->vartab.pool_size ? p->vartab.pool_size : 256;

	while (wanted < need)
		wanted *= 2;

	if (wanted > MAX_VAR_POOL_SIZE)
		wanted = MAX_VAR_POOL_SIZE;

	char *pool = TPL_realloc(p->vartab.pool, wanted);

	if (!pool)
		return false;

	memset(pool + p->vartab.pool_size, 0, wanted - p->vartab.pool_size);
	p->vartab.pool = pool;
	p->vartab.pool_size = wanted;
	return true;
}

// Records a name offset for a variable, growing to fit. Silently does
// nothing past MAX_VARS: the caller that matters has already raised
// "too many vars", and the rest only ever clear an entry.

static void vartab_set_off(parser *p, unsigned i, pl_idx off)
{
	if (vartab_entry_room(p, i))
		p->vartab.v[i].off = off;
}

// Reset empties the table without giving the memory back: a consult runs
// thousands of clauses through one parser, and freeing per clause would
// trade a fixed cost for a churning one.

static void vartab_reset(parser *p)
{
	if (p->vartab.pool)
		memset(p->vartab.pool, 0, p->vartab.pool_size);

	if (p->vartab.v)
		memset(p->vartab.v, 0, p->vartab.alloc * sizeof(var_entry));

	p->vartab.num_vars = 0;
}

static void vartab_free(parser *p)
{
	TPL_free(p->vartab.pool);
	TPL_free(p->vartab.v);
	memset(&p->vartab, 0, sizeof(p->vartab));
}

// Goal expansion hands the table to a sub-parser and takes it back, which
// was a struct assignment when the table was inline. It has to be a real
// copy: the two parsers go on to be destroyed independently, and one error
// path destroys the sub-parser without ever copying back.

static bool vartab_copy(parser *dst, const parser *src)
{
	vartab_free(dst);

	if (src->vartab.pool_size) {
		dst->vartab.pool = TPL_malloc(src->vartab.pool_size);

		if (!dst->vartab.pool)
			return false;

		memcpy(dst->vartab.pool, src->vartab.pool, src->vartab.pool_size);
		dst->vartab.pool_size = src->vartab.pool_size;
	}

	if (src->vartab.alloc) {
		dst->vartab.v = TPL_malloc(src->vartab.alloc * sizeof(var_entry));

		if (!dst->vartab.v)
			return false;

		memcpy(dst->vartab.v, src->vartab.v,
			src->vartab.alloc * sizeof(var_entry));
		dst->vartab.alloc = src->vartab.alloc;
	}

	dst->vartab.num_vars = src->vartab.num_vars;
	return true;
}

static pl_idx get_varno(parser *p, const char *src, bool in_body, unsigned depth)
{
	int anon = !strcmp(src, "_");
	size_t offset = 0;
	unsigned i = 0, nesting_offset = p->is_consulting ? 0 : 1;

	// The walk is bounded by both the pool AND MAX_VARS. The pool holds
	// MAX_VAR_POOL_SIZE bytes, so with short names it can describe far
	// more variables than the parallel arrays have room for - 16000
	// bytes of "_G1\0" is about 4000 against MAX_VARS of 1024 - and
	// every one of in_head[i], in_body[i], depth[i], off[i], vars[i]
	// was being written at that unbounded i.

	while ((offset < p->vartab.pool_size) && (i < MAX_VARS)
		&& p->vartab.pool[offset]) {
		if (!strcmp(p->vartab.pool+offset, src) && !anon) {
			if (in_body)
				p->vartab.v[i].in_body++;
			else
				p->vartab.v[i].in_head++;

			if (depth > p->vartab.v[i].depth)
				p->vartab.v[i].depth = depth - nesting_offset;

			return i;
		}

		offset += strlen(p->vartab.pool+offset) + 1;
		i++;
	}

	if (i >= MAX_VARS) {
		fprintf(stderr, "Error: too many vars, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);
		p->error = true;
		return 0;
	}

	size_t len = strlen(src);

	if ((offset+len+1) >= MAX_VAR_POOL_SIZE) {
		fprintf(stderr, "Error: var pool exhausted, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);
		p->error = true;
		return 0;
	}

	// One past the name's own NUL, so the scan still finds a terminator.
	if (!vartab_pool_room(p, offset+len+2) || !vartab_entry_room(p, i)) {
		fprintf(stderr, "Error: out of memory, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);
		p->error = true;
		return 0;
	}

	memcpy(p->vartab.pool+offset, src, len+1);

	if (in_body)
		p->vartab.v[i].in_body++;
	else
		p->vartab.v[i].in_head++;

	if (depth > p->vartab.v[i].depth)
		p->vartab.v[i].depth = depth - nesting_offset;

	p->vartab.num_vars++;
	return i;
}

static unsigned get_in_head(parser *p, const char *name)
{
	bool anon = !strcmp(name, "_");
	size_t offset = 0;
	unsigned i = 0;

	while ((offset < p->vartab.pool_size) && (i < MAX_VARS)
		&& p->vartab.pool[offset]) {
		if (!strcmp(p->vartab.pool+offset, name) && !anon) {
			return p->vartab.v[i].in_head;
		}

		offset += strlen(p->vartab.pool+offset) + 1;
		i++;
	}

	return 0;
}

static unsigned get_in_body(parser *p, const char *name)
{
	bool anon = !strcmp(name, "_");
	size_t offset = 0;
	unsigned i = 0;

	while ((offset < p->vartab.pool_size) && (i < MAX_VARS)
		&& p->vartab.pool[offset]) {
		if (!strcmp(p->vartab.pool+offset, name) && !anon) {
			return p->vartab.v[i].in_body;
		}

		offset += strlen(p->vartab.pool+offset) + 1;
		i++;
	}

	return 0;
}

void assign_vars(parser *p, unsigned start, bool rebase)
{
	if (!p || p->error)
		return;

	clause *cl = p->cl;
	cl->is_first_cut = false;
	p->start_term = true;

	if (!p->reuse) {
		vartab_reset(p);
		cl->num_vars = 0;
		p->num_vars = 0;
	}

	// Assign body variables first (why?)...

	const cell *body = get_body(cl->cells);
	bool in_body = p->in_body;

	for (unsigned i = 0; i < cl->cidx; i++) {
		cell *c = cl->cells + i;

		if (c == body)
			in_body = true;

		if (!in_body)
			continue;

		if (!is_var(c))
			continue;

		if (rebase) {
			char tmpbuf[20];
			snprintf(tmpbuf, sizeof(tmpbuf), "___V%u", c->var_num);
			c->var_num = get_varno(p, tmpbuf, in_body, c->var_num);
		} else
			c->var_num = get_varno(p, C_STR(p, c), in_body, c->var_num);

		c->var_num += start;

		if (c->var_num == MAX_VARS) {
			fprintf(stderr, "Error: max vars reached, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);
			p->error = true;
			return;
		}

		vartab_set_off(p, c->var_num, c->val_off);
		if (vartab_entry_room(p, c->var_num))
			p->vartab.v[c->var_num].used++;
	}

	// ... then the head...

	in_body = p->in_body;

	for (unsigned i = 0; i < cl->cidx; i++) {
		cell *c = cl->cells + i;

		if (c == body)
			in_body = true;

		if (in_body)
			break;

		if (!is_var(c))
			continue;

		if (rebase) {
			char tmpbuf[20];
			snprintf(tmpbuf, sizeof(tmpbuf), "___V%u", c->var_num);
			c->var_num = get_varno(p, tmpbuf, in_body, c->var_num);
		} else
			c->var_num = get_varno(p, C_STR(p, c), in_body, c->var_num);

		c->var_num += start;

		if (c->var_num == MAX_VARS) {
			fprintf(stderr, "Error: max vars reached, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);
			p->error = true;
			return;
		}

		vartab_set_off(p, c->var_num, c->val_off);
		if (vartab_entry_room(p, c->var_num))
			p->vartab.v[c->var_num].used++;
	}

	cl->num_vars = p->vartab.num_vars;
	p->num_vars = p->vartab.num_vars;

	// Now set flags...

	for (unsigned i = 0; i < cl->cidx; i++) {
		cell *c = cl->cells + i;

		if (!is_var(c))
			continue;

		// A ref is a runtime variable: val_off is NOT an offset into
		// the global atom table, so C_STR() on it reads wherever that
		// happens to land. ASan on a Logtalk load: 16 bytes past a
		// 144000-byte atom table, faulting in get_in_head()'s first
		// strcmp. The flag is the only thing distinguishing a ref from
		// a named var, and it was being cleared one line before the
		// name was used.
		//
		// A ref carries no source name, so there is nothing to count
		// occurrences of. Classify it global - the conservative answer,
		// costing an optimisation rather than risking a wrong one.

		bool was_ref = is_ref(c);
		c->flags &= ~FLAG_VAR_REF;

		if (was_ref) {
			c->flags |= FLAG_VAR_GLOBAL;
			continue;
		}

		if (c->val_off == g_anon_s)
			c->flags |= FLAG_VAR_ANON;

		unsigned var_in_head = get_in_head(p, C_STR(p, c));
		unsigned var_in_body = get_in_body(p, C_STR(p, c));
		unsigned occurrances = var_in_head + var_in_body;
		bool var_is_global = is_global(c);

		if (var_in_head && (p->vartab.v[c->var_num].depth > 1)) {
			var_is_global = true;
		} else if (var_in_body && (p->vartab.v[c->var_num].depth > 1)) {
			var_is_global = true;
		}

		if (!occurrances)		// Anonymous vars weren't
			occurrances = 1;	// counted it seems

		if (var_is_global) {
			c->flags |= FLAG_VAR_GLOBAL;
		} else {
			if (occurrances == 1)
				c->flags |= FLAG_VAR_VOID;
			else if (!var_in_body)
				c->flags |= FLAG_VAR_TEMPORARY;
			else if (var_in_body)
				c->flags |= FLAG_VAR_LOCAL;
		}
	}

	for (unsigned i = 0; i < cl->num_vars; i++) {
		if (p->is_consulting && !p->do_read_term && ((i < p->vartab.alloc) && (p->vartab.v[i].used == 1))
			// && (p->vartab.name[i][strlen(p->vartab.name[i])-1] != '_')
			&& (GET_POOL(p, vartab_off(p, i))[0] != '_')) {
			if (!p->pl->quiet
				&& !((cl->cells->val_off == g_neck_s) && get_arity(cl->cells) == 1)
				&& !((cl->cells->val_off == g_quad_s) && ((get_arity(cl->cells) == 1) || (get_arity(cl->cells) == 2)))
				&& !p->m->in_quad)
				fprintf(stderr, "Warning: singleton: %s, near %s:%d\n", GET_POOL(p, vartab_off(p, i)), get_loaded(p->m, p->m->filename), p->line_num);
		}
	}

	cell *c = make_a_cell(p);
	ENSURE(c);
	c->tag = TAG_END;
	c->num_cells = 1;
}

// We have 'lhs || rhs', so:
// Build lhs (a string) into a new list
// append rhs as the new list tail
// replace 'lhs || rhs' with new list

static void replace_double_bar(parser *p, pl_idx i, pl_idx last_idx)
{
	cell *c = p->cl->cells + i;
	cell *lhs = p->cl->cells + last_idx;
	cell *rhs = c + 1;

	// Build lhs into a list and append rhs + nil

	if (is_nil(lhs)) {
		memmove(lhs, rhs, (p->cl->cidx-(rhs-p->cl->cells))*sizeof(cell));
		p->cl->cidx -= 2;  // lhs + ||
	} else {
		char *src = C_STR(p, lhs);
		query *q = query_create(p->m);
		cell *l = string_to_chars_list(q, lhs);
		unshare_cells(lhs, lhs->num_cells);
		cell *tmp = TPL_calloc((l->num_cells-1)+rhs->num_cells+1, sizeof(cell));
		cell *tmp2 = tmp;
		tmp2 += copy_cells(tmp, l, l->num_cells-1);
		tmp->num_cells -= 1;
		tmp2 += dup_cells(tmp2, rhs, rhs->num_cells);
		tmp->num_cells += rhs->num_cells;
		*tmp2 = *make_nil();
		tmp->num_cells += 1;

		// Make room then copy

		unsigned tot_cells = lhs->num_cells+c->num_cells+rhs->num_cells;
		unsigned extra_cells = tmp->num_cells - tot_cells;
		//printf("*** tot_cells = %u, extra_cells = %u\n", tot_cells, extra_cells);

		make_room(p, extra_cells);
		c = p->cl->cells + i;
		lhs = p->cl->cells + last_idx;
		rhs = c + 1;

		cell *end = rhs + rhs->num_cells;
		memmove(end+extra_cells, end, (p->cl->cidx - (end - p->cl->cells))*sizeof(cell));
		memmove(lhs, tmp, tmp->num_cells*sizeof(cell));

		p->cl->cidx += extra_cells;
		TPL_free(tmp);
		query_destroy(q);
	}
}

// Reduce a vector of cells in token order to a parse tree. This is
// done in two passes: first find the lowest priority unapplied
// operator then apply args to that operator. This works by swapping
// in place the args eg. 'arg1 op arg2' becomes 'op arg1 arg2' in
// the resultant vector. This is done as long as necessary for the
// complete vector to build a term representing a clause.

static bool reduce(parser *p, pl_idx start_idx, bool last_op)
{
	pl_idx lowest = IDX_MAX, work_idx, end_idx = p->cl->cidx - 1;
	bool do_work = false, bind_le = false;

	for (pl_idx i = start_idx; i < p->cl->cidx;) {
		cell *c = p->cl->cells + i;

		if ((c->num_cells > 1) || !is_interned(c) || !c->priority) {
			i += c->num_cells;
			continue;
		}

#if 0
		if (!p->is_consulting)
			printf("*** OP1 start=%u '%s' type=%u, specifier=%u, pri=%u, last_op=%d, is_op=%d\n", start_idx, C_STR(p, c), c->tag, GET_OP(c), c->priority, last_op, IS_OP(c));
#endif

		if ((i == start_idx) && (i == end_idx)) {
			c->priority = 0;
			i++;
			continue;
		}

		if (bind_le ? c->priority <= lowest : c->priority < lowest) {
			lowest = c->priority;
			work_idx = i;
			do_work = true;
		}

		bind_le = is_xfy(c) || is_fy(c) ? true || is_yf(c): false;
		i++;
	}

	if (!do_work)
		return false;

	pl_idx last_idx = IDX_MAX;

	for (pl_idx i = start_idx; i <= end_idx;) {
		cell *c = p->cl->cells + i;

		if ((c->num_cells > 1) || !is_interned(c) || !c->priority) {
			last_idx = i;
			i += c->num_cells;
			continue;
		}

		if ((c->priority != lowest) || (i != work_idx)) {
			last_idx = i;
			i += c->num_cells;
			continue;
		}

#if 0
		if (!p->is_consulting)
			printf("*** OP2 last=%u/start=%u '%s' type=%u, specifier=%u, pri=%u, last_op=%d, is_op=%d\n", last_idx, start_idx, C_STR(p, c), c->tag, GET_OP(c), c->priority, last_op, IS_OP(c));
#endif

		set_arity(c, 1);

		// Prefix...

		if (is_fx(c)) {
			const cell *rhs = c + 1;

			if (is_fx(rhs) && !get_arity(rhs) && (rhs->priority == c->priority)) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, operator clash, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "operator_clash";
				p->error = true;
				return false;
			}

			rhs += rhs->num_cells;

			if ((((pl_idx)(rhs - p->cl->cells)) < end_idx)
				&& is_xf(rhs) && (rhs->priority == c->priority)) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, operator clash, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "operator_clash";
				p->error = true;
				return false;
			}
		}

		if (is_prefix(c)) {
			cell *rhs = c + 1;

			if (is_infix(rhs) && !get_arity(rhs) && (rhs->priority > c->priority)) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, operator clash, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "operator_clash";
				p->error = true;
				return false;
			}

			if (is_prefix(rhs) && !get_arity(rhs) && (rhs->priority > c->priority)) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, operator clash, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "operator_clash";
				p->error = true;
				return false;
			}
		}

		if (is_prefix(c)) {
			const cell *rhs = c + 1;
			pl_idx off = (pl_idx)(rhs - p->cl->cells);

			if (off > end_idx) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, missing operand to prefix, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "operand_missing";
				p->error = true;
				return false;
			}

			c->num_cells += rhs->num_cells;
			break;
		}

		// Postfix...

		cell *rhs = c + 1;

		if (is_xf(rhs) && (rhs->priority == c->priority)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, operator clash, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "operator_clash";
			p->error = true;
			return false;
		}

		if (is_prefix(rhs) && !get_arity(rhs) && (rhs->priority > c->priority)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, operator clash, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "operator_clash";
			p->error = true;
			return false;
		}

		cell save = *c;

		if (is_postfix(c)) {
			pl_idx off = last_idx;

			if (off > end_idx) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, missing operand to postfix, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "operand_missing";
				p->error = true;
				return false;
			}

			cell *lhs = p->cl->cells + last_idx;
			save.num_cells += lhs->num_cells;
			int cells_to_move = lhs->num_cells;
			cell *save_c = lhs;
			const cell *src = c - 1;
			cell *dst = c;

			while (cells_to_move--)
				*dst-- = *src--;

			*save_c = save;
			break;
		}

		// Infix...

		if (c->val_off == g_double_bar_s) {
			replace_double_bar(p, i, last_idx);
			break;
		}

		if (is_infix(rhs) && !get_arity(rhs)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, operator clash, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "operator_clash";
			p->error = true;
			return false;
		}

		pl_idx off = (pl_idx)(rhs - p->cl->cells);
		bool nolhs = last_idx == IDX_MAX;

		if (i == start_idx) nolhs = true;

		if (nolhs || (off > end_idx)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, missing operand to infix, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "operand_missing";
			p->error = true;
			return false;
		}

		cell *lhs = p->cl->cells + last_idx;

		if (is_infix(lhs) && !get_arity(lhs)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, operator clash, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "operator_clash";
			p->error = true;
			return false;
		}

		save.num_cells += lhs->num_cells;
		int cells_to_move = lhs->num_cells;
		lhs = c - 1;

		while (cells_to_move--)
			*c-- = *lhs--;

		*c = save;
		c->num_cells += rhs->num_cells;
		set_arity(c, 2);

		if (is_xfx(c)) {
			cell *next = c + c->num_cells;
			i = next - p->cl->cells;

			if ((i <= end_idx)
				&& (is_xfx(next))
				&& (next->priority == c->priority)) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, operator clash, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "operator_clash";
				p->error = true;
				return false;
			}
		}

		c = p->cl->cells + last_idx;
		lhs = c + 1;
		rhs = lhs + lhs->num_cells;

		if (is_var(lhs) && (c->val_off == g_eq_s)) {
			c = rhs;

			for (unsigned i = 0; i < rhs->num_cells; i++, c++) {
				if (is_var(c))
					c->flags |= FLAG_VAR_GLOBAL;
			}
		}

		break;
	}

	return true;
}

// Stop when no more reductions to do.

static bool analyze(parser *p, pl_idx start_idx, bool last_op)
{
	while (reduce(p, start_idx, last_op))
		;

	return !p->error;
}

static bool term_expansion(parser *p);

static bool term_expansion(parser *p)
{
	if (p->error || p->internal || !is_interned(p->cl->cells))
		return false;

	cell *c = p->cl->cells;

	c = p->cl->cells;
	module *m = p->m;
	predicate *pr = find_functor(m, "term_expansion", 2);

	if (!pr || !pr->head) {
		m = p->pl->user_m;
		pr = find_functor(m, "term_expansion", 2);
	}

	if (!pr || !pr->head)
		return false;

	cell *h = get_head(c);

	//if (h->val_off == g_term_expansion_s)
	//	return false;

	// Module-qualified heads: skip expansion for hook installations
	// (M:term_expansion/M:goal_expansion clauses would otherwise feed
	// back into the expander), but let ordinary M:Head clauses through -
	// eg. clauses of tabled predicates need the rename expansion.

	if (h->val_off == g_colon_s) {
		cell *qh = h + 1;
		qh = qh + qh->num_cells;

		if (is_interned(qh) && ((qh->val_off == g_term_expansion_s)
			|| !CMP_STRING_TO_CSTR(p, qh, "goal_expansion")))
			return false;
	}

	query *q = query_create(m);
	check_error(q);
	q->trace = false;
	cell *tmp = alloc_heap(q, 1+c->num_cells+2);
	unsigned num_cells = 0;
	make_instr(tmp+num_cells++, g_term_expansion_s, NULL, 2, c->num_cells+1);
	dup_cells(tmp+num_cells, c, c->num_cells);
	num_cells += c->num_cells;
	make_ref(tmp+num_cells++, p->cl->num_vars, 0);
	make_end(tmp+num_cells);
	execute(q, tmp, p->cl->num_vars+1);

	if (q->retry != QUERY_OK) {
		query_destroy(q);
		return false;
	}

	cell *arg1 = tmp + 1;
	cell *arg2 = arg1 + arg1->num_cells;
	c = deref(q, arg2, 0);
	q->max_depth = -1;
	char *src = print_canonical_to_strbuf(q, c, q->latest_ctx, 1);

	if (!src) {
		query_destroy(q);
		p->error = true;
		return false;
	}

	//fprintf(stderr, "+++ term_expansion %s/%u ==> ", C_STR(p, h), get_arity(h));

	strcat(src, ".");
	parser *p2 = parser_create(p->m);
	check_error(p2);
	p2->srcptr = src;
	p2->expand_dcg = true;		// a --> result gets translated (issue #1142)
	tokenize(p2, false, false);

	if (p2->error) {
		parser_destroy(p2);
		query_destroy(q);
		TPL_free(src);	// FIX: free leaked src
		p->error = true;
		return false;
	}

	process_clause(p2->m, p2->cl, NULL);
	TPL_free(src);

	clear_clause(p->cl);
	TPL_free(p->cl);
	p->cl = p2->cl;					// Take the completed clause
	p->num_vars = p2->num_vars;
	p2->cl = NULL;

	parser_destroy(p2);
	//DUMP_TERM("old", get_head(p->cl->cells), 0, 1);
	query_destroy(q);

	return term_expansion(p);
}

static void fixup_expansion_var_collisions(cell *goal, unsigned num_vars_before, parser *p2)
{
	bool in_goal[MAX_VARS] = {0};

	for (cell *c = goal, *end = goal + goal->num_cells; c < end; c++) {
		if (is_var(c) && (c->var_num < MAX_VARS))
			in_goal[c->var_num] = true;
	}

	pl_idx remap_from[MAX_VARS], remap_to[MAX_VARS];
	unsigned nremaps = 0;

	for (pl_idx i = 0; i < p2->cl->cidx; i++) {
		cell *c = p2->cl->cells + i;

		if (!is_var(c))
			continue;

		if (c->var_num >= num_vars_before)
			continue;

		if ((c->var_num < MAX_VARS) && in_goal[c->var_num])
			continue;

		unsigned found = nremaps;

		for (unsigned j = 0; j < nremaps; j++) {
			if (remap_from[j] == c->var_num) {
				found = j;
				break;
			}
		}

		if (found == nremaps) {
			if (nremaps >= MAX_VARS)
				continue;

			remap_from[nremaps] = c->var_num;
			remap_to[nremaps] = (pl_idx)p2->cl->num_vars++;
			nremaps++;
		}

		c->var_num = remap_to[found];

		if (c->var_num < MAX_VARS)
			vartab_set_off(p2, c->var_num, 0);
	}
}


static cell *goal_expansion_(parser *p, cell *goal);

// goal_expansion() and term_to_body_conversion() call each other: an
// expansion's output is re-walked as a body, and any goal in it is
// expanded again. A goal that reintroduces itself therefore recurses
// through BOTH functions, so the guard has to be nesting depth held on the
// prolog instance - each expansion runs in a NEW parser, so a counter
// on the parser resets every round too.
//
// Issue #900: goal_expansion(p(B), B) :- b((m,p(_))). The expansion
// changes the goal every time, so a fixpoint check alone never fires;
// the C stack gave out at ~5500 levels.

// Each level allocates a parser AND a query, so the C stack gives out
// somewhere past 750 levels. Real chains are two or three deep - dcgs,
// clpz and atts all expand once - so this leaves ample headroom while
// staying an order of magnitude below the stack limit.

#define MAX_GOAL_EXPANSIONS 64

static cell *goal_expansion(parser *p, cell *goal)
{
	if (p->pl->goal_expansions >= MAX_GOAL_EXPANSIONS) {
		if (!p->error) {
			fprintf(stderr, "Error: goal_expansion/2 did not terminate, %s:%d\n",
				get_loaded(p->m, p->m->filename), p->line_num);
			p->error_desc = "goal_expansion_limit";
			p->error = true;
		}

		return goal;
	}

	p->pl->goal_expansions++;
	cell *ret = goal_expansion_(p, goal);
	p->pl->goal_expansions--;
	return ret;
}

static cell *goal_expansion_(parser *p, cell *goal)
{
	if (p->error || p->internal || !is_interned(goal) || !is_callable(goal))
		return goal;

	if ((goal->val_off == g_goal_expansion_s) && (get_arity(goal) == 2))
		return goal;

	if (goal->val_off == g_cut_s)
		return goal;

	if (get_builtin_term(p->m, goal, NULL, NULL) /*|| is_op(goal)*/)
		return goal;

	if (!search_goal_expansion(p->m, goal))
		return goal;

	if (!CMP_STRING_TO_CSTR(p, goal, "phrase") && !p->is_consulting)
		return goal;

	//if (search_predicate(p->m, goal))
	//	return goal;

	if (p->pl->in_goal_expansion) {
		//printf("??? goal_expansion %s/%u\n", C_STR(p, goal), get_arity(goal));
		return goal;
	}

	//printf("*** [%s] goal_expansion %s/%u, p->is_command=%d\n", p->m->name, C_STR(p, goal), get_arity(goal), p->is_command);

	// Scryer-compatible: user:goal_expansion/2 is registered in `user`, but
	// each module has its own (empty) goal_expansion/2 that shadows it. When
	// the hook lives only in `user`, run the expansion query in user_m so the
	// unqualified call resolves it (no need to export goal_expansion/2).
	module *exp_m = p->m;
	if (!find_goal_expansion(p->m, goal) && p->pl->user_m && find_goal_expansion_specific(p->pl->user_m, goal))
		exp_m = p->pl->user_m;

	query *q = query_create(exp_m);
	check_error(q);
	q->trace = false;
	q->varnames = true;
	q->max_depth = -1;
	char *dst = print_canonical_to_strbuf(q, goal, 0, 0);
	q->varnames = false;
	SB(s);
	SB_sprintf(s, "goal_expansion((%s),_TermOut), !.", dst);
	TPL_free(dst);

	//DUMP_TERM("old", p->cl->cells, 0, 0);

	// Note: since only parsing goals we need to preserve
	// the varnames so they get reused. Only genuinely new
	// variables should create anew. Hence we pull the
	// vartab from the main parser... IS THIS TRUE?

	//printf("+++ goal_expansion %s/%u\n", C_STR(p, goal), get_arity(goal));
	p->pl->in_goal_expansion = true;
	const unsigned num_vars_before = p->cl->num_vars;
	parser *p2 = parser_create(exp_m);
	check_error(p2, query_destroy(q));
	q->top = p2;
	p2->cl->num_vars = p->cl->num_vars;
	if (!vartab_copy(p2, p)) { p->error = true; }
	p2->reuse = true;
	p2->line_num = p->line_num;
	p2->skip = true;
	p2->srcptr = SB_cstr(s);
	tokenize(p2, false, false);

	if (p2->error) {
		parser_destroy(p2);
		query_destroy(q);
		p->error = true;
		SB_free(s);
		return goal;
	}

	process_clause(p2->m, p2->cl, NULL);
	execute(q, p2->cl->cells, p2->cl->num_vars);
	SB_free(s);
	p->pl->in_goal_expansion = false;

	if (q->retry != QUERY_OK) {
		parser_destroy(p2);
		query_destroy(q);
		return goal;
	}

	//printf("-- goal_expansion %s/%u\n", C_STR(p, goal), get_arity(goal));

	clear_write_options(q);

	for (unsigned i = 0; i < p->cl->num_vars; i++)
		ignore_name(q, i);

	// Never let the variable count go backwards: the sub-parser may
	// have fewer variables than the clause already has, and lowering
	// the count makes the next expansion hand out a slot that is
	// still in use, silently merging two distinct variables...

	if (p2->cl->num_vars > p->cl->num_vars)
		p->cl->num_vars = p2->cl->num_vars;
	frame *f = GET_FRAME(0);
	char *src = NULL;

	for (unsigned i = 0; i < p2->cl->num_vars; i++) {
		if (!vartab_off(p2, i))
			continue;

		if (strcmp(GET_POOL(p, vartab_off(p2, i)), "_TermOut"))
			continue;

		slot *e = get_slot(q, f, i);

		if (is_empty(&e->c))
			continue;

		cell *c = deref(q, &e->c, e->c.val_ctx);

		// Copy into orig query?

		q->varnames = true;
		q->max_depth = -1;
		src = print_canonical_to_strbuf(q, c, q->latest_ctx, 1);
		q->varnames = false;
		strcat(src, ".");
		break;
	}

	if (!src) {
		parser_destroy(p2);
		query_destroy(q);
		p->error = true;
		TPL_free(src);
		return goal;
	}

	parser_reset(p2);
	p2->cl->num_vars = p->cl->num_vars;
	if (!vartab_copy(p2, p)) { p->error = true; }
	p2->reuse = true;
	p2->srcptr = src;
	tokenize(p2, false, false);

	if (is_var(p2->cl->cells)) {
		if (!p2->do_read_term)
			fprintf(stderr, "Error: instantiation error, goal_expansion/2, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		p2->error_desc = "instantiation_error";
		p2->error = true;
	}

	if (p2->error) {
		parser_destroy(p2);
		query_destroy(q);
		p->error = true;
		TPL_free(src);
		return goal;
	}

	process_clause(p2->m, p2->cl, NULL);
	TPL_free(src);
	fixup_expansion_var_collisions(goal, num_vars_before, p2);

	// Push the updated vartab back...

	if (p2->cl->num_vars > p->cl->num_vars)
		p->cl->num_vars = p2->cl->num_vars;

	if (!vartab_copy(p, p2)) { p->error = true; }

	// snip the old goal...

	const unsigned goal_idx = goal - p->cl->cells;
	const unsigned save_num_cells = goal->num_cells;
	cell *save_cells = TPL_malloc(sizeof(cell) * save_num_cells);
	if (!save_cells) return goal;
	memcpy(save_cells, goal, sizeof(cell) * save_num_cells);
	unsigned trailing = p->cl->cidx - (goal_idx + goal->num_cells);
	p->cl->cidx -= goal->num_cells;
	unshare_cells(goal, goal->num_cells);
	memmove(goal, goal + goal->num_cells, sizeof(cell)*trailing);

	// make room for new goal...

	const unsigned new_cells = p2->cl->cidx-1;		// skip TAG_END
	trailing = p->cl->cidx - goal_idx;
	make_room(p, new_cells);
	goal = p->cl->cells + goal_idx;

	// shift up...

	memmove(goal+new_cells, goal, sizeof(cell)*trailing);

	// paste the new goal...

	memcpy(goal, p2->cl->cells, sizeof(cell)*new_cells);
	p->cl->cidx += new_cells;

	//DUMP_TERM("new", p->cl->cells, 0, 0);

	// done

	parser_destroy(p2);
	query_destroy(q);

	const bool unchanged = (new_cells == save_num_cells)
		&& !memcmp(goal, save_cells, sizeof(cell) * new_cells);

	TPL_free(save_cells);

	if (unchanged)
		return goal;

	return goal_expansion(p, goal);
}

static void expand_meta_predicate(parser *p, predicate *pr, cell *goal)
{
	uint32_t arity = get_arity(goal);

	// Both `goal` and `k` point into p->cl->cells, which make_room()
	// below can grow and hence move. Their indices survive the realloc
	// where the pointers do not.

	const unsigned goal_idx = goal - p->cl->cells;

	for (cell *k = goal+1, *m = pr->meta_args+1; arity--; k += k->num_cells, m += m->num_cells) {
		cell tmpbuf[2];

		if (is_interned(k) && ((k->val_off == g_call_s) || (k->val_off == g_once_s) || (k->val_off == g_ignore_s)))
			continue;
		else if ((get_arity(k) == 2) && (k->val_off == g_colon_s) && is_atom(FIRST_ARG(k)))
			continue;
		else if (!is_interned(k) || is_iso_list(k))
			continue;
		else if (is_interned(m) && (m->val_off == g_colon_s)) {
			make_instr(tmpbuf+0, g_colon_s, bif_iso_qualify_2, 2, 1+k->num_cells);
			SET_OP(tmpbuf+0, OP_XFY);;
			make_atom(tmpbuf+1, new_atom(p->pl, p->m->name));
		} else if (is_smallint(m) && is_positive(m) && (get_smallint(m) <= 9)) {
			make_instr(tmpbuf+0, g_colon_s, bif_iso_qualify_2, 2, 1+k->num_cells);
			SET_OP(tmpbuf+0, OP_XFY);
			make_atom(tmpbuf+1, new_atom(p->pl, p->m->name));
		} else
			continue;

		// get some space...

		unsigned new_cells = 2, k_idx = k - p->cl->cells;
		unsigned trailing = (p->cl->cidx - k_idx) + 1;
		make_room(p, new_cells);

		// ... and re-derive, because that may have moved the clause.
		// k_idx was already being computed for the trailing count; the
		// pointers were simply never refreshed from it. A parsed clause
		// usually arrives with enough slack that make_room() returns
		// without reallocating, which is why this stayed latent.

		k = p->cl->cells + k_idx;
		goal = p->cl->cells + goal_idx;

		// shift up...

		memmove(k+new_cells, k, sizeof(cell)*trailing);

		// paste the new goal...

		memcpy(k, tmpbuf, sizeof(cell)*new_cells);
		p->cl->cidx += new_cells;
		goal->num_cells += new_cells;
	}
}

static bool is_meta_arg(predicate *pr, cell *c, unsigned arg, int *extra)
{
	if (!pr->meta_args)
		return false;

	if (arg >= get_arity(&pr->key))
		return false;

	unsigned i = 0;

	for (cell *m = pr->meta_args+1; m && (i < get_arity(&pr->key)); m += m->num_cells, i++) {
		if (!is_integer(m) || (i != arg))
			continue;

		if (extra)
			*extra = get_smallint(m);

		return true;
	}

	return false;
}

static cell *insert_call_here(parser *p, cell *c, cell *p1)
{
	pl_idx c_idx = c - p->cl->cells, p1_idx = p1 - p->cl->cells;
	make_room(p, 1);

	cell *last = p->cl->cells + (p->cl->cidx - 1);
	int cells_to_move = p->cl->cidx - p1_idx;
	cell *dst = last + 1;

	while (cells_to_move--)
		*dst-- = *last--;

	p1 = p->cl->cells + p1_idx;
	make_instr(p1, g_call_s, bif_iso_call_1, 1, 1);
	p->cl->cidx++;
	return p->cl->cells + c_idx;
}

static cell *term_to_body_conversion(parser *p, cell *c)
{
	//printf("*** term_to_body_conversion %s/%u, p->is_command=%d\n", C_STR(p, c), get_arity(c), p->is_command);

	pl_idx c_idx = c - p->cl->cells;
	bool is_head = c_idx == 0;

	if (is_xfx(c) || is_xfy(c)) {
		if ((c->val_off == g_conjunction_s)
			|| (c->val_off == g_disjunction_s)
			|| (c->val_off == g_if_then_s)
			|| (c->val_off == g_soft_cut_s)
			|| (c->val_off == g_neck_s)) {
			cell *lhs = c + 1;
			int extra = 0;

			if (is_var(lhs)) {
				c = insert_call_here(p, c, lhs);
				lhs = c + 1;
			} else {
				set_arity(lhs, get_arity(lhs) + extra);

				if ((c->val_off != g_neck_s))
					lhs = goal_expansion(p, lhs);

				if (!is_head)
					lhs = term_to_body_conversion(p, lhs);

				set_arity(lhs, get_arity(lhs) - extra);
			}

			cell *rhs = lhs + lhs->num_cells;
			extra = 0;
			c = p->cl->cells + c_idx;

			if (is_var(rhs)) {
				// insert_call_here() grows the clause, so lhs and rhs
				// go stale here just as they do in the else branch -
				// and both are dereferenced below to size c. Only c was
				// being refreshed.

				const pl_idx lhs_idx = lhs - p->cl->cells;
				const pl_idx rhs_idx = rhs - p->cl->cells;
				c = insert_call_here(p, c, rhs);
				lhs = p->cl->cells + lhs_idx;
				rhs = p->cl->cells + rhs_idx;
			} else {
				pl_idx lhs_idx = lhs - p->cl->cells;
				set_arity(rhs, get_arity(rhs) + extra);
				rhs = goal_expansion(p, rhs);
				rhs = term_to_body_conversion(p, rhs);
				set_arity(rhs, get_arity(rhs) - extra);

				// Both calls above can grow, and hence move, the
				// clause. Re-derive the pointers into it; rhs is
				// already the freshly returned one...

				lhs = p->cl->cells + lhs_idx;
				c = p->cl->cells + c_idx;
			}

			c->num_cells = 1 + lhs->num_cells + rhs->num_cells;
		}
	} else if (is_prefix(c)) {
		if (c->val_off == g_neck_s) {
			cell *rhs = c + 1;

			if (is_var(rhs)) {
				c = insert_call_here(p, c, rhs);
				rhs = c + 1;
			} else {
				rhs = goal_expansion(p, rhs);
				rhs = term_to_body_conversion(p, rhs);
				c = p->cl->cells + c_idx;	// may have moved
			}

			c->num_cells = 1 + rhs->num_cells;
		} else if (c->val_off == g_negation_s) {
			cell *rhs = c + 1;

			if (!is_var(rhs)) {
				rhs = goal_expansion(p, rhs);
				c = p->cl->cells + c_idx;	// may have moved
				c->num_cells = 1 + rhs->num_cells;
			}
		}
	} else if (!is_head && get_arity(c)) {
		bool is_goal_expansion = find_goal_expansion(p->m, c);
		predicate *pr = find_predicate(p->m, c);
		bool meta = !pr || pr->is_meta_predicate || is_goal_expansion || p->m->wild_goal_expansion;
		bool control = false;

		if ((c->val_off == g_throw_s) && (get_arity(c) == 1))
			control = true;
		else if ((c->val_off == g_catch_s) && (get_arity(c) == 3))
			control = true;

		if (pr) {
			if (pr->alias)
				pr = pr->alias;

			if (pr->is_meta_predicate) {
				expand_meta_predicate(p, pr, c);

				// It inserts cells, so the clause may have moved.

				c = p->cl->cells + c_idx;
			}
		}

		if (meta) {
			pl_idx old_val_off = c->val_off;
			unsigned old_arity = get_arity(c);
			c = goal_expansion(p, c);
			c_idx = c - p->cl->cells;

			if ((c->val_off != old_val_off) || (get_arity(c) != old_arity)) {
				pr = find_predicate(p->m, c);

				if (pr) {
					if (pr->alias)
						pr = pr->alias;

					if (pr->is_meta_predicate) {
						expand_meta_predicate(p, pr, c);
						c = p->cl->cells + c_idx;
					}
				}

				control = false;

				if ((c->val_off == g_throw_s) && (get_arity(c) == 1))
					control = true;
				else if ((c->val_off == g_catch_s) && (get_arity(c) == 3))
					control = true;
			}
		}

		cell *arg = c + 1;
		uint32_t arity = get_arity(c), i = 0;

		while (arity--) {
			int extra = 0;
			bool meta = pr ? is_meta_arg(pr, c, i, &extra) : false;
			int save_num_cells = arg->num_cells;
			set_arity(arg, get_arity(arg) + extra);

			if (meta)
				arg = goal_expansion(p, arg);

			if (control || meta)
				arg = term_to_body_conversion(p, arg);

			set_arity(arg, get_arity(arg) - extra);
			c = p->cl->cells + c_idx;	// may have moved
			c->num_cells += arg->num_cells - save_num_cells;
			arg += arg->num_cells;
			i++;
		}
	}

	c_idx = c - p->cl->cells;
	return p->cl->cells + c_idx;
}

void term_to_body(parser *p)
{
	term_to_body_conversion(p, p->cl->cells);
	p->cl->cells->num_cells = p->cl->cidx - 1;	// Drops TAG_END
}

cell *check_body_callable(cell *c)
{
	if ((get_arity(c) == 2) && (is_xfx(c) || is_xfy(c))) {
		if ((c->val_off == g_conjunction_s)
			|| (c->val_off == g_disjunction_s)
			|| (c->val_off == g_if_then_s)
			|| (c->val_off == g_soft_cut_s)
			|| (c->val_off == g_neck_s)) {
			cell *lhs = c + 1;
			cell *tmp;

			if ((tmp = check_body_callable(lhs)) != NULL)
				return tmp;

			cell *rhs = lhs + lhs->num_cells;

			if ((tmp = check_body_callable(rhs)) != NULL)
				return tmp;
		}

		return NULL;
	}

	return !is_callable(c) && !is_var(c) ? c : NULL;
}

bool virtual_term(parser *p, const char *src)
{
	parser *p2 = parser_create(p->m);
	check_error(p2);
	p2->is_consulting = true;
	p2->srcptr = (char*)src;
	tokenize(p2, false, false);

	if (p2->error) {
		parser_destroy(p2);
		p->error = true;
		return false;
	}

	parser_destroy(p2);
	return true;
}

cell *make_interned(parser *p, pl_idx offset)
{
	cell *c = make_a_cell(p);
	c->tag = TAG_INTERNED;
	c->num_cells = 1;
	c->val_off = offset;
	return c;
}

static int get_octal(const char **srcptr)
{
	const char *src = *srcptr;
	int v = 0;

	while (*src == '0')
		src++;

	while ((*src >= '0') && (*src <= '7')) {
		v *= 8;
		char ch = *src++;
		v += ch - '0';
	}

	*srcptr = src;
	return v;
}

static int get_hex(const char **srcptr, unsigned n, bool *error)
{
	const char *src = *srcptr;

	if (*src == '\\') {
		*error = true;
		return 0;
	}

	unsigned orig_n = n;
	int v = 0;

	while (*src == '0') {
		src++; n--;
	}

	while (((*src >= '0') && (*src <= '9')) ||
		((*src >= 'a') && (*src <= 'f')) ||
		((*src >= 'A') && (*src <= 'F'))) {
		v *= 16;
		char ch = *src++;
		n--;

		if ((ch >= 'a') && (ch <= 'f'))
			v += 10 + (ch - 'a');
		else if ((ch >= 'A') && (ch <= 'F'))
			v += 10 + (ch - 'A');
		else
			v += ch - '0';

		if (!n)
			break;
	}

	if (n && ((orig_n == 4) || (orig_n == 8))) {
		*error = true;
		return 0;
	}

	*srcptr = src;
	return v;
}

const char *g_escapes = "\e\a\f\b\t\v\r\n\x20\x7F\'\\\"`";
const char *g_anti_escapes = "eafbtvrnsd'\\\"`";

static int get_escape(parser *p, const char **_src, bool *error, bool number)
{
	const char *src = *_src;
	int ch = (unsigned char)*src++;
	const char *ptr = strchr(g_anti_escapes, ch);

	if (ptr && ((ch != 's') || !p->flags.strict_iso))
		ch = g_escapes[ptr-g_anti_escapes];
	else if ((isdigit(ch) || (ch == 'x')
		|| (((ch == 'u') || (ch == 'U')) && (p->flags.json || !p->flags.strict_iso))
		)
		&& !number) {
		bool unicode = false;

		if (ch == 'x') {
			ch = get_hex(&src, UINT_MAX, error);
		} else if (ch == 'U') {
			ch = get_hex(&src, 8, error);
			unicode = true;
		} else if (ch == 'u') {
			ch = get_hex(&src, 4, error);
			unicode = true;

#if 0
			if (((unsigned)ch > 0xd800) && (src[0] == '\\') && (src[1] == 'u')) {
				src += 2;
				int ch2 = get_hex(&src, 4, error);
				ch = (((unsigned)ch - 0xd800) * 0x400) + ((unsigned)ch2 - 0xdc00) + 0x10000;
			}
#endif
		} else {
			src--;
			ch = get_octal(&src);
		}

		if (!p->error && (*src != '\\') && unicode && p->flags.json)
			src--;
		else if (!unicode && (*src++ != '\\')) {
			//if (!p->do_read_term)
			//	fprintf(stderr, "Error: syntax error, closing \\ missing\n");
			*_src = src;
			*error = true;
			return 0;
		}

		if ((unsigned)ch > 0x10FFFF) {
			//if (!p->do_read_term)
			//	fprintf(stderr, "Error: syntax error, illegal character code\n");
			*_src = src;
			*error = true;
			return 0;
		}
	} else if ((((ch != '\\') && (ch != '"') && (ch != '\'') && (ch != '\r') && (ch != '\n'))
		|| number) && !isdigit(ch)) {
		*_src = --src;
		*error = true;
		return 0;
	}

	*_src = src;
	return ch;
}

#define isbdigit(ch) (((ch) >= '0') && ((ch) <= '1'))
#define isodigit(ch) (((ch) >= '0') && ((ch) <= '7'))

void read_integer(parser *p, mp_int v2, int base, const char **srcptr)
{
	const char *src = *srcptr;
	int spacers = 0;

	while (*src) {
		if ((base == 2) && !isbdigit(*src))
			break;

		if ((base == 8) && !isodigit(*src))
			break;

		if ((base == 10) && !isdigit((unsigned char)*src))
			break;

		if ((base == 16) && !isxdigit((unsigned char)*src))
			break;

		if (spacers > 1) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, illegal character, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			*srcptr = src;
			p->error = true;
			return;
		}

		spacers = 0;
		SB_putchar(p->token, *src);
		src++;

		int last_ch = *src;

		while (*src == '_') {
			spacers++;
			src++;
		}

		if (last_ch == '_') {
			p->srcptr = (char*)src;
			src = eat_space(p);
		}
	}

	if (spacers) {
		if (!p->do_read_term)
			fprintf(stderr, "Error: syntax error, illegal character, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		*srcptr = src;
		p->error = true;
		return;
	}

	if ((base != 16) && !isdigit((unsigned char)src[-1]))
		src--;
	else if ((base == 16) && !isxdigit((unsigned char)src[-1]))
		src--;

	mp_int_read_cstring(v2, base, (char*)SB_cstr(p->token), NULL);
	SB_free(p->token);
	*srcptr = src;
}

static bool parse_number(parser *p, const char **srcptr, bool neg)
{
	set_smallint(&p->v, 0);
	p->v.flags = 0;
	const char *s = *srcptr;

	if (*s == '.')
		return false;

	PARSE_LOOP:

	if ((*s == '.') && isdigit((unsigned char)s[1])) {
		if (!p->do_read_term)
			fprintf(stderr, "Error: syntax error, parsing number, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		p->error_desc = "number";
		p->error = true;
		return false;
	}

	// These index a ctype table with a char, which is signed: any byte
	// over 0x7f arrives negative, and the lookup is then undefined. A
	// 4-byte codepoint after 0' starts at 0xf0, i.e. -16.
	if (!isdigit((unsigned char)*s))
		return false;

	if ((s[0] == '0') && (s[1] == '\'') && iscntrl((unsigned char)s[2])) {
		if (!p->do_read_term)
			fprintf(stderr, "Error: syntax error, parsing number, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		p->error_desc = "number";
		p->error = true;
		return false;
	}

	if ((s[0] == '0') && (s[1] == '\'') && (s[2] == '\'')
		&& (isspace((unsigned char)s[3]) || (s[3] == '.') || (s[3] == ',') || (s[3] == ';') || !s[3])) {
		if (!p->do_read_term)
			fprintf(stderr, "Error: syntax error, parsing number, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		p->error_desc = "number";
		p->error = true;
		return false;
	}

	if ((s[0] == '0') && (s[1] == '\'') && (s[2] == '\\') && !s[3]) {
		if (!p->do_read_term)
			fprintf(stderr, "Error: syntax error, parsing octal, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		p->error_desc = "number";
		p->error = true;
		return false;
	}

	if ((s[0] == '0') && (s[1] == '\'') && (s[2] == '\\') && isdigit((unsigned char)s[3])) {
		char *s2 = (char*)s+3;
		long long v = strtoll(s2, &s2, 8);

		if ((*s2 != '\\') || (v >= INT64_MAX)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, parsing octal, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "number";
			p->error = true;
			return false;
		}

		s2++;
		p->v.tag = TAG_INT;
		set_smallint(&p->v, neg ? -v : v);
		*srcptr = s2;
		return true;
	}

	if ((s[0] == '0') && (s[1] == '\'') && (s[2] == '\\') && ((s[3] == '+') || (s[3] == '-'))) {
		if (!p->do_read_term)
			fprintf(stderr, "Error: syntax error, parsing octal, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		p->error_desc = "number";
		p->error = true;
		return false;
	}

	if ((s[0] == '0') && (s[1] == '\'') && !((s[2] == '\\') && (s[3] == '\n'))
		&& (!search_op(p->m, "", NULL, false) || ((s[2] == '\'') && (s[3] == '\'')))) {
		if (!s[2] || (s[2] == '\n')) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, parsing number, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "number";
			p->error = true;
			return false;
		}

		s += 2;
		int v;

		if (*s == '\\') {
			s++;

			if ((*s == '+') || (*s == '-')) {
				if (*s == '-')
					neg = true;

				s++;

				if (*s != '\'') {
					if (!p->do_read_term)
						fprintf(stderr, "Error: syntax error, parsing number, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

					p->error_desc = "number";
					p->error = true;
					return false;
				}

				s++;
				goto PARSE_LOOP;
			}

			int save_ch = s[0];
			v = get_escape(p, &s, &p->error, false);

			// v is a codepoint, so the wide form here.
			if ((((save_ch == '0')) && !iswcntrl(v)) || p->error) {
				//printf("*** *s=%d, iscntrl=%d, save_ch=%d, v=%d\n", *s, iscntrl(*s), save_ch, v);

				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, parsing number, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "number";
				p->error = true;
				return false;
			}

		} else if ((*s == '\'') && s[1] == '\'') {
			s++;
			v = *s++;
		} else if ((*s == '\'') && p->flags.strict_iso && search_op(p->m, "", NULL, false)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, parsing number, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "number";
			p->error = true;
			return false;
		} else
			v = get_char_utf8(&s);

		if (p->error) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, parsing number, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "number";
			p->error = true;
			return false;
		}

		p->v.tag = TAG_INT;
		set_smallint(&p->v, neg ? -v : v);
		*srcptr = s;
		return true;
	}

	mpz_t v2;
	mp_int_init(&v2);
	mp_small val;
	char *tmpptr = (char*)s;

	if ((*s == '0') && (s[1] == 'b')) {
		s += 2;

		read_integer(p, &v2, 2, &s);

		if (mp_int_to_int(&v2, &val) == MP_RANGE) {
			p->v.val_bigint = TPL_malloc(sizeof(bigint));
			ENSURE(p->v.val_bigint);
			p->v.val_bigint->refcnt = 1;
			mp_int_init_copy(&p->v.val_bigint->ival, &v2);
			if (neg) p->v.val_bigint->ival.sign = MP_NEG;
			p->v.flags |= FLAG_INT_BIG | FLAG_MANAGED;
		} else {
			set_smallint(&p->v, neg ? -val : val);
			mp_int_clear(&v2);
		}

		p->v.tag = TAG_INT;
		*srcptr = s;
		return true;
	}

	if ((*s == '0') && (s[1] == 'o')) {
		s += 2;

		read_integer(p, &v2, 8, &s);

		if (mp_int_to_int(&v2, &val) == MP_RANGE) {
			p->v.val_bigint = TPL_malloc(sizeof(bigint));
			ENSURE(p->v.val_bigint);
			p->v.val_bigint->refcnt = 1;
			mp_int_init_copy(&p->v.val_bigint->ival, &v2);
			if (neg) p->v.val_bigint->ival.sign = MP_NEG;
			p->v.flags |= FLAG_INT_BIG | FLAG_MANAGED;
		} else {
			set_smallint(&p->v, neg ? -val : val);
			mp_int_clear(&v2);
		}

		p->v.tag = TAG_INT;
		*srcptr = s;
		return true;
	}

	if ((*s == '0') && (s[1] == 'x')) {
		s += 2;

		read_integer(p, &v2, 16, &s);

		if (mp_int_to_int(&v2, &val) == MP_RANGE) {
			p->v.val_bigint = TPL_malloc(sizeof(bigint));
			ENSURE(p->v.val_bigint);
			p->v.val_bigint->refcnt = 1;
			mp_int_init_copy(&p->v.val_bigint->ival, &v2);
			if (neg) p->v.val_bigint->ival.sign = MP_NEG;
			p->v.flags |= FLAG_INT_BIG | FLAG_MANAGED;
		} else {
			set_smallint(&p->v, neg ? -val : val);
			mp_int_clear(&v2);
		}

		p->v.tag = TAG_INT;
		*srcptr = s;
		return true;
	}

	read_integer(p, &v2, 10, &s);

	// An overflowing float is syntactically fine, it just exceeds an
	// implementation defined limit, so 8.12.2 f applies and not
	// 8.16.7.3 e (issue #1140). Which limit depends on the sign, as
	// with min_integer/max_integer: strtod only ever sees the digits
	// here, the sign is carried in neg.

	if (p->flags.json && s && ((*s == 'e') || (*s == 'E')) && isdigit((unsigned char)s[1])) {
		p->v.tag = TAG_FLOAT;
		errno = 0;
		pl_flt v = strtod(tmpptr, &tmpptr);

		if (isinf(v) && (errno == ERANGE)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: float overflow, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = neg ? "min_float" : "max_float";
			p->error_type = "representation_error";
			p->error = true;
			return false;
		}

		set_float(&p->v, neg?-v:v);
#ifdef FE_INVALID
		feclearexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW);
#endif
		*srcptr = tmpptr;
		mp_int_clear(&v2);
		return true;
	}

	if (s && (*s == '.') && isdigit((unsigned char)s[1])) {
		p->v.tag = TAG_FLOAT;
		errno = 0;
		pl_flt v = strtod(tmpptr, &tmpptr);

		if (isinf(v) && (errno == ERANGE)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: float overflow, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = neg ? "min_float" : "max_float";
			p->error_type = "representation_error";
			p->error = true;
			return false;
		}

		set_float(&p->v, neg?-v:v);
#ifdef FE_INVALID
		feclearexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW);
#endif
		*srcptr = tmpptr;
		mp_int_clear(&v2);
		return true;
	}

	if (mp_int_to_int(&v2, &val) == MP_RANGE) {
		p->v.val_bigint = TPL_malloc(sizeof(bigint));
		ENSURE(p->v.val_bigint);
		p->v.val_bigint->refcnt = 1;
		mp_int_init_copy(&p->v.val_bigint->ival, &v2);
		if (neg) p->v.val_bigint->ival.sign = MP_NEG;
		p->v.flags |= FLAG_INT_BIG | FLAG_MANAGED;
	} else {
		set_smallint(&p->v, neg ? -val : val);
	}

	mp_int_clear(&v2);
	int ch;
	p->v.tag = TAG_INT;

	if ((s[-1] == '.') || isspace((unsigned char)s[-1]))
		s--;

	*srcptr = s;
	ch = peek_char_utf8(s);

	while (iswspace(ch)) {
		s++;
		ch = peek_char_utf8(s);
	}

	if (ch == '(') {
		if (!p->do_read_term)
			fprintf(stderr, "Error: syntax error, parsing number, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		p->error_desc = "unexpected_char";
		p->error = true;
		return false;
	}

	return true;
}

inline static bool is_matching_pair(int ch, int next_ch, int lh, int rh)
{
	return (ch == lh) && (next_ch == rh);
}

// Same as getline(), but for a socket-backed parser waits in short,
// alarm-aware poll() slices first - see tpl_wait_fd_readable() - and
// then reads the line with tpl_getline_nb() rather than
// tpl_getline_fp(): a socket fd is non-blocking now, and getc()'s EOF on
// EAGAIN is indistinguishable there from a real end of file, so the
// plain version silently returns a half-arrived line as though it were
// whole. Everything past a term's first line is read here, so a term
// whose closing ".\n" lands in a later packet than its body used to be
// truncated into a syntax error - see the linda library's tests.

static ssize_t getline_interruptible(parser *p)
{
	if (p->is_socket && p->q) {
		if (!tpl_wait_fd_readable(p->q, fileno(p->fp))) {
			errno = EINTR;
			return -1;
		}

		return tpl_getline_nb(&p->save_line, &p->n_line, p->q, p->fp, NULL);
	}

	return tpl_getline_fp(&p->save_line, &p->n_line, p->fp);
}

char *eat_space(parser *p)
{
	if (!*p->srcptr)
		return p->srcptr;

	p->did_getline = false;
	const char *src = p->srcptr;
	bool done;

	do {
		if (!src) {
			if (p->no_fp || getline_interruptible(p) == -1) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, parsing number, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error = true;
				return NULL;
			}

			p->did_getline = true;
			src = p->srcptr = p->save_line;
		}

		done = true;
		int ch = peek_char_utf8(src);

		while (iswspace(ch)) {
			if (ch == '\n')
				p->line_num++;

			get_char_utf8(&src);
			ch = peek_char_utf8(src);
		}

		if ((*src == '%') && !p->fp) {
			while (*src && (*src != '\n'))
				src++;

			if (*src == '\n')
				p->line_num++;

			//src++;
			done = false;
			continue;
		}

		if ((!*src || (*src == '%')) && p->fp) {
			while (*src && (*src != '\n'))
				src++;

			if (*src == '\n')
				p->line_num++;

			if (*src) {
				src++;
				done = false;
				continue;
			}

			if (p->no_fp || getline_interruptible(p) == -1) {
				if (errno == EINTR) {
					clearerr(p->fp);
					p->error = true;
				}

				return p->srcptr = "";
			}

			p->did_getline = true;
			src = p->srcptr = p->save_line;
			done = false;
			continue;
		}

		do {
			if (!p->is_comment && (src[0] == '/') && (src[1] == '*')) {
				p->is_comment = true;
				src += 2;
				continue;
			}

			if (p->is_comment && (src[0] == '*') && (src[1] == '/')) {
				p->is_comment = false;
				src += 2;

				if (!is_number(&p->v))	// For number_chars
					p->srcptr = (char*)src;

				done = false;
				continue;
			}

			if (*src == '\n')
				p->line_num++;

			if (p->is_comment)
				src++;

			if ((!src || !*src) && p->is_comment && p->fp) {
				if (p->no_fp || getline_interruptible(p) == -1) {
					if (!p->do_read_term)
						fprintf(stderr, "Error: syntax error, parsing number, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

					p->error = true;
					return NULL;
				}

				p->did_getline = true;
				src = p->srcptr = p->save_line;
			}
		}
		 while (*src && p->is_comment);
	}
	 while (!done);

	return (char*)src;
}

static bool check_space_before_function(parser *p, int ch, const char *src)
{
	if (iswspace(ch) && (SB_strcmp(p->token, ".") || p->is_quoted)) {
		p->srcptr = (char*)src;
		//src = eat_space(p);
		bool nl = false;

		while (is_blank_utf8(peek_char_utf8(src)))
			src += len_char_utf8(src);

		while (*src == '\n') {
			nl = true;
			src++;
		}

		while (is_blank_utf8(peek_char_utf8(src)))
			src += len_char_utf8(src);

		if ((!src || !*src) && !nl) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, incomplete statement, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "incomplete_statement";
			p->error = true;
			return false;
		}

		if (!p->is_op && (*src == '(')) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, operator expected before parens, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "operator_expected";
			p->error = true;
			return false;
		}
	}

	return true;
}

static bool contains_null(const char *src, size_t len)
{
	if (!*src)
		return false;

	for (size_t i = 0; i < len; i++) {
		if (!*src++)
			return true;
	}

	return false;
}

const char *eat_continuation(const char* src)
{
	while ((src[0] == '\\') && (src[1] == '\n'))
		src += 2;

	return src;
}

bool get_token(parser *p, bool last_op, bool was_postfix)
{
	if (p->error || !p->srcptr || !*p->srcptr)
		return false;

	const char *src = p->srcptr;

	SB_init(p->token);
	p->v.tag = TAG_INTERNED;
	p->v.flags = 0;
	p->v.num_cells = 1;
	p->quote_char = 0;
	p->was_string = p->is_string = p->is_quoted = p->is_var = p->is_op = p->is_symbol = false;
	src = eat_space(p);

	if (!src || !*src) {
		p->srcptr = (char*)src;
		return false;
	}

	// Where a token would start there may be no character at all: an
	// ill-formed UTF-8 sequence is not text, so there is nothing here
	// for a syntax to be wrong about. It is reported as the same
	// representation error a character-level read of it gives (ISO
	// 13211-1 8.12.1.3 i), which is what lets read/1 meet a stream's
	// 0xff sentinel and say so (issue #1099).

	if (peek_char_utf8_strict(src) == UTF8_INVALID) {
		if (!p->do_read_term)
			fprintf(stderr, "Error: not a character, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		p->error_desc = "character";
		p->error_type = "representation_error";
		p->error = true;
		p->srcptr = (char*)src;
		return false;
	}

	if (p->double_bar) {
		p->double_bar = false;
		p->is_op = true;
		SB_strcpy(p->token, DOUBLE_BAR);
		return true;
	}

	// A closing quote's lookahead for '||' (below) found a single '|'
	// instead. It cannot just leave that '|' unconsumed in the source
	// for this call to re-scan, because finding out took an eat_space()
	// that may have crossed a line - and with it, getline()'d over the
	// buffer the '|' lived in (issue #1134). So it is handed back as
	// its own token here instead, the way a genuine '||' already is
	// above, rather than by rewinding a pointer that may be stale.

	if (p->pending_bar) {
		p->pending_bar = false;
		p->is_op = true;
		SB_strcpy(p->token, "|");
		return true;
	}

	// Numbers...

	const char *tmpptr = src;
	bool neg = false;

	if (p->last_neg) {
		p->last_neg = false;
		neg = true;
	}

	if ((*src != '-') && parse_number(p, &src, neg)) {
		if (neg) p->cl->cidx--;

		if (p->q && !SB_try_strcatn(p->token, tmpptr, src-tmpptr)) {
			SB_free(p->token);
			(void)throw_error(p->q, p->q->st.instr, p->q->st.cur_ctx,
				"resource_error", "memory");
			return false;
		} else if (!p->q)
			SB_strcatn(p->token, tmpptr, src-tmpptr);

		p->srcptr = (char*)src;
		int ch = peek_char_utf8(src);

		if (!check_space_before_function(p, ch, src))
			return false;

		src = p->srcptr;
		ch = peek_char_utf8(src);

		if (ch == '(') {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, parsing number, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "number";
			p->error = true;
			return false;
		}

		return true;
	}

	p->last_neg = false;

	// Quoted...

	if ((*src == '"') || (*src == '`') || (*src == '\'')) {
		p->quote_char = *src++;
		p->is_quoted = true;

		if ((p->quote_char == '"') && !p->flags.double_quote_atom) {
			p->is_string = true;
		}

		for (;;) {
			int ch = 0;

			for (; *src && (ch = get_char_utf8(&src));) {
				if (ch == '\n') {
					if (!p->do_read_term)
						fprintf(stderr, "Error: syntax error, unterminated quoted atom, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

					p->error_desc = "unterminated_quoted_atom";
					p->error = true;
					p->srcptr = (char*)src;
					return false;
				}

				if ((ch == p->quote_char) && (*src == ch)) {
					ch = *src++;
				} else if ((ch == p->quote_char) && (ch == '\'')) {
					p->quote_char = 0;
					break;
				} else if (ch == p->quote_char) {
					if (p->flags.double_quote_atom) {
						p->quote_char = 0;
						break;
					}

					// Check for double bar

					p->srcptr = (char*)src;
					src = eat_space(p);

					if (*src != '|') {
						p->quote_char = 0;
						break;
					}

					src++;
					p->srcptr = (char*)src;
					src = eat_space(p);

					if (*src != '|') {
						// Only a single '|', not '||': it belongs to
						// whatever follows the string, not to it. Both
						// eat_space() calls above may have crossed a
						// line and getline()'d a new one into the same
						// buffer, so the position before them can no
						// longer be trusted as a rewind target (issue
						// #1134) - hand the '|' back as its own token
						// on the next call instead (see p->pending_bar
						// at the top of this function).
						p->pending_bar = true;
						p->quote_char = 0;
						break;
					}

					src++;

					// Double bar

					p->srcptr = (char*)src;
					src = eat_space(p);

					if (*src != '"') {
						if ((src[0] == '[') && (src[1] == ']')) {
							p->quote_char = 0;
							p->is_string = true;
							src += 2;
							break;
						}

						p->double_bar = true;
						p->quote_char = 0;
						p->srcptr = (char*)src;
						break;
					}

					src++;
					continue;
				}

				if (ch < ' ') {
					if (!p->do_read_term)
						fprintf(stderr, "Error: syntax error, invalid quoted character, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

					p->error_desc = "invalid_quoted_character";
					p->error = true;
					p->srcptr = (char*)src;
					return false;
				}

				if ((ch == '\\') && p->flags.character_escapes) {
					int ch2 = *src;
					ch = get_escape(p, &src, &p->error, false);

					if (!p->error) {
						if (ch2 == '\n') {
							//p->line_num++;
							continue;
						}
					} else {
						if (!p->do_read_term)
							fprintf(stderr, "Error: syntax error, illegal character escape <<%s>>, %s:%d\n", p->srcptr, get_loaded(p->m, p->m->filename), p->line_num);

						p->error_desc = "illegal_character_escape";
						p->error = true;
						p->srcptr = (char*)src;
						return false;
					}
				}

				SB_putchar(p->token, ch);
			}

			if (p->quote_char && p->fp) {
				if (p->no_fp || getline_interruptible(p) == -1) {
					p->srcptr = "";

					if (!p->do_read_term)
						fprintf(stderr, "Error: syntax error, unterminated quoted atom, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

					p->error_desc = "unterminated_quoted_atom";
					p->error = true;
					return false;
				}

				src = p->srcptr = p->save_line;
				continue;
			}

			if (p->is_string && !p->flags.json && !SB_strlen(p->token)) {
				SB_strcpy(p->token, "[]");
				p->was_string = true;
				p->is_string = false;
				p->srcptr = (char*)src;
				return true;
			}

			if (!p->is_string
				&& SB_strcmp(p->token, "[")
				&& SB_strcmp(p->token, "(")
				&& SB_strcmp(p->token, "{")
				&& SB_strcmp(p->token, "]")
				&& SB_strcmp(p->token, ")")
				&& SB_strcmp(p->token, "}"))
			{
				if (SB_strlen(p->token) && contains_null(SB_cstr(p->token), SB_strlen(p->token))) {
					p->quote_char = -1;
				} else if (search_op(p->m, SB_cstr(p->token), NULL, false)) {
					p->is_op = true;

					if (!SB_strcmp(p->token, ","))
						p->quote_char = -1;
				} else
					p->quote_char = -1;
			} else
				p->quote_char = -1;

			if (!src || !*src || !ch) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, unexpected term %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "unterminated_quoted_atom";
				p->error = true;
				return false;
			}

			if (*src) {
				p->srcptr = (char*)src;
				ch = peek_char_utf8(src);

				if (!check_space_before_function(p, ch, src))
					return false;

				if (!strcmp(SB_cstr(p->token), "-") && last_op && !was_postfix)
					p->last_neg = true;
			} else
				src = NULL;

			p->srcptr = (char*)src;
			return true;
		}
	}

	// Atoms (including variables)...

	int ch = peek_char_utf8(src);

	if (iswalpha(ch)
#ifdef __APPLE__
		|| iswideogram(ch)
#endif
		|| (ch == '_')) {
		while (iswalnum(ch)
#ifdef __APPLE__
			|| iswideogram(ch)
#endif
			|| (ch == '_')) {
			get_char_utf8(&src);
			SB_putchar(p->token, ch);
			ch = peek_char_utf8(src);
		}

		int ch_start = peek_char_utf8(SB_cstr(p->token));

		if ((!p->flags.var_prefix && !p->flags.json && iswupper(ch_start)) || (ch_start == '_')) {
			if (!p->is_number_chars) p->is_var = true;
		} else if (search_op(p->m, SB_cstr(p->token), NULL, false)) {
			if (!p->is_number_chars) p->is_op = true;
		}

		p->srcptr = (char*)src;
		int ch = peek_char_utf8(src);

		if (!check_space_before_function(p, ch, src))
			return false;

		src = p->srcptr;
		return true;
	}

	ch = get_char_utf8(&src);
	int next_ch = peek_char_utf8(src);
	p->srcptr = (char*)src;

	if ((ch == '(') || (ch == '[') || (ch == '{') || (ch == ',')
		|| (ch == '}') || (ch == ']') || (ch == ')')) {
		src = eat_space(p);

		if (!src || !*src) {
			SB_putchar(p->token, ch);
			p->is_op = search_op(p->m, SB_cstr(p->token), NULL, false);
			p->srcptr = (char*)src;
			return true;
		}

		next_ch = peek_char_utf8(src);

		if (is_matching_pair(ch, next_ch, '[',']')) {
			SB_strcpy(p->token, "[]");
			get_char_utf8(&src);
			p->srcptr = (char*)src;
			int ch = peek_char_utf8(src);

			if (!check_space_before_function(p, ch, src))
				return false;

			src = p->srcptr;
			return true;
		}

		if (is_matching_pair(ch, next_ch, '{','}')) {
			SB_strcpy(p->token, "{}");
			get_char_utf8(&src);
			p->srcptr = (char*)src;
			return true;
		}
	} else
		next_ch = peek_char_utf8(src);

	// Symbols...

	if (is_matching_pair(ch, next_ch, ')','(') ||
		is_matching_pair(ch, next_ch, ']','(') ||
		is_matching_pair(ch, next_ch, '}','(') ||
		is_matching_pair(ch, next_ch, '}','(')) {
		if (!p->do_read_term)
			fprintf(stderr, "Error: syntax error, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		p->error_desc = "operator_expected";
		p->error = true;
		p->srcptr = (char*)src;
		return false;
	}

	p->is_symbol = true;

	do {
		SB_putchar(p->token, ch);

		if (((ch < 256) && strchr(g_solo, ch)) || iswspace(ch))
			break;

		int ch_next = peek_char_utf8(src);

		if (ch_next == '%')
			break;

		if (p->flags.json && (ch_next == '-'))
			break;

		ch = ch_next;

		if (((ch < 256) && strchr(g_solo, ch)) || iswspace(ch) || iswalnum(ch) || (ch == '_'))
			break;

		ch = get_char_utf8(&src);
	}
	 while (ch);

	p->is_op = search_op(p->m, SB_cstr(p->token), NULL, false);
	p->srcptr = (char*)src;
	ch = peek_char_utf8(src);

	if (!p->is_op && !check_space_before_function(p, ch, p->srcptr))
		return false;

	if (*src) {
		while (is_space_utf8(peek_char_utf8(src)))
			src += len_char_utf8(src);
	}

	ch = peek_char_utf8(src);

	if (SB_strcmp(p->token, "(") && !check_space_before_function(p, ch, p->srcptr))
		return false;

	if (!SB_strcmp(p->token, ".") && ch == '|')
		p->quote_char = '\'';

	if (!strcmp(SB_cstr(p->token), "-") && last_op && !was_postfix)
		p->last_neg = true;

	return true;
}

static bool process_term(parser *p, cell *p1)
{
	if (conditionals(p, p1))
		return true;

	if (p->m->ifs_blocked[p->m->if_depth])
		return true;

	if (quads(p, p1))
		return true;

	// A directive that the loader has fully handled needs no clause in
	// the database. The exception is initialization/1, which returns
	// false here so that it is stored as '$directive'(initialization(G))
	// for the end-of-load runner to retract and call; plain clauses
	// return false too, and are stored as themselves.

	directives(p, p1);

	if (p->error)
		return false;

	bool consulting = true;

	cell *h = get_head(p1);

	if (is_var(h)) {
		if (!p->do_read_term)
			printf("Error: instantiation error, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		p->error_desc = "instantiation_error";
		p->error = true;
		return false;
	} else if (is_number(h)) {
		if (!p->do_read_term)
			printf("Error: type error, callable, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

		p->error_desc = "type_error";
		p->error = true;
		return false;
	}

	if (is_cstring(h)) {
		pl_idx off = new_atom(p->pl, C_STR(p, h));
		if (off == ERR_IDX) {
			p->error = true;
			return false;
		}

		unshare_cell(h);
		h->tag = TAG_INTERNED;
		h->val_off = off;
		h->flags = 0;
		set_arity(h, 0);
	}

	rule *r;

	if ((r = assertz_to_db(p->m, p->cl->num_vars, p1, consulting)) == NULL) {
		if ((!p->do_read_term) && 0)
			printf("Error: assertion failed '%s', %s:%d\n", SB_cstr(p->token), get_loaded(p->m, p->m->filename), p->line_num);

		p->error = true;
		return false;
	}

	check_first_cut(&r->cl);
	r->cl.is_fact = !get_logical_body(r->cl.cells);
	r->line_num_start = p->line_num_start;
	r->line_num_end = p->line_num;
	p->line_num_start = 0;
	return true;
}

bool expand_term(parser *p, cell *c)
{
	PROLOG_LIST_HANDLER(c);
	bool tail = false;

	while (is_iso_list(c)) {
		cell *h = PROLOG_LIST_HEAD(c);
		parser *p2 = parser_create(p->m);
		check_error(p2);
		TPL_free(p2->cl);
		p2->cl = TPL_calloc(1, sizeof(clause) + (sizeof(cell)*h->num_cells+1));
		check_error(p2->cl, parser_destroy(p2));
		dup_cells(p2->cl->cells, h, h->num_cells);
		p2->cl->num_allocated_cells = h->num_cells;
		p2->cl->cidx = h->num_cells;
		p2->cl->num_vars = p->cl->num_vars;
		term_expansion(p2);

		if (is_iso_list(p2->cl->cells)) {
			if (!expand_term(p2, p2->cl->cells)) {
				parser_destroy(p2);
				return false;
			}
		} else {
			cell *c2 = p2->cl->cells;

			// A --> among the terms the hook returned, which never
			// met tokenize()'s translation branch (issue #1142).
			// dcg_expand_clause() emits fresh vars named but unnumbered
			// and zeroes num_vars, so the assign_vars() after it is not
			// optional (see new_var() in bif_dcgs.c); the other two
			// finish a clause built here rather than tokenized.

			if (is_interned(c2) && (c2->val_off == g_dcg_s)
				&& (get_arity(c2) == 2)) {
				if (!dcg_expand_clause(p2)) {
					parser_destroy(p2);
					return false;
				}

				assign_vars(p2, 0, false);

				if (p2->error) {
					parser_destroy(p2);
					return false;
				}

				process_clause(p2->m, p2->cl, NULL);
				term_to_body(p2);
				c2 = p2->cl->cells;
			}

			if (!process_term(p2, c2)) {
				parser_destroy(p2);
				return false;
			}

			if (p2->already_loaded_error) {
				parser_destroy(p2);
				return false;
			}
		}

		parser_destroy(p2);
		c = PROLOG_LIST_TAIL(c);

		if (is_nil(c) || is_var(c))
			tail = true;
	}

	if (!tail && !process_term(p, c)) {
		p->error = true;
		return false;
	}

	return true;
}

unsigned tokenize(parser *p, bool is_arg_processing, bool is_consing)
{
	pl_idx arg_idx = p->cl->cidx, save_idx = 0;
	bool last_op = true, is_func = false, last_num = false;
	bool last_bar = false, last_prefix = false, last_postfix = false;
	int entered = p->entered;
	unsigned arity = 1;
	p->depth++;

	while (get_token(p, last_op, last_postfix)) {
		if (p->error && !p->do_read_term)
			break;

		if (p->was_partial) {
			p->was_partial = false;
			last_op = false;
			continue;
		}

#if 0
		int ch = peek_char_utf8(SB_cstr(p->token));
		fprintf(stderr,
			"Debug: '%s' (%d) line_num=%d, symbol=%d, quoted=%d, tag=%u, op=%d, lastop=%d, string=%d\n",
			SB_cstr(p->token), ch, p->line_num, p->is_symbol, p->quote_char, p->v.tag, p->is_op, last_op, p->is_string);
#endif

		if (!p->quote_char
			&& !SB_strcmp(p->token, ".")
			&& (*p->srcptr != ',')
			&& (*p->srcptr != '(')
			&& (*p->srcptr != ')')
			&& (*p->srcptr != ']')
			&& (*p->srcptr != '}')
			&& !isalnum((unsigned char)*p->srcptr)
			&& (*p->srcptr != '_')
			&& ((*p->srcptr != ' ') || !p->is_op)
			) {

			if (p->nesting_parens || p->nesting_brackets || p->nesting_braces) {
				if (!p->do_read_term)
					printf("Error: syntax error, mismatched parens/brackets/braces, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "mismatched_parens_or_brackets_or_braces";
				p->error = true;
				p->nesting_parens = p->nesting_brackets = p->nesting_braces = 0;
			}

			if (!p->cl->cidx) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, incomplete statement, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "incomplete_statement";
				p->error = true;
				return 0;
			}

			if (analyze(p, 0, last_op)) {
				if (p->cl->cells->num_cells < p->cl->cidx) {
					if (!p->do_read_term)
						printf("Error: syntax error, operator expected unfinished input '%s', %s:%d\n", SB_cstr(p->token), get_loaded(p->m, p->m->filename), p->line_num);

					p->error_desc = "operator_expected";
					p->error = true;
					return 0;
				}

				// DCG translation happens HERE, ahead of assign_vars(),
				// so the translated clause goes through the ENTIRE
				// pipeline exactly as a hand-written one would.
				//
				// This is not a free choice. goal_expansion() prints a
				// goal and re-parses it, carrying variable identity
				// across that boundary by NAME through the inherited
				// vartab (p2->vartab = p->vartab, reuse = true). Fresh
				// variables invented after assign_vars() have no vartab
				// entry, so that re-parse hands them new slots and the
				// S0/S threading is silently lost. Letting assign_vars()
				// see them is what registers the names.

				if ((p->is_consulting || p->expand_dcg) && !p->skip && !p->internal
					&& is_interned(p->cl->cells)
					&& (p->cl->cells->val_off == g_dcg_s)
					&& (get_arity(p->cl->cells) == 2)) {
					// expand_dcg is the re-parse of a term_expansion/2
					// result (issue #1142). A --> the hook emitted has
					// already sailed past this branch as whatever the
					// hook was given, so it is translated on the way
					// back in - still ahead of assign_vars(), the order
					// dcg_expand_clause() requires. We used to assert
					// it raw as a fact for '-->'/2.
					//
					// Still true, and the reason the hook cannot simply
					// move ahead of translation: term_expansion()
					// produces a fully processed clause via its own
					// print-and-reparse, so it cannot move ahead of
					// assign_vars(), and translation cannot move after
					// it without losing the variable registration that
					// goal_expansion depends on (see §10 of
					// docs/native-dcg-design.md). So a --> term written
					// in the source still never reaches the hook.
					//
					// Note also that tabled DCG rules work *because*
					// the rename in library/tabling.pl's
					// user:term_expansion runs after translation.

					dcg_expand_clause(p);

					if (p->error) return 0;
				}

				assign_vars(p, p->read_term_slots, false);

				if (p->error) return 0;

				if (p->is_consulting && !p->skip && check_body_callable(p->cl->cells)) {
					if (!p->do_read_term)
						printf("Error: type error, not callable, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

					p->error_desc = "callable";
					p->error = true;
					return 0;
				}


				process_clause(p->m, p->cl, NULL);

				if (!p->one_shot /*|| p->is_command*/)
					term_to_body(p);

				if (p->is_consulting && !p->skip) {
					term_expansion(p);
					cell *p1 = p->cl->cells;

					if (!get_arity(p1) && !strcmp(C_STR(p, p1), "begin_of_file")) {
						p->end_of_term = true;
						last_op = true;
						last_num = false;
						p->cl->cidx = 0;
						continue;
					}

					if (!get_arity(p1) && !strcmp(C_STR(p, p1), "end_of_file")) {
						p->end_of_term = true;
						p->end_of_file = true;

						if (p->m->quad_query && !p->m->quad_recorded)
							fprintf(stderr, "Warning: quad query without answer description, %s:%d\n", get_loaded(p->m, p->m->filename), p->m->quad_line_num);

						quad_reset(p->m);
						process_module(p->m);
						return 0;
					}
				}

				if (p->is_consulting && !p->skip) {
					cell *c = p->cl->cells;

					if (!expand_term(p, c))
						return 0;

					if (p->already_loaded_error)
						return 0;

					p->cl->cidx = 0;
				}
			}

			p->end_of_term = true;
			last_op = true;

			if (p->interactive)
				p->fp = NULL;

			if (p->one_shot)
				break;

			last_num = false;
			continue;
		}

		if (!p->line_num_start)
			p->line_num_start = p->line_num;

		p->end_of_term = false;

		if (!p->quote_char && !last_op &&
			(!SB_strcmp(p->token, "[") || !SB_strcmp(p->token, "{"))) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, needs operator '%s', %s:%d\n", SB_cstr(p->token), get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "needs_operator";
			p->error = true;
			break;
		}

		if (!p->quote_char && p->last_close && !SB_strcmp(p->token, "(")) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, needs operator '%s', %s:%d\n", SB_cstr(p->token), get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "needs_operator";
			p->error = true;
			break;
		}

		if (!p->quote_char && !SB_strcmp(p->token, "[")) {
			save_idx = p->cl->cidx;
			cell *c = make_interned(p, g_dot_s);
			set_arity(c, 2);
			p->start_term = true;
			p->nesting_brackets++;
			bool was_consing = p->was_consing;
			p->was_consing = false;
			p->entered = '[';
			tokenize(p, true, true);

			if (!p->was_consing)
				make_interned(p, g_nil_s);

			p->start_term = p->last_close = false;
			p->was_consing = was_consing;
			last_bar = last_op = false;

			if (p->error)
				break;

			c = p->cl->cells + save_idx;
			c->num_cells = p->cl->cidx - save_idx;
			fix_list(c);
			last_num = false;
			continue;
		}

		if (!p->quote_char && !SB_strcmp(p->token, "{")) {
			save_idx = p->cl->cidx;
			cell *c = make_interned(p, g_braces_s);
			ENSURE(c);
			set_arity(c, 1);
			p->start_term = true;
			p->nesting_braces++;
			p->entered = '{';
			tokenize(p, false, false);
			last_bar = false;

			if (p->error)
				break;

			c = p->cl->cells+save_idx;
			c->num_cells = p->cl->cidx - save_idx;
			p->start_term = p->last_close = false;
			last_op = false;
			last_num = false;
			continue;
		}

		if (!p->quote_char && !SB_strcmp(p->token, "(")) {
			p->start_term = true;
			p->nesting_parens++;
			p->entered = '(';
			unsigned tmp_arity = tokenize(p, is_func, false);
			last_bar = false;

			if (p->error)
				break;

			if (is_func) {
				// f() is how both SWI and XSB spell a zero-argument
				// call, and the Janus interface is written in it
				// throughout. ISO has no such term and Trealla has no
				// way to represent one - functor(X, f, 0) is the atom f
				// - so under the empty_args flag it reads as '()'(f):
				// an ordinary compound, distinct from the atom, that
				// every other part of the system already understands.
				// The functor is not a possible identifier, so nothing
				// can collide with it.

				if ((tmp_arity == 1) && p->flags.empty_args && p->last_empty_arglist) {
					cell *arg = make_a_cell(p);
					cell *c = p->cl->cells + save_idx;	// may have moved
					*arg = *c;
					arg->num_cells = 1;
					arg->priority = 0;
					SET_OP(arg, 0);
					set_arity(arg, 0);
					c->tag = TAG_INTERNED;
					c->flags = 0;
					c->priority = 0;
					SET_OP(c, 0);
					c->val_off = new_atom(p->pl, "()");
					tmp_arity = 1;
				}

				cell *c = p->cl->cells + save_idx;

				if (is_cstring(c)) {
					pl_idx off = new_atom(p->pl, C_STR(p, c));

					if (off == ERR_IDX) {
						p->error = true;
						break;
					}

					unshare_cell(c);
					c->tag = TAG_INTERNED;
					c->flags = 0;
					c->match = NULL;
					c->val_off = off;
				}

				set_arity(c, tmp_arity);
				c->num_cells = p->cl->cidx - save_idx;
			}

			is_func = last_op = false;
			last_num = false;
			p->start_term = p->last_close = false;
			continue;
		}

		if (!p->quote_char && !is_arg_processing && !is_consing && last_op && !last_postfix && !SB_strcmp(p->token, ",")) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, quotes needed around operator '%s', %s:%d\n", SB_cstr(p->token), get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "quotes_needed";
			p->error = true;
			break;
		}

		if (!p->quote_char && is_arg_processing && !is_consing && p->is_op /*&& last_op*/
			&& SB_strcmp(p->token, "|")
 			&& SB_strcmp(p->token, ",")
 			) {
			unsigned priority = search_op(p->m, SB_cstr(p->token), NULL, false);

			if (!last_op && (priority > 999)) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, parens needed around operator '%s', %s:%d\n", SB_cstr(p->token), get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "parens_needed";
				p->error = true;
				break;
			}
		}

		if (!p->quote_char && is_consing && !SB_strcmp(p->token, ",")) {
			if ((arg_idx == p->cl->cidx) || !p->cl->cidx) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, missing arg '%s', %s:%d\n", p->save_line?p->save_line:"", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "args";
				p->error = true;
				break;
			}

			if ((*p->srcptr == ',') && !p->flags.double_quote_codes) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, missing element '%s', %s:%d\n", p->save_line?p->save_line:"", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "missing_element";
				p->error = true;
				break;
			}

			if (p->was_consing || last_op) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, parsing list '%s', %s:%d\n", p->save_line?p->save_line:"", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "list";
				p->error = true;
				break;
			}

			cell *c = make_interned(p, g_dot_s);
			set_arity(c, 2);
			p->start_term = last_op = true;
			last_num = false;
			p->last_close = false;
			continue;
		}

		if (!p->quote_char &&
			((is_arg_processing && !SB_strcmp(p->token, ",")) ||
			(is_consing && !p->was_consing && !p->start_term
				&& (!SB_strcmp(p->token, ",") || !SB_strcmp(p->token, "|")))
			)) {
			if ((arg_idx == p->cl->cidx) || !p->cl->cidx) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, missing arg '%s', %s:%d\n", p->save_line?p->save_line:"", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "args";
				p->error = true;
				break;
			}

			analyze(p, arg_idx, last_op);
			arg_idx = p->cl->cidx;

			if (*p->srcptr == ',') {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, missing arg '%s', %s:%d\n", p->save_line?p->save_line:"", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "args";
				p->error = true;
				break;
			}

			if (is_arg_processing)
				arity++;

			if (is_consing && !SB_strcmp(p->token, "|")) {
				p->was_consing = last_bar = true;
				//is_consing = false;
			}

			p->last_close = false;
			last_op = true;
			last_num = false;
			continue;
		}

		if (!p->is_quoted /*&& is_consing*/ && (p->start_term || last_op) && !SB_strcmp(p->token, "|")) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, parsing '%s', %s:%d\n", p->save_line?p->save_line:"", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "list";
			p->error = true;
			break;
		}

		if (!p->is_quoted && p->was_consing && is_consing && !SB_strcmp(p->token, "|")) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, parsing list '%s', %s:%d\n", p->save_line?p->save_line:"", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "list";
			p->error = true;
			break;
		}

		if (!p->is_quoted && p->was_consing && last_bar && !SB_strcmp(p->token, "]")) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, parsing list '%s', %s:%d\n", p->save_line?p->save_line:"", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "list";
			p->error = true;
			break;
		}

		// f() under the empty_args flag: an empty argument list is
		// allowed for a functor, and only for a functor. Bare () stays a
		// syntax error, as do [] spelled this way and {}.

		bool empty_arglist = is_arg_processing && p->flags.empty_args
			&& !p->quote_char && !SB_strcmp(p->token, ")");

		if (!p->quote_char && p->start_term && !empty_arglist &&
			(!SB_strcmp(p->token, "]") || !SB_strcmp(p->token, ")") || !SB_strcmp(p->token, "}"))) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, start of rule expected, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "start_expected";
			p->error = true;
			break;
		}

		if (!p->quote_char && !SB_strcmp(p->token, ")")) {
			if ((arg_idx == p->cl->cidx) && !empty_arglist) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, missing arg '%s', %s:%d\n", p->save_line?p->save_line:"", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "args";
				p->error = true;
				break;
			}

			if (entered != '(') {
				if (!p->do_read_term)
					printf("Error: syntax error, mismatched parens/brackets/braces, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "mismatched_parens_or_brackets_or_braces";
				p->error = true;
				p->nesting_parens = p->nesting_brackets = p->nesting_braces = 0;
			}

			p->last_close = true;
			p->last_empty_arglist = (arg_idx == p->cl->cidx);
			p->nesting_parens--;
			analyze(p, arg_idx, last_op=false);
			return arity;
		}

		if (!p->quote_char && !SB_strcmp(p->token, "]")) {
			if (arg_idx == p->cl->cidx) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, missing arg '%s', %s:%d\n", p->save_line?p->save_line:"", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "args";
				p->error = true;
				break;
			}

			if (entered != '[') {
				if (!p->do_read_term)
					printf("Error: syntax error, mismatched parens/brackets/braces, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "mismatched_parens_or_brackets_or_braces";
				p->error = true;
				p->nesting_parens = p->nesting_brackets = p->nesting_braces = 0;
			}

			p->last_close = true;
			p->nesting_brackets--;
			analyze(p, arg_idx, last_op=false);
			return arity;
		}

		if (!p->quote_char && !SB_strcmp(p->token, "}")) {
			if (arg_idx == p->cl->cidx) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, missing arg '%s', %s:%d\n", p->save_line?p->save_line:"", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "args";
				p->error = true;
				break;
			}

			if (entered != '{') {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, mismatched parens/brackets/braces, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "mismatched_parens_or_brackets_or_braces";
				p->error = true;
				p->nesting_parens = p->nesting_brackets = p->nesting_braces = 0;
			}

			p->last_close = true;
			p->nesting_braces--;
			analyze(p, arg_idx, last_op=false);
			return arity;
		}

		p->last_close = false;

		if (p->is_var && (*p->srcptr == '(')) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, var as functor, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "variable_cannot_be_functor";
			p->error = true;
			break;
		}

		unsigned specifier = 0;
		int priority = 0;

		if (is_interned(&p->v)) {
			char *s = eat_space(p);

			if (!s || !*s) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, incomplete, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "incomplete";
				p->error = true;
				break;
			}

#if 0
			int nextch = *s;
			bool noneg = (!SB_strcmp(p->token, "-") || !SB_strcmp(p->token, "+")) && (nextch == '='); // Hack

			if (noneg && !p->is_string) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, incomplete, needs parenthesis, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "incomplete";
				p->error = true;
				break;
			}
#endif

			priority = search_op(p->m, SB_cstr(p->token), &specifier, last_op);
		}

		if (!SB_strcmp(p->token, "!") &&
			((*p->srcptr == ')') || (*p->srcptr == ';') || (*p->srcptr == ',') || (*p->srcptr == '.')))
			p->quote_char = 1;

		if (p->quote_char && last_op) {
			specifier = 0;
			priority = 0;
		}

		if (priority && (last_op || last_bar)
			&& !IS_POSTFIX(specifier)) {
			char *s = eat_space(p);

			if (!s || !*s) {
				if (!p->do_read_term)
					fprintf(stderr, "Error: syntax error, incomplete, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);

				p->error_desc = "error_incomplete";
				p->error = true;
				break;
			}

			int nextch = *s;

			if (IS_PREFIX(specifier) && p->is_symbol && last_prefix)
				;
			else if ((nextch == ',')
				|| (nextch == ';')
				|| (nextch == ')')
				|| (nextch == '|')
				|| (nextch == ']')
				|| (nextch == '}')
			) {
				if ((SB_strcmp(p->token, "-") && SB_strcmp(p->token, "+")) || is_consing) {
					specifier = 0;
					priority = 0;
				}
			}
		}

		if (priority && IS_POSTFIX(specifier) && is_arg_processing && last_op && !last_postfix) {
			specifier = 0;
			priority = 0;
		}

		if (priority && IS_INFIX(specifier) && last_op && !last_postfix) {
			specifier = 0;
			priority = 0;
		}

		// Operators in canonical form..

		if (last_op && priority && (*p->srcptr == '(')) {
			p->v.tag = TAG_INTERNED;
			specifier = 0;
			priority = 0;
			p->quote_char = 0;
		}

		is_func = last_op && is_interned(&p->v) && !specifier && !last_num && (*p->srcptr == '(');

		if (!is_func && last_op && (is_arg_processing && priority >= 1200) && !p->is_quoted) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, parens needed around operator '%s', %s:%d\n", SB_cstr(p->token), get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "parens_needed";
			p->error = true;
			break;
		}

		if ((p->was_string || p->is_string) && is_func && !p->double_bar) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, near \"%s\", expected atom, %s:%d\n", SB_cstr(p->token), get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "expected_atom";
			p->error = true;
			break;
		}

		if (is_func) {
			p->is_op = false;
			specifier = 0;
			save_idx = p->cl->cidx;
		}

		if (!p->is_op && !is_func && last_op && last_postfix && 0) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, near '%s', operator expected postfix '%s', %s:%d\n", SB_cstr(p->token), p->save_line?p->save_line:"", get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "operator_expected";
			p->error = true;
			break;
		}

		if (is_consing && IS_INFIX(specifier) && (priority >= 1000)) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, near '%s', expected, %s:%d\n", SB_cstr(p->token), get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "operator_expected";
			p->error = true;
			break;
		}

		if ((!p->is_op || IS_PREFIX(specifier)) && !is_func && !last_op) {
			if (!p->do_read_term)
				fprintf(stderr, "Error: syntax error, near '%s', operator expected, %s:%d\n", SB_cstr(p->token), get_loaded(p->m, p->m->filename), p->line_num);

			p->error_desc = "operator_expected";
			p->error = true;
			break;
		}

		last_op = SB_strcmp(p->token, ")") && priority;
		last_postfix = IS_POSTFIX(specifier);
		last_prefix = last_op && IS_PREFIX(specifier);

		p->start_term = false;
		cell *c = make_a_cell(p);
		c->num_cells = 1;
		c->tag = p->v.tag;
		c->flags = p->v.flags;
		SET_OP(c,specifier);
		c->priority = priority;
		bool found = false;
		last_num = is_number(c);

		if (is_bigint(&p->v)) {
			c->val_bigint = p->v.val_bigint;
		} else if (is_smallint(&p->v)) {
			set_smallint(c, get_smallint(&p->v));
		} else if (p->v.tag == TAG_FLOAT) {
			set_float(c, get_float(&p->v));
#ifdef FE_INVALID
			feclearexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW);
#endif
		} else if (!p->is_string
			&& (!p->is_quoted || is_func || p->is_op || p->is_var || p->is_consulting
			|| (get_builtin(p->pl, SB_cstr(p->token), SB_strlen(p->token), 0, &found, NULL), found)
			|| !SB_strcmp(p->token, "[]"))
			) {
			if (is_func && !SB_strcmp(p->token, "."))
				c->priority = 0;

			// We temporarily make use of 'var_num' to hold the nesting info
			// on the var, used for determining later if it's a global or not.

			if (p->is_var) {
				c->tag = TAG_VAR;
				c->var_num = (2*p->nesting_braces) + (2*p->nesting_brackets) + p->nesting_parens;
			}

			if (!p->is_number_chars) {
				c->val_off = new_atom(p->pl, SB_cstr(p->token));
				ENSURE(c->val_off != ERR_IDX);
			}
		} else {
			c->tag = TAG_CSTR;
			size_t toklen = SB_strlen(p->token);

			if ((toklen < MAX_SMALL_STRING) && !p->is_string) {
				memcpy(c->val_chr, SB_cstr(p->token), toklen);
				c->val_chr[toklen] = '\0';
				c->chr_len = toklen;
			} else {
				if (p->is_string && p->flags.double_quote_codes)
					c->flags |= FLAG_CSTR_CODES;

				if (p->is_string) {
					c->flags |= FLAG_CSTR_STRING;
					set_arity(c, 2);
				}

				if (!p->is_number_chars)
					make_string_internal(c, SB_cstr(p->token), toklen, 0);
			}
		}

		last_bar = false;
	}

	p->depth--;
	return !p->error ? 1 : 0;
}

bool run(parser *p, const char *prolog_src, bool dump, query **subq, unsigned int yield_time_in_ms)
{
	if ((*prolog_src == '.') && !prolog_src[1]) {
		fprintf(stderr, "Error: syntax error, incomplete statement, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);
		return false;
	}

	SB(pr);
	SB_sprintf(pr, "%s", prolog_src);
	SB_trim_ws(pr);
	SB_trim(pr, '.');
	SB_strcat(pr, ".");

	p->in_body = true;
	p->srcptr = SB_cstr(pr);
	bool ok;

	while (p->srcptr && *p->srcptr) {
		parser_reset(p);
		p->line_num_start = 0;
		p->line_num = 1;
		p->one_shot = true;
		p->is_command = true;
		p->is_consulting = false;
		tokenize(p, false, false);

		if (p->error) {
			p->pl->error = p->error;
			break;
		}

		if (!p->cl->cidx)
			break;

		if (!p->error && !p->end_of_term && !p->m->run_init) {
			fprintf(stderr, "Error: syntax error, missing operand or operator, %s:%d\n", get_loaded(p->m, p->m->filename), p->line_num);
			p->error = true;
		}

		if (p->error) {
			p->pl->did_dump_vars = true;
			p->srcptr = NULL;
			p->pl->error = p->error;
			SB_free(pr);
			return false;
		}

		if (p->skip) {
			p->pl->status = true;
			p->srcptr = NULL;
			SB_free(pr);
			return true;
		}

		query *q = query_create(p->m);
		CHECKED(q, p->srcptr = NULL, SB_free(pr));

		// A returned query outlives this call, and its goal's cells -
		// a string literal among them - live in this parser's clause.
		// So the parser goes with the query rather than being destroyed
		// underneath it.

		if (subq) {
			*subq = q;
			q->owns_top = true;
		}

		if (yield_time_in_ms > 0)
			do_yield_at(q, yield_time_in_ms);

		q->top = p;
		q->do_dump_vars = dump;
		q->run_init = p->m->run_init;
		execute(q, p->cl->cells, p->cl->num_vars);

		if (q->halt) {
			p->pl->halt = q->halt;
			p->pl->halt_code = q->halt_code;
		}

		p->pl->status = q->status;
		p->pl->error = q->error;
		p->pl->is_redo = q->is_redo;
		ok = !q->error;

		// p->m is not taken from q->st.m: a goal ending in another module
		// (any library predicate does) would move the toplevel there, so
		// later consults and asserts would land in it (issue #1079).

		if (!subq)
			query_destroy(q);

		if (p->pl->is_query)
			break;

		if (!ok)
			break;
	}

	// No fflush() here: streams[0] is stdin, and flushing an input stream
	// is undefined. glibc and the BSDs ignore it, but musl, MSVCRT and
	// Solaris libc discard the buffered input - which is the rest of a
	// piped query stream, so only the first query ever ran there.

	stream *str = &p->pl->streams[0];
	TPL_free(str->data);
	str->data = NULL;
	str->data_len = 0;
	str->srclen = 0;
	p->srcptr = NULL;
	SB_free(pr);
	return ok;
}
