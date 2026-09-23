#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "module.h"
#include "network.h"
#include "query.h"

int format_integer(char *dst, cell *c, int grouping, int sep, int decimals, int radix)
{
	char *tmpbuf1 = NULL, *tmpbuf2 = NULL;
	char xtmpbuf1[256], xtmpbuf2[256];

	if (is_smallint(c)) {
		pl_int v = get_smallint(c);

		if (v < 0) {
			xtmpbuf1[0] = '-';
			sprint_int(xtmpbuf1+1, sizeof(xtmpbuf1)-1, llabs(v), radix);
		} else
			sprint_int(xtmpbuf1, sizeof(xtmpbuf1), v, radix);

		tmpbuf1 = xtmpbuf1;
		tmpbuf2 = xtmpbuf2;
	} else {
		size_t len = mp_int_string_len(&c->val_bigint->ival, abs(radix)) - 1;
		tmpbuf1 = TPL_malloc(len+1);
		check_error(tmpbuf1);
		mp_int_to_string(&c->val_bigint->ival, abs(radix), tmpbuf1, len+1);
		len *= 2;
		tmpbuf2 = TPL_malloc(len+1);
		check_error(tmpbuf2);
	}

	const char *src = tmpbuf1 + strlen(tmpbuf1) - 1;	// start from back
	char *dst2 = tmpbuf2;
	int i = 1, j = 1;

	while (src > tmpbuf1) {
		*dst2++ = *src--;

		if (grouping && !decimals && !(i++ % grouping) && *src)
			*dst2++ = sep;

		if (decimals && (j++ == decimals)) {
			*dst2++ = '.';
			decimals = 0;
			i = 1;
		}
	}

	*dst2++ = *src;
	*dst2 = '\0';
	src = tmpbuf2 + (strlen(tmpbuf2) - 1);
	dst2 = dst;

	while (src >= tmpbuf2)
		*dst2++ = *src--;

	*dst2 = '\0';

	if (!is_smallint(c)) {
		TPL_free(tmpbuf1);
		TPL_free(tmpbuf2);
	}

	return dst2 - dst;
}

typedef struct {
	cell *p;
	pl_ctx p_ctx;
	const char *srcbuf;
	const char *src;
	size_t srclen;
}
 list_reader_t;

static int get_next_char(query *q, list_reader_t *fmt)
{
	if (fmt->src) {
		int len = len_char_utf8(fmt->src);
		int ch = get_char_utf8(&fmt->src);
		fmt->srclen -= len;
		return ch;
	}

	fmt->p = fmt->p + 1;
	cell *head = deref(q, fmt->p, fmt->p_ctx);
	int ch;

	if (is_smallint(head))
		ch = get_smallint(head);
	else if (is_atom(head)) {
		const char *s = C_STR(q, head);
		ch = peek_char_utf8(s);
	} else
		return -1;

	fmt->p = fmt->p + fmt->p->num_cells;
	fmt->p = deref(q, fmt->p, fmt->p_ctx);
	fmt->p_ctx = q->latest_ctx;
	return ch;
}

static cell *get_next_cell(query *q, list_reader_t *fmt, bool *is_var, pl_ctx *ctx)
{
	*is_var = false;

	if (fmt->src)
		return NULL;

	if (!is_list(fmt->p)) {
		if (is_var(fmt->p))
			*is_var = true;

		return NULL;
	}

	cell *head = fmt->p + 1;
	cell *tail = head + head->num_cells;

	head = deref(q, head, fmt->p_ctx);
	*ctx = q->latest_ctx;

	fmt->p = deref(q, tail, fmt->p_ctx);
	fmt->p_ctx = q->latest_ctx;
	return head;
}

static bool is_more_data(query *q, list_reader_t *fmt)
{
	if (fmt->src)
		return fmt->srclen;

	if (!fmt->p)
		return false;

	// A list can end in a packed string, as [H|T] does when T was taken
	// from one, so read on from that as bytes.

	if (is_string(fmt->p)) {
		fmt->src = C_STR(q, fmt->p);
		fmt->srclen = C_STRLEN(q, fmt->p);
		return fmt->srclen;
	}

	return is_list(fmt->p);
}

#define CHECK_BUF(len) {									\
	unsigned n = ((len) > 0 ? (len) : 1) + 1;	            \
	if (n >= tmpbuf_free) {									\
		size_t save_offset = dst - tmpbuf;					\
		tmpbuf_size += n;									\
		tmpbuf = TPL_realloc(tmpbuf, (tmpbuf_size*=2));			\
		CHECKED(tmpbuf);							\
		dst = tmpbuf + save_offset;							\
		tmpbuf_free = tmpbuf_size - save_offset;			\
	}                                                       \
	tmpbuf_free -= len;										\
}

// eval(q,c) LOOKS like an assignment but the macro body ends in three
// bare return statements - q->did_throw, is_var, and the is_builtin
// check in builtins.h. Inside do_format that returns past every
// TPL_free(tmpbuf), leaking the 8 KB format buffer on any arithmetic
// error: format("~d", [X]) with X unbound leaked 8192 bytes per call,
// as did ~c ~e ~E ~f ~g ~G ~D ~r ~R with a non-evaluable argument.
//
// This is the same macro with the buffer released on each exit. It has
// to be a macro too, because it declares p1 in the caller's scope and
// because those returns must return from do_format.

#define FMT_EVAL(p1, c)														\
	cell p1 = is_evaluable(c) || is_builtin(c) ? (call_builtin(q,c,c##_ctx), q->accum) : \
		is_callable(c) ? (call_userfun(q, c, c##_ctx), q->accum) : *c;		\
	q->accum.flags = 0;														\
	q->accum.num_cells = 1;													\
	if (q->did_throw) {														\
		TPL_free(tmpbuf);													\
		return true;														\
	}																		\
	if (is_var(c)) {														\
		TPL_free(tmpbuf);													\
		return throw_error(q, c, q->st.cur_ctx, "instantiation_error", "number"); \
	}																		\
	if (is_builtin(c) && c->bif_ptr && (c->bif_ptr->fn != bif_iso_float_1) && (c->bif_ptr->fn != bif_iso_integer_1)) { \
		TPL_free(tmpbuf);													\
		return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");	\
	}

bool do_format(query *q, cell *str, pl_ctx str_ctx, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx)
{
	list_reader_t fmt1 = {0}, fmt2 = {0};
	list_reader_t save_fmt1 = {0}, save_fmt2 = {0};
	fmt1.p = p1;
	fmt1.p_ctx = p1_ctx;
	fmt1.srcbuf = is_atom(p1) ? C_STR(q, p1) : NULL;
	fmt1.srclen = is_atom(p1) ? C_STRLEN(q, p1) : 0;
	fmt1.src = fmt1.srcbuf;
	fmt2.p = p2;
	fmt2.p_ctx = p2_ctx;

	size_t tmpbuf_size = 1024*8;
	char *tmpbuf = TPL_malloc(tmpbuf_size);
	CHECKED(tmpbuf);
	char *dst = tmpbuf;
	*dst = '\0';
	size_t tmpbuf_free = tmpbuf_size;
	bool redo = false, start_of_line = true;
	int tab_at = 1, tabs = 0, diff = 0, last_at = 0, tab_char = ' ';
	int remainder = 0, tabs_seen = 0;
	size_t tab_off = 0;
	save_fmt1 = fmt1;
	save_fmt2 = fmt2;

	bool is_partial = false;

	if (p2 && !check_list(q, p2, p2_ctx, &is_partial, NULL) && is_partial) {
		TPL_free(tmpbuf);
		return throw_error(q, p2, p2_ctx, "instantiation_error", "atom");
	}

	while (is_more_data(q, &fmt1)) {
		int argval = 0, noargval = 1, argval_specified = 0;
		const char *line_start = strrchr(tmpbuf, '\n');
		line_start = line_start ? line_start + 1 : tmpbuf;
		int pos = strlen_utf8(line_start) + 1;
		list_reader_t tmp_fmt1 = fmt1, tmp_fmt2 = fmt2;
		int ch = get_next_char(q, &fmt1);

		if (ch != '~') {
			CHECK_BUF(MAX_BYTES_PER_CODEPOINT);
			dst += put_char_utf8(dst, ch);
			start_of_line = ch == '\n';

			if (start_of_line)
				last_at = 0;

			continue;
		}

		ch = get_next_char(q, &fmt1);
		pl_ctx c_ctx = p2_ctx;

		if (ch == '*') {
			bool is_var;
			cell *c = get_next_cell(q, &fmt2, &is_var, &c_ctx);

			if (is_var) {
				TPL_free(tmpbuf);
				return throw_error(q, p2, p2_ctx, "instantiation_error", "atom");
			}

			if (is_negative(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, p2, p2_ctx, "domain_error", "positive");
			}

			FMT_EVAL(p1, c);
			c = &p1;

			if (is_float(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "integer");
			}

			if (!is_integer(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");
			}

			noargval = 0;

			if (!c || !is_integer(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "integer");
			}

			argval = get_smallint(c);
			argval_specified = 1;
			ch = get_next_char(q, &fmt1);
		} else if (ch == '`') {
			ch = get_next_char(q, &fmt1);
			argval = ch;
			ch = get_next_char(q, &fmt1);
		} else {
			while (iswdigit(ch)) {
				argval_specified = 1;
				noargval = 0;
				argval *= 10;
				argval += ch - '0';
				ch = get_next_char(q, &fmt1);
				continue;
			}
		}

		CHECK_BUF(argval);

		if (ch == 'n') {
			while (argval-- > 1)
				*dst++ = '\n';

			*dst++ = '\n';
			*dst = '\0';
			last_at = 0;
			start_of_line = true;
			continue;
		}

		if (ch == 'N') {
			if (!start_of_line) {
				start_of_line = true;
				CHECK_BUF(1);
				*dst++ = '\n';
				*dst = '\0';
				last_at = 0;
			}

			continue;
		}

		if (ch == 't') {
			if (!redo && !tabs) {
				save_fmt1 = tmp_fmt1;
				save_fmt2 = tmp_fmt2;
				tab_at = pos;
				tab_off = dst - tmpbuf;
				tabs++;
			} else if (!redo) {
				tabs++;
			} else if (redo) {
				tab_char = argval ? argval : ' ';
				tabs_seen++;
				int n = diff + (tabs_seen > tabs - remainder ? 1 : 0);

				for (int i = 0; i < n; i++) {
					CHECK_BUF(MAX_BYTES_PER_CODEPOINT);
					dst += put_char_utf8(dst, tab_char);
				}
			}

			continue;
		}

		if (ch == '|') {
			int at = last_at = argval ? argval : pos - 1;

			if (!tabs) {
				for (int i = 0; i < (at+1)-pos; i++) {
					CHECK_BUF(MAX_BYTES_PER_CODEPOINT);
					dst += put_char_utf8(dst, tab_char);
				}

				continue;
			}

			if (!redo) {
				if (!tabs) {
					tab_at = pos;
					dst = tmpbuf + tab_at - 1;
					diff = (at - pos) + 1;

					for (int i = 0; i < diff; i++) {
						CHECK_BUF(MAX_BYTES_PER_CODEPOINT);
						dst += put_char_utf8(dst, tab_char);
					}
				} else {
					fmt1 = save_fmt1;
					fmt2 = save_fmt2;
					dst = tmpbuf + tab_off;
					*dst = '\0';
					int total = (at - pos) + 1;
					diff = total / tabs;
					remainder = total - (diff * tabs);
					tabs_seen = 0;
				}
			} else {
				tabs = 0;
			}

			redo = !redo;
			continue;
		}

		if (ch == '+') {
			if (!redo) {
				int at = last_at = last_at + (noargval ? 8 : argval);

				if (!tabs) {
					diff = (at - pos) + 1;

					for (int i = 0; i < diff; i++) {
						CHECK_BUF(MAX_BYTES_PER_CODEPOINT);
						dst += put_char_utf8(dst, tab_char);
					}

					continue;
				} else {
					fmt1 = save_fmt1;
					fmt2 = save_fmt2;
					dst = tmpbuf + tab_off;
					*dst = '\0';
					int total = (at - pos) + 1;
					diff = total / tabs;
					remainder = total - (diff * tabs);
					tabs_seen = 0;
				}
			} else {
				tabs = 0;
			}

			redo = !redo;
			continue;
		}

		if (ch == '~') {
			CHECK_BUF(1);
			*dst++ = '~';
			*dst = '\0';
			start_of_line = false;
			continue;
		}

		if (!p2 || !is_list(p2)) {
			TPL_free(tmpbuf);
			return throw_error(q, make_nil(), q->st.cur_ctx, "domain_error", "non_empty_list");
		}

		bool is_var;
		cell *c = get_next_cell(q, &fmt2, &is_var, &c_ctx);

		if (is_var) {
			TPL_free(tmpbuf);
			return throw_error(q, p2, p2_ctx, "instantiation_error", "atom");
		}

		if (!c) {
			TPL_free(tmpbuf);
			return throw_error(q, make_nil(), p2_ctx, "domain_error", "non_empty_list");
		}

		if (ch == 'i')
			continue;

		start_of_line = false;
		size_t len = 0;

		if ((ch == 'a') && !is_atom(c)) {
			TPL_free(tmpbuf);
			return throw_error(q, c, q->st.cur_ctx, "type_error", "atom");
		}

		switch(ch) {
		case 's':
			if (is_iso_list(c) && !check_list(q, c, c_ctx, &is_partial, NULL) && is_partial) {
				TPL_free(tmpbuf);
				return throw_error(q, c, c_ctx, "instantiation_error", "atom");
			}

			if (is_nil(c)) {
			} else if (is_var(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, c_ctx, "instantiation_error", "atom");
			} else if (is_number(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, c_ctx, "type_error", "atom");
			} else if (is_atom(c)) {
				int len = noargval ? (int)C_STRLEN_UTF8(c) : MIN_OF(argval, (int)C_STRLEN_UTF8(c));
				const char *src = C_STR(q, c);

				while (len--) {
					int ch = get_char_utf8(&src);
					CHECK_BUF(MAX_BYTES_PER_CODEPOINT);
					dst += put_char_utf8(dst, ch);
					argval--;
				}

				argval -= len;
			} else {
				list_reader_t fmt3 = {0};
				fmt3.p = c;
				fmt3.p_ctx = c_ctx;
				int len = noargval ? INT_MAX : argval;

				while (is_more_data(q, &fmt3) && len--) {
					int ch = get_next_char(q, &fmt3);
					CHECK_BUF(MAX_BYTES_PER_CODEPOINT);
					dst += put_char_utf8(dst, ch);
					argval--;
				}

				len = 0;
			}

			while (!noargval && argval--) {
				CHECK_BUF(MAX_BYTES_PER_CODEPOINT);
				dst += put_char_utf8(dst, ' ');
			}

			break;

		case 'c': {
			FMT_EVAL(p1, c);
			c = &p1;

			if (is_float(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "integer");
			}

			if (!is_integer(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");
			}

			while (argval-- > 1) {
				CHECK_BUF(MAX_BYTES_PER_CODEPOINT);
				dst += put_char_utf8(dst, (int)get_smallint(c));
			}

			CHECK_BUF(MAX_BYTES_PER_CODEPOINT);
			dst += put_char_utf8(dst, (int)get_smallint(c));
			len = 0;
			break;
		}
		case 'e':
		case 'E': {
			FMT_EVAL(p1, c);
			c = &p1;

			if (!is_float(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");
			}

			len = argval < 4096 ? 4096 : argval;
			CHECK_BUF(len);

			if (argval || argval_specified) {
				if (ch == 'e')
					//len = snprintf(dst, len, "%.*e", argval?argval:1, is_float(c) ? (argval?get_float(c):floor(get_float(c))) : get_smallint(c));
					len = snprintf(dst, len, "%.*e", argval, is_float(c) ? get_float(c) : get_smallint(c));
				else
					//len = snprintf(dst, len, "%.*", argval?argval:1, is_float(c) ? (argval?get_float(c):floor(get_float(c))) : get_smallint(c));
					len = snprintf(dst, len, "%.*E", argval, is_float(c) ? get_float(c) : get_smallint(c));
			} else {
				if (ch == 'e')
					len = snprintf(dst, len, "%e", is_float(c) ? get_float(c) : get_smallint(c));
				else
					len = snprintf(dst, len, "%E", is_float(c) ? get_float(c) : get_smallint(c));
			}

			break;
		}
		case 'g':
		case 'G': {
			FMT_EVAL(p1, c);
			c = &p1;

			if (!is_float(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");
			}

			len = argval < 4096 ? 4096 : argval;
			CHECK_BUF(len);

			if (argval || argval_specified) {
				if (ch == 'g')
					//len = snprintf(dst, len, "%.*g", argval?argval:1, is_float(c) ? (argval?get_float(c):floor(get_float(c))) : get_smallint(c));
					len = snprintf(dst, len, "%.*g", argval, is_float(c) ? get_float(c) : get_smallint(c));
				else
					//len = snprintf(dst, len, "%.*G", argval?argval:1, is_float(c) ? (argval?get_float(c):floor(get_float(c))) : get_smallint(c));
					len = snprintf(dst, len, "%.*G", argval, is_float(c) ? get_float(c) : get_smallint(c));
			} else {
				if (ch == 'g')
					len = snprintf(dst, len, "%g", is_float(c) ? get_float(c) : get_smallint(c));
				else
					len = snprintf(dst, len, "%G", is_float(c) ? get_float(c) : get_smallint(c));
			}

			break;
		}
		case 'f': {
			FMT_EVAL(p1, c);
			c = &p1;

			if (!is_float(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");
			}

			len = argval < 4096 ? 4096 : argval;
			CHECK_BUF(len);

			if (argval || argval_specified)
				//len = snprintf(dst, len, "%.*f", argval?argval:1, is_float(c) ? (argval?get_float(c):floor(get_float(c))) : get_smallint(c));
				len = snprintf(dst, len, "%.*f", argval, is_float(c) ? get_float(c) : get_smallint(c));
			else
				len = snprintf(dst, len, "%f", is_float(c) ? get_float(c) : get_smallint(c));

			break;
		}
		case 'I': {
			FMT_EVAL(p1, c);
			c = &p1;

			if (is_float(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "integer");
			}

			if (!is_integer(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");
			}

			char *tmpbuf2 = print_term_to_strbuf(q, c, 0, 0);
			len = strlen(tmpbuf2);
			TPL_free(tmpbuf2);
			CHECK_BUF(len*2+1);
			len = format_integer(dst, c, noargval?3:argval, '_', 0, 10);
			break;
		}
		case 'd': {
			FMT_EVAL(p1, c);
			c = &p1;

			if (is_float(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "integer");
			}

			if (!is_integer(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");
			}

			char *tmpbuf2 = print_term_to_strbuf(q, c, 0, 0);
			len = strlen(tmpbuf2);
			TPL_free(tmpbuf2);
			CHECK_BUF(len*2+1);
			len = format_integer(dst, c, 0, ',', noargval?0:argval, 10);
			break;
		}
		case 'D': {
			FMT_EVAL(p1, c);
			c = &p1;

			if (is_float(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "integer");
			}

			if (!is_integer(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");
			}


			char *tmpbuf2 = print_term_to_strbuf(q, c, 0, 0);
			len = strlen(tmpbuf2);
			TPL_free(tmpbuf2);
			CHECK_BUF(len*2+1);
			len = format_integer(dst, c, 3, ',', noargval?0:argval, 10);
			break;
		}
		case 'r': {
			if (!noargval && ((argval < 2) || (argval > 36))) {
				TPL_free(tmpbuf);
				return throw_error(q, p1, p1_ctx, "domain_error", "radix_invalid");
			}

			FMT_EVAL(p1, c);
			c = &p1;

			if (is_float(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "integer");
			}

			if (!is_integer(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");
			}

			char *tmpbuf2 = print_term_to_strbuf(q, c, 0, 0);
			len = strlen(tmpbuf2);
			TPL_free(tmpbuf2);
			CHECK_BUF(len*10);
			len = format_integer(dst, c, 0, ',', 0, !argval?8:argval);
			break;
		}
		case 'R': {
			if (!noargval && ((argval < 2) || (argval > 36))) {
				TPL_free(tmpbuf);
				return throw_error(q, p1, p1_ctx, "domain_error", "radix_invalid");
			}

			FMT_EVAL(p1, c);
			c = &p1;

			if (is_float(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "integer");
			}

			if (!is_integer(c)) {
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "type_error", "evaluable");
			}

			char *tmpbuf2 = print_term_to_strbuf(q, c, 0, 0);
			len = strlen(tmpbuf2);
			TPL_free(tmpbuf2);
			CHECK_BUF(len*10);
			len = format_integer(dst, c, 0, ',', 0, !argval?-8:-argval);
			break;
		}
		case 'p': {
			cell *tmp;
			pl_idx num_cells;
			cell pc = {0};
			make_atom(&pc, new_atom(q->pl, "portray"));
			set_arity(&pc, 1);

			predicate *pr = find_predicate(q->st.m, &pc);

			if (pr && pr->cnt) {
				if (str) {
					cell p1[1+1+c->num_cells];
					make_instr(p1+0, new_atom(q->pl, "$portray"), NULL, 2, 1+c->num_cells);
					p1[1] = *str;
					dup_cells_by_ref(p1+2, c, c_ctx, c->num_cells);
					tmp = prepare_call(q, CALL_SKIP, p1, q->st.cur_ctx, 1);
					num_cells = p1->num_cells;
				} else {
					cell p1[1+c->num_cells];
					make_instr(p1+0, new_atom(q->pl, "$portray"), NULL, 1, c->num_cells);
					dup_cells_by_ref(p1+1, c, c_ctx, c->num_cells);
					tmp = prepare_call(q, CALL_SKIP, p1, q->st.cur_ctx, 1);
					num_cells = p1->num_cells;
				}

				make_end(tmp+num_cells);
				query *q2 = query_create_subquery(q, tmp);
				start(q2);
				query_destroy(q2);
				clear_write_options(q);
				break;
			}

			if (!str) {
				int n = q->pl->current_output;
				stream *str2 = &q->pl->streams[n];
				q->quoted = 1;
				q->numbervars = true;
				print_term_to_stream(q, str2, c, c_ctx, 1);
				q->numbervars = false;
				q->quoted = 0;

				if (isatty(fileno(str2->fp)))
					fflush(str2->fp);
			} else {
				int n = get_stream(q, str);
				stream *str2 = &q->pl->streams[n];
				q->quoted = 1;
				q->numbervars = true;
				print_term_to_stream(q, str2, c, c_ctx, 1);
				q->numbervars = false;
				q->quoted = 0;

				if (isatty(fileno(str2->fp)))
					fflush(str2->fp);
			}

			break;
		}

		case 'k':
		case 'q':
		case 'w':
		case 'a':
		{
			int saveq = q->quoted;
			bool canonical = false, quoted = false;
			q->numbervars = true;

			if (ch != 'q')
				q->double_quotes = true;

			if (ch == 'k') {
				canonical = true;
			} else if ((ch == 'q') ||(ch == 'p')) {
				quoted = true;
			}

			if (quoted)
				q->quoted = 1;

			if (is_string(c) && !q->quoted)
				q->quoted = -1;

			if (argval)
				q->max_depth = argval;

			if (canonical) {
				q->ignore_ops = true;
				q->quoted = 1;
			}

			char *tmpbuf2 = print_term_to_strbuf(q, c, c_ctx, 1);

			if (q->cycle_error) {
				TPL_free(tmpbuf2);		// the matching block below frees
				TPL_free(tmpbuf);		// both; this one did not
				return throw_error(q, c, q->st.cur_ctx, "resource_error", "cyclic");
			}

			len = strlen(tmpbuf2);
			CHECK_BUF(len);
			strcpy(dst, tmpbuf2);
			TPL_free(tmpbuf2);
			clear_write_options(q);
			q->quoted = saveq;
			break;
		}

		case 'W':
		{
			int saveq = q->quoted;
			q->numbervars = true;
			q->double_quotes = true;

			if (argval)
				q->max_depth = argval;

			pl_ctx c2_ctx;
			cell *c2 = get_next_cell(q, &fmt2, &is_var, &c2_ctx);

			if (!c2) {
				TPL_free(tmpbuf);
				return throw_error(q, c, c_ctx, "domain_error", "empty_list1");
			}

			q->flags = q->st.m->flags;
			q->numbervars = false;
			cell *vnames = NULL;
			pl_ctx vnames_ctx;
			PROLOG_LIST_HANDLER(c2);

			while (is_iso_list(c2)) {
				cell *h = PROLOG_LIST_HEAD(c2);
				h = deref(q, h, c2_ctx);
				pl_ctx h_ctx = q->latest_ctx;

				if (!parse_write_params(q, h, h_ctx, &vnames, &vnames_ctx)) {
					clear_write_options(q);
					return true;
				}

				c2 = PROLOG_LIST_TAIL(c2);
				c2 = deref(q, c2, c2_ctx);
				c2_ctx = q->latest_ctx;
			}

			if (is_var(c2)) {
				TPL_free(tmpbuf);
				clear_write_options(q);
				return throw_error(q, c2, c2_ctx, "instantiation_error", "write_option");
			}

			if (!is_nil(c2)) {
				TPL_free(tmpbuf);
				clear_write_options(q);
				return throw_error(q, c2, c2_ctx, "type_error", "list");
			}


			char *tmpbuf2 = print_term_to_strbuf(q, c, c_ctx, 1);

			if (q->cycle_error) {
				TPL_free(tmpbuf2);
				TPL_free(tmpbuf);
				return throw_error(q, c, q->st.cur_ctx, "resource_error", "cyclic");
			}

			len = strlen(tmpbuf2);
			CHECK_BUF(len);
			strcpy(dst, tmpbuf2);
			TPL_free(tmpbuf2);
			clear_write_options(q);
			q->quoted = saveq;
			break;
		}

		default:
			TPL_free(tmpbuf);
			return throw_error(q, c, q->st.cur_ctx, "existence_error", "format_character");
		}

		dst += len;
	}

	*dst = '\0';
	size_t len = dst - tmpbuf;

	if (fmt2.p) {
		cell *save_l = fmt2.p;
		pl_ctx save_l_ctx = fmt2.p_ctx, c_ctx;
		bool is_var;
		cell *c = get_next_cell(q, &fmt2, &is_var, &c_ctx);

		if (is_var) {
			TPL_free(tmpbuf);
			return throw_error(q, p2, p2_ctx, "instantiation_error", "atom");
		}

		if (c) {
			TPL_free(tmpbuf);
			return throw_error(q, save_l, save_l_ctx, "domain_error", "empty_list");
		}
	}

	if (str == NULL) {
		int n = q->st.m->pl->current_output;
		stream *str = &q->pl->streams[n];
		const char *tmpsrc = tmpbuf;

		while (len) {
			size_t tmpbuf_free = tpl_write(tmpsrc, len, str);
			FILE *fp_wr = str->fp_out ? str->fp_out : str->fp;

			if (!tmpbuf_free) {
				if (feof(fp_wr) || ferror(fp_wr)) {
					// A non-task socket is non-blocking now (see
					// bif_net.c), so a zero-progress write here is
					// routinely just EAGAIN - the peer's receive window
					// is full, not a real error - and worth waiting out
					// rather than raising. See the matching comment on
					// bif_sys_bwrite_2() in bif_streams.c for why task
					// queries are not covered by this yet, and for why
					// this checks str->fp_out rather than str->fp (the
					// read side - a different FILE* from what tpl_write()
					// above actually writes through, so this was always
					// dead before).
					if (!q->is_task && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
						clearerr(fp_wr);

						if (tpl_wait_fd_writable(q, fileno(fp_wr)))
							continue;
					}

					// An output error must raise, not print to stderr
					// and fail silently...
					TPL_free(tmpbuf);
					return throw_stream_gone(q, str);
				}
			}

			clearerr(fp_wr);
			len -= tmpbuf_free;
			tmpsrc += tmpbuf_free;
		}

		if (fflush(str->fp))
			return throw_stream_gone(q, str);
	} else if (is_compound(str)
		&& ((CMP_STRING_TO_CSTR(q, str, "atom")
		&& CMP_STRING_TO_CSTR(q, str, "chars")
		&& CMP_STRING_TO_CSTR(q, str, "string"))
		|| (get_arity(str) > 1) || !is_var(str+1))) {
		TPL_free(tmpbuf);
		return throw_error(q, str, str_ctx, "type_error", "structure");
	} else if (is_compound(str) && !CMP_STRING_TO_CSTR(q, str, "atom")) {
		cell *c = deref(q, str+1, str_ctx);
		cell tmp;
		CHECKED(make_cstringn(&tmp, tmpbuf, len), TPL_free(tmpbuf));
		unify(q, c, q->latest_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
	} else if (is_compound(str)) {
		cell *c = deref(q, str+1, str_ctx);
		cell tmp;

		if (strlen(tmpbuf))
			CHECKED(make_stringn(&tmp, tmpbuf, len), TPL_free(tmpbuf));
		else
			make_atom(&tmp, g_nil_s);

		unify(q, c, q->latest_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
	} else if (is_stream(str)) {
		int n = get_stream(q, str);
		stream *str = &q->pl->streams[n];
		const char *tmpsrc = tmpbuf;

		while (len) {
			size_t tmpbuf_free = tpl_write(tmpsrc, len, str);
			FILE *fp_wr = str->fp_out ? str->fp_out : str->fp;

			if (!tmpbuf_free) {
				if (feof(fp_wr) || ferror(fp_wr)) {
					// A non-task socket is non-blocking now (see
					// bif_net.c), so a zero-progress write here is
					// routinely just EAGAIN - the peer's receive window
					// is full, not a real error - and worth waiting out
					// rather than raising. See the matching comment on
					// bif_sys_bwrite_2() in bif_streams.c for why task
					// queries are not covered by this yet, and for why
					// this checks str->fp_out rather than str->fp (the
					// read side - a different FILE* from what tpl_write()
					// above actually writes through, so this was always
					// dead before).
					if (!q->is_task && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
						clearerr(fp_wr);

						if (tpl_wait_fd_writable(q, fileno(fp_wr)))
							continue;
					}

					// An output error must raise, not print to stderr
					// and fail silently...
					TPL_free(tmpbuf);
					return throw_stream_gone(q, str);
				}
			}

			clearerr(fp_wr);
			len -= tmpbuf_free;
			tmpsrc += tmpbuf_free;
		}

		if (fflush(str->fp))
			return throw_stream_gone(q, str);
	} else {
		TPL_free(tmpbuf);
		return throw_error(q, str, str_ctx, "domain_error", "stream_or_alias");
	}

	TPL_free(tmpbuf);
	return true;
}

static bool bif_format_1(query *q)
{
	GET_FIRST_ARG(p1,atom_or_list);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	return do_format(q, NULL, 0, p1, p1_ctx, NULL, q->st.cur_ctx);
}

static bool bif_format_2(query *q)
{
	GET_FIRST_ARG(p1,atom_or_list);
	GET_NEXT_ARG(p2,list_or_nil);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	if (is_nil(p1)) {
		if (is_nil(p2))
			return true;
		else
			return throw_error(q, p2, p2_ctx, "domain_error", "list");
	}

	return do_format(q, NULL, 0, p1, p1_ctx, !is_nil(p2)?p2:NULL, p2_ctx);
}

static bool bif_format_3(query *q)
{
	GET_FIRST_ARG(pstr,any);
	GET_NEXT_ARG(p1,atom_or_list);
	GET_NEXT_ARG(p2,list_or_nil);

	if (is_closed_stream(q->pl, pstr))
		return throw_error(q, pstr, pstr_ctx, "existence_error", "stream");

	int n = get_stream(q, pstr);

	if ((n < 0) && !is_compound(pstr)) {
		return throw_error(q, pstr, pstr_ctx, "domain_error", "stream_or_alias");
	} else if (!is_compound(pstr)) {
		stream *str = &q->pl->streams[n];

		if (!strcmp(str->mode, "read"))
			return throw_error(q, pstr, pstr_ctx, "permission_error", "output,stream");

		if (str->binary) {
			cell tmp;
			make_int(&tmp, n);
			return throw_error(q, &tmp, pstr_ctx, "permission_error", "output,binary_stream");
		}

		if (is_nil(p1)) {
			if (is_nil(p2))
				return true;
			else
				return throw_error(q, p2, p2_ctx, "domain_error", "list");
		}
	}

	return do_format(q, pstr, pstr_ctx, p1, p1_ctx, !is_nil(p2)?p2:NULL, p2_ctx);
}

builtins g_format_bifs[] =
{
	{"format", 1, bif_format_1, "+string", false, false, BLAH},
	{"format", 2, bif_format_2, "+string,+list", false, false, BLAH},
	{"format", 3, bif_format_3, "+stream,+string,+list", false, false, BLAH},

	{0}
};
