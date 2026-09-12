/*
 * Term printing (ISO/IEC 13211-1 Cor.3 §7.10.4–7.10.5).
 *
 * Frozen public API (query.h / module.h / internal.h):
 *   print_term, print_term_to_stream, print_term_to_strbuf
 *   print_canonical, print_canonical_to_stream, print_canonical_to_strbuf
 *   clear_write_options, partial_clear_write_options
 *   needs_quoting, formatted, sprint_int
 *   chars_list_to_string, string_to_chars_list
 *
 * Write-option flags on query (set by bif_streams parse_write_params):
 *   quoted, ignore_ops, numbervars, variable_names(+ctx), max_depth
 *   Trealla: json, double_quotes, varnames, nl, fullstop, portrayed
 * Dump/listing: portray_vars, is_dump_vars, do_dump_vars, dump_var_num/ctx
 * Spacing state: last_thing (WAS_OTHER|SPACE|COMMA|SYMBOL)
 *
 * Dispatch order mirrors ISO writing a1→a2→e1→e2→e3→f→h.
 * Extensions: json escapes, double_quotes chars strings, dump "ab"||I spines.
 *
 * File layout: visit/cycle → atoms/escapes → variables → lists/chars →
 * compounds (canonical / operators) → print_term_dispatch → sinks/options.
 */

#include <ctype.h>
#include <stdarg.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "module.h"
#include "network.h"
#include "parser.h"
#include "query.h"

typedef struct visit_ visit;

struct visit_ {
	visit *next;
	cell *c;
	pl_ctx c_ctx;
};

// Append to q->sb.
//
// These replace SB_sprintf(q->sb, "%s", x), which ran the argument
// through snprintf twice -- once to measure, once to write -- so any
// expression passed to it was evaluated twice. That was a live leak
// where the argument allocated: formatted() returns a strdup'd buffer
// and only the second copy was ever freed. It also made the SB_ macros'
// brace-block expansion visible at every call site, hence the stray
// `if (x) SB_putchar(...)` lines with no semicolon. These are plain
// functions: one evaluation, ordinary statement syntax.
//
// An append that finds no memory flags q->sb rather than aborting; the
// sinks below turn that into resource_error(memory), re issue #801.

static void emit_n(query *q, const char *src, size_t len)
{
	SB_try_strcatn(q->sb, src, len);
}

static void emit(query *q, const char *src)
{
	emit_n(q, src, strlen(src));
}

static void emit_char(query *q, int ch)
{
	SB_try_putchar(q->sb, ch);
}

// Small fixed-width renderings, so only the append can fail.

static void emit_sprintf(query *q, const char *fmt, ...)
{
	char tmpbuf[128];
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(tmpbuf, sizeof(tmpbuf), fmt, args);
	va_end(args);

	if (len > 0)
		emit_n(q, tmpbuf, (size_t)len < sizeof(tmpbuf) ? (size_t)len : sizeof(tmpbuf)-1);
}

// A bignum needs a buffer of its own, and converting into it allocates
// too; either failing used to drop the number or write through a null.

static bool emit_bigint(query *q, mp_int n, int radix)
{
	size_t len = mp_int_string_len(n, radix) - 1;
	char *dst = TPL_malloc(len+1);

	if (!dst) {
		q->sb_buf.oom = true;
		return false;
	}

	if (mp_int_to_string(n, radix, dst, len+1) != MP_OK) {
		TPL_free(dst);
		q->sb_buf.oom = true;
		return false;
	}

	emit(q, dst);				// mp_int_string_len() is an upper bound
	TPL_free(dst);
	return true;
}

static void emit_unget(query *q)
{
	SB_unget(q->sb);
}

// Resolve c against ctx when running, leaving both alone otherwise.
// Replaces the `if (running) x = deref(...); if (running) x_ctx =
// q->latest_ctx;` couplet that appeared at 21 sites.
static cell *deref_if(query *q, int running, cell *c, pl_ctx *ctx)
{
	if (!running)
		return c;

	c = deref(q, c, *ctx);
	*ctx = q->latest_ctx;
	return c;
}

// May this packed string be written in "..." notation? (#1103)
//
// The answer must depend only on the *content* of the list and the
// double_quotes flag in force at print time -- never on how the term
// was built. Otherwise two terms that are == print differently, eg
//
//   ?- atom_codes(A,[0]), atom_codes(A,Cs), Ds = [0], Cs = Ds.
//
// where Cs is a packed string and Ds a cons list, yet Cs == Ds.
//
// A codes list is always written in list form: that matches what the
// cons-list path does for the same content, and it is what the default
// double_quotes=codes already did for packed strings.

static bool dq_string_ok(const query *q, const cell *c)
{
	if (!q->st.m->flags.double_quote_chars)
		return false;

	if (is_string(c) && (c->flags & FLAG_CSTR_CODES))
		return false;

	return true;
}

static bool has_visited(visit *visited, cell *c, pl_ctx c_ctx)
{
	while (visited) {
		if ((visited->c == c) && (visited->c_ctx == c_ctx))
			return true;

		visited = visited->next;
	}

	return false;
}

static void clear_visited(visit *visited, visit *save_visited)
{
	while (visited != save_visited) {
		visit *tmp = visited;
		visited = visited->next;
		TPL_free(tmp);
	}
}

cell *string_to_chars_list(query *q, cell *p)
{
	PROLOG_LIST_HANDLER(p);
	init_tmp_heap(q);

	while (is_list(p)) {
		cell *h = PROLOG_LIST_HEAD(p);
		append_list(q, h);
		p = PROLOG_LIST_TAIL(p);
	}

	return end_list(q);
}

char *chars_list_to_string(query *q, cell *p_chars, pl_ctx p_chars_ctx)
{
	PROLOG_LIST_HANDLER(p_chars);
	SB(pr);

	while (is_list(p_chars)) {
		cell *h = PROLOG_LIST_HEAD(p_chars);
		h = deref(q, h, p_chars_ctx);

		if (is_integer(h)) {
			int ch = get_smallint(h);
			SB_putchar(pr, ch);
		} else {
			const char *p = C_STR(q, h);
			int ch = peek_char_utf8(p);
			SB_putchar(pr, ch);
		}

		p_chars = PROLOG_LIST_TAIL(p_chars);
		p_chars = deref(q, p_chars, p_chars_ctx);
		p_chars_ctx = q->latest_ctx;
	}

	char *tmp = TPL_malloc(SB_strlen(pr)+1+1);	// Allow for optional '.' at end, plus null
	check_error(tmp, SB_free(pr));
	strcpy(tmp, SB_cstr(pr));
	SB_free(pr);							// a list over SB_LEN long leaked it
	return tmp;
}

bool needs_quoting(module *m, const char *src, int srclen)
{
	if (!*src)
		return true;

	if (!strcmp(src, ",") || !strcmp(src, ".") || !strcmp(src, "|"))
		return true;

	if ((src[0] == '/') && (src[1] == '*'))
		return true;

	int first_ch = peek_char_utf8(src);

	if (!iswalnum(first_ch) && strchr(src, '_'))
		return true;

	if (iswupper(first_ch) || iswdigit(first_ch) || (first_ch == '_'))
		return true;

	if (!strcmp(src, "{}") || !strcmp(src, "[]")
		|| !strcmp(src, "!") || !strcmp(src, ";")
		|| !strcmp(src, "\\")
		)
		return false;

	const char *s = src;
	int slen = srclen;

	while (slen > 0) {
		slen -= len_char_utf8(s);
		int ch = get_char_utf8(&s);

		if (((ch < 256) && strchr(g_solo, ch))
			|| iswspace(ch) || iswblank(ch)
			|| (ch == 0xa0) || (ch == 0x85)
			|| (ch == 0x2007) || (ch == 0x202f)
			)
			return true;

		if (!iswalnum(ch) && (ch != '_') && !(iswgraph(ch) && (ch <= 255)))
			return true;
	}

	int cnt = 0, alphas = 0, graphs = 0;

	while (srclen > 0) {
		srclen -= len_char_utf8(src);
		int ch = get_char_utf8(&src);
		cnt++;

		if (iswalnum(ch)
#ifdef __APPLE__
			|| iswideogram(ch)
#endif
			|| (ch == '_')
			)
			alphas++;
		else if ((ch < 256) && iswgraph(ch) && (ch != '%'))
			graphs++;
		else if (iswgraph(ch) && (ch != '%')
			//&& (cnt == 1)	// Hack
			)
			graphs++;
	}

	if (cnt == alphas)
		return false;

	if (cnt == graphs)
		return false;

	return true;
}

static bool op_needs_quoting(module *m, const char *src, int srclen)
{
	if (!strcmp(src, "{}") || !strcmp(src, "[]") || !strcmp(src, "!"))
		return false;

	if (!srclen)
		return true;

	int first_ch = peek_char_utf8(src);

	if (iswupper(first_ch) || iswdigit(first_ch) || (first_ch == '_'))
		return true;

	if (match_op(m, src, NULL, false))
		return strchr(src, ' ')
			|| strchr(src, '\'')
			|| strchr(src, '\"')
			|| !strcmp(src, "(")
			|| !strcmp(src, ")")
			|| !strcmp(src, "[")
			|| !strcmp(src, "]")
			|| !strcmp(src, "{")
			|| !strcmp(src, "}");

	if (!iswlower(first_ch) || !iswalpha(first_ch)) { // NO %/
		static const char *s_symbols = "+-*<>=@#^~\\:$.";
		int quote = false;

		while (srclen--) {
			if (!strchr(s_symbols, *src)) {
				quote = true;
				break;
			}

			src++;
		}

		return quote;
	}

	while (srclen > 0) {
		int lench = len_char_utf8(src);
		int ch = get_char_utf8(&src);
		srclen -= lench;

		if (!iswalnum(ch) && (ch != '_'))
			return true;
	}

	return false;
}

static bool has_spaces(const char *src, int srclen)
{
	if (!*src)
		return true;

	while (srclen > 0) {
		int lench = len_char_utf8(src);
		int ch = get_char_utf8(&src);
		srclen -= lench;

		// A codepoint, so the wide form: isspace() is undefined past
		// unsigned char and overruns its table on some libcs.
		if (iswspace(ch))
			return true;
	}

	return false;
}

// Escape src into sb. False means it ran out of memory part-way.

static bool format_into(stringbuf *sb, const char *src, int srclen, bool dq, bool json)
{
	char tmpbuf[16];

	while (srclen > 0) {
		int lench = len_char_utf8(src);
		int ch = get_char_utf8(&src);
		srclen -= lench;
		const char *ptr = (lench == 1) && (ch != ' ') && (ch != '\e') ? strchr(g_escapes, ch) : NULL;

		if ((ch == '\'') && dq)
			ptr = 0;

		if (ch && ptr) {
			if (!sb_try_putchar(sb, '\\')) return false;
			if (!sb_try_putchar(sb, g_anti_escapes[ptr-g_escapes])) return false;
		} else if (!json && !dq && (ch == '\'')) {
			if (!sb_try_putchar(sb, '\'')) return false;
			if (!sb_try_putchar(sb, ch)) return false;
		} else if (ch == (dq?'"':'\'')) {
			if (!sb_try_putchar(sb, '\\')) return false;
			if (!sb_try_putchar(sb, ch)) return false;
		} else if ((!json && (ch > 127) && (iswblank(ch) || iswspace(ch)))
			|| (!json && ((ch == 0x85) || (ch == 0xA0) || (ch == 0x2007) || (ch == 0x202f)))
			|| (!json && (ch < ' '))) {
			snprintf(tmpbuf, sizeof(tmpbuf), "\\x%x\\", ch);
			if (!sb_try_strcat(sb, tmpbuf)) return false;
		} else if (json && (ch < ' ')) {
			if (!sb_try_putchar(sb, '\\')) return false;

			switch (ch) {
			case '\b': if (!sb_try_putchar(sb, 'b')) return false; break;
			case '\n': if (!sb_try_putchar(sb, 'n')) return false; break;
			case '\f': if (!sb_try_putchar(sb, 'f')) return false; break;
			case '\r': if (!sb_try_putchar(sb, 'r')) return false; break;
			case '\t': if (!sb_try_putchar(sb, 't')) return false; break;
			default:
				snprintf(tmpbuf, sizeof(tmpbuf), "u%04X", ch);
				if (!sb_try_strcat(sb, tmpbuf)) return false;
			}
		} else if (((unsigned)ch > 0x10000) && json) {
			unsigned ch1 = (ch - 0x10000) / 0x400 + 0xd800;
			unsigned ch2 = (ch - 0x10000) % 0x400 + 0xdc00;
			snprintf(tmpbuf, sizeof(tmpbuf), "\\u%04X", ch1);
			if (!sb_try_strcat(sb, tmpbuf)) return false;
			snprintf(tmpbuf, sizeof(tmpbuf), "\\u%04X", ch2);
			if (!sb_try_strcat(sb, tmpbuf)) return false;
		} else if (ch == '\\') {
			if (!sb_try_putchar(sb, '\\')) return false;
			if (!sb_try_putchar(sb, ch)) return false;
		} else {
			if (!sb_try_putchar(sb, ch)) return false;
		}
	}

	return true;
}

char *formatted(const char *src, int srclen, bool dq, bool json)
{
	SB(sb);

	if (!format_into(&sb_buf, src, srclen, dq, json)) {
		SB_free(sb);
		return NULL;
	}

	char *dst = TPL_strdup(SB_cstr(sb));
	SB_free(sb);
	return dst;
}

// Escaped text straight into q->sb: the copy formatted() hands back
// doubles the peak, and was the first allocation to fail on a long one.

static void emit_formatted(query *q, const char *src, int srclen, bool dq, bool json)
{
	format_into(&q->sb_buf, src, srclen, dq, json);
}

static size_t sprint_int_(char *dst, size_t dstlen, pl_int n, int pbase)
{
	int base = abs(pbase);
	const char *save_dst = dst;

	if ((n / base) > 0)
		dst += sprint_int_(dst, dstlen, n / base, pbase);

	int n2 = n % base;

	if (n2 > 9) {
		n2 -= 10;
		n2 += pbase < 0 ? 'A' : 'a';
	} else
		n2 += '0';

	if (dstlen)
		*dst++ = n2;
	else
		dst++;

	return dst - save_dst;
}

size_t sprint_int(char *dst, size_t dstlen, pl_int n, int base)
{
	const char *save_dst = dst;

	if ((n < 0) && (base == 10)) {
		if (dstlen)
			*dst++ = '-';
		else
			dst++;

		// NOTE: according to the man pages:
		//
		//		"Trying to take the absolute value of
		// 		the most negative integer is not defined."
		//

		if (n == PL_INT_MIN)
			n = imaxabs(n+1) - 1;
		else
			n = imaxabs(n);
	}

	if (n == 0) {
		if (dstlen)
			*dst++ = '0';
		else
			dst++;

		if (dstlen)
			*dst = '\0';

		return dst - save_dst;
	}

	dst += sprint_int_(dst, dstlen, n, base);

	if (dstlen)
		*dst = '\0';

	return dst - save_dst;
}

static void format_double(double num, char *res, size_t reslen) {
	snprintf(res, reslen, "%.16g", num);

	if (strtod(res, NULL) != num)
		snprintf(res, reslen, "%.17g", num);
}

// Make sure we have a trailing dot if needed...

static void reformat_float(char *tmpbuf, size_t tmplen, pl_flt v)
{
	format_double(v, tmpbuf, tmplen);
	char tmpbuf2[256];
	strcpy(tmpbuf2, tmpbuf);
	const char *src = tmpbuf2;
	char *dst = tmpbuf;

	if (*src == '-')
		*dst++ = *src++;

	while (isdigit((unsigned char)*src))
		*dst++ = *src++;

	if ((*src != '.') && (*src != ',')) {
		*dst++ = '.';
		*dst++ = '0';
	} else if (*src == ',') {
		*dst++ = '.';
		src++;
	}

	while (*src)
		*dst++ = *src++;

	*dst = '\0';
}

static const char *varformat2(char *tmpbuf, size_t tmplen, cell *c)
{
	mpz_t tmp;

	if (is_smallint(c))
		mp_int_init_value(&tmp, c->val_int);
	else
		mp_int_init_copy(&tmp, &c->val_bigint->ival);

	mp_small num;
	mp_int_mod_value(&tmp, 26, &num);
	char *dst = tmpbuf;
	dst += snprintf(dst, tmplen, "%c", 'A'+(unsigned)(num));
	mp_int_div_value(&tmp, 26, &tmp, NULL);

	if (mp_int_compare_zero(&tmp) > 0)
		dst += mp_int_to_string(&tmp, 10, dst, tmplen);

	mp_int_clear(&tmp);
	return tmpbuf;
}

static const char *varformat(char *tmpbuf, size_t tmplen, unsigned long long num, bool listing)
{
	char *dst = tmpbuf;

#if __APPLE__
	dst += snprintf(dst, tmplen, "%s%c", listing?"":"_", 'A'+(unsigned)(num%26));
	if ((num/26) > 0) dst += snprintf(dst, tmplen, "%"PRIu64"", (int64_t)(num/26));
#else
	dst += sprintf(dst, "%s%c", listing?"":"_", 'A'+(unsigned)(num%26));
	if ((num/26) > 0) dst += sprintf(dst, "%"PRIu64"", (int64_t)(num/26));
#endif

	return tmpbuf;
}

// Map a slot to a stable printed name, generating one on first sight.
//
// q->ignores[] marks name numbers that are already spoken for by
// variables the user wrote as _A, _B1 and so on, so a generated name
// never collides with a source one. Numbers are taken and never
// released within one print, so the search for a free one only ever
// moves forward: q->name_idx is that cursor, reset alongside print_idx.
//
// Past MAX_IGNORES the cursor keeps counting with nothing to record,
// which is safe because varunformat() rejects anything above _Z26 - no
// source name can reserve a number that high. It used to stop dead at
// MAX_IGNORES and hand every later variable that same number, so a term
// with more than 8192 distinct variables printed them all as _C315
// (= varformat(8192)). That is issue #1108.

// Same growth idiom as the name tables below, for the set of numbers that
// are already taken. Growing to fit rather than doubling blindly, because
// the caller may reserve a number well past the current end: a source
// variable named _Z26 asks for 701 on its own.

static bool ignore_grow(query *q, unsigned n)
{
	if (n < q->ignores_alloc)
		return true;

	if (n >= MAX_IGNORES)
		return false;

	unsigned wanted = q->ignores_alloc ? q->ignores_alloc : 64;

	while (wanted <= n)
		wanted *= 2;

	if (wanted > MAX_IGNORES)
		wanted = MAX_IGNORES;

	bool *ignores = TPL_realloc(q->ignores, wanted * sizeof(bool));

	if (!ignores)
		return false;

	memset(ignores + q->ignores_alloc, 0,
		(wanted - q->ignores_alloc) * sizeof(bool));
	q->ignores = ignores;
	q->ignores_alloc = wanted;
	return true;
}

void ignore_name(query *q, unsigned n)
{
	if (ignore_grow(q, n))
		q->ignores[n] = true;
}

bool name_is_ignored(const query *q, unsigned n)
{
	return (n < q->ignores_alloc) && q->ignores[n];
}

// Room for one more name. The tables start empty and double from small,
// so a query that prints no variables - which is most of them - allocates
// nothing, and one that prints a handful pays for a handful. MAX_TABS is
// still the ceiling, so the fallback below stays reachable and a
// pathological term cannot grow these without bound.
//
// Failing to grow is not fatal: the caller falls back to naming from the
// slot, exactly as it does past the ceiling.

static bool grow_slot_names(query *q)
{
	if (q->print_idx < q->tabs_alloc)
		return true;

	unsigned wanted = q->tabs_alloc ? q->tabs_alloc * 2 : 64;

	if (wanted > MAX_TABS)
		wanted = MAX_TABS;

	pl_idx *tab1 = TPL_realloc(q->tab1, wanted * sizeof(pl_idx));

	if (!tab1)
		return false;

	q->tab1 = tab1;
	pl_idx *tab2 = TPL_realloc(q->tab2, wanted * sizeof(pl_idx));

	if (!tab2)
		return false;

	q->tab2 = tab2;
	q->tabs_alloc = wanted;
	return true;
}

static const char *get_slot_name(query *q, pl_idx slot_nbr, bool listing, char tmpbuf[256])
{
	for (unsigned i = 0; i < q->print_idx; i++) {
		if (q->tab1[i] == slot_nbr) {
			return varformat(tmpbuf, 256, q->tab2[i], listing);
		}
	}

	// tab1/tab2 were fixed at MAX_TABS and written without a bound
	// check, so a term with more distinct variables than that corrupted
	// whatever followed - silently just above the limit, fatally a
	// little further out. Beyond it, derive the name from the slot
	// instead: still stable across occurrences of the same variable,
	// and offset past every number the cursor above can reach so it
	// cannot collide with a recorded one.

	if ((q->print_idx >= MAX_TABS) || !grow_slot_names(q))
		return varformat(tmpbuf, 256,
			(unsigned long long)MAX_IGNORES + MAX_TABS + slot_nbr, listing);

	unsigned i = q->print_idx++;
	q->tab1[i] = slot_nbr;

	while ((q->name_idx < MAX_IGNORES) && name_is_ignored(q, q->name_idx))
		q->name_idx++;

	unsigned j = q->name_idx++;
	ignore_name(q, j);

	q->tab2[i] = j;
	return varformat(tmpbuf, 256, j, listing);
}

static void print_variable(query *q, cell *c, pl_ctx c_ctx, bool running)
{
	const frame *f = GET_FRAME(running ? c_ctx : 0);
	pl_idx slot_nbr = running ?
		(pl_idx)(get_actual_slot_num(q, f, c->var_num))
		: c->var_num;

	char tmpbuf[256];

	if (q->varnames && !is_anon(c) && running && !q->cycle_error && (c_ctx == 0)) {
		if (vartab_off(q->top, c->var_num)) {
			emit(q, GET_POOL(q, vartab_off(q->top, c->var_num)));
		} else {
			emit(q, get_slot_name(q, slot_nbr, q->listing||q->portray_vars, tmpbuf));
		}
	} else if (q->portray_vars || (q->is_dump_vars && q->cycle_error)) {
		emit(q, get_slot_name(q, slot_nbr, q->listing||q->portray_vars, tmpbuf));
	} else if (q->is_dump_vars) {
		if ((c_ctx == 0) && (c->var_num < q->top->num_vars) && !is_anon(c)
			&& (strcmp(GET_POOL(q, vartab_off(q->top, c->var_num)), "_"))) {
			emit(q, GET_POOL(q, vartab_off(q->top, c->var_num)));
		} else {
			emit(q, get_slot_name(q, slot_nbr, q->listing||q->portray_vars, tmpbuf));
		}
	} else if (q->listing && is_anon(c)) {
		emit(q, C_STR(q, c));
	} else if (q->listing) {
		emit(q, get_slot_name(q, slot_nbr, q->listing||q->portray_vars, tmpbuf));
	} else if (!running && !is_ref(c)) {
		emit(q, C_STR(q, c));
	} else {
		emit_sprintf(q, "_%u", (unsigned)slot_nbr);
	}

#if 0
	if (is_global(c)) {
		emit(q, "g");
	} else if (is_void(c)) {
		emit(q, "v");
	} else if (is_local(c)) {
		emit(q, "l");
	} else if (is_temporary(c)) {
		emit(q, "t");
	}
#endif
}

// A cyclic term whose cycle begins below the variable being reported
// cannot close on that variable: S = [z|T], T = "abcdef"||T is not
// S = "zabcdef"||S, which repeats the z. The entry needs a name of its
// own, so register it here and emit _S1, _S2, ... for it; dump_vars()
// then prints an equation for each. Two answers sharing one cycle find
// it already registered and so share the name (issue #1138).

// The reported variable standing for this very term, if any. A cycle
// closing on it is named by it - X in X = [X|_A], or T when the query
// itself mentions T - rather than by a name we invent (issue #1138).

static const char *reported_var_of(query *q, const cell *c, pl_ctx c_ctx)
{
	if (!q->top)
		return NULL;

	const frame *f0 = GET_FRAME(0);

	for (unsigned i = 0; i < q->top->num_vars; i++) {
		slot *e0 = get_slot(q, f0, i);

		if (is_empty(&e0->c))
			continue;

		cell *v = deref(q, &e0->c, 0);

		if ((v != c) || (q->latest_ctx != c_ctx))
			continue;

		const char *name = GET_POOL(q, vartab_off(q->top, i));

		if (name && name[0] && strcmp(name, "_") && strcmp(name, "__G_"))
			return name;
	}

	return NULL;
}

static unsigned find_cycle_slot(query *q, uint32_t var_num, pl_ctx ctx)
{
	if (!q->is_dump_vars)
		return 0;

	for (unsigned i = 0; i < q->num_cycle_vars; i++) {
		if ((q->cycle_vars[i].var_num == var_num)
			&& (q->cycle_vars[i].ctx == ctx))
			return i + 1;
	}

	return 0;
}

static unsigned name_cycle_slot(query *q, uint32_t var_num, pl_ctx ctx)
{
	unsigned n = find_cycle_slot(q, var_num, ctx);

	if (n || !q->is_dump_vars || (q->num_cycle_vars >= MAX_CYCLE_VARS))
		return n;

	q->cycle_vars[q->num_cycle_vars].var_num = var_num;
	q->cycle_vars[q->num_cycle_vars].ctx = ctx;
	return ++q->num_cycle_vars;
}

// Name of the n'th cycle entry (1-based). Drawn from the same generated
// -name space as every other unnamed variable in the answer, so it reads
// as _A, _B like the rest and cannot collide with a source name.
// get_slot_name() keys on the slot, so every occurrence of one entry -
// and every answer sharing it - gets the same name.

const char *cycle_slot_name(query *q, unsigned n, char tmpbuf[256])
{
	const frame *f = GET_FRAME(q->cycle_vars[n-1].ctx);
	pl_idx slot_nbr = (pl_idx)get_actual_slot_num(q, f, q->cycle_vars[n-1].var_num);
	return get_slot_name(q, slot_nbr, q->listing||q->portray_vars, tmpbuf);
}

static void emit_cycle_slot(query *q, unsigned n)
{
	char tmpbuf[256];
	emit(q, cycle_slot_name(q, n, tmpbuf));
	q->last_thing = WAS_OTHER;
}

static unsigned find_cycle_var(query *q, const cell *c, pl_ctx c_ctx)
{
	if (!is_var(c))
		return 0;

	return find_cycle_slot(q, c->var_num, is_ref(c) ? c->val_ctx : c_ctx);
}

// Emit the name of the cycle entry c, registering it if new. False when
// there is no such name to give, leaving the caller its old behaviour.

static bool emit_cycle_var(query *q, const cell *c, pl_ctx c_ctx)
{
	if (!q->is_dump_vars || !is_var(c))
		return false;

	unsigned n = name_cycle_slot(q, c->var_num, is_ref(c) ? c->val_ctx : c_ctx);

	if (!n)
		return false;

	emit_cycle_slot(q, n);
	return true;
}

// True when c is a top-level query var in the unreified spine — a
// rightslicing cut-point. Named vars print as themselves; anons as `_`.
static bool is_dump_spine_var(query *q, cell *c, pl_ctx c_ctx)
{
	if (!q->is_dump_vars || !is_var(c))
		return false;

	// A cycle entry already named is a cut-point too, so its own
	// equation stops at it rather than going round again (#1138).
	if (find_cycle_var(q, c, c_ctx))
		return true;

	// So is a tail that leads straight back to the term being dumped:
	// the loop closes here, and walking on would go round it once more
	// before saying so. is_cyclic_term() cannot be used to spot this -
	// it bumps q->vgen, which is the very counter this walk's own cycle
	// marks are compared against, and wiping those hangs the printer.
	if (q->dump_var_cell) {
		cell *d = deref(q, c, c_ctx);

		if ((d == q->dump_var_cell) && (q->latest_ctx == q->dump_var_cell_ctx))
			return true;
	}

	pl_ctx ctx = is_ref(c) ? c->val_ctx : c_ctx;

	if (ctx != 0)
		return false;

	if (is_anon(c))
		return true;

	if (c->var_num >= q->top->num_vars)
		return false;

	const char *name = GET_POOL(q, vartab_off(q->top, c->var_num));

	if (!name || !name[0] || !strcmp(name, "__G_"))
		return false;

	if (!strcmp(name, "_"))
		return true;

	if (q->pl->quiet && (name[0] == '_'))
		return false;

	return true;
}

// Follow var-to-var aliases only. Full deref would walk into a binding
// like Y = -Y and lose the variable identity needed for naming (test0842);
// skipping deref entirely misses Cor.3 leftmost-alias selection (#1091).
static bool var_root_slot(query *q, cell *c, pl_ctx c_ctx, uint32_t *var_num, pl_ctx *out_ctx)
{
	if (!is_var(c) || is_anon(c))
		return false;

	if (is_ref(c))
		c_ctx = c->val_ctx;

	const frame *f = GET_FRAME(c_ctx);
	slot *e = get_slot(q, f, c->var_num);
	uint32_t vn = c->var_num;

	while (is_var(&e->c)) {
		c_ctx = e->c.val_ctx;
		vn = e->c.var_num;
		cell *next = &e->c;

		if (is_ref(next))
			c_ctx = next->val_ctx;

		f = GET_FRAME(c_ctx);
		slot *e2 = get_slot(q, f, vn);

		if (e == e2)
			break;

		e = e2;
	}

	*var_num = vn;
	*out_ctx = c_ctx;
	return true;
}

static bool dump_variable(query *q, cell *c, pl_ctx c_ctx, bool running)
{
	if (!q->variable_names)
		return false;

	uint32_t c_vn;
	pl_ctx c_root_ctx;

	if (running) {
		if (!var_root_slot(q, c, c_ctx, &c_vn, &c_root_ctx))
			return false;
	} else {
		if (!is_var(c) || is_anon(c))
			return false;
		c_vn = c->var_num;
		c_root_ctx = is_ref(c) ? c->val_ctx : c_ctx;
	}

	cell *l = q->variable_names;
	pl_ctx l_ctx = q->variable_names_ctx;
	PROLOG_LIST_HANDLER(l);

	while (is_iso_list(l)) {
		cell *h = PROLOG_LIST_HEAD(l);
		h = running ? deref(q, h, l_ctx) : h;
		pl_ctx h_ctx = running ? q->latest_ctx : l_ctx;
		cell *name = running ? deref(q, h+1, h_ctx) : h+1;
		cell *v = h+2;
		pl_ctx v_ctx = h_ctx;
		uint32_t v_vn;
		pl_ctx v_root_ctx;
		bool v_ok;

		if (running)
			v_ok = var_root_slot(q, v, v_ctx, &v_vn, &v_root_ctx);
		else if (is_var(v) && !is_anon(v)) {
			v_ok = true;
			v_vn = v->var_num;
			v_root_ctx = is_ref(v) ? v->val_ctx : v_ctx;
		} else
			v_ok = false;

		if (v_ok && (v_vn == c_vn) && (v_root_ctx == c_root_ctx)) {
			emit(q, C_STR(q, name));
			q->last_thing = WAS_OTHER;
			return true;
		}

		l = PROLOG_LIST_TAIL(l);
		l = running ? deref(q, l, l_ctx) : l;
		l_ctx = running ? q->latest_ctx : 0;
	}

	// Prefer the var's own top-level name over dump_var_num (issue #890).
	if ((c_root_ctx == 0) && (c_vn < q->top->num_vars)) {
		const char *name = GET_POOL(q, vartab_off(q->top, c_vn));

		if (name && name[0] && strcmp(name, "_") && strcmp(name, "__G_")) {
			emit(q, name);
			q->last_thing = WAS_OTHER;
			return true;
		}
	}

	c = deref(q, c, c_ctx);
	c_ctx = q->latest_ctx;

	if (q->do_dump_vars && is_cyclic_term(q, c, c_ctx)) {
		// Where the loop closes. On the variable being dumped it prints
		// as that variable (X = [X|_A]); anywhere else naming it after
		// that one would turn S = [9|T], T = [1,2|T] into S = [9,1,2|S],
		// a different term - so the entry gets a name of its own and
		// dump_vars() defines it (issue #1138).
		// A reported variable standing for this very term already names
		// it; only invent a name when none does.
		const char *name = reported_var_of(q, c, c_ctx);

		if (name) {
			emit(q, name);
			q->last_thing = WAS_OTHER;
			return true;
		}

		unsigned n = name_cycle_slot(q, c_vn, c_root_ctx);

		if (n) {
			emit_cycle_slot(q, n);
			return true;
		}

		emit(q, GET_POOL(q, vartab_off(q->top, q->dump_var_num)));
		return true;
	}

	return false;
}

static void print_string_canonical(query *q, cell *c)
{
	int cnt = 1;
	PROLOG_LIST_HANDLER(c);

	emit(q, "'.'(");

	while (is_list(c)) {
		cell *h = PROLOG_LIST_HEAD(c);

		if (is_number(h)) {
			emit_sprintf(q, "%d", (int)h->val_int);
		} else if (needs_quoting(q->st.m, C_STR(q, h), C_STRLEN(q, h))) {
			emit(q, "'");
			emit_formatted(q, C_STR(q, h), C_STRLEN(q, h), false, false);
			emit(q, "'");
		} else
			emit(q, C_STR(q, h));

		c = PROLOG_LIST_TAIL(c);

		if (!is_list(c)) {
			emit(q, ",[]");
			break;
		}

		emit(q, ",'.'(");
		cnt++;
	}

	while (cnt--) {
		emit(q, ")");
	}
}

static void print_string_list(query *q, cell *c, bool cons)
{
	PROLOG_LIST_HANDLER(c);
	if (!cons) { emit(q, "["); }
	unsigned print_list = 0;

	while (is_list(c)) {
		cell *h = PROLOG_LIST_HEAD(c);

		if (q->max_depth && (print_list >= q->max_depth)) {
			emit_unget(q);
			emit(q, "|...");
			q->last_thing = WAS_OTHER;
			//q->cycle_error = true;
			break;
		}

		if (is_number(h)) {
			emit_sprintf(q, "%d", (int)h->val_int);
		} else if (needs_quoting(q->st.m, C_STR(q, h), C_STRLEN(q, h)) && q->quoted) {
			emit(q, "'");
			emit_formatted(q, C_STR(q, h), C_STRLEN(q, h), false, false);
			emit(q, "'");
		} else{
			emit_formatted(q, C_STR(q, h), C_STRLEN(q, h), false, false);
		}

		c = PROLOG_LIST_TAIL(c);

		if (!is_list(c))
			break;

		emit(q, ",");
		print_list++;
	}

	if (!cons) { emit(q, "]"); }
}

static bool print_term_dispatch(query *q, cell *c, pl_ctx c_ctx, int running, int cons, unsigned depth, visit *);

static void print_iso_list(query *q, cell *c, pl_ctx c_ctx, int running, bool cons, unsigned depth, visit *visited)
{
	visit *save_visited = visited;
	pl_ctx orig_c_ctx = c_ctx;
	unsigned print_list = 0;

	// Tortoise-and-hare (issue #1121), via the same term_next() used by
	// skip_max_list()'s Brent's-algorithm walk. has_visited() below only
	// catches a cycle back to a node still on the C call stack (nested
	// compounds), and the single-hop check just below only catches the
	// spine looping back to *this* iteration's start. Neither sees a
	// spine that takes 2+ hops to return to an earlier node it already
	// passed - eg. A=[c|D], D=[D|A], which just cycles A,D,A,D,... here.
	cell *tortoise = c;
	pl_ctx tortoise_ctx = c_ctx;
	cell *cycle_link = NULL;
	pl_ctx cycle_link_ctx = 0;
	unsigned hops = 0;
	bool tortoise_done = false;

	while (is_iso_list(c)) {
		CHECK_INTERRUPT();
		cell *save_c = c;
		pl_ctx save_c_ctx = c_ctx;

		if (running) {
			if (hops && (c == tortoise) && (c_ctx == tortoise_ctx)) {
				emit_unget(q);
				emit(q, "|");

				if (!cycle_link || !q->do_dump_vars
					|| !dump_variable(q, cycle_link, cycle_link_ctx, running)) {
					if (!cycle_link
						|| !emit_cycle_var(q, cycle_link, cycle_link_ctx))
						emit(q, "...");
				}

				emit(q, "]");
				q->last_thing = WAS_OTHER;
				break;
			}

			if ((hops & 1) && !tortoise_done)
				tortoise = term_next(q, tortoise, &tortoise_ctx, &tortoise_done);

			hops++;
		}

		if (q->max_depth && (print_list >= q->max_depth)) {
			emit_unget(q);
			emit(q, "|...]");
			q->last_thing = WAS_OTHER;
			//q->cycle_error = true;
			break;
		}

		if (!cons) {
			emit(q, "[");
			q->last_thing = WAS_OTHER;
		}

		cell *save_head = c + 1;
		pl_ctx save_head_ctx = c_ctx;
		cell *head = save_head;
		pl_ctx head_ctx = save_head_ctx;
		head = deref_if(q, running, head, &head_ctx);
		int parens = 0;

		if (has_visited(visited, head, head_ctx)
			|| ((head == save_c) && (head_ctx == save_c_ctx))) {
			// A cycle through the head closes on the variable being
			// dumped (X = [X|_A]), unlike one along the spine - see
			// the tail case below and issue #1138.
			if (!q->do_dump_vars
				|| !dump_variable(q, save_head, save_head_ctx, running)) {
				if ((q->portray_vars || q->do_dump_vars) && ((unsigned)q->dump_var_num != (unsigned)-1))
					emit(q, GET_POOL(q, vartab_off(q->top, q->dump_var_num)));
				else
					emit(q, "...");
			}
		} else {
			bool special_op = false;

			if (is_interned(head)) {
				unsigned specifier = 0;
				unsigned priority = match_op(q->st.m, C_STR(q, head), &specifier, get_arity(head));
				special_op = (priority >= 1000);
			}

			visit me = {.next = visited, .c = head, .c_ctx = head_ctx};
			parens = is_compound(head) && special_op;
			if (parens) {  emit(q, "("); q->last_thing = WAS_OTHER; }
			q->parens = parens;
			print_term_dispatch(q, head, head_ctx, running, -1, depth+1, &me);
			q->parens = false;
		}

		q->cycle_error = false;
		if (parens) { emit(q, ")"); }

		cell *tail = c + 1; tail += tail->num_cells;
		pl_ctx tail_ctx = c_ctx;
		cell *save_tail = tail;
		pl_ctx save_tail_ctx = tail_ctx;
		tail = deref_if(q, running, tail, &tail_ctx);

		if (has_visited(visited, tail, tail_ctx)
			|| ((tail == save_c) && (tail_ctx == save_c_ctx))
			) {
			emit(q, "|");
			cell v = *(c+1);

			if ((q->portray_vars || q->do_dump_vars) && (orig_c_ctx == 0) && q->is_dump_vars) {
				if (q->do_dump_vars) {
					if (!dump_variable(q, save_tail, save_tail_ctx, running))
						print_variable(q, save_tail, save_tail_ctx, running);
				} else
					emit(q, GET_POOL(q, vartab_off(q->top, v.var_num)));
			} else if (q->is_dump_vars && q->do_dump_vars
				&& dump_variable(q, save_tail, save_tail_ctx, running)) {
				// A reported variable closes the loop under its own name
			} else if (emit_cycle_var(q, save_tail, save_tail_ctx)) {
				// The spine loops back here: name the entry rather than
				// give up with '...' (issue #1138). Only when dumping an
				// answer - write_term/2 keeps its ellipsis.
			} else {
				emit(q, "...");
			}

			emit(q, "]");
			q->last_thing = WAS_OTHER;
			q->cycle_error = true;
			break;
		}

		bool has_vars = false, is_partial = false;

		if (is_interned(tail) && !is_compound(tail)) {
			const char *src = C_STR(q, tail);

			if (strcmp(src, "[]")) {
				emit(q, "|");
				print_term_dispatch(q, tail, tail_ctx, running, true, depth+1, visited);
			}
		} else if (q->st.m->flags.double_quote_chars && running
			&& !q->ignore_ops && q->double_quotes
			&& (scan_is_chars_list2(q, tail, tail_ctx, false, &has_vars, &is_partial, NULL) > 0)
			&& !is_partial)
			{
			char *tmp_src = chars_list_to_string(q, tail, tail_ctx);
			size_t tmp_len = strlen(tmp_src);

			// A one-char tail joins the list as another element;
			// anything longer splices on as a "..." string. A lone
			// quote is the exception - it splices, so that the
			// element form never has to escape it.

			if ((tmp_len == 1) && (*tmp_src != '\'')) {
				emit(q, ",");

				if (needs_quoting(q->st.m, tmp_src, 1)) {
					emit(q, "'");
					emit_n(q, tmp_src, tmp_len);
					emit(q, "'");
				} else
					emit_n(q, tmp_src, tmp_len);
			} else {
				emit(q, "|\"");

				if ((tmp_len > 1) && needs_quoting(q->st.m, tmp_src, tmp_len))
					emit_formatted(q, tmp_src, tmp_len, true, false);
				else
					emit_n(q, tmp_src, tmp_len);

				emit(q, "\"");
			}

			TPL_free(tmp_src);
			print_list++;
		} else if (is_string(tail) && (!q->double_quotes || !dq_string_ok(q, tail))) {
			emit(q, ",");
			print_string_list(q, tail, true);
			emit(q, "]");
			q->last_thing = WAS_OTHER;
			//q->cycle_error = true;
			break;
		} else if (is_iso_list(tail)) {
			if ((tail == save_c) && (tail_ctx == save_c_ctx) && running) {
				emit(q, "|");
				//q->cycle_error = true;

				if (q->is_dump_vars) {
					if (!dump_variable(q, save_tail, save_tail_ctx, running))
						print_variable(q, save_tail, save_tail_ctx, 0);
				} else
					print_variable(q, save_tail, save_tail_ctx, 1);
			} else {
				emit(q, ",");
				q->last_thing = WAS_COMMA;
				cycle_link = save_tail;
				cycle_link_ctx = save_tail_ctx;
				c = tail;
				c_ctx = tail_ctx;
				print_list++;
				cons = true;
				continue;
			}
		} else if (is_string(tail) && q->double_quotes) {
			emit(q, "|\"");
			emit_formatted(q, C_STR(q, tail), C_STRLEN(q, tail), true, false);
			emit(q, "\"");
			print_list++;
			q->last_thing = WAS_OTHER;
		} else {
			emit(q, "|");

			if (is_var(tail)) {
				if (!dump_variable(q, tail, tail_ctx, running))
					print_variable(q, tail, tail_ctx, running);
			} else {
				visit *me = TPL_malloc(sizeof(visit));

				if (!me) {
					q->sb_buf.oom = true;
					break;
				}

				me->next = visited;
				me->c = tail;
				me->c_ctx = tail_ctx;
				visited = me;
				unsigned specifier = 0;
				unsigned priority = match_op(q->st.m, C_STR(q, tail), &specifier, get_arity(tail));
				bool tail_parens = (is_infix(tail) || is_prefix(tail)) && (priority >= 1000);
				if (tail_parens) { emit(q, "("); q->last_thing = WAS_OTHER; }
				print_term_dispatch(q, tail, tail_ctx, running, true, depth+1, visited);
				if (tail_parens) { emit(q, ")"); }
			}
		}

		if (!cons || print_list) {
			emit(q, "]");
			q->last_thing = WAS_OTHER;
		}

		break;
	}

	clear_visited(visited, save_visited);
}

static void print_iso_list_canonical(query *q, cell *c, pl_ctx c_ctx, int running, unsigned depth)
{
	cell *save_c = c;
	pl_ctx save_ctx = c_ctx;
	unsigned print_list = 0;
	int cnt = 1;
	PROLOG_LIST_HANDLER(c);

	emit(q, "'.'(");

	while (is_list(c)) {
		CHECK_INTERRUPT();

		if (q->max_depth && (print_list++ >= q->max_depth)) {
			emit(q, ",...");
			q->last_thing = WAS_OTHER;
			break;
		}

		cell *head = PROLOG_LIST_HEAD(c);
		pl_ctx head_ctx = c_ctx;
		head = deref_if(q, running, head, &head_ctx);
		bool special_op = false;

		if (is_interned(head)) {
			unsigned specifier = 0;
			unsigned priority = match_op(q->st.m, C_STR(q, head), &specifier, get_arity(head));
			special_op = (priority >= 1000);
		}

		bool parens = is_compound(head) && special_op;
		if (parens) {  emit(q, "("); q->last_thing = WAS_OTHER; }
		q->parens = parens;
		print_term_dispatch(q, head, head_ctx, running, -1, depth+1, NULL);
		q->parens = false;
		if (parens) { emit(q, ")"); }

		c = PROLOG_LIST_TAIL(c);
		c = deref_if(q, running, c, &c_ctx);

		if (!is_list(c)) {
			emit(q, ",");
			print_term_dispatch(q, c, c_ctx, running, -1, depth+1, NULL);
			break;
		}

		if ((c == save_c) && (c_ctx == save_ctx)) {
			emit(q, ",...");
			q->last_thing = WAS_OTHER;
			break;
		}

		emit(q, ",'.'(");
		cnt++;
	}

	while (cnt--) {
		emit(q, ")");
	}
}

static void print_list(query *q, cell *c, pl_ctx c_ctx, int running, bool cons, unsigned depth, visit *visited)
{
	/* ISO e2: list notation when !ignore_ops; else canonical '.'/2 */
	if (q->ignore_ops)
		print_iso_list_canonical(q, c, c_ctx, running, depth);
	else
		print_iso_list(q, c, c_ctx, running, cons, depth, visited);
}

static bool print_canonical_compound(query *q, cell *c, pl_ctx c_ctx, bool running, unsigned depth, visit *visited,
	const char *src, size_t src_len)
{
	/* ISO e1 numbervars / e3 {} / f canonical / atoms */
	bool is_needs_quoting = needs_quoting(q->st.m, src, src_len);
	int quote = ((running <= 0) || q->quoted) && !is_var(c) && is_needs_quoting;
	int dq = 0, braces = 0;
	if (is_string(c) && q->double_quotes && dq_string_ok(q, c)) dq = quote = 1;
	if (q->quoted < 0) quote = 0;
	if ((get_arity(c) == 1) && is_interned(c) && !strcmp(src, "{}")) braces = 1;
	cell *c1 = get_arity(c) && running ? deref(q, FIRST_ARG(c), c_ctx) : NULL;

	if (running && is_interned(c) && get_arity(c)
		&& q->numbervars && (c->val_off == g_sys_var_s) && c1
		&& is_integer(c1) && (get_smallint(c1) >= 0)) {
		char tmpbuf[256];
		emit(q, varformat2(tmpbuf, sizeof(tmpbuf), c1));
		q->last_thing = WAS_OTHER;
		return true;
	}

	emit(q, !braces&&quote?dq?"\"":"'":"");

	unsigned len_str = src_len;

	if (braces && !q->ignore_ops)
		;
	else if (quote) {
		if (is_blob(c) && q->max_depth && (len_str >= q->max_depth) && (src_len > 128))
			len_str = q->max_depth;

		emit_formatted(q, src, len_str, dq, q->json);

		if (is_blob(c) && q->max_depth && (len_str > q->max_depth) && (src_len > 128)) {
			emit_unget(q);
			emit(q, "...");
			q->last_thing = WAS_SYMBOL;
		} else
			q->last_thing = WAS_OTHER;
	} else {
		int ch = peek_char_utf8(src);
		bool is_symbol = !needs_quoting(q->st.m, src, src_len) && !iswalpha(ch)
			&& strcmp(src, "\\") && strcmp(src, ",") && strcmp(src, ";")
			&& strcmp(src, "[]") && strcmp(src, "{}") && !q->parens;

		if ((q->last_thing == WAS_SYMBOL) && is_symbol && !q->parens && !quote
			&& (get_arity(c) == 1) // Only if prefix
			) {
			emit(q, " ");
			q->last_thing = WAS_SPACE;
		}

		emit_n(q, src, len_str);
		q->last_thing = is_symbol ? WAS_SYMBOL : WAS_OTHER;
	}

	emit(q, !braces&&quote?dq?"\"":"'":"");
	q->did_quote = !braces&&quote;

	if (is_compound(c) && !is_string(c)) {
		uint32_t arity = get_arity(c);
		emit(q, braces&&!q->ignore_ops?"{":"(");
		q->last_thing = WAS_OTHER;
		q->parens = true;

		for (c++; arity--; c += c->num_cells) {
			cell *tmp = c;
			pl_ctx tmp_ctx = c_ctx;
			tmp = deref_if(q, running, tmp, &tmp_ctx);
			bool is_cyclic = has_visited(visited, tmp, tmp_ctx);

			if (q->is_dump_vars && is_cyclic) {
				// Where the cycle closes. '...' was the only answer for
				// an entry no query variable named, which reads as a
				// depth elision even when no max_depth is set (issue
				// #1138) - name it instead, as the list spine does.
				// has_visited() already established the cycle, so no
				// is_cyclic_term() call here: it bumps q->vgen, which
				// an enclosing list walk is using to spot its own.
				const char *nm;

				if (c_ctx == 0) {
					emit(q, GET_POOL(q, vartab_off(q->top, c->var_num)));
				} else if ((nm = reported_var_of(q, tmp, tmp_ctx)) != NULL) {
					emit(q, nm);
				} else if (emit_cycle_var(q, c, c_ctx)) {
					;
				} else {
					emit(q, "...");
				}
				if (arity) { emit(q, ","); }
				q->last_thing = WAS_OTHER;
				continue;
			}

			if (q->max_depth && ((depth+!braces) >= q->max_depth)) {
				if (q->variable_names && is_var(c)) {
					//if (!dump_variable(q, c, c_ctx, running))
					//	print_variable(q, c, c_ctx, running);
					emit(q, "...");
				} else if (is_var(c) && q->top && (c->var_num < q->top->num_vars)) {
					// Guard the vartab the way the other lookups do: a
					// thread or engine query has no parser at all, so
					// this read ran off a NULL q->top and crashed.
					emit(q, GET_POOL(q, vartab_off(q->top, c->var_num)));
				} else {
					emit(q, "...");
				}

				q->last_thing = WAS_SYMBOL;

				if (arity) {
					emit(q, ",");
					q->last_thing = WAS_OTHER;
				}
				continue;
			}

			bool parens = false;

			if (!braces && is_interned(tmp) && !q->ignore_ops) {
				unsigned tmp_priority = match_op(q->st.m, C_STR(q, tmp), NULL, get_arity(tmp));

				if ((tmp_priority >= 1000) && get_arity(tmp))
					q->parens = parens = true;
			}

			if (parens) { emit(q, "("); q->last_thing = WAS_OTHER; }

			visit me = {.next = visited, .c = tmp, .c_ctx = tmp_ctx};
			q->parens = parens;
			print_term_dispatch(q, tmp, tmp_ctx, running, 0, depth+1, &me);
			q->parens = false;
			if (parens) {emit(q, ")"); }
			if (arity) {emit(q, ","); }
		}

		emit(q, braces&&!q->ignore_ops?"}":")");
		q->parens = false;
	} else if (q->last_thing != WAS_SYMBOL)
		q->last_thing = WAS_OTHER;

	return true;
}

static bool print_operator(query *q, cell *c, pl_ctx c_ctx, bool running, unsigned depth, visit *visited,
	const char *src, size_t src_len, unsigned my_specifier, unsigned my_priority)
{
	/* ISO h — operator form; last_thing tracks spacing to avoid ambiguity */
	bool is_op_infix = IS_INFIX(my_specifier);
	bool is_op_prefix = IS_PREFIX(my_specifier);
	bool is_op_postfix = IS_POSTFIX(my_specifier);
	bool is_op_yfx = is_op_infix && (my_specifier == OP_YFX);
	bool is_op_xfy = is_op_infix && (my_specifier == OP_XFY);
	size_t srclen = src_len;

	if (is_op_postfix) {
		cell *lhs = c + 1;
		cell *save_lhs = lhs;
		pl_ctx lhs_ctx = c_ctx;
		lhs = deref_if(q, running, lhs, &lhs_ctx);
		unsigned lhs_specifier = false;
		unsigned lhs_pri = is_interned(lhs) ? match_op(q->st.m, C_STR(q, lhs), &lhs_specifier, get_arity(lhs)) : 0;
		bool is_lhs_xf = IS_XF(lhs_specifier);
		bool is_lhs_yf = IS_YF(lhs_specifier);
		bool is_op_lhs = lhs_pri;
		bool parens = is_lhs_xf;
		bool space = (c->val_off == g_minus_s) && (is_number(lhs) || is_op_lhs);
		if ((c->val_off == g_plus_s) && is_op_lhs) space = true;
		int ch = peek_char_utf8(src);
		if (iswalpha(ch)) space = true;
		if (lhs_pri > my_priority) { parens = true; space = false; }

		if (!is_var(lhs) && q->max_depth && ((depth+1) >= q->max_depth)) {
			if (q->last_thing != WAS_SPACE) emit(q, " ");
			emit(q, "...");
			q->last_thing = WAS_SYMBOL;
			return true;
		} else {
			visit me = {.next = visited, .c = lhs, .c_ctx = lhs_ctx};

			// FIXME: this shadows the lhs_ctx deref'd above, so the
			// visit record and the recursive call below disagree
			// about the context, and when !running the shadow is a
			// hard 0 rather than c_ctx. Left as-is: it is the only
			// postfix path and no test distinguishes the two, so
			// which one is intended needs deciding, not guessing.

			pl_ctx lhs_ctx = running ? q->latest_ctx : 0;

			if (parens) { emit(q, "("); q->last_thing = WAS_OTHER; }
			print_term_dispatch(q, lhs, lhs_ctx, running, 0, depth+1, &me);
			if (parens) { emit(q, ")"); q->last_thing = WAS_OTHER; }
			q->last_thing = WAS_OTHER;
		}

		if (q->is_dump_vars && has_visited(visited, lhs, lhs_ctx)) {
			if (q->is_dump_vars) {
				emit(q, !is_ref(save_lhs) ? C_STR(q, save_lhs) : "_");
			} else
				print_variable(q, save_lhs, lhs_ctx, 1);

			q->last_thing = WAS_OTHER;
			return true;
		}

		if ((q->last_thing != WAS_SPACE) && (space || is_lhs_yf)) {
			emit(q, " ");
			q->last_thing = WAS_SPACE;
		}

		int quote = q->quoted && needs_quoting(q->st.m, src, src_len);
		if (quote) {
			if (!parens) emit(q, " ");
			emit(q, quote?"'":"");
		}

		emit_n(q, src, srclen);
		if (quote) { emit(q, quote?"'":""); }
		//if (q->last_thing != WAS_SPACE) { emit(q, " "); q->last_thing = WAS_SPACE; }
		else q->last_thing = WAS_OTHER;
		return true;
	}

	if (is_op_prefix) {
		if (q->last_thing == WAS_SYMBOL) {
			emit(q, " ");
			q->last_thing = WAS_SPACE;
		}

		cell *rhs = c + 1;
		cell *save_rhs = rhs;
		pl_ctx rhs_ctx = c_ctx;
		const char *rhs_src = C_STR(q, rhs);
		rhs = deref_if(q, running, rhs, &rhs_ctx);
		unsigned rhs_pri = is_interned(rhs) ? match_op(q->st.m, C_STR(q, rhs), NULL, get_arity(rhs)) : 0;
		bool is_op_rhs = rhs_pri;

		if ((q->last_thing == WAS_SYMBOL) && !strcmp(src, "\\+")) {
			emit(q, " ");
			q->last_thing = WAS_SPACE;
		}

		bool space = (c->val_off == g_minus_s) && (is_number(rhs) || is_op_rhs);
		if ((c->val_off == g_plus_s) && is_op_rhs) space = true;
		int ch = peek_char_utf8(src);
		if (iswalpha(ch)) space = true;
		if (/*is_op_rhs ||*/ is_negative(rhs) || is_float(rhs)) space = true;
		if (is_interned(rhs) && !iswalpha(peek_char_utf8(rhs_src)) && !is_op(rhs)) space = true;

		bool parens = false;
		if ((!strcmp(src, ":-") || !strcmp(src, "?-")) && (rhs_pri >= my_priority)) parens = true;
		if (!strcmp(src, "+") && (is_infix(rhs) || is_postfix(rhs))) parens = true;
		if (rhs_pri > my_priority) parens = true;
		if ((rhs_pri > 0) && !get_arity(rhs)) parens = true;
		//if (my_priority && (rhs_pri == my_priority) && strcmp(src, "-") && strcmp(src, "+")) parens = true;
		if (!strcmp(src, "-") && (rhs_pri == my_priority) && (get_arity(rhs) > 1)) parens = true;
		if ((c->val_off == g_minus_s) && is_number(rhs) && !is_negative(rhs)) parens = true;
		if ((c->val_off == g_minus_s) && match_op(q->st.m, C_STR(q, rhs), NULL, true) && !get_arity(rhs)) parens = true;
		if ((c->val_off == g_plus_s) && match_op(q->st.m, C_STR(q, rhs), NULL, true) && !get_arity(rhs)) parens = true;

		if (!strcmp(src, "?-") || !strcmp(src, ":-")) space = 1;

		bool quote = q->quoted && needs_quoting(q->st.m, src, src_len);

		if (is_interned(rhs) && !get_arity(rhs) && !parens) {
			if (!iswalnum(peek_char_utf8(rhs_src)) && strcmp(rhs_src, "[]") && strcmp(rhs_src, "{}"))
				space = 1;
		}

		if (quote) { emit(q, quote?"'":""); }
		emit_n(q, src, srclen);
		if (quote)
			{ emit(q, quote?"' ":""); q->last_thing = WAS_SPACE; }
		else if (!iswalpha(peek_char_utf8(src)))
			q->last_thing = WAS_SYMBOL;
		else
			q->last_thing = WAS_OTHER;

		if (q->is_dump_vars && has_visited(visited, rhs, rhs_ctx)) {
			if (!dump_variable(q, save_rhs, rhs_ctx, 1))
				print_variable(q, save_rhs, rhs_ctx, 1);

			q->last_thing = WAS_OTHER;
			return true;
		}

		if ((q->last_thing != WAS_SPACE) && (space || parens)) {
			emit(q, " ");
			q->last_thing = WAS_SPACE;
		}

		if (!is_var(rhs) && q->max_depth && ((depth+1) >= q->max_depth)) {
			if (!space) { emit(q, " "); }
			emit(q, "...");
			q->last_thing = WAS_SYMBOL;
			return true;
		}

		visit me = {.next = visited, .c = rhs, .c_ctx = rhs_ctx};

		if (parens) { emit(q, "("); q->last_thing = WAS_OTHER; }
		q->parens = parens;
		print_term_dispatch(q, rhs, rhs_ctx, running, 0, depth+1, &me);
		q->parens = false;
		if (parens) { emit(q, ")"); q->last_thing = WAS_OTHER; }
		return true;
	}

	// Is infix

	cell *lhs = c + 1;
	cell *save_lhs = lhs;
	pl_ctx lhs_ctx = c_ctx;
	cell *rhs = lhs + lhs->num_cells;
	cell *save_rhs = rhs;
	pl_ctx rhs_ctx = c_ctx;
	lhs = deref_if(q, running, lhs, &lhs_ctx);
	rhs = deref_if(q, running, rhs, &rhs_ctx);
	const char *lhs_src = C_STR(q, lhs);
	const char *rhs_src = C_STR(q, rhs);

	int quote = q->quoted && has_spaces(src, src_len);
	if (op_needs_quoting(q->st.m, src, src_len)) quote = 1;

	// Print LHS..

	unsigned lhs_specifier = 0;
	unsigned lhs_pri_1 = is_interned(lhs) ? match_op(q->st.m, C_STR(q, lhs), &lhs_specifier, get_arity(lhs)) : 0;
	unsigned lhs_pri_2 = is_interned(lhs) && !get_arity(lhs) ? match_op(q->st.m, C_STR(q, lhs), &lhs_specifier, true) : 0;
	bool lhs_postfix = (get_arity(lhs) == 1) && IS_POSTFIX(lhs_specifier);

	bool lhs_parens = lhs_pri_1 >= my_priority;
	//if (lhs_postfix) lhs_parens = true;
	if ((lhs_pri_1 == my_priority) && is_op_yfx) lhs_parens = false;
	if (lhs_pri_2 > 0) lhs_parens = true;
	if (is_compound(lhs) && (lhs_pri_1 <= my_priority) && (lhs->val_off == g_plus_s)) { lhs_parens = false; }
	bool lhs_space = lhs_postfix;

	if ((q->last_thing != WAS_SPACE) && lhs_space) {
		emit(q, " ");
		q->last_thing = WAS_SPACE;
	}

	if (!is_var(lhs) && q->max_depth && ((depth+1) >= q->max_depth)) {
		if (q->last_thing != WAS_SPACE) emit(q, " ");
		emit(q, "...");
		q->last_thing = WAS_SYMBOL;
	} else if (q->is_dump_vars && has_visited(visited, lhs, lhs_ctx)) {
		if (q->is_dump_vars) {
			emit(q, !is_ref(save_lhs) ? C_STR(q, save_lhs) : "_");
		} else
			print_variable(q, save_lhs, lhs_ctx, 1);

		q->last_thing = WAS_OTHER;
	} else {
		visit me = {.next = visited, .c = lhs, .c_ctx = lhs_ctx};
		if (lhs_parens) { emit(q, "("); q->last_thing = WAS_OTHER; }
		q->parens = lhs_parens;
		print_term_dispatch(q, lhs, lhs_ctx, running, 0, depth+1, &me);
		q->parens = false;
		if (lhs_parens) { emit(q, ")"); q->last_thing = WAS_OTHER; }
	}

	bool space = false;

	if (is_interned(lhs) && !get_arity(lhs) && !lhs_parens) {
		if (!iswalpha(peek_char_utf8(lhs_src)) && !iswdigit(peek_char_utf8(lhs_src)) && (peek_char_utf8(lhs_src) != '$')
			&& strcmp(src, ",") && strcmp(src, ";")
			&& strcmp(lhs_src, "[]") && strcmp(lhs_src, "{}")
			)
			space = true;
	}

	bool extra_space = false;

	if ((q->last_thing != WAS_SPACE) && (space || lhs_space) && !quote) {
		emit(q, " ");
		q->last_thing = WAS_SPACE;
		if (!lhs_space) extra_space = true;
	}

	int ch = peek_char_utf8(src);
	bool is_symbol = !needs_quoting(q->st.m, src, src_len)
		&& !iswalpha(ch) && strcmp(src, ",") && strcmp(src, ";")
		&& strcmp(src, "[]") && strcmp(src, "{}") && !q->parens;

	if (!*src || ((q->last_thing == WAS_SYMBOL) && is_symbol && !lhs_parens && !q->parens))
		space = true;

	if ((q->last_thing != WAS_SPACE) && !is_symbol && space && !quote) {
		emit(q, " ");
		q->last_thing = WAS_SPACE;
	}

	// Print OP..

	//q->last_thing_was_symbol += is_symbol;
	space = iswalpha(peek_char_utf8(src)) || (q->last_thing == WAS_SYMBOL);

	if (!strcmp(lhs_src, "!"))
		space = false;

	if (q->listing && !depth && !strcmp(src, ":-"))
		space = true;

	if ((q->last_thing != WAS_SPACE) && space && !quote) {
		emit(q, " ");
		q->last_thing = WAS_SPACE;
	}

	if (quote) { emit(q, quote?"'":""); }
	emit_n(q, src, srclen);
	if (quote) { emit(q, quote?"'":""); }
	q->last_thing = strcmp(src, "|") ? WAS_SYMBOL : WAS_OTHER;

	if (q->listing && !depth && !strcmp(src, ":-")) {
		emit(q, "\n  ");
	}

	if (extra_space)
		space = true;

	if ((q->last_thing != WAS_SPACE) && space && !quote) {
		emit(q, " ");
		q->last_thing = WAS_SPACE;
	}

	// Print RHS..

	unsigned rhs_pri_1 = is_interned(rhs) ? match_op(q->st.m, C_STR(q, rhs), NULL, get_arity(rhs)) : 0;
	unsigned rhs_pri_2 = is_interned(rhs) && !get_arity(rhs) ? match_op(q->st.m, C_STR(q, rhs), NULL, true) : 0;
	bool rhs_parens = rhs_pri_1 >= my_priority;
	space = is_number(rhs) && is_negative(rhs);

	if (!rhs_parens && is_prefix(rhs) && strcmp(src, "|"))
		space = true;

	bool rhs_is_symbol = is_interned(rhs) && !get_arity(rhs)
		&& !iswalpha(peek_char_utf8(rhs_src))
		&& !needs_quoting(q->st.m, C_STR(q, rhs), C_STRLEN(q, rhs))
		&& strcmp(C_STR(q, rhs), "[]") && strcmp(C_STR(q, rhs), "{}")
		&& !rhs_parens;

	if (rhs_is_symbol && strcmp(C_STR(q, rhs), "[]") && strcmp(C_STR(q, rhs), "{}") && strcmp(C_STR(q, rhs), "!"))
		space = true;

	if ((rhs_pri_1 == my_priority) && is_op_xfy)
		rhs_parens = false;

	if (rhs_pri_2 > 0)
		rhs_parens = true;

	if ((q->last_thing != WAS_SPACE) && space && !rhs_parens && !quote) {
		emit(q, " ");
		q->last_thing = WAS_SPACE;
	}

	if (!is_var(rhs) && q->max_depth && ((depth+1) >= q->max_depth)) {
		if (q->last_thing != WAS_SPACE) emit(q, " ");
		emit(q, "...");
		q->last_thing = WAS_SYMBOL;
	} else if (q->is_dump_vars && has_visited(visited, rhs, rhs_ctx)) {
		if (q->is_dump_vars) {
			emit(q, !is_ref(save_rhs) ? C_STR(q, save_rhs) : "_");
		} else
			print_variable(q, save_rhs, rhs_ctx, 1);

		q->last_thing = WAS_OTHER;
	} else {
		visit me = {.next = visited, .c = rhs, .c_ctx = rhs_ctx};
		if (rhs_parens) { emit(q, "("); q->last_thing = WAS_OTHER; }
		q->parens = rhs_parens || space;
		print_term_dispatch(q, rhs, rhs_ctx, running, 0, depth+1, &me);
		q->parens = false;
		if (rhs_parens) { emit(q, ")"); q->last_thing = WAS_OTHER; }
		else if (rhs_is_symbol) { q->last_thing = WAS_SYMBOL; }
	}

	return true;

}

static bool print_interned(query *q, cell *c, pl_ctx c_ctx, bool running, unsigned depth, visit *visited)
{
	// ATOM / COMPOUND — choose ISO f/e3 vs h
	const char *src = !is_ref(c) ? C_STR(q, c) : "_";
	size_t src_len = !is_ref(c) ? C_STRLEN(q, c) : 1;

	// '()'(foo) writes as foo(), so a term read under the empty_args
	// flag reads back as itself. Only under the flag: with it off, foo()
	// would not read back at all, and '()'(foo) - which always does - is
	// the honest output. The argument has to be an atom for the same
	// reason; '()'(1) would print as 1(), which reads back as nothing.

	if ((get_arity(c) == 1) && is_interned(c) && (src_len == 2)
		&& !strcmp(src, "()") && q->st.m->flags.empty_args) {
		cell *arg = FIRST_ARG(c);
		pl_ctx arg_ctx = c_ctx;
		arg = deref_if(q, running, arg, &arg_ctx);

		if (is_interned(arg) && !get_arity(arg)) {
			print_term_dispatch(q, arg, arg_ctx, running, 0, depth+1, visited);
			emit(q, "()");
			q->last_thing = WAS_OTHER;
			return true;
		}
	}
	unsigned my_specifier = 0;
	unsigned my_priority = match_op(q->st.m, src, &my_specifier, get_arity(c));

	if (!my_priority
		|| ((IS_PREFIX(my_specifier) || IS_POSTFIX(my_specifier)) && (get_arity(c) != 1))
		|| (IS_INFIX(my_specifier) && (get_arity(c) != 2))
		) {
		my_priority = 0;
	}

	bool is_op = my_priority;
	unsigned pri = 0, spec = 0;

	if (!is_op && !is_var(c) && (get_arity(c) == 1)
		&& (pri = match_op(q->st.m, src, &spec, get_arity(c)))) {
		if (IS_PREFIX(spec)) {
			is_op = true;
			my_specifier = spec;
			my_priority = pri;
		}
	}

	if (q->ignore_ops || !is_op || !get_arity(c))
		return print_canonical_compound(q, c, c_ctx, running, depth, visited, src, src_len);

	return print_operator(q, c, c_ctx, running, depth, visited, src, src_len, my_specifier, my_priority);
}


static bool print_chars_quoted(query *q, cell *c, pl_ctx c_ctx, int running, unsigned depth)
{
	/* Trealla double_quotes / #890 rightsplice chars printing */
	int is_chars_list = is_string(c) && q->double_quotes && dq_string_ok(q, c);
	bool possible_chars = false, has_var = false, is_partial = false;
	cell *v = NULL;

	if (is_interned(c) && (C_STRLEN_UTF8(c) == 1) && !q->ignore_ops && q->double_quotes)
		possible_chars = true;

	if (!is_chars_list && running && possible_chars
		&& (scan_is_chars_list2(q, c, c_ctx, false, &has_var, &is_partial, &v) > 0))
		is_chars_list += q->st.m->flags.double_quote_chars && scan_is_chars_list2(q, c, c_ctx, false, &has_var, &is_partial, &v);

	if (!is_chars_list)
		return false;

	cell *l = c;
	pl_ctx l_ctx = c_ctx;
	emit(q, "\"");
	unsigned cnt = 0;
	PROLOG_LIST_HANDLER(l);
	bool closing_quote = true;
	bool any = false, done = false;
	cell *cut_var = NULL;
	pl_ctx cut_var_ctx = 0;
	cell *cycle_entry = NULL;		// the node the spine loops back to
	pl_ctx cycle_entry_ctx = 0;

	// Fresh visit gen so marks left by scan_is_chars_list2 do not
	// make the print walk think it has already hit a cycle (#890).
	if (running) {
		if (++q->vgen == 0) q->vgen = 1;
	}

	while (is_list(l)) {
		if (q->max_depth && (cnt++ >= q->max_depth)) {
			emit(q, "\"||... ");
			closing_quote = false;
			done = true;
			break;
		}

		cell *h = PROLOG_LIST_HEAD(l);
		pl_ctx h_ctx = l_ctx;
		slot *e = NULL;
		uint32_t save_vgen = 0;
		int both = 0;

		if (running) {
			DEREF_VAR(any, both, save_vgen, e, e->vgen, h, h_ctx, q->vgen);
			if (e) e->vgen = save_vgen;
		}

		if (!both && (c->flags & FLAG_CSTR_CODES) && (h->val_uint < ' ')) {
			char tmpbuf[2];
			tmpbuf[0] = h->val_uint;
			tmpbuf[1] = 0;
			emit_formatted(q, tmpbuf, 1, true, q->json);
		} else if (is_smallint(h) && !both) {
			emit_char(q, h->val_uint);
		} else {
			emit_formatted(q, C_STR(q, h), C_STRLEN(q, h), true, q->json);
		}

		cell *tail_cell = PROLOG_LIST_TAIL(l);

		// Rightslicing: stop at a query var in the unreified
		// spine so mutual cycles print as L="ab"||I, I="cd"||L (#890).
		if (is_dump_spine_var(q, tail_cell, l_ctx)) {
			cut_var = tail_cell;
			cut_var_ctx = l_ctx;
			is_partial = true;
			break;
		}

		l = tail_cell;
		e = NULL;
		both = 0;
		any = false;

		// Keep the undereferenced tail: if it turns out to close the
		// cycle it is the node to name, not the term it derefs to.
		cell *entry = l;
		pl_ctx entry_ctx = l_ctx;

		if (running) DEREF_VAR(any, both, save_vgen, e, e->vgen, l, l_ctx, q->vgen);

		if (both) {
			q->cycle_error = true;
			cycle_entry = entry;
			cycle_entry_ctx = entry_ctx;
			break;
		}
	}

	if (closing_quote) emit(q, "\"");

	if (is_partial && !done) {
		emit(q, "||");
		if (cut_var) {
			if (find_cycle_var(q, cut_var, cut_var_ctx)) {
				emit_cycle_var(q, cut_var, cut_var_ctx);
			} else if (is_anon(cut_var)) {
				emit(q, "_");
			} else if (!dump_variable(q, cut_var, cut_var_ctx, 0)) {
				print_variable(q, cut_var, cut_var_ctx, 0);
			}
		} else if (q->cycle_error) {
			// The loop closes on cycle_entry, which is the reported
			// variable only when the cycle starts at the very top; any
			// prefix before it makes closing on the root a different
			// term (issue #1138). Prefer that variable's own name and
			// only name the entry separately when it has none.
			if (cycle_entry
				&& dump_variable(q, cycle_entry, cycle_entry_ctx, running))
				;
			else if (emit_cycle_var(q, cycle_entry, cycle_entry_ctx))
				;
			else if (!dump_variable(q, v?v:c, c_ctx, !v))
				print_variable(q, v?v:c, c_ctx, !v);
		} else {
			if (is_op(l)) {
				emit_char(q, '(');
			}
			print_term_dispatch(q, l, 0, running, 0, depth+1, NULL);
			if (is_op(l)) {
				emit_char(q, ')');
			} else if (q->last_thing) {
				emit_char(q, ' ');
			}
		}
	}

	q->last_thing = WAS_OTHER;
	return true;
}

static bool print_term_dispatch(query *q, cell *c, pl_ctx c_ctx, int running, int cons, unsigned depth, visit *visited)
{
	/* ISO/Cor.3 7.10.5 write_term decision tree (plus Trealla specials). */

	if (depth > g_max_depth) {
		emit(q, "...");
		q->cycle_error = true;
		q->last_thing = WAS_OTHER;
		return false;
	}

	// a1 / a2 — variables (variable_names leftmost; else _…)
	if (is_var(c)) {
		if (!dump_variable(q, c, c_ctx, running))
			print_variable(q, c, c_ctx, running);
		q->last_thing = WAS_OTHER;
		return true;
	}

	// THREAD OBJECTS

	if ((c->tag == TAG_INT) && (c->flags & FLAG_INT_THREAD)) {
		int n = get_smallint(c);
		thread *t = find_thread_by_id(q->pl, n);

		// A retired id has no entry any more. It still prints, as a
		// thread - there is nothing left to say which kind it was.

		if (!t) {
			emit_sprintf(q, "'$thread'(%d)", (int)get_smallint(c));
		} else if (t->is_queue_only) {
			emit_sprintf(q, "'$queue'(%d)", (int)get_smallint(c));
		} else if (t->is_mutex_only) {
			emit_sprintf(q, "'$mutex'(%d)", (int)get_smallint(c));
		} else {
			emit_sprintf(q, "'$thread'(%d)", (int)get_smallint(c));
		}

		q->last_thing = WAS_OTHER;
		return true;
	}

	// ALIAS

	if ((c->tag == TAG_INT) && (c->flags & FLAG_INT_ALIAS)) {
		emit_sprintf(q, "'$alias'(%d)", (int)get_smallint(c));
		q->last_thing = WAS_OTHER;
		return true;
	}

	// MAP

	if ((c->tag == TAG_INT) && (c->flags & FLAG_INT_MAP)) {
		emit_sprintf(q, "'$map'(%d)", (int)get_smallint(c));
		q->last_thing = WAS_OTHER;
		return true;
	}

	// STREAM

	if ((c->tag == TAG_INT) && (c->flags & FLAG_INT_STREAM)) {
		emit_sprintf(q, "'$stream'(%d)", (int)get_smallint(c));
		q->last_thing = WAS_OTHER;
		return true;
	}

	// BLOB

	if (is_blob(c)) {
		emit_sprintf(q, "'$blob'(%p)", c->val_ptr);
		q->last_thing = WAS_OTHER;
		return true;
	}

	// NEGATIVE

	if (is_number(c) && is_negative(c)) {
		if (is_negative(c) && (q->last_thing == WAS_SYMBOL)) {
			emit(q, " ");
			q->last_thing = WAS_SPACE;
		}
	}

	// numbers
	// RATIONAL

	if (is_rational(c)) {
		if (!emit_bigint(q, &c->val_bigint->irat.num, 10))
			return false;

		emit(q, " rdiv ");

		if (!emit_bigint(q, &c->val_bigint->irat.den, 10))
			return false;

		q->last_thing = WAS_OTHER;
		return true;
	}

	// BIG INTEGER

	if (is_bigint(c)) {
		if (!emit_bigint(q, &c->val_bigint->ival, 10))
			return false;

		q->last_thing = WAS_OTHER;
		return true;
	}

	// SMALL INTEGER

	if (is_smallint(c)) {
		emit_sprintf(q, "%lld", (long long)c->val_int);
		q->last_thing = WAS_OTHER;
		return true;
	}

	// FLOAT

	if (is_float(c)) {
		if (c->val_float == 0.0)
			c->val_float = fabs(c->val_float);

		char tmpbuf[256];

		if (!q->json && !isnan(c->val_float) && !isinf(c->val_float))
			reformat_float(tmpbuf, sizeof(tmpbuf), c->val_float);
		else
			snprintf(tmpbuf, sizeof(tmpbuf), "%.*g", 17, get_float(c));

		emit(q, tmpbuf);
		q->last_thing = WAS_OTHER;
		return true;
	}

	// strings (canonical / list / double_quotes)
	if (is_string(c) && q->ignore_ops) {
		print_string_canonical(q, c);
		q->last_thing = WAS_OTHER;
		return true;
	}

	if (is_string(c) && (!q->double_quotes || !dq_string_ok(q, c))) {
		print_string_list(q, c, cons > 0);
		q->last_thing = WAS_OTHER;
		return true;
	}

	// STRING / CHARS (Trealla double_quotes + #890)
	if (print_chars_quoted(q, c, c_ctx, running, depth))
		return true;

	// e2 — lists (ISO when !ignore_ops; canonical when ignore_ops)
	if (is_iso_list(c)) {
		print_list(q, c, c_ctx, running, cons > 0, depth+1, visited);
		q->last_thing = WAS_OTHER;
		return true;
	}

	// e1 $VAR / e3 {} / f canonical / h operators / atoms
	return print_interned(q, c, c_ctx, running, depth, visited);
}

// Render c into q->sb under the given options. Shared spine of the six
// public entry points below, which differ only in these two flags, the
// initial spacing state, and where the finished bytes go.
//
// The initial spacing state is not uniform and is preserved as it was:
// the two plain-term writers that target an output sink start from
// WAS_SPACE, the other four from WAS_OTHER. Making it uniform changes
// output, so it stays a parameter.

static bool print_to_sb(query *q, cell *c, pl_ctx c_ctx, int running, bool canonical, int initial)
{
	if (canonical) {
		q->ignore_ops = true;
		q->quoted = 1;
	}

	q->last_thing = initial;
	q->did_quote = false;
	SB_init(q->sb);

	visit me = {.next = NULL, .c = c, .c_ctx = c_ctx};
	prolog_lock(q->pl);
	print_term_dispatch(q, c, c_ctx, running, false, 0, &me);
	prolog_unlock(q->pl);

	if (q->fullstop) emit_char(q, '.');
	if (q->nl) emit_char(q, '\n');

	if (canonical) {
		q->ignore_ops = false;
		q->quoted = 0;
	}

	return !SB_oom(q->sb);
}

// Hand q->sb to the caller as a fresh allocation. The spare byte is
// for dcg_expansion, which appends to the result in place.
//
// The two strbuf entry points used to disagree on a failed malloc:
// one wrote through the null pointer, the other returned early and
// leaked q->sb. Now neither happens.

static char *sb_take(query *q, bool ok)
{
	char *buf = ok ? TPL_malloc(SB_strlen(q->sb)+1+1) : NULL;

	if (buf)
		strcpy(buf, SB_cstr(q->sb));

	SB_free(q->sb);

	if (!buf)
		(void)throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

	return buf;
}

// Write q->sb out and release it. Writes through str when given one,
// else straight to fp; the two report a write error differently, a
// stream being closed and named in the error term.

static bool sb_flush(query *q, stream *str, FILE *fp, bool ok)
{
	if (!ok) {
		SB_free(q->sb);
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");
	}

	const char *src = SB_cstr(q->sb);
	ssize_t len = SB_strlen(q->sb);

	while (len) {
		size_t nbytes = str ? tpl_write(src, len, str) : fwrite(src, 1, len, fp);

		if (!nbytes && str && !q->is_task && ((errno == EAGAIN) || (errno == EWOULDBLOCK)) && ferror(fp)) {
			// A non-task socket is non-blocking now (see bif_net.c), so a
			// zero-progress write here is routinely just EAGAIN - the
			// peer's receive window is full, not a real error. Waiting
			// for room is far cheaper than the alternative below, which
			// closes the stream outright.
			clearerr(fp);

			if (tpl_wait_fd_writable(q, fileno(str->fp_out)))
				continue;
		}

		if (ferror(fp)) {
			SB_free(q->sb);

			if (!str)
				return throw_error(q, q->st.instr, q->st.cur_ctx, "existence_error", "stream");

			cell tmp_err;
			make_int(&tmp_err, str->idx);
			tmp_err.flags |= FLAG_INT_STREAM;
			stream_close(q, str->idx);
			return throw_error(q, &tmp_err, q->st.cur_ctx, "existence_error", "stream");
		}

		len -= nbytes;
		src += nbytes;
	}

	fflush(fp);
	SB_free(q->sb);
	return true;
}

char *print_canonical_to_strbuf(query *q, cell *c, pl_ctx c_ctx, int running)
{
	bool ok = print_to_sb(q, c, c_ctx, running, true, WAS_OTHER);
	return sb_take(q, ok);
}

bool print_canonical_to_stream(query *q, stream *str, cell *c, pl_ctx c_ctx, int running)
{
	bool ok = print_to_sb(q, c, c_ctx, running, true, WAS_OTHER);
	return sb_flush(q, str, str->fp_out, ok);
}

bool print_canonical(query *q, FILE *fp, cell *c, pl_ctx c_ctx, int running)
{
	bool ok = print_to_sb(q, c, c_ctx, running, true, WAS_OTHER);
	return sb_flush(q, NULL, fp, ok);
}

char *print_term_to_strbuf(query *q, cell *c, pl_ctx c_ctx, int running)
{
	bool ok = print_to_sb(q, c, c_ctx, running, false, WAS_OTHER);
	return sb_take(q, ok);
}

bool print_term_to_stream(query *q, stream *str, cell *c, pl_ctx c_ctx, int running)
{
	bool ok = print_to_sb(q, c, c_ctx, running, false, WAS_SPACE);
	return sb_flush(q, str, str->fp_out, ok);
}

bool print_term(query *q, FILE *fp, cell *c, pl_ctx c_ctx, int running)
{
	bool ok = print_to_sb(q, c, c_ctx, running, false, WAS_SPACE);
	return sb_flush(q, NULL, fp, ok);
}

void partial_clear_write_options(query *q)
{
	q->max_depth = q->pl->def_max_depth;
	q->quoted = 0;
	q->nl = q->fullstop = q->varnames = q->ignore_ops = false;
	q->parens = q->numbervars = q->json = q->double_quotes = false;
	q->portrayed = false;
	q->last_thing = WAS_OTHER;
	q->variable_names = NULL;
	q->cycle_error = false;
}

void clear_write_options(query *q)
{
	partial_clear_write_options(q);
	q->print_idx = q->name_idx = 0;

	// Only what was actually handed out, not a fixed 8KB.
	if (q->ignores)
		memset(q->ignores, 0, q->ignores_alloc * sizeof(bool));
}
