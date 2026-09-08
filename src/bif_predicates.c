#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "base64.h"
#include "module.h"
#include "parser.h"
#include "query.h"

#if USE_OPENSSL
#include "openssl/sha.h"
#include "openssl/hmac.h"
#endif

// The host operating system, for current_prolog_flag(os, OS). Every
// build knows its own at compile time except a Cosmopolitan one, whose
// single binary runs on several systems and so has to ask.

#if defined(__COSMOPOLITAN__)
#include <sys/utsname.h>

static const char *host_os(void)
{
	// Cosmopolitan builds are NOTHREADS, and the answer never changes
	// within a run, so the buffer is written once and idempotently.

	static char name[32];

	if (!*name) {
		struct utsname u;

		if (uname(&u) != 0)
			return "unknown";

		if (!strcmp(u.sysname, "Darwin"))
			return strcpy(name, "macos");

		// Otherwise the system's own name in lower case: linux,
		// freebsd, netbsd, openbsd, windows...

		size_t i = 0;

		for (; u.sysname[i] && (i < sizeof(name)-1); i++)
			name[i] = tolower((unsigned char)u.sysname[i]);

		name[i] = '\0';
	}

	return name;
}
#else
static const char *host_os(void)
{
#if defined(_WIN32)
	return "windows";
#elif defined(__ANDROID__)		// tested first: it defines __linux__ too
	return "android";
#elif defined(__APPLE__)
	return "macos";
#elif defined(__linux__)
	return "linux";
#elif defined(__FreeBSD__)
	return "freebsd";
#elif defined(__OpenBSD__)
	return "openbsd";
#elif defined(__NetBSD__)
	return "netbsd";
#elif defined(__DragonFly__)
	return "dragonfly";
#elif defined(__wasi__)
	return "wasi";
#elif defined(__EMSCRIPTEN__)
	return "emscripten";
#elif defined(__sun)
	return "solaris";
#elif defined(__HAIKU__)
	return "haiku";
#elif defined(__riscos__)
	return "riscos";
#elif defined(__CYGWIN__)
	return "cygwin";
#else
	return "unknown";
#endif
}
#endif

// Whether this build has rational numbers, for
// current_prolog_flag(rationals, B). imath's imrat is compiled in
// unconditionally today, so this is true everywhere; it is a flag
// rather than a constant so that a build without it has somewhere to
// say so, and so that library code can ask instead of assuming.

#define HAS_RATIONALS true

// The unix flag was the atom true whatever the host, Windows included.
// Deriving it from the same answer keeps the two from disagreeing.

static bool host_is_unix(void)
{
	const char *os = host_os();

	return strcmp(os, "windows")
		&& strcmp(os, "wasi")
		&& strcmp(os, "riscos")
		&& strcmp(os, "unknown");
}

static bool bif_nb_setarg_3(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,compound);
	GET_NEXT_ARG(p3,integer);

	unsigned arg_nbr = get_smalluint(p1);

	if ((arg_nbr == 0) || (arg_nbr > get_arity(p2)))
		return false;

	p2 = p2 + 1;

	for (unsigned i = 1; i <= arg_nbr; i++) {
		if (i == arg_nbr) {
			*p2 = *p3;
			return true;
		}

		p2 += p2->num_cells;
	}

	return true;
}

static bool bif_sys_list_1(query *q);

static bool bif_findnsols_ge_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (is_bigint(p2))
		return throw_error(q, p2, p2_ctx, "domain_error", "small_integer_range");

	int num = get_smallint(p1), count;

	if (is_integer(p2)) {
		count = get_smallint(p2);
	} else {
		p2 = deref(q, p2+1, p2_ctx);
		count = get_smallint(p2);
	}

	if (num < count) {
		set_smallint(p1, num+1);
		return false;
	}

	set_smallint(p1, 1);
	return true;
}

static bool bif_findnsols_4(query *q)
{
	GET_FIRST_ARG(p0,any);
	GET_NEXT_ARG(p1,any);
	GET_NEXT_ARG(p2,callable);
	GET_NEXT_ARG(p3,list_or_nil_or_var);
	int nsols = 0;
	cell *p01 = NULL;

	if (!q->retry) {
		if (is_compound(p0) && (get_arity(p0) == 1) && (p0->val_off == g_count_s))
			p01 = deref(q, p0+1, p0_ctx);

		if (is_smallint(p0)) {
			nsols = get_smallint(p0);
		} else if (is_compound(p0) && (get_arity(p0) == 1) && (p0->val_off == g_count_s) && is_smallint(p01)) {
			nsols = get_smallint(p01);
		} else
			return throw_error(q, p0, p0_ctx, "type_error", "integer");

		if (nsols < 0)
			return throw_error(q, p0, p0_ctx, "domain_error", "not_less_than_zero");

		bool is_partial = false;

		// This checks for a valid list (it allows for partial but acyclic lists)...

		if (is_iso_list(p3) && !check_list(q, p3, p3_ctx, &is_partial, NULL) && !is_partial)
			return throw_error(q, p3, p3_ctx, "type_error", "list");

		CHECKED(init_tmp_heap(q));
		cell *tmp2 = clone_term_to_tmp(q, p2, p2_ctx);
		CHECKED(tmp2);

		if (check_body_callable(tmp2))
			return throw_error(q, p2, p2_ctx, "type_error", "callable");

		if (!grab_queuen(q))
			return throw_error(q, p2, p2_ctx, "resource_error", "max_queues");

		cell *tmp;
		pl_idx num_cells;

		if (is_integer(p0)) {
			tmp = prepare_call(q, CALL_NOSKIP, tmp2, p2_ctx, 1+p1->num_cells+3+2+1);
			CHECKED(tmp, drop_queuen(q));
			num_cells = tmp2->num_cells;
			make_instr(tmp+num_cells++, g_sys_queue_s, bif_sys_queue_1, 1, p1->num_cells);
			num_cells += dup_cells_by_ref(tmp+num_cells, p1, p1_ctx, p1->num_cells);
			make_instr(tmp+num_cells++, g_ge_s, bif_findnsols_ge_2, 2, 2);
			make_int(tmp+num_cells++, 1);
			make_int(tmp+num_cells++, nsols);
		} else {
			tmp = prepare_call(q, CALL_NOSKIP, tmp2, p2_ctx, 1+p1->num_cells+3+2+1);
			CHECKED(tmp, drop_queuen(q));
			num_cells = tmp2->num_cells;
			make_instr(tmp+num_cells++, g_sys_queue_s, bif_sys_queue_1, 1, p1->num_cells);
			num_cells += dup_cells_by_ref(tmp+num_cells, p1, p1_ctx, p1->num_cells);
			make_instr(tmp+num_cells++, g_ge_s, bif_findnsols_ge_2, 2, 2);
			make_int(tmp+num_cells++, 1);
			make_indirect(tmp+num_cells++, p0, p0_ctx);
		}

		make_instr(tmp+num_cells++, g_sys_list_s, bif_sys_list_1, 1, 1);
		make_ref(tmp+num_cells++, p3->var_num, p3_ctx);
		make_call(q, tmp+num_cells);
		CHECKED(push_barrier(q), drop_queuen(q));
		q->st.instr = tmp;
		return true;
	}

	if (!queuen_used(q)) {
		drop_queuen(q);
		return false;
	}

	// Retry takes the queue

	pl_idx num_cells = queuen_used(q);
	const pl_idx solns_cells = num_cells;
	cell *solns = take_queuen(q);
	drop_queuen(q);

	// Now grab matching solutions with fresh variables for each...

	CHECKED(init_tmp_heap(q), free_solns(solns, solns_cells));

	for (cell *c = solns; num_cells; num_cells -= c->num_cells, c += c->num_cells) {
		cell *tmp = alloc_tmp(q, 1);
		CHECKED(tmp, free_solns(solns, solns_cells));
		make_instr(tmp, g_dot_s, NULL, 2, 0);
		q->noderef = true;
		tmp = copy_term_to_tmp(q, c, q->st.cur_ctx, false);
		q->noderef = false;
		CHECKED(tmp, free_solns(solns, solns_cells));
	}

	cell *l = end_list(q);
	free_solns(solns, solns_cells);
	CHECKED(l);
	return unify(q, p3, p3_ctx, l, q->st.cur_ctx);
}

static bool bif_iso_findall_3(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,callable);
	GET_NEXT_ARG(p3,list_or_nil_or_var);

	if (!q->retry) {
		bool is_partial = false;

		// This checks for a valid list (it allows for partial but acyclic lists)...

		if (is_iso_list(p3) && !check_list(q, p3, p3_ctx, &is_partial, NULL) && !is_partial)
			return throw_error(q, p3, p3_ctx, "type_error", "list");

		CHECKED(init_tmp_heap(q));
		cell *tmp2 = clone_term_to_tmp(q, p2, p2_ctx);
		CHECKED(tmp2);

		if (check_body_callable(tmp2))
			return throw_error(q, p2, p2_ctx, "type_error", "callable");

		if (!grab_queuen(q))
			return throw_error(q, p2, p2_ctx, "resource_error", "max_queues");

		cell *tmp = prepare_call(q, CALL_NOSKIP, tmp2, p2_ctx, 1+p1->num_cells+2);
		CHECKED(tmp, drop_queuen(q));
		pl_idx num_cells = tmp2->num_cells;
		make_instr(tmp+num_cells++, g_sys_queue_s, bif_sys_queue_1, 1, p1->num_cells);
		num_cells += dup_cells_by_ref(tmp+num_cells, p1, p1_ctx, p1->num_cells);
		make_instr(tmp+num_cells++, g_fail_s, bif_iso_fail_0, 0, 0);
		make_call(q, tmp+num_cells);
		CHECKED(push_barrier(q), drop_queuen(q));
		q->st.instr = tmp;
		return true;
	}

	if (!queuen_used(q)) {
		drop_queuen(q);
		return unify(q, p3, p3_ctx, make_nil(), q->st.cur_ctx);
	}

	// Retry takes the queue

	pl_idx num_cells = queuen_used(q);
	const pl_idx solns_cells = num_cells;
	cell *solns = take_queuen(q);
	drop_queuen(q);

	// Now grab matching solutions with fresh variables for each...

	CHECKED(init_tmp_heap(q), free_solns(solns, solns_cells));

	for (cell *c = solns; num_cells; num_cells -= c->num_cells, c += c->num_cells) {
		cell *tmp = alloc_tmp(q, 1);
		CHECKED(tmp, free_solns(solns, solns_cells));
		make_instr(tmp, g_dot_s, NULL, 2, 0);
		q->noderef = true;
		tmp = copy_term_to_tmp(q, c, q->st.cur_ctx, false);
		q->noderef = false;
		CHECKED(tmp, free_solns(solns, solns_cells));
	}

	cell *l = end_list(q);
	free_solns(solns, solns_cells);
	CHECKED(l);
	return unify(q, p3, p3_ctx, l, q->st.cur_ctx);
}

static bool bif_iso_unify_with_occurs_check_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	bool save = q->flags.occurs_check;
	q->flags.occurs_check = OCCURS_CHECK_TRUE;
	bool ok = unify(q, p1, p1_ctx, p2, p2_ctx);
	q->flags.occurs_check = save;
	return ok;
}

static bool bif_sys_unifiable_3(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	GET_NEXT_ARG(p3,list_or_nil_or_var);
	CHECKED(push_choice(q));
	pl_idx save_tp = q->st.tp;

	if (!unify(q, p1, p1_ctx, p2, p2_ctx) && !q->cycle_error) {
		drop_choice(q);
		return false;
	}

	CHECKED(init_tmp_heap(q));

	// Go thru trail, getting the bindings...

	while (save_tp < q->st.tp) {
		const trail *tr = get_trail(q, save_tp);
		const frame *f = GET_FRAME(tr->val_ctx);
		slot *e = get_slot(q, f, tr->var_num);
		cell *c = deref(q, &e->c, e->c.val_ctx);
		pl_ctx c_ctx = q->latest_ctx;
		cell *tmp = TPL_malloc(sizeof(cell)*(2+c->num_cells));
		CHECKED(tmp);
		make_instr(tmp, g_unify_s, bif_iso_unify_2, 2, 1+c->num_cells);
		SET_OP(tmp, OP_XFX);
		cell v;
		make_ref(&v, tr->var_num, q->st.cur_ctx);
		tmp[1] = v;
		dup_cells_by_ref(tmp+2, c, c_ctx, c->num_cells);
		append_list(q, tmp);
		TPL_free(tmp);
		save_tp++;
	}

	undo_me(q);
	drop_choice(q);

	cell *l = end_list(q);
	return unify(q, p3, p3_ctx, l, q->st.cur_ctx);
}

static bool bif_iso_notunifiable_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	cell tmp2;
	make_instr(&tmp2, g_unify_s, bif_iso_unify_2, 2, 0);
	SET_OP(&tmp2, OP_XFX);
	cell *tmp = prepare_call(q, CALL_NOSKIP, &tmp2, q->st.cur_ctx, p1->num_cells+p2->num_cells+4);
	pl_idx num_cells = 0;
	tmp[num_cells++].num_cells += p1->num_cells+p2->num_cells;
	num_cells += dup_cells_by_ref(tmp+num_cells, p1, p1_ctx, p1->num_cells);
	num_cells += dup_cells_by_ref(tmp+num_cells, p2, p2_ctx, p2->num_cells);
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_instr(tmp+num_cells++, g_fail_s, bif_iso_fail_0, 0, 0);
	make_call(q, tmp+num_cells);
	CHECKED(push_succeed_on_retry_with_barrier(q, 0));
	q->st.instr = tmp;
	return true;
}

bool bif_iso_unify_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	return unify(q, p1, p1_ctx, p2, p2_ctx);
}

static bool bif_sys_undo_1(query *q)
{
	GET_FIRST_RAW_ARG(p1,var);
	undo_var(q, p1, p1_ctx);
	return true;
}

static bool bif_iso_repeat_0(query *q)
{
	CHECKED(push_choice(q));
	return true;
}

bool bif_iso_halt_0(query *q)
{
	q->halt_code = 0;
	q->halt = q->error = true;
	return false;
}

static bool bif_iso_halt_1(query *q)
{
	GET_FIRST_ARG(p1,integer);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	q->halt_code = get_smallint(p1);
	q->halt = q->error = true;
	return false;
}

static bool bif_iso_number_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return is_number(p1);
}

static bool bif_iso_atom_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return is_iso_atom(p1);
}

static bool bif_iso_compound_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return is_structure(p1) ? 1 : 0;
}

static bool bif_iso_atomic_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return is_iso_atomic(p1);
}

static bool bif_iso_var_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return is_var(p1);
}

static bool bif_iso_nonvar_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return !is_var(p1);
}

static bool bif_iso_ground_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return is_ground(p1) || !has_vars(q, p1, p1_ctx);
}

static bool bif_iso_callable_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return is_callable(p1);
}

static bool bif_iso_char_code_2(query *q)
{
	GET_FIRST_ARG(p1,character_or_var);
	GET_NEXT_ARG(p2,integer_or_var);

	if (is_var(p1) && is_var(p2))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

	if (is_var(p2)) {
		const char *src = C_STR(q, p1);
		size_t len = len_char_utf8(src);

		if (len != C_STRLEN(q, p1))
			return throw_error(q, p1, p1_ctx, "type_error", "character");

		int ch = peek_char_utf8(src);
		cell tmp;
		make_int(&tmp, ch);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	if (is_integer(p2) && is_negative(p2))
		return throw_error(q, p2, p2_ctx, "representation_error", "character_code");

	if (is_integer(p2) && (get_smallint(p2) > MAX_CODEPOINT))
		return throw_error(q, p2, p2_ctx, "representation_error", "character_code");

	if (is_var(p1)) {
		char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
		int n = put_char_utf8(tmpbuf, get_smallint(p2));
		cell tmp;
		make_smalln(&tmp, tmpbuf, n);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	const char *src = C_STR(q, p1);
	size_t len = len_char_utf8(src);

	if (len != C_STRLEN(q, p1))
		return throw_error(q, p1, p1_ctx, "type_error", "integer");

	int ch = peek_char_utf8(src);
	return ch == get_smallint(p2);
}

static bool bif_iso_atom_chars_2(query *q)
{
	GET_FIRST_ARG(p1,iso_atom_or_var);
	GET_NEXT_ARG(p2,list_or_nil_or_var);

	if (is_var(p1) && is_var(p2))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

	// This checks for a valid list (it allows for partial but acyclic lists)...

	bool is_partial = false;

	if (is_iso_list(p2) && !check_list(q, p2, p2_ctx, &is_partial, NULL) && !is_partial)
		return throw_error(q, p2, p2_ctx, "type_error", "list");

	if (is_atom(p1) && !C_STRLEN(q, p1) && is_nil(p2))
		return true;

	if (is_var(p1) && is_nil(p2)) {
		cell tmp;
		make_atom(&tmp, g_empty_s);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	if (is_var(p2) && !C_STRLEN(q, p1))
		return unify(q, p2, p2_ctx, make_nil(), q->st.cur_ctx);

	if (is_var(p2)) {
		cell tmp;
		make_stringn(&tmp, C_STR(q, p1), C_STRLEN(q, p1));
		bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	}

	if (is_string(p2)) {
		cell tmp;
		CHECKED(make_slice(q, &tmp, p2, 0, C_STRLEN(q, p2)));
		tmp.flags &= ~FLAG_CSTR_STRING;
		set_arity(&tmp, 0);
		bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	}

	// Verify the list

	if (!is_var(p2)) {
		cell *save_p2 = p2;
		pl_ctx save_p2_ctx = p2_ctx;
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p2)) {
			cell *head = PROLOG_LIST_HEAD(p2);
			head = deref(q, head, p2_ctx);

			if (!is_atom(head) && is_var(p1))
				return throw_error(q, head, q->latest_ctx, "type_error", "character");

			if (!is_atom(head) && !is_var(head))
				return throw_error(q, head, q->latest_ctx, "type_error", "character");

			if (is_atom(head)) {
				const char *src = C_STR(q, head);
				size_t len = len_char_utf8(src);

				if (len < C_STRLEN(q, head))
					return throw_error(q, head, q->latest_ctx, "type_error", "character");
			}

			cell *tail = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, tail, p2_ctx);
			p2_ctx = q->latest_ctx;
		}

		if (!is_nil(p2) && !is_var(p2))
			return throw_error(q, p2, p2_ctx, "type_error", "list");

		p2 = save_p2;
		p2_ctx = save_p2_ctx;
	}

	if (is_string(p2) && is_var(p1)) {
		cell tmp = *p2;
		tmp.flags &= ~FLAG_CSTR_STRING;
		set_arity(&tmp, 0);
		bool ok = unify(q, p1, p1_ctx, p2, q->st.cur_ctx);
		return ok;
	}

	if (!is_var(p2) && is_var(p1)) {
		SB(pr);
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p2)) {
			cell *head = PROLOG_LIST_HEAD(p2);
			head = deref(q, head, p2_ctx);
			const char *src = C_STR(q, head);
			SB_strcatn(pr, src, len_char_utf8(src));
			cell *tail = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, tail, p2_ctx);
			p2_ctx = q->latest_ctx;
		}

		if (!is_nil(p2)) {
			SB_free(pr);		// unreachable today: the arg type check
			return throw_error(q, p2, p2_ctx, "type_error", "list");
		}					// rejects improper lists first

		cell tmp;
		make_cstring(&tmp, SB_cstr(pr));
		SB_free(pr);
		bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	}

	const char *src = C_STR(q, p1);
	size_t len = C_STRLEN(q, p1);
	CHECKED(init_tmp_heap(q));

	while (len) {
		size_t n = len_char_utf8(src);
		cell tmp2;
		make_smalln(&tmp2, src, n);
		src += n;
		len -= n;
		append_list(q, &tmp2);
	}

	cell *l = end_list(q);
	CHECKED(l);
	return unify(q, p2, p2_ctx, l, q->st.cur_ctx);
}

static bool bif_iso_number_chars_2(query *q)
{
	GET_FIRST_ARG(p1,number_or_var);
	GET_NEXT_ARG(p2,list_or_nil_or_var);
	cell *orig_p2 = p2;

	if (is_var(p1) && is_var(p2))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

	// This checks for a valid list (it allows for partial but acyclic lists)...

	bool is_partial = false;

	if (is_iso_list(p2) && !check_list(q, p2, p2_ctx, &is_partial, NULL) && !is_partial)
		return throw_error(q, p2, p2_ctx, "type_error", "list");

	if (is_nil(p2))
		return throw_error(q, p2, p2_ctx, "syntax_error", "incomplete");

	// Verify the list

	pl_int cnt = 0;
	bool any_vars = false;

	if (!is_var(p2)) {
		cell *save_p2 = p2;
		pl_ctx save_p2_ctx = p2_ctx;
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p2)) {
			cell *head = PROLOG_LIST_HEAD(p2);
			head = deref(q, head, p2_ctx);

			if (is_var(head))
				any_vars = true;

			if (!is_atom(head) && is_var(p1))
				return throw_error(q, head, q->latest_ctx, "type_error", "character");

			if (!is_atom(head) && !is_var(head))
				return throw_error(q, head, q->latest_ctx, "type_error", "character");

			if (is_atom(head)) {
				const char *src = C_STR(q, head);
				size_t len = len_char_utf8(src);

				if (len < C_STRLEN(q, head))
					return throw_error(q, head, q->latest_ctx, "type_error", "character");
			}

			cell *tail = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, tail, p2_ctx);
			p2_ctx = q->latest_ctx;
			cnt++;
		}

		if (!is_nil(p2) && !is_var(p2))
			return throw_error(q, orig_p2, p2_ctx, "type_error", "list");

		if (is_var(p2))
			any_vars = true;

		p2 = save_p2;
		p2_ctx = save_p2_ctx;
	}

	if (is_var(p1) && any_vars)
		return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

	if (!is_var(p2) && !any_vars) {
		SB(pr);
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p2)) {
			cell *head = PROLOG_LIST_HEAD(p2);
			head = deref(q, head, p2_ctx);
			int ch = peek_char_utf8(C_STR(q, head));

			if (!ch)
				return throw_error(q, head, q->latest_ctx, "syntax_error", "illegal_character");

			if (!SB_try_putchar(pr, ch)) {
				SB_free(pr);
				return throw_error(q, q->st.instr, q->st.cur_ctx,
					"resource_error", "memory");
			}
			cell *tail = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, tail, p2_ctx);
			p2_ctx = q->latest_ctx;
		}

		if (!SB_try_putchar(pr, '\0')) {
			SB_free(pr);
			return throw_error(q, q->st.instr, q->st.cur_ctx,
				"resource_error", "memory");
		}
		parser *p = query_parser(q);

		if (!p) {
			SB_free(pr);
			return throw_error(q, q->st.instr, q->st.cur_ctx,
				"resource_error", "memory");
		}

		parser_reset(p);
		p->srcptr = SB_cstr(pr);
		p->is_number_chars = true;
		p->do_read_term = true;
		bool ok = tokenize(p, false, false);
		p->do_read_term = false;

		if (q->did_throw) {
			SB_free(pr);
			return ok;
		}

		if (!is_number(&p->v) || p->error
			|| p->nesting_parens || p->nesting_braces || p->nesting_brackets
			|| (p->cl->cidx > 1)
			) {
			SB_free(pr);
			const char *error_type = p->error&&p->error_type?p->error_type:"syntax_error";
			bool ok = throw_error(q, orig_p2, p2_ctx, error_type, p->error&&p->error_desc?p->error_desc:"unexpected_char");
			return ok;
		}

		SB_free(pr);
		cell *tmp = &p->v;
		bool ok2 = unify(q, p1, p1_ctx, tmp, q->st.cur_ctx);
		return ok2;
	}

	q->ignore_ops = true;
	q->quoted = 1;
	q->last_thing = WAS_OTHER;
	q->did_quote = false;
	char *buf = print_term_to_strbuf(q, p1, p1_ctx, 1);
	q->ignore_ops = false;
	q->quoted = 0;
	cell tmp;
	make_string(&tmp, buf);
	TPL_free(buf);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_iso_atom_codes_2(query *q)
{
	GET_FIRST_ARG(p1,iso_atom_or_var);
	GET_NEXT_ARG(p2,list_or_nil_or_var);

	if (is_var(p1) && is_var(p2))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

	// This checks for a valid list (it allows for partial but acyclic lists)...

	bool is_partial = false;

	if (is_iso_list(p2) && !check_list(q, p2, p2_ctx, &is_partial, NULL) && !is_partial)
		return throw_error(q, p2, p2_ctx, "type_error", "list");

	if (!is_var(p2) && is_nil(p2)) {
		cell tmp;
		make_atom(&tmp, g_empty_s);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	if (is_var(p2) && !C_STRLEN(q, p1))
		return unify(q, p2, p2_ctx, make_nil(), q->st.cur_ctx);

	// Verify the list

	if (!is_var(p2) && !is_codes(p2)) {
		cell *save_p2 = p2;
		pl_ctx save_p2_ctx = p2_ctx;
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p2)) {
			cell *head = PROLOG_LIST_HEAD(p2);
			head = deref(q, head, p2_ctx);

			if (!is_integer(head) && is_var(p1))
				return throw_error(q, head, q->latest_ctx, "type_error", "integer");

			if (!is_integer(head) && !is_var(head))
				return throw_error(q, head, q->latest_ctx, "type_error", "integer");

			cell *tail = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, tail, p2_ctx);
			p2_ctx = q->latest_ctx;
		}

		if (!is_nil(p2) && !is_var(p2))
			return throw_error(q, p2, p2_ctx, "type_error", "list");

		p2 = save_p2;
		p2_ctx = save_p2_ctx;
	}

	if (!is_var(p2) && is_var(p1)) {
		SB(pr);
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p2)) {
			cell *head = PROLOG_LIST_HEAD(p2);
			head = deref(q, head, p2_ctx);
			pl_int val;

			if (is_bigint(head)) {
				mp_small tmp;
				mp_int_to_int(&head->val_bigint->ival, &tmp);
				val = tmp;
			} else {
				val = get_smallint(head);
			}

			if ((val < 0) || (val > MAX_CODEPOINT)) {
				SB_free(pr);
				return throw_error(q, head, q->latest_ctx, "representation_error", "character_code");
			}

			char ch[MAX_BYTES_PER_CODEPOINT+1];
			int len;

			if (!val) {
				ch[0] = 0;
				len = 1;
			} else
				len = put_char_utf8(ch, val);

			SB_strcatn(pr, ch, len);
			cell *tail = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, tail, p2_ctx);
			p2_ctx = q->latest_ctx;

		}

		if (!is_nil(p2)) {
			SB_free(pr);		// unreachable today: the arg type check
			return throw_error(q, p2, p2_ctx, "type_error", "list");
		}					// rejects improper lists first

		cell tmp;
		make_cstringn(&tmp, SB_cstr(pr), SB_strlen(pr));
		SB_free(pr);
		bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	}

	cell tmp;

	if (is_iso_atom(p1) || !is_codes(p1)) {
		make_stringn(&tmp, C_STR(q, p1), C_STRLEN(q, p1));
	} else {
		tmp = *p1;
		share_cell(&tmp);
	}

	tmp.flags |= FLAG_CSTR_STRING | FLAG_CSTR_CODES;
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_string_codes_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,list_or_nil_or_var);

	if (is_var(p1) && is_var(p2))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

	// This checks for a valid list (it allows for partial but acyclic lists)...

	bool is_partial = false;

	if (is_iso_list(p2) && !check_list(q, p2, p2_ctx, &is_partial, NULL) && !is_partial)
		return throw_error(q, p2, p2_ctx, "type_error", "list");

	if (!is_cstring(p1) && !is_var(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "atom");

	if (!is_var(p2) && is_nil(p2)) {
		cell tmp;
		make_atom(&tmp, g_empty_s);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	if (is_var(p2) && !C_STRLEN(q, p1))
		return unify(q, p2, p2_ctx, make_nil(), q->st.cur_ctx);

	// Verify the list

	if (!is_var(p2) && !is_codes(p2)) {
		cell *save_p2 = p2;
		pl_ctx save_p2_ctx = p2_ctx;
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p2)) {
			cell *head = PROLOG_LIST_HEAD(p2);
			head = deref(q, head, p2_ctx);

			if (!is_integer(head) && is_var(p1))
				return throw_error(q, head, q->latest_ctx, "type_error", "integer");

			if (!is_integer(head) && !is_var(head))
				return throw_error(q, head, q->latest_ctx, "type_error", "integer");

			cell *tail = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, tail, p2_ctx);
			p2_ctx = q->latest_ctx;
		}

		if (!is_nil(p2) && !is_var(p2))
			return throw_error(q, p2, p2_ctx, "type_error", "list");

		p2 = save_p2;
		p2_ctx = save_p2_ctx;
	}

	if (!is_var(p2) && is_var(p1)) {
		SB(pr);
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p2)) {
			cell *head = PROLOG_LIST_HEAD(p2);
			head = deref(q, head, p2_ctx);
			pl_int val;

			if (is_bigint(head)) {
				mp_small tmp;
				mp_int_to_int(&head->val_bigint->ival, &tmp);
				val = tmp;
			} else {
				val = get_smallint(head);
			}

			if ((val < 0) || (val > MAX_CODEPOINT)) {
				SB_free(pr);
				return throw_error(q, head, q->latest_ctx, "representation_error", "character_code");
			}

			char ch[MAX_BYTES_PER_CODEPOINT+1];
			int len;

			if (!val) {
				ch[0] = 0;
				len = 1;
			} else
				len = put_char_utf8(ch, val);

			SB_strcatn(pr, ch, len);
			cell *tail = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, tail, p2_ctx);
			p2_ctx = q->latest_ctx;

		}

		if (!is_nil(p2)) {
			SB_free(pr);		// unreachable today: the arg type check
			return throw_error(q, p2, p2_ctx, "type_error", "list");
		}					// rejects improper lists first

		cell tmp;
		make_stringn(&tmp, SB_cstr(pr), SB_strlen(pr));
		SB_free(pr);
		bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	}

	cell tmp;

	if (is_iso_atom(p1) || !is_codes(p1)) {
		make_stringn(&tmp, C_STR(q, p1), C_STRLEN(q, p1));
	} else {
		tmp = *p1;
		share_cell(&tmp);
	}

	tmp.flags |= FLAG_CSTR_STRING | FLAG_CSTR_CODES;
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_hex_bytes_2(query *q)
{
	GET_FIRST_ARG(p1,list_or_nil_or_var);
	GET_NEXT_ARG(p2,iso_list_or_nil_or_var);

	if (is_var(p1) && is_var(p2))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

	// This checks for a valid list (it allows for partial but acyclic lists)...

	bool is_partial = false;

	if (is_iso_list(p2) && !check_list(q, p2, p2_ctx, &is_partial, NULL) && !is_partial)
		return throw_error(q, p2, p2_ctx, "type_error", "list");

	if (is_nil(p2))
		return unify(q, p1, p1_ctx, make_nil(), q->st.cur_ctx);

	// Verify the list

	if (!is_var(p2)) {
		cell *save_p2 = p2;
		pl_ctx save_p2_ctx = p2_ctx;
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p2)) {
			cell *head = PROLOG_LIST_HEAD(p2);
			head = deref(q, head, p2_ctx);

			if (!is_integer(head) && is_var(p1))
				return throw_error(q, head, q->latest_ctx, "type_error", "integer");

			if (!is_integer(head) && !is_var(head))
				return throw_error(q, head, q->latest_ctx, "type_error", "integer");

			cell *tail = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, tail, p2_ctx);
			p2_ctx = q->latest_ctx;
		}

		if (!is_nil(p2) && !is_var(p2))
			return throw_error(q, p2, p2_ctx, "type_error", "list");

		p2 = save_p2;
		p2_ctx = save_p2_ctx;
	}

	if (!is_var(p2) && is_var(p1)) {
		SB(pr);
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p2)) {
			cell *head = PROLOG_LIST_HEAD(p2);
			head = deref(q, head, p2_ctx);

			pl_int val = get_smallint(head);

			if ((val < 0) || (val > 255)) {
				SB_free(pr);
				return throw_error(q, head, q->latest_ctx, "representation_error", "byte");
			}

			char ch[10];
			snprintf(ch, sizeof(ch), "%02X", (unsigned)val);
			SB_strcat(pr, ch);
			cell *tail = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, tail, p2_ctx);
			p2_ctx = q->latest_ctx;

		}

		if (!is_nil(p2)) {
			SB_free(pr);
			return throw_error(q, p2, p2_ctx, "type_error", "list");
		}

		cell tmp;
		make_string(&tmp, SB_cstr(pr));
		SB_free(pr);
		bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	}

	if (is_list(p2) && is_iso_list(p1)) {
		PROLOG_LIST_HANDLER(p1);
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p1) && is_list(p2)) {
			cell *h11 = PROLOG_LIST_HEAD(p1);
			h11 = deref(q, h11, p1_ctx);
			pl_ctx h11_ctx = q->latest_ctx;
			p1 = PROLOG_LIST_TAIL(p1);
			p1 = deref(q, p1, p1_ctx);
			p1_ctx = q->latest_ctx;
			cell *h12 = PROLOG_LIST_HEAD(p1);
			h12 = deref(q, h12, p1_ctx);
			pl_ctx h12_ctx = q->latest_ctx;

			cell *h2 = PROLOG_LIST_HEAD(p2);
			h2 = deref(q, h2, p2_ctx);
			unsigned n = get_smalluint(h2);

			unsigned n1 = (n >> 4) & 0xF;
			int ch;
			if (n1 < 10) ch = '0' + n1;
			else { n1 -= 10; ch = 'a' + n1; }
			char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
			put_char_utf8(tmpbuf, ch);
			cell tmp;
			make_cstring(&tmp, tmpbuf);
			unify(q, h11, h11_ctx, &tmp, q->st.cur_ctx);
			unshare_cell(&tmp);

			unsigned n2 = n & 0xF;
			if (n2 < 10) ch = '0' + n2;
			else { n2 -= 10; ch = 'a' + n2; }
			put_char_utf8(tmpbuf, ch);
			make_cstring(&tmp, tmpbuf);

			if (!unify(q, h12, h12_ctx, &tmp, q->st.cur_ctx)) {
				unshare_cell(&tmp);
				return false;
			}

			unshare_cell(&tmp);
			p1 = PROLOG_LIST_TAIL(p1);
			p1 = deref(q, p1, p1_ctx);
			p1_ctx = q->latest_ctx;
			p2 = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, p2, p2_ctx);
			p2_ctx = q->latest_ctx;
		}
	}

	PROLOG_LIST_HANDLER(p1);
	CHECKED(init_tmp_heap(q));

	while (is_list(p1)) {
		cell *h = PROLOG_LIST_HEAD(p1);
		h = deref(q, h, p1_ctx);

		if (!is_atom(h))
			return throw_error(q, p1, p1_ctx, "type_error", "char");

		const char *src = C_STR(q, h);
		int n = peek_char_utf8(src);
		unsigned val = 0;

		if (iswdigit(n))
			val += n - '0';
		else if ((n >= 'a') && (n <= 'f'))
			val += (n - 'a') + 10;
		else if ((n >= 'A') && (n <= 'F'))
			val += (n - 'A') + 10;
		else
			return throw_error(q, p1, p1_ctx, "representation_error", "byte");

		val <<= 4;

		p1 = PROLOG_LIST_TAIL(p1);
		p1 = deref(q, p1, p1_ctx);
		p1_ctx = q->latest_ctx;

		if (!is_list(p1))
			return throw_error(q, p1, p1_ctx, "domain_error", "hex_encoding");

		h = PROLOG_LIST_HEAD(p1);
		h = deref(q, h, p1_ctx);

		if (!is_atom(h))
			return throw_error(q, p1, p1_ctx, "type_error", "char");

		src = C_STR(q, h);
		n = peek_char_utf8(src);

		if (iswdigit(n))
			val += n - '0';
		else if ((n >= 'a') && (n <= 'f'))
			val += (n - 'a') + 10;
		else if ((n >= 'A') && (n <= 'F'))
			val += (n - 'A') + 10;
		else
			return throw_error(q, p1, p1_ctx, "representation_error", "byte");

		cell tmp;
		make_int(&tmp, (int)val);
		append_list(q, &tmp);
		p1 = PROLOG_LIST_TAIL(p1);
		p1 = deref(q, p1, p1_ctx);
		p1_ctx = q->latest_ctx;
	}

	if (!is_nil(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "hex_encoding");

	cell *l = end_list(q);
	CHECKED(l);
	return unify(q, p2, p2_ctx, l, q->st.cur_ctx);
}

static bool bif_iso_number_codes_2(query *q)
{
	GET_FIRST_ARG(p1,number_or_var);
	GET_NEXT_ARG(p2,list_or_nil_or_var);
	cell *orig_p2 = p2;

	if (is_var(p1) && is_var(p2))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

	// This checks for a valid list (it allows for partial but acyclic lists)...

	bool is_partial = false;

	if (is_iso_list(p2) && !check_list(q, p2, p2_ctx, &is_partial, NULL) && !is_partial)
		return throw_error(q, p2, p2_ctx, "type_error", "list");

	if (is_nil(p2))
		return throw_error(q, p2, p2_ctx, "syntax_error", "incomplete");

	// Verify the list

	int cnt = 0;
	bool any_vars = false;

	if (!is_var(p2)) {
		cell *save_p2 = p2;
		pl_ctx save_p2_ctx = p2_ctx;
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p2)) {
			cell *head = PROLOG_LIST_HEAD(p2);
			head = deref(q, head, p2_ctx);

			if (is_var(head))
				any_vars = true;

			if (!cnt && !is_integer(head) && is_var(p1))
				return throw_error(q, head, q->latest_ctx, "syntax_error", "integer");

			if (!is_integer(head) && is_var(p1))
				return throw_error(q, head, q->latest_ctx, "type_error", "integer");

			if (!is_integer(head) && !is_var(head))
				return throw_error(q, head, q->latest_ctx, "type_error", "integer");

			cell *tail = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, tail, p2_ctx);
			p2_ctx = q->latest_ctx;
			cnt++;
		}

		if (!is_nil(p2) && !is_var(p2))
			return throw_error(q, orig_p2, p2_ctx, "type_error", "list");

		if (is_var(p2))
			any_vars = true;

		p2 = save_p2;
		p2_ctx = save_p2_ctx;
	}

	if (is_var(p1) && any_vars)
		return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

	if (!is_var(p2) && !any_vars) {
		SB(pr);
		PROLOG_LIST_HANDLER(p2);

		while (is_list(p2)) {
			cell *head = PROLOG_LIST_HEAD(p2);
			head = deref(q, head, p2_ctx);
			pl_int val;

			if (is_bigint(head)) {
				mp_small tmp;
				mp_int_to_int(&head->val_bigint->ival, &tmp);
				val = tmp;
			} else {
				val = get_smallint(head);
			}

			if ((val < 0) || (val > MAX_CODEPOINT)) {
				SB_free(pr);
				return throw_error(q, head, q->latest_ctx, "representation_error", "character_code");
			}

			if (!SB_try_putchar(pr, val)) {
				SB_free(pr);
				return throw_error(q, q->st.instr, q->st.cur_ctx,
					"resource_error", "memory");
			}
			cell *tail = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, tail, p2_ctx);
			p2_ctx = q->latest_ctx;
		}

		if (!SB_try_putchar(pr, '\0')) {
			SB_free(pr);
			return throw_error(q, q->st.instr, q->st.cur_ctx,
				"resource_error", "memory");
		}
		parser *p = query_parser(q);

		if (!p) {
			SB_free(pr);
			return throw_error(q, q->st.instr, q->st.cur_ctx,
				"resource_error", "memory");
		}

		parser_reset(p);
		p->srcptr = SB_cstr(pr);
		p->is_number_chars = true;
		p->do_read_term = true;
		bool ok = tokenize(p, false, false);
		p->do_read_term = false;

		if (q->did_throw) {
			p->srcptr = NULL;
			SB_free(pr);
			return ok;
		}

		if (!is_number(&p->v) || p->error
			|| p->nesting_parens || p->nesting_braces || p->nesting_brackets
			|| (p->cl->cidx > 1)
			) {
			SB_free(pr);
			const char *error_type = p->error&&p->error_type?p->error_type:"syntax_error";
			bool ok = throw_error(q, orig_p2, p2_ctx, error_type, p->error&&p->error_desc?p->error_desc:"unexpected_char");
			return ok;
		}

		SB_free(pr);
		cell tmp = p->v;
		bool ok2 = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
		return ok2;
	}

	q->ignore_ops = true;
	q->quoted = 1;
	q->last_thing = WAS_OTHER;
	q->did_quote = false;
	char *buf = print_term_to_strbuf(q, p1, p1_ctx, 1);
	q->ignore_ops = false;
	q->quoted = 0;
	cell tmp;
	make_string(&tmp, buf);
	tmp.flags |= FLAG_CSTR_CODES;
	TPL_free(buf);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool do_sub_atom(query *q, cell *p1, cell *p2, pl_ctx p2_ctx, cell *p3, pl_ctx p3_ctx, cell *p4, pl_ctx p4_ctx, cell *p5)
{
	if (!q->retry) {
		q->st.v1 = 0;
	}

	const char *src = C_STR(q, p1), *s = C_STR(q, p5);
	pl_int srclen = C_STRLEN(q, p1), before = (int)q->st.v1, len = C_STRLEN(q, p5);
	const char *src2 = src + before;
	src2 = strstr(src2, s);

	if (q->retry && !srclen)
		return false;

	if (!src2)
		return false;

	pl_int after = srclen - (src2 - src) - len;
	before = src2 - src;

	if (after < 0)
		return false;

	q->st.v1 = before + 1;

	if (after && strstr(src2+1, s))
		CHECKED(push_choice(q));

	cell tmp;
	make_int(&tmp, pos_at_offset(C_STR(q, p1), C_STRLEN(q, p1), before));
	unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, len);
	unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, pos_at_offset(C_STR(q, p1), C_STRLEN(q, p1), after));
	unify(q, p4, p4_ctx, &tmp, q->st.cur_ctx);
	return true;
}

static bool bif_iso_sub_string_5(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,integer_or_var);		// before
	GET_NEXT_ARG(p3,integer_or_var);		// len
	GET_NEXT_ARG(p4,integer_or_var);		// after
	GET_NEXT_ARG(p5,atom_or_var);

	if (is_integer(p2) && is_negative(p2))
		return throw_error(q, p2, p2_ctx, "domain_error", "not_less_than_zero");

	if (is_integer(p3) && is_negative(p3))
		return throw_error(q, p3, p3_ctx, "domain_error", "not_less_than_zero");

	if (is_integer(p4) && is_negative(p4))
		return throw_error(q, p4, p4_ctx, "domain_error", "not_less_than_zero");

	if (!is_var(p1) && is_var(p2) && is_var(p3) && is_var(p4) && !is_var(p5)) {
		return do_sub_atom(q, p1, p2, p2_ctx, p3, p3_ctx, p4, p4_ctx, p5);
	}

	const size_t len_p1 = C_STRLEN_UTF8(p1);
	size_t before = 0, len = 0, after = 0;
	bool fixed = ((is_integer(p2) ? 1: 0) + (is_integer(p3) ? 1 : 0) + (is_integer(p4) ? 1 : 0)) >= 2;

	if ((!is_var(p2) || !is_var(p4)) && !is_var(p5))
		fixed = true;

	if (!q->retry) {
		CHECKED(push_choice(q));

		if (!is_var(p2))
			before = get_smallint(p2);

		if (!is_var(p3))
			len = get_smallint(p3);

		if (!is_var(p4))
			after = get_smallint(p4);

		if (is_var(p2) && is_integer(p3) && is_integer(p4))
			before = len_p1 - after - len;

		if (is_var(p3) && is_integer(p2) && is_integer(p4))
			len = len_p1 - before - after;
	} else {
		before = q->st.v1;
		len = q->st.v2;
	}

	if (len > (len_p1 - before)) {
		before++;
		len = 0;
	}

	if (before > len_p1) {
		drop_choice(q);
		return false;
	}

	for (size_t i = before; i <= len_p1; i++) {
		for (size_t j = len; j <= (len_p1 - i); j++) {
			q->st.v1 = i;
			q->st.v2 = j + 1;
			CHECKED(push_choice(q));
			cell tmp;
			size_t before = i;
			make_int(&tmp, before);

			if (!unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx)) {
				retry_choice(q);
				continue;
			}

			size_t len = j;
			make_int(&tmp, len);

			if (!unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx)) {
				retry_choice(q);
				continue;
			}

			size_t after = (len_p1 - before) - len;
			make_int(&tmp, after);

			if (!unify(q, p4, p4_ctx, &tmp, q->st.cur_ctx)) {
				retry_choice(q);
				continue;
			}

			size_t ipos = offset_at_pos(C_STR(q, p1), C_STRLEN(q, p1), i);
			size_t jpos = offset_at_pos(C_STR(q, p1), C_STRLEN(q, p1), i + j);

			CHECKED(make_slice(q, &tmp, p1, ipos, jpos - ipos));

			if (is_atom(p5) && !CMP_STRING_TO_CSTRN(q, p5, C_STR(q, &tmp), C_STRLEN(q, &tmp))) {
				unshare_cell(&tmp);

				if (fixed) {
					drop_choice(q);
					drop_choice(q);
				}

				return true;
			}

			if (!unify(q, p5, p5_ctx, &tmp, q->st.cur_ctx)) {
				unshare_cell(&tmp);
				retry_choice(q);
				continue;
			}

			unshare_cell(&tmp);

			if (fixed) {
				drop_choice(q);
				drop_choice(q);
			}

			return true;
		}

		len = 0;
	}

	drop_choice(q);
	return false;
}

static bool bif_iso_sub_atom_5(query *q)
{
	GET_FIRST_ARG(p1,iso_atom);
	GET_NEXT_ARG(p2,integer_or_var);		// before
	GET_NEXT_ARG(p3,integer_or_var);		// len
	GET_NEXT_ARG(p4,integer_or_var);		// after
	GET_NEXT_ARG(p5,iso_atom_or_var);

	return bif_iso_sub_string_5(q);
}

// NOTE: this just handles the mode(S,S,+) case...

static bool do_atom_concat_equal_3(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,var);
	GET_NEXT_ARG(p3,atom);

	if (C_STRLEN_UTF8(p3) & 1)
		return false;

	size_t len3 = C_STRLEN(q, p3);
	cell tmp;
	CHECKED(make_slice(q, &tmp, p3, 0, len3/2));

	if (!unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx))
		return false;

	GET_FIRST_ARG(p1x,atom);
	GET_NEXT_ARG(p2x,atom_or_var);
	CHECKED(make_slice(q, &tmp, p3, len3/2, len3/2));

	if (!unify(q, p2x, p2x_ctx, &tmp, q->st.cur_ctx))
		return false;

	return true;
}

// NOTE: this just handles the mode(-,-,+) case...

static bool do_atom_concat_3(query *q)
{
	if (!q->retry) {
		GET_FIRST_ARG(p1,var);
		GET_NEXT_ARG(p2,var);
		GET_NEXT_ARG(p3,atom);

		if ((p1->val_ctx == p2->val_ctx) && (p1->var_num == p2->var_num))
			return do_atom_concat_equal_3(q);

		cell tmp;
		make_atom(&tmp, g_empty_s);

		if (!unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx))
			return false;

		GET_FIRST_ARG(p1x,atom);
		GET_NEXT_ARG(p2x,atom_or_var);

		if (!unify(q, p2x, p2x_ctx, p3, q->st.cur_ctx))
			return false;

		if (C_STRLEN(q, p3))
			CHECKED(push_choice(q));

		return true;
	}

	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,atom);
	const char *s2 = C_STR(q, p2);
	size_t len = len_char_utf8(s2);
	size_t len1 = C_STRLEN(q, p1);
	size_t len2 = C_STRLEN(q, p2);
	bool done = false;

	if (!*(s2+len))
		done = true;

	GET_RAW_ARG(1,p1_raw);
	GET_RAW_ARG(2,p2_raw);
	cell tmp;
	CHECKED(make_slice(q, &tmp, p3, 0, len1+len));
	reset_var(q, p1_raw, p1_raw_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	CHECKED(make_slice(q, &tmp, p2, len, len2-len));
	reset_var(q, p2_raw, p2_raw_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);

	if (!done)
		CHECKED(push_choice(q));

	return true;
}

static bool bif_iso_atom_concat_3(query *q)
{
	if (q->retry)
		return do_atom_concat_3(q);

	GET_FIRST_ARG(p1,iso_atom_or_var);
	GET_NEXT_ARG(p2,iso_atom_or_var);
	GET_NEXT_ARG(p3,iso_atom_or_var);

	if (is_var(p1) && is_var(p2))
		return do_atom_concat_3(q);

	if (is_var(p3)) {
		if (!is_atom(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		if (!is_atom(p2))
			return throw_error(q, p2, p2_ctx, "type_error", "atom");

		SB(pr);
		SB_strcatn(pr, C_STR(q, p1), C_STRLEN(q, p1));
		SB_strcatn(pr, C_STR(q, p2), C_STRLEN(q, p2));
		cell tmp;
		make_cstring(&tmp, SB_cstr(pr));
		SB_free(pr);
		bool ok = unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	}

	if (is_var(p1)) {
		size_t len2 = C_STRLEN(q, p2), len3 = C_STRLEN(q, p3);
		const char *s2 = C_STR(q, p2), *s3 = C_STR(q, p3);

		if (len2 > len3)
			return false;

		if (memcmp(s3+(len3-len2), s2, len2))
			return false;

		cell tmp;
		CHECKED(make_slice(q, &tmp, p3, 0, len3-len2));
		bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	}

	if (is_var(p2)) {
		size_t len1 = C_STRLEN(q, p1), len3 = C_STRLEN(q, p3);
		const char *s1 = C_STR(q, p1), *s3 = C_STR(q, p3);

		if (len1 > len3)
			return false;

		if (memcmp(s3, s1, len1))
			return false;

		cell tmp;
		CHECKED(make_slice(q, &tmp, p3, len1, len3-len1));
		bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	}

	size_t len1 = C_STRLEN(q, p1), len2 = C_STRLEN(q, p2), len3 = C_STRLEN(q, p3);
	const char *s1 = C_STR(q, p1), *s2 = C_STR(q, p2), *s3 = C_STR(q, p3);

	if ((len1 + len2) != len3)
		return false;

	if (memcmp(s3, s1, len1))
		return false;

	if (memcmp(s3+len1, s2, len2))
		return false;

	return true;
}

static bool bif_iso_atom_length_2(query *q)
{
	GET_FIRST_ARG(p1,iso_atom);
	GET_NEXT_ARG(p2,smallint_or_var);

	if (is_negative(p2))
		return throw_error(q, p2, p2_ctx, "domain_error", "not_less_than_zero");

	size_t len = substrlen_utf8(C_STR(q, p1), C_STRLEN(q, p1));
	cell tmp;
	make_int(&tmp, len);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static void compare_and_zero(uint64_t v1, uint64_t *v2, uint64_t *v)
{
	if (v1 != *v2) {
		*v2 = v1;
		*v = 0;
	}
}

#define MASK_FINAL 0x0000FFFFFFFFFFFF // Final 48 bits

void uuid_gen(prolog *pl, uuid *u)
{
	prolog_lock(pl);

	if (!pl->seed)
		pl->seed = (uint64_t)time(0) & MASK_FINAL;

	uint64_t now = wall_time_in_usec();
	compare_and_zero(now, &pl->s_last, &pl->s_cnt);
	u->u1 = now;
	u->u2 = pl->s_cnt++;
	u->u2 <<= 48;
	u->u2 |= pl->seed;
	prolog_unlock(pl);
}

char *uuid_to_buf(const uuid *u, char *buf, size_t buflen)
{
	snprintf(buf, buflen, "%016"PRIx64"-%04"PRIx64"-%012"PRIx64"",
		 u->u1,
		 (u->u2 >> 48),
		 (u->u2 & MASK_FINAL));

	return buf;
}

int uuid_from_buf(const char *s, uuid *u)
{
	if (!s) {
		uuid tmp = {0};
		*u = tmp;
		return 0;
	}

	uint64_t p1 = 0, p2 = 0, p3 = 0;

	if (sscanf(s, "%"PRIx64"%*c%"PRIx64"%*c%"PRIx64"", &p1, &p2, &p3) != 3) {
		uuid tmp = {0};
		*u = tmp;
		return 0;
	}

	u->u1 = p1;
	u->u2 = p2 << 48;
	u->u2 |= p3 & MASK_FINAL;
	return 1;
}

static bool bif_iso_arg_3(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);
	GET_NEXT_ARG(p3,any);

	if (!is_structure(p2))
			return throw_error(q, p2, p2_ctx, "type_error", "compound");

	if (is_negative(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "not_less_than_zero");

	if (is_bigint(p1))
		return false;

	unsigned arg_nbr = get_smalluint(p1);

	if ((arg_nbr == 0) || (arg_nbr > get_arity(p2)))
		return false;

	if (is_list(p2)) {
		PROLOG_LIST_HANDLER(p2);
		cell *c = PROLOG_LIST_HEAD(p2);
		c = deref(q, c, p2_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (arg_nbr == 1)
			return unify(q, c, c_ctx, p3, p3_ctx);

		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;
		return unify(q, p2, p2_ctx, p3, p3_ctx);
	}

	p2 = p2 + 1;

	for (unsigned i = 1; i <= arg_nbr; i++) {
		if (i == arg_nbr) {
			cell *c = deref(q, p2, p2_ctx);
			pl_ctx c_ctx = q->latest_ctx;
			return unify(q, p3, p3_ctx, c, c_ctx);
		}

		p2 += p2->num_cells;
	}

	return true;
}

static bool bif_iso_univ_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,list_or_nil_or_var);

	if (is_var(p1) && is_var(p2))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

	if (is_var(p1) && is_nil(p2))
		return throw_error(q, p2, p2_ctx, "domain_error", "non_empty_list");

	// This checks for a valid list (it allows for partial but acyclic lists)...

	bool is_partial = false;

	if (is_iso_list(p2) && !check_list(q, p2, p2_ctx, &is_partial, NULL) && !is_partial)
		return throw_error(q, p2, p2_ctx, "type_error", "list");

	if (is_string(p1)) {
		cell tmp;
		make_atom(&tmp, g_dot_s);
		allocate_list(q, &tmp);
		PROLOG_LIST_HANDLER(p1);
		cell *h = PROLOG_LIST_HEAD(p1);
		append_list(q, h);
		cell *t = PROLOG_LIST_TAIL(p1);
		append_list(q, t);
		cell *l = end_list(q);
		CHECKED(l);
		return unify(q, p2, p2_ctx, l, p1_ctx);
	}

	if (is_var(p2)) {
		cell tmp2 = *p1;
		tmp2.num_cells = 1;
		CLR_OP(&tmp2);
		set_arity(&tmp2, 0);
		allocate_list(q, &tmp2);
		unsigned arity = get_arity(p1);
		p1++;

		while (arity--) {
			append_list(q, p1);
			p1 += p1->num_cells;
		}

		cell *l = end_list(q);
		CHECKED(l);
		return unify(q, p2, p2_ctx, l, p1_ctx);
	}

	if (is_var(p1)) {
		cell *p22 = p2;
		if (is_iso_list(p2)) p22++;
		p22 = deref(q, p22, p2_ctx);

		if (is_var(p22))
			return throw_error(q, p2, p2_ctx, "instantiation_error", "not_sufficiently_instantiated");

		cell *tmp;
		CHECKED(init_tmp_heap(q));
		uint32_t arity = 0;
		cell *save_p2 = p2;
		cell *l = p2;
		pl_ctx l_ctx = p2_ctx;
		PROLOG_LIST_HANDLER(l);

		while (is_list(l)) {
			cell *h = PROLOG_LIST_HEAD(l);
			h = deref(q, h, l_ctx);
			pl_ctx h_ctx = q->latest_ctx;
			cell *tmp = append_to_tmp(q, h, h_ctx);

			if (is_cstring(tmp) && is_string(save_p2))
				convert_to_literal(q->st.m, tmp);

			l = PROLOG_LIST_TAIL(l);
			l = deref(q, l, l_ctx);
			l_ctx = q->latest_ctx;
			arity++;
		}

		if (is_var(l))
			return throw_error(q, p2, p2_ctx, "instantiation_error", "list");

		if (!is_nil(l))
			return throw_error(q, save_p2, p2_ctx, "type_error", "list");

		arity--;
		cell *tmp2 = get_tmp_heap(q, 0);
		pl_idx num_cells = tmp_heap_used(q);

		if (is_cstring(tmp2) && !is_string(save_p2)) {
			// convert_to_literal() reads the text out and rewrites the
			// cell in place as an interned atom, clearing FLAG_MANAGED
			// - after which the cell no longer looks managed and
			// nothing can ever release this reference. Hold the blob
			// across the conversion, then hand it straight back.

			cell save = *tmp2;
			share_cell(tmp2);
			convert_to_literal(q->st.m, tmp2);
			unshare_cell(&save);
		}

		if (!is_interned(tmp2) && arity)
			return throw_error(q, tmp2, q->st.cur_ctx, "type_error", "atom");

		if (get_arity(tmp2) && arity)
			return throw_error(q, tmp2, q->st.cur_ctx, "type_error", "atom");

		if (get_arity(tmp2))
			return throw_error(q, tmp2, q->st.cur_ctx, "type_error", "atomic");

		CHECKED(tmp = alloc_heap(q, num_cells));
		dup_cells(tmp, tmp2, num_cells);
		tmp->num_cells = num_cells;
		set_arity(tmp, arity);
		bool found = false;

		if (is_callable(tmp)) {
			if ((tmp->bif_ptr = get_builtin_term(q->st.m, tmp, &found, NULL)), found) {
				if (tmp->bif_ptr->evaluable)
					tmp->flags |= FLAG_INTERNED_EVALUABLE;
				else
					tmp->flags |= FLAG_INTERNED_BUILTIN;
			}
		}

		unsigned specifier;

		if (search_op(q->st.m, C_STR(q, tmp), &specifier, arity == 1)) {
			if ((arity == 2) && IS_INFIX(specifier))
				SET_OP(tmp, specifier);
			else if ((arity == 1) && IS_POSTFIX(specifier))
				SET_OP(tmp, specifier);
			else if ((arity == 1) && IS_PREFIX(specifier))
				SET_OP(tmp, specifier);
		}

		return unify(q, p1, p1_ctx, tmp, q->st.cur_ctx);
	}

	// Atomic / functor head of Term =.. List. Same cstring pitfall as
	// functor/3: clearing FLAG_CSTR_BLOB on a managed atom turns the
	// strbuf pointer into bogus character data.
	cell tmp;

	if (is_string(p1)) {
		make_atom(&tmp, g_dot_s);
	} else if (is_cstring(p1) && !get_arity(p1)) {
		pl_idx off = new_atom(q->pl, C_STR(q, p1));
		CHECKED(off != ERR_IDX);
		make_atom(&tmp, off);
	} else {
		tmp = *p1;
		tmp.num_cells = 1;
		tmp.flags = 0;

		if (is_builtin(p1)) {
			tmp.flags &= ~FLAG_INTERNED_BUILTIN;
			tmp.bif_ptr = NULL;
		}

		CLR_OP(&tmp);
		set_arity(&tmp, 0);
	}

	allocate_list(q, &tmp);
	unsigned arity = get_arity(p1);
	p1++;

	while (arity--) {
		append_list(q, p1);
		p1 += p1->num_cells;
	}

	cell *l = end_list(q);
	CHECKED(l);
	return unify(q, p2, p2_ctx, l, p1_ctx);
}

static cell *do_term_variables(query *q, cell *p1, pl_ctx p1_ctx)
{
	frame *f = GET_CURR_FRAME();
	q->varno = f->actual_slots;
	collect_vars(q, p1, p1_ctx);
	const unsigned cnt = q->tab_idx;
	if (!init_tmp_heap(q)) return NULL;
	cell *tmp = alloc_tmp(q, (cnt*2)+1);
	if (!tmp) return NULL;

	if (cnt) {
		unsigned idx = 0;

		for (unsigned i = 0, done = 0; i < cnt; i++) {
			make_atom(tmp+idx, g_dot_s);
			set_arity(&tmp[idx], 2);
			tmp[idx].num_cells = ((cnt-done)*2)+1;
			idx++;
			make_ref(tmp+idx, q->tabs[i].var_num, q->tabs[i].ctx);

			if (q->tabs[i].is_anon)
				tmp[idx].flags |= FLAG_VAR_ANON;

			idx++;
			done++;
		}

		make_atom(tmp+idx++, g_nil_s);
		set_arity(&tmp[0], 2);
		tmp[0].num_cells = idx;
	} else
		make_atom(tmp, g_nil_s);

	return tmp;
}

static bool bif_iso_term_variables_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,iso_list_or_nil_or_var);

	// This checks for a valid list (it allows for partial but acyclic lists)...

	bool is_partial = false;

	if (is_iso_list(p2) && !check_list(q, p2, p2_ctx, &is_partial, NULL) && !is_partial)
		return throw_error(q, p2, p2_ctx, "type_error", "list");

	if (!is_var(p1) && (!is_compound(p1) || is_ground(p1)))
		return unify(q, p2, p2_ctx, make_nil(), q->st.cur_ctx);

	cell *tmp = do_term_variables(q, p1, p1_ctx);
	CHECKED(tmp);
	cell *tmp2 = alloc_heap(q, tmp->num_cells);
	CHECKED(tmp2);
	dup_cells(tmp2, tmp, tmp->num_cells);
	return unify(q, p2, p2_ctx, tmp2, q->st.cur_ctx);
}

static cell *do_term_singletons(query *q, cell *p1, pl_ctx p1_ctx)
{
	frame *f = GET_CURR_FRAME();
	q->varno = f->actual_slots;
	collect_vars(q, p1, p1_ctx);
	const unsigned cnt = q->tab_idx;
	unsigned cnt2 = 0;

	for (unsigned i = 0; i < cnt; i++) {
		if (q->tabs[i].cnt != 1)
			continue;

		cnt2++;
	}

	if (!init_tmp_heap(q)) return NULL;
	cell *tmp = alloc_tmp(q, (cnt2*2)+1);
	if (!tmp) return NULL;

	if (cnt2) {
		unsigned idx = 0;

		for (unsigned i = 0, done = 0; i < cnt; i++) {
			if (q->tabs[i].cnt != 1)
				continue;

			make_atom(tmp+idx, g_dot_s);
			set_arity(&tmp[idx], 2);
			tmp[idx].num_cells = ((cnt2-done)*2)+1;
			idx++;
			make_ref(tmp+idx, q->tabs[i].var_num, q->tabs[i].ctx);
			idx++;
			done++;
		}

		make_atom(tmp+idx++, g_nil_s);
		set_arity(&tmp[0], 2);
		tmp[0].num_cells = idx;
	} else
		make_atom(tmp, g_nil_s);

	return tmp;		// returns on tmp_heap
}

static bool bif_term_singletons_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,iso_list_or_nil_or_var);

	// This checks for a valid list (it allows for partial but acyclic lists)...

	bool is_partial = false;

	if (is_iso_list(p2) && !check_list(q, p2, p2_ctx, &is_partial, NULL) && !is_partial)
		return throw_error(q, p2, p2_ctx, "type_error", "list");

	if (!is_var(p1) && (is_atom(p1) || is_number(p1)))
		return unify(q, p2, p2_ctx, make_nil(), q->st.cur_ctx);

	cell *tmp = do_term_singletons(q, p1, p1_ctx);
	CHECKED(tmp);
	cell *tmp2 = alloc_heap(q, tmp->num_cells);
	CHECKED(tmp2);
	dup_cells(tmp2, tmp, tmp->num_cells);
	return unify(q, p2, p2_ctx, tmp2, q->st.cur_ctx);
}

static bool do_copy_term(query *q, bool copy_attrs)
{
	GET_FIRST_RAW_ARG(p1r,any);
	GET_NEXT_RAW_ARG(p2r,any);
	q->dump_var_num = is_var(p1r) ? p1r->var_num : (unsigned)-1;
	q->dump_var_ctx = is_var(p1r) ? p1r_ctx : (unsigned)-1;
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);

	if (is_atomic(p1) || (is_atomic(p2) && !is_var(p1)))
		return unify(q, p1, p1_ctx, p2, p2_ctx);

	cell *tmp;

	// close_cycles: bind interior cyclic back-edges instead of dropping
	// them (issue #1121). Restricted to copy_term_nat/2 - with attributes
	// in play, closing the cycle can recurse back into this same function.

	q->close_cycles = !copy_attrs;

	if (is_var(p1r) && is_var(p2r))
		tmp = copy_term_to_heap_with_replacement(q, p1, p1_ctx, copy_attrs, p1r, p1r_ctx, p2r, p2r_ctx);
	else
		tmp = copy_term_to_heap(q, p1, p1_ctx, copy_attrs);

	q->close_cycles = false;
	q->dump_var_num = -1;
	q->dump_var_ctx = -1;

	if (!tmp)
		abandon_clone_cycles(q);

	CHECKED(tmp);

	// Reget as slots may have reallocated if copy_attrs=true...

	GET_FIRST_ARG(p1x,any);
	GET_NEXT_ARG(p2x,any);
	return unify(q, p2x, p2x_ctx, tmp, q->st.cur_ctx);
}

static bool bif_iso_copy_term_2(query *q)
{
	return do_copy_term(q, false);
}

static bool bif_duplicate_term_2(query *q)
{
	return do_copy_term(q, true);
}

static bool bif_copy_term_nat_2(query *q)
{
	return do_copy_term(q, false);
}

static bool bif_sys_clone_term_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);

	if (is_atomic(p1) || is_atomic(p2))
		return unify(q, p1, p1_ctx, p2, p2_ctx);

	cell *tmp = clone_term_to_heap(q, p1, p1_ctx);
	CHECKED(tmp);
	return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
}

static bool bif_iso_functor_3(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);

	if (is_var(p1)) {
		GET_NEXT_ARG(p3,any);

		if (!is_iso_atomic(p2))
			return throw_error(q, p2, p2_ctx, "type_error", "atomic");

		if (!is_integer(p3))
			return throw_error(q, p3, p3_ctx, "type_error", "integer");

		if (is_negative(p3))
			return throw_error(q, p3, p3_ctx, "domain_error", "not_less_than_zero");

		if (is_bigint(p3))
			return throw_error(q, p3, p3_ctx, "resource_error", "memory");

		if (!is_atom(p2) && is_positive(p3))
			return throw_error(q, p2, p2_ctx, "type_error", "atom");

		uint32_t arity = (uint32_t)get_smallint(p3);

		if (!arity)
			return unify(q, p1, p1_ctx, p2, p2_ctx);

		int var_num = create_vars(q, arity);
		CHECKED(var_num != -1);
		GET_FIRST_ARG(p1,any);
		GET_NEXT_ARG(p2,any);
		cell *tmp = alloc_heap(q, 1+arity);
		CHECKED(tmp);
		*tmp = (cell){0};
		tmp[0].tag = TAG_INTERNED;
		set_arity(&tmp[0], arity);
		tmp[0].num_cells = 1 + arity;

		if (!is_interned(p2)) {
			tmp[0].val_off = new_atom(q->pl, C_STR(q, p2));

#if 1
			if (tmp[0].val_off == ERR_IDX) {
				q->oom = true;
				return false;
			}
#endif

			CHECKED(tmp[0].val_off != ERR_IDX);
		} else
			tmp[0].val_off = p2->val_off;

		for (unsigned i = 1; i <= arity; i++) {
			memset(tmp+i, 0, sizeof(cell));
			tmp[i].tag = TAG_VAR;
			tmp[i].num_cells = 1;
			tmp[i].var_num = var_num++;
			tmp[i].val_ctx = q->st.cur_ctx;
			tmp[i].flags = FLAG_VAR_REF | FLAG_VAR_ANON;
		}

		bool status;

		if (!call_check(q, tmp, &status, false))
			return status;

		return unify(q, p1, p1_ctx, tmp, q->st.cur_ctx);
	}

	// Return the atomic functor. Long cstring atoms are managed blobs;
	// clearing flags (as for interned atoms) makes C_STR read the
	// strbuf pointer bits as characters. Intern so the result is a
	// stable atom. ISO strings are the '.'/2 functor.
	cell tmp;

	if (is_string(p1)) {
		make_atom(&tmp, g_dot_s);
	} else if (is_cstring(p1)) {
		pl_idx off = new_atom(q->pl, C_STR(q, p1));
		CHECKED(off != ERR_IDX);
		make_atom(&tmp, off);
	} else {
		tmp = *p1;
		tmp.num_cells = 1;
		tmp.flags = 0;
		CLR_OP(&tmp);
		set_arity(&tmp, 0);
	}

	if (!unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
		return false;

	GET_NEXT_ARG(p3,any);
	make_uint(&tmp, get_arity(p1));
	return unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_iso_current_rule_1(query *q)
{
	GET_FIRST_ARG(p1,structure);
	int add_two = 0;

	if (!CMP_STRING_TO_CSTR(q, p1, "/"))
		;
	else if (!CMP_STRING_TO_CSTR(q, p1, "//"))
		add_two = 2;
	else
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *pf = deref(q, p1+1,p1_ctx);
	cell *pa = deref(q, p1+2, p1_ctx);

	if (!is_atom(pf))
		return throw_error(q, p1, p1_ctx, "type_error", "atom");

	if (!is_integer(pa))
		return throw_error(q, p1, p1_ctx, "type_error", "integer");

	const char *functor = C_STR(q, pf);
	size_t functor_len = C_STRLEN(q, pf);
	unsigned arity = get_smallint(pa) + add_two;

	if (strchr(functor, ':')) {
		char tmpbuf1[256], tmpbuf2[256];
		tmpbuf1[0] = tmpbuf2[0] = '\0';
		sscanf(functor, "%255[^:]:%255s", tmpbuf1, tmpbuf2);
		tmpbuf1[sizeof(tmpbuf1)-1] = tmpbuf2[sizeof(tmpbuf2)-1] = '\0';
		module *m = m = find_module(q->pl, tmpbuf1);
		if (!m) return false;

		if (find_functor(m, functor, arity))
			return true;

		return false;
	}

	cell tmp = (cell){0};
	tmp.tag = TAG_INTERNED;
	tmp.val_off = new_atom(q->pl, functor);
	set_arity(&tmp, arity);

	if (search_predicate(q->st.m, &tmp))
		return true;

	bool found = false;

	if (get_builtin(q->pl, functor, functor_len, arity, &found, NULL), found)
		return true;

	return false;
}

static bool search_functor(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx)
{
	if (!q->retry)
		q->st.tmp_iter = sl_first(q->st.m->index);

	CHECKED(push_choice(q));
	predicate *pr = NULL;

	while (sl_next(q->st.tmp_iter, (void*)&pr)) {
		const char *src = C_STR(q, &pr->key);

		if (src[0] == '$')
			continue;

		if (pr->is_abolished || pr->is_builtin)
			continue;

		cell tmpn, tmpa;
		make_atom(&tmpn, pr->key.val_off);
		make_int(&tmpa, get_arity(&pr->key));

		if (unify(q, p1, p1_ctx, &tmpn, q->st.cur_ctx)
			&& unify(q, p2, p2_ctx, &tmpa, q->st.cur_ctx)) {
			return true;
		}
	}

	sl_done(q->st.tmp_iter);
	drop_choice(q);
	return false;
}

static bool bif_iso_current_predicate_1(query *q)
{
	GET_FIRST_ARG(p_pi,any);

	if (!CMP_STRING_TO_CSTR(q, p_pi, ":")) {
		module *tmp_m = find_module(q->pl, C_STR(q, p_pi+1));
		if(!tmp_m) return false;
		q->st.m = tmp_m;
		p_pi += 2;
	}

	if (is_var(p_pi)) {
		cell tmp1, tmp2;
		cell *p1 = &tmp1, *p2 = &tmp2;
		pl_ctx p1_ctx = q->st.cur_ctx;
		pl_ctx p2_ctx = q->st.cur_ctx;
		frame *f = GET_CURR_FRAME();
		unsigned var_num = f->actual_slots;
		make_ref(&tmp1, var_num++, q->st.cur_ctx);
		make_ref(&tmp2, var_num++, q->st.cur_ctx);
		if (create_vars(q, 2) < 0)
			return throw_error(q, p1, p1_ctx, "resource_error", "stack");
		GET_FIRST_ARG(p_pi,any);
		bool ok = search_functor(q, p1, p1_ctx, p2, p2_ctx) ? true : false;
		cell *tmp = alloc_heap(q, 3);
		make_instr(tmp, g_slash_s, NULL, 2, 2);
		tmp[1] = *p1;
		tmp[2] = *p2;
		SET_OP(tmp, OP_YFX);
		return ok && unify(q, p_pi, p_pi_ctx, tmp, q->st.cur_ctx);
	}

	if (get_arity(p_pi) != 2)
		return throw_error(q, p_pi, p_pi_ctx, "type_error", "predicate_indicator");

	if (CMP_STRING_TO_CSTR(q, p_pi, "/"))
		return throw_error(q, p_pi, p_pi_ctx, "type_error", "predicate_indicator");

	cell *p1 = p_pi + 1;
	p1 = deref(q, p1, p_pi_ctx);
	pl_ctx p1_ctx = q->latest_ctx;

	if (!is_atom(p1) && !is_var(p1))
		return throw_error(q, p_pi, p_pi_ctx, "type_error", "predicate_indicator");

	cell *p2 = p_pi + 2;
	p2 = deref(q, p2, p_pi_ctx);
	pl_ctx p2_ctx = q->latest_ctx;

	if ((!is_integer(p2) || is_negative(p2)) && !is_var(p2))
		return throw_error(q, p_pi, p_pi_ctx, "type_error", "predicate_indicator");

	if (is_var(p1) || is_var(p2))
		return search_functor(q, p1, p1_ctx, p2, p2_ctx);

	cell tmp = (cell){0};
	tmp.tag = TAG_INTERNED;
	tmp.val_off = is_interned(p1) ? p1->val_off : new_atom(q->pl, C_STR(q, p1));
	set_arity(&tmp, get_smallint(p2));
	predicate *pr;

	if (q->st.m == q->pl->user_m)
		pr = search_predicate(q->st.m, &tmp);
	else
		pr = find_predicate(q->st.m, &tmp);

	if (!pr)
		return false;

	return true;
}

static bool bif_cyclic_term_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return is_cyclic_term(q, p1, p1_ctx) ? true : false;
}

static bool bif_iso_acyclic_term_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return is_acyclic_term(q, p1, p1_ctx) ? true : false;
}

static bool bif_sys_current_prolog_flag_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,any);

	if (!CMP_STRING_TO_CSTR(q, p1, "double_quotes")) {
		cell tmp;

		if (q->st.m->flags.double_quote_atom)
			make_atom(&tmp, new_atom(q->pl, "atom"));
		else if (q->st.m->flags.double_quote_codes)
			make_atom(&tmp, new_atom(q->pl, "codes"));
		else if (q->st.m->flags.double_quote_chars)
			make_atom(&tmp, new_atom(q->pl, "chars"));

		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "answer_write_options")) {
		cell tmp[2];
		make_instr(tmp+0, new_atom(q->pl, "max_depth"), NULL, 1, 1);
		make_uint(tmp+1, q->pl->def_max_depth);
		allocate_list(q, tmp);
		make_instr(tmp+0, new_atom(q->pl, "quoted"), NULL, 1, 1);
		make_atom(tmp+1, q->pl->def_quoted?g_true_s:g_false_s);
		append_list(q, tmp);
		make_instr(tmp+0, new_atom(q->pl, "double_quotes"), NULL, 1, 1);
		make_atom(tmp+1, q->pl->def_double_quotes?g_true_s:g_false_s);
		append_list(q, tmp);
		return unify(q, p2, p2_ctx, end_list(q), q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "char_conversion")) {
		cell tmp;

		if (q->st.m->flags.char_conversion)
			make_atom(&tmp, g_on_s);
		else
			make_atom(&tmp, g_off_s);

		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "verbose")) {
		cell tmp;
		make_atom(&tmp, q->pl->quiet ? g_false_s : g_true_s);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "global_bb")) {
		cell tmp;
		make_atom(&tmp, q->pl->global_bb ? g_true_s : g_false_s);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "tabling")) {
		cell tmp;
		make_atom(&tmp, q->pl->tabling ? g_true_s : g_false_s);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "max_table_answer_size")) {
		cell tmp;
		if (q->pl->tbl_max_answer_size)
			make_int(&tmp, q->pl->tbl_max_answer_size);
		else
			make_atom(&tmp, new_atom(q->pl, "infinite"));
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "max_table_subgoal_size")) {
		cell tmp;
		if (q->pl->tbl_max_subgoal_size)
			make_int(&tmp, q->pl->tbl_max_subgoal_size);
		else
			make_atom(&tmp, new_atom(q->pl, "infinite"));
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "max_answers_for_subgoal")) {
		cell tmp;
		if (q->pl->tbl_max_answers_for_subgoal)
			make_int(&tmp, q->pl->tbl_max_answers_for_subgoal);
		else
			make_atom(&tmp, new_atom(q->pl, "infinite"));
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
#if USE_THREADS
	} else if (!CMP_STRING_TO_CSTR(q, p1, "threads")) {
		cell tmp;
		make_atom(&tmp, g_true_s);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "os_threads")) {
		cell tmp;
		make_int(&tmp, g_max_os_threads ? g_max_os_threads : MAX_ACTUAL_THREADS);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
#else
	} else if (!CMP_STRING_TO_CSTR(q, p1, "threads")) {
		cell tmp;
		make_atom(&tmp, g_false_s);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
#endif
	} else if (!CMP_STRING_TO_CSTR(q, p1, "unix")) {
		cell tmp;
		make_atom(&tmp, host_is_unix() ? g_true_s : g_false_s);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "os")) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, host_os()));
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "rationals")) {
		cell tmp;
		make_atom(&tmp, HAS_RATIONALS ? g_true_s : g_false_s);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "occurs_check")) {
		cell tmp;

		if (q->st.m->flags.occurs_check == OCCURS_CHECK_TRUE)
			make_atom(&tmp, g_true_s);
		else if (q->st.m->flags.occurs_check == OCCURS_CHECK_FALSE)
			make_atom(&tmp, g_false_s);
		else
			make_atom(&tmp, new_atom(q->pl, "error"));

		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "encoding")) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, "UTF-8"));
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "empty_args")) {
		cell tmp;

		if (q->st.m->flags.empty_args)
			make_atom(&tmp, g_on_s);
		else
			make_atom(&tmp, g_off_s);

		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "strict_iso")) {
		cell tmp;

		if (q->st.m->flags.strict_iso)
			make_atom(&tmp, g_on_s);
		else
			make_atom(&tmp, g_off_s);

		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "debug")) {
		cell tmp;

		if (q->st.m->flags.debug)
			make_atom(&tmp, g_on_s);
		else
			make_atom(&tmp, g_off_s);

		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "character_escapes")) {
		cell tmp;

		if (q->st.m->flags.character_escapes)
			make_atom(&tmp, g_true_s);
		else
			make_atom(&tmp, g_false_s);

		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "dialect")) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, "trealla"));
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "integer_rounding_function")) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, "toward_zero"));
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "bounded")) {
		cell tmp;
		make_atom(&tmp, g_false_s);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "max_arity")) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, "unbounded"));
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "max_procedure_arity")) {
		cell tmp;
		make_int(&tmp, MAX_PROCEDURE_ARITY);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "max_integer")) {
		return false;
	} else if (!CMP_STRING_TO_CSTR(q, p1, "min_integer")) {
		return false;
	} else if (!CMP_STRING_TO_CSTR(q, p1, "cpu_count")) {
		cell tmp;
		make_int(&tmp, g_cpu_count);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "version")) {
		unsigned v1 = 0;
		sscanf(g_version, "v%u", &v1);
		cell tmp;
		make_int(&tmp, v1);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "version_data")) {
		unsigned v1 = 0, v2 = 0, v3 = 0;
		sscanf(g_version, "v%u.%u.%u", &v1, &v2, &v3);
		cell *tmp = alloc_heap(q, 5);
		CHECKED(tmp);
		make_atom(&tmp[0], new_atom(q->pl, "trealla"));
		make_int(&tmp[1], v1);
		make_int(&tmp[2], v2);
		make_int(&tmp[3], v3);
		make_atom(&tmp[4], g_nil_s);
		set_arity(&tmp[0], 4);
		tmp[0].num_cells = 5;
		return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "version_git")) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, g_version));
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "argv")) {
		if (g_avc >= g_ac)
			return unify(q, p2, p2_ctx, make_nil(), q->st.cur_ctx);

		int i = g_avc;
		cell tmp;
		make_string(&tmp, g_av[i++]);
		allocate_list(q, &tmp);

		while (i < g_ac) {
			make_string(&tmp, g_av[i++]);
			append_list(q, &tmp);
		}

		cell *l = end_list(q);
		CHECKED(l);
		return unify(q, p2, p2_ctx, l, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "raw_argv")) {
		int i = 0;
		cell tmp;
		make_string(&tmp, g_argv[i++]);
		allocate_list(q, &tmp);

		while (i < g_argvc) {
			make_string(&tmp, g_argv[i++]);
			append_list(q, &tmp);
		}

		cell *l = end_list(q);
		CHECKED(l);
		return unify(q, p2, p2_ctx, l, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "unknown")) {
		cell tmp;
		make_atom(&tmp,
			q->st.m->flags.unknown == UNK_ERROR ? new_atom(q->pl, "error") :
			q->st.m->flags.unknown == UNK_WARNING ? new_atom(q->pl, "warning") :
			q->st.m->flags.unknown == UNK_CHANGEABLE ? new_atom(q->pl, "changeable") :
			new_atom(q->pl, "fail"));
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	} else if (!CMP_STRING_TO_CSTR(q, p1, "generate_debug_info")) {
	}

	return throw_error(q, p1, p1_ctx, "domain_error", "prolog_flag");
}

static bool answer_write_options_error(query *q, cell *c)
{
	cell *tmp = alloc_heap(q, 2+c->num_cells);
	CHECKED(tmp);
	make_instr(tmp, g_plus_s, bif_iso_add_2, 2, 1+c->num_cells);
	make_atom(tmp+1, new_atom(q->pl, "answer_write_options"));
	dup_cells(tmp+2, c, c->num_cells);
	SET_OP(tmp, OP_YFX);
	return throw_error(q, tmp, q->st.cur_ctx, "domain_error", "flag_value");
}

static bool flag_value_error(query *q, cell *p1, cell *p2)
{
	cell *tmp = alloc_heap(q, 2+p2->num_cells);
	CHECKED(tmp);
	make_instr(tmp, g_plus_s, bif_iso_add_2, 2, 1+p2->num_cells);
	make_atom(tmp+1, p1->val_off);
	dup_cells(tmp+2, p2, p2->num_cells);
	SET_OP(tmp, OP_YFX);
	return throw_error(q, tmp, q->st.cur_ctx, "domain_error", "flag_value");
}

static bool bif_iso_set_prolog_flag_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);

	if (!is_atom(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "atom");

	if (has_vars(q, p2, p2_ctx))
		return throw_error(q, p2, p2_ctx, "instantiation_error", "var");

	if (!CMP_STRING_TO_CSTR(q, p1, "double_quotes")) {
		if (!CMP_STRING_TO_CSTR(q, p2, "atom")) {
			q->st.m->flags.double_quote_chars = q->st.m->flags.double_quote_codes = false;
			q->st.m->flags.double_quote_atom = true;
		} else if (!CMP_STRING_TO_CSTR(q, p2, "codes")) {
			q->st.m->flags.double_quote_chars = q->st.m->flags.double_quote_atom = false;
			q->st.m->flags.double_quote_codes = true;
		} else if (!CMP_STRING_TO_CSTR(q, p2, "chars")) {
			q->st.m->flags.double_quote_atom = q->st.m->flags.double_quote_codes = false;
			q->st.m->flags.double_quote_chars = true;
		} else {
			return flag_value_error(q, p1, p2);
		}

		q->st.m->p->flags = q->st.m->flags;
	} else if (!CMP_STRING_TO_CSTR(q, p1, "answer_write_options")) {
		cell *l = p2;
		l = deref(q, l, p2_ctx);
		pl_ctx l_ctx = q->latest_ctx;

		if (!is_list_or_nil(l))
			return answer_write_options_error(q, l);

		PROLOG_LIST_HANDLER(l);

		while (is_iso_list(l)) {
			cell *h = PROLOG_LIST_HEAD(l);
			h = deref(q, h, l_ctx);
			pl_ctx h_ctx = q->latest_ctx;

			if (!is_compound(h))
				return answer_write_options_error(q, h);

			cell *h1 = h + 1;
			h1 = deref(q, h1, h_ctx);

			if (!CMP_STRING_TO_CSTR(q, h, "max_depth") && (get_arity(h) == 1)) {
				if (!is_integer(h1))
					return answer_write_options_error(q, h);

				if (is_negative(h1))
					return answer_write_options_error(q, h);

				q->pl->def_max_depth = get_smallint(h1);
			} else if (!CMP_STRING_TO_CSTR(q, h, "quoted") && (get_arity(h) == 1)) {
				if (!is_atom(h1))
					return answer_write_options_error(q, h);

				if (!CMP_STRING_TO_CSTR(q, h1, "true") || !CMP_STRING_TO_CSTR(q, h1, "on"))
					q->pl->def_quoted = true;
				else if (!CMP_STRING_TO_CSTR(q, h1, "false") || !CMP_STRING_TO_CSTR(q, h1, "off"))
					q->pl->def_quoted = false;
				else
					return answer_write_options_error(q, h);
			} else if (!CMP_STRING_TO_CSTR(q, h, "double_quotes") && (get_arity(h) == 1)) {
				if (!is_atom(h1))
					return answer_write_options_error(q, h);

				if (!CMP_STRING_TO_CSTR(q, h1, "true") || !CMP_STRING_TO_CSTR(q, h1, "on"))
					q->pl->def_double_quotes = true;
				else if (!CMP_STRING_TO_CSTR(q, h1, "false") || !CMP_STRING_TO_CSTR(q, h1, "off"))
					q->pl->def_double_quotes = false;
				else
					return answer_write_options_error(q, h);
			} else
				return answer_write_options_error(q, h);

			l = PROLOG_LIST_TAIL(l);
			l = deref(q, l, l_ctx);
			l_ctx = q->latest_ctx;
		}
	} else if (!CMP_STRING_TO_CSTR(q, p1, "character_escapes")) {
		if (!CMP_STRING_TO_CSTR(q, p2, "true") || !CMP_STRING_TO_CSTR(q, p2, "on"))
			q->st.m->flags.character_escapes = true;
		else if (!CMP_STRING_TO_CSTR(q, p2, "false") || !CMP_STRING_TO_CSTR(q, p2, "off"))
			q->st.m->flags.character_escapes = false;
		else {
			return flag_value_error(q, p1, p2);
		}
	} else if (!CMP_STRING_TO_CSTR(q, p1, "char_conversion")) {
		if (!CMP_STRING_TO_CSTR(q, p2, "true") || !CMP_STRING_TO_CSTR(q, p2, "on"))
			q->st.m->flags.char_conversion = true;
		else if (!CMP_STRING_TO_CSTR(q, p2, "false") || !CMP_STRING_TO_CSTR(q, p2, "off"))
			q->st.m->flags.char_conversion = false;
		else {
			return flag_value_error(q, p1, p2);
		}
	} else if (!CMP_STRING_TO_CSTR(q, p1, "occurs_check")) {
		if (!CMP_STRING_TO_CSTR(q, p2, "true") || !CMP_STRING_TO_CSTR(q, p2, "on"))
			q->st.m->flags.occurs_check = OCCURS_CHECK_TRUE;
		else if (!CMP_STRING_TO_CSTR(q, p2, "false") || !CMP_STRING_TO_CSTR(q, p2, "off"))
			q->st.m->flags.occurs_check = OCCURS_CHECK_FALSE;
		else if (!CMP_STRING_TO_CSTR(q, p2, "error"))
			q->st.m->flags.occurs_check = OCCURS_CHECK_ERROR;
		else {
			return flag_value_error(q, p1, p2);
		}
	} else if (!CMP_STRING_TO_CSTR(q, p1, "debug")) {
		if (!CMP_STRING_TO_CSTR(q, p2, "true") || !CMP_STRING_TO_CSTR(q, p2, "on"))
			q->st.m->flags.debug = true;
		else if (!CMP_STRING_TO_CSTR(q, p2, "false") || !CMP_STRING_TO_CSTR(q, p2, "off"))
			q->st.m->flags.debug = false;
		else {
			return flag_value_error(q, p1, p2);
		}
	} else if (!CMP_STRING_TO_CSTR(q, p1, "global_bb")) {
		if (!CMP_STRING_TO_CSTR(q, p2, "true") || !CMP_STRING_TO_CSTR(q, p2, "on"))
			q->pl->global_bb = true;
		else if (!CMP_STRING_TO_CSTR(q, p2, "false") || !CMP_STRING_TO_CSTR(q, p2, "off"))
			q->pl->global_bb = false;
		else {
			return flag_value_error(q, p1, p2);
		}
	} else if (!CMP_STRING_TO_CSTR(q, p1, "tabling")) {
		if (!CMP_STRING_TO_CSTR(q, p2, "true") || !CMP_STRING_TO_CSTR(q, p2, "on"))
			q->pl->tabling = true;
		else if (!CMP_STRING_TO_CSTR(q, p2, "false") || !CMP_STRING_TO_CSTR(q, p2, "off"))
			q->pl->tabling = false;
		else {
			return flag_value_error(q, p1, p2);
		}
	} else if (!CMP_STRING_TO_CSTR(q, p1, "max_table_answer_size")) {
		if (is_atom(p2) && !CMP_STRING_TO_CSTR(q, p2, "infinite"))
			q->pl->tbl_max_answer_size = 0;
		else if (is_bigint(p2) || (is_integer(p2) && is_negative(p2)))
			return throw_error(q, p2, p2_ctx, "domain_error", "not_less_than_zero");
		else if (is_smallint(p2))
			q->pl->tbl_max_answer_size = (unsigned)get_smallint(p2);
		else {
			return flag_value_error(q, p1, p2);
		}
	} else if (!CMP_STRING_TO_CSTR(q, p1, "max_table_subgoal_size")) {
		if (is_atom(p2) && !CMP_STRING_TO_CSTR(q, p2, "infinite"))
			q->pl->tbl_max_subgoal_size = 0;
		else if (is_bigint(p2) || (is_integer(p2) && is_negative(p2)))
			return throw_error(q, p2, p2_ctx, "domain_error", "not_less_than_zero");
		else if (is_smallint(p2))
			q->pl->tbl_max_subgoal_size = (unsigned)get_smallint(p2);
		else {
			return flag_value_error(q, p1, p2);
		}
	} else if (!CMP_STRING_TO_CSTR(q, p1, "max_answers_for_subgoal")) {
		if (is_atom(p2) && !CMP_STRING_TO_CSTR(q, p2, "infinite"))
			q->pl->tbl_max_answers_for_subgoal = 0;
		else if (is_bigint(p2) || (is_integer(p2) && is_negative(p2)))
			return throw_error(q, p2, p2_ctx, "domain_error", "not_less_than_zero");
		else if (is_smallint(p2))
			q->pl->tbl_max_answers_for_subgoal = (unsigned)get_smallint(p2);
		else {
			return flag_value_error(q, p1, p2);
		}
	} else if (!CMP_STRING_TO_CSTR(q, p1, "strict_iso")) {
		if (!CMP_STRING_TO_CSTR(q, p2, "true") || !CMP_STRING_TO_CSTR(q, p2, "on"))
			q->st.m->flags.strict_iso = true;
		else if (!CMP_STRING_TO_CSTR(q, p2, "false") || !CMP_STRING_TO_CSTR(q, p2, "off"))
			q->st.m->flags.strict_iso = false;
		else {
			return flag_value_error(q, p1, p2);
		}
	} else if (!CMP_STRING_TO_CSTR(q, p1, "empty_args")) {
		if (!CMP_STRING_TO_CSTR(q, p2, "true") || !CMP_STRING_TO_CSTR(q, p2, "on"))
			q->st.m->flags.empty_args = true;
		else if (!CMP_STRING_TO_CSTR(q, p2, "false") || !CMP_STRING_TO_CSTR(q, p2, "off"))
			q->st.m->flags.empty_args = false;
		else {
			return flag_value_error(q, p1, p2);
		}
	} else if (!CMP_STRING_TO_CSTR(q, p1, "unknown")) {
		if (!CMP_STRING_TO_CSTR(q, p2, "fail")) {
			q->st.m->flags.unknown = UNK_FAIL;
		} else if (!CMP_STRING_TO_CSTR(q, p2, "error")) {
			q->st.m->flags.unknown = UNK_ERROR;
		} else if (!CMP_STRING_TO_CSTR(q, p2, "warning")) {
			q->st.m->flags.unknown = UNK_WARNING;
		} else if (!CMP_STRING_TO_CSTR(q, p2, "changeable")) {
			q->st.m->flags.unknown = UNK_CHANGEABLE;
		} else {
			return flag_value_error(q, p1, p2);
		}
	} else if (!CMP_STRING_TO_CSTR(q, p1, "bounded")
		|| !CMP_STRING_TO_CSTR(q, p1, "max_procedure_arity")
		|| !CMP_STRING_TO_CSTR(q, p1, "max_arity")
		|| !CMP_STRING_TO_CSTR(q, p1, "cpu_count")
		|| !CMP_STRING_TO_CSTR(q, p1, "max_integer")
		|| !CMP_STRING_TO_CSTR(q, p1, "min_integer")
		|| !CMP_STRING_TO_CSTR(q, p1, "version")
		|| !CMP_STRING_TO_CSTR(q, p1, "version_data")
		|| !CMP_STRING_TO_CSTR(q, p1, "version_git")
		|| !CMP_STRING_TO_CSTR(q, p1, "encoding")
		|| !CMP_STRING_TO_CSTR(q, p1, "unix")
		|| !CMP_STRING_TO_CSTR(q, p1, "os")
		|| !CMP_STRING_TO_CSTR(q, p1, "rationals")
		|| !CMP_STRING_TO_CSTR(q, p1, "threads")
#if USE_THREADS
		|| !CMP_STRING_TO_CSTR(q, p1, "os_threads")
#endif
		|| !CMP_STRING_TO_CSTR(q, p1, "verbose")
		|| !CMP_STRING_TO_CSTR(q, p1, "integer_rounding_function")
		|| !CMP_STRING_TO_CSTR(q, p1, "dialect")
		|| !CMP_STRING_TO_CSTR(q, p1, "pid")
		) {
		return throw_error(q, p1, p1_ctx, "permission_error", "modify,flag");
	} else if (!CMP_STRING_TO_CSTR(q, p1, "generate_debug_info")) {
	} else {
		return throw_error(q, p1, p1_ctx, "domain_error", "prolog_flag");
	}

	q->flags = q->st.m->flags;
	return true;
}

static cell *convert_to_list(query *q, cell *c, pl_idx num_cells)
{
	if ((!num_cells || !c->num_cells)) {
		cell *c = alloc_tmp(q, 1);
		if (!c) return c;
		make_atom(c, g_nil_s);
		return c;
	}

	allocate_list(q, c);
	num_cells -= c->num_cells;
	c += c->num_cells;

	while (num_cells > 0) {
		append_list(q, c);
		num_cells -= c->num_cells;
		c += c->num_cells;
	}

	// This function is only ever called on a queue which
	// already has a safe_copy done, so the end_list below
	// can do an unsafe copy.

	return end_list_unsafe(q);
}

static bool bif_sys_list_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	cell *l = convert_to_list(q, q->queues[q->st.qnum].queue, q->queues[q->st.qnum].qp);
	CHECKED(l);
	drop_queuen(q);
	return unify(q, p1, p1_ctx, l, q->st.cur_ctx);
}

bool bif_sys_queue_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	CHECKED(init_tmp_heap(q), q->st.qnum--);
	cell *tmp = clone_term_to_tmp(q, p1, p1_ctx);
	CHECKED(tmp, q->st.qnum--);
	cell *c = tmp;

	for (unsigned i = 0; i < tmp->num_cells; i++, c++) {
		if (!is_ref(c))
			continue;

		const frame *f = GET_FRAME(c->val_ctx);
		const slot *e = get_slot(q, f, c->var_num);
		cell *attrs = e->c.val_attrs;
	}

	// A cycle cannot survive being queued. The clone has no frame slots
	// to hang one from, so it emits a fresh variable where the back-edge
	// was and the term quietly stops being cyclic - see issue #989, where
	// that produced answers that looked plausible and were wrong.
	//
	// Refusing is the same position library(builtins) already takes for
	// bagof/3 and setof/3, which check acyclic_term(G) before they start.
	// A solution that merely *references* a cyclic term still living in
	// an outer frame would sometimes survive this, by accident of the
	// reference outliving the copy; it is rejected too, rather than have
	// findall/3 depend on which frame the cycle happens to sit in.

	if (q->cycle_dropped) {
		q->st.qnum--;
		return throw_error(q, p1, p1_ctx, "type_error", "acyclic_term");
	}

	CHECKED(alloc_queuen(q, q->st.qnum, tmp), q->st.qnum--);
	return true;
}

static bool do_op(query *q, cell *p3, pl_ctx p3_ctx)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,atom);

	if (!is_atom(p3))
		return throw_error(q, p3, p3_ctx, "type_error", "atom");

	unsigned specifier, pri = get_smallint(p1);

	if (!CMP_STRING_TO_CSTR(q, p2, "fx"))
		specifier = OP_FX;
	else if (!CMP_STRING_TO_CSTR(q, p2, "fy"))
		specifier = OP_FY;
	else if (!CMP_STRING_TO_CSTR(q, p2, "xf"))
		specifier = OP_XF;
	else if (!CMP_STRING_TO_CSTR(q, p2, "xfx"))
		specifier = OP_XFX;
	else if (!CMP_STRING_TO_CSTR(q, p2, "xfy"))
		specifier = OP_XFY;
	else if (!CMP_STRING_TO_CSTR(q, p2, "yf"))
		specifier = OP_YF;
	else if (!CMP_STRING_TO_CSTR(q, p2, "yfx"))
		specifier = OP_YFX;
	else
		return throw_error(q, p2, p2_ctx, "domain_error", "operator_specifier");

	if (pri && !CMP_STRING_TO_CSTR(q, p3, "|") && (!IS_INFIX(specifier) || (pri < 1001)))
		return throw_error(q, p3, p3_ctx, "permission_error", "create,operator");

	if (true /*q->st.m->flags.strict_iso*/) {
		if (!CMP_STRING_TO_CSTR(q, p3, "[]"))
			return throw_error(q, p3, p3_ctx, "permission_error", "create,operator");

		if (!CMP_STRING_TO_CSTR(q, p3, "{}"))
			return throw_error(q, p3, p3_ctx, "permission_error", "create,operator");
	}

	if (!CMP_STRING_TO_CSTR(q, p3, ","))
		return throw_error(q, p3, p3_ctx, "permission_error", "modify,operator");

	unsigned tmp_optype = 0;
	unsigned tmp_pri = search_op(q->st.m, C_STR(q, p3), &tmp_optype, false);

	if (IS_INFIX(specifier) && IS_POSTFIX(tmp_optype) && (true || q->st.m->flags.strict_iso))
		return throw_error(q, p3, p3_ctx, "permission_error", "create,operator");

	if (!tmp_pri && !pri)
		return true;

	if (IS_POSTFIX(specifier) && (IS_INFIX(tmp_optype)/* || tmp_pri*/) && (true || q->st.m->flags.strict_iso))
		return throw_error(q, p3, p3_ctx, "permission_error", "create,operator");

	if (IS_POSTFIX(specifier) && IS_INFIX(tmp_optype) && (true || q->st.m->flags.strict_iso))
		return throw_error(q, p3, p3_ctx, "permission_error", "create,operator");

	if (!set_op(q->st.m, C_STR(q, p3), specifier, pri))
		return throw_error(q, p3, p3_ctx, "resource_error", "too_many_ops");

	return true;
}

static bool bif_iso_op_3(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,list_or_atom);

	if (is_negative(p1) || is_gt(p1,1200))
		return throw_error(q, p1, p1_ctx, "domain_error", "operator_priority");

	q->ops_dirty = true;
	PROLOG_LIST_HANDLER(p3);

	while (is_list(p3)) {
		cell *h = PROLOG_LIST_HEAD(p3);
		h = deref(q, h, p3_ctx);
		bool ok = do_op(q, h, q->latest_ctx);

		if (ok != true)
			return ok;

		p3 = PROLOG_LIST_TAIL(p3);
		p3 = deref(q, p3, p3_ctx);
		p3_ctx = q->latest_ctx;

		if (is_var(p3))
			return throw_error(q, p3, p3_ctx, "instantiation_error", "atom");

		if (is_nil(p3))
			return true;
	}

	if (is_atom(p3))
		return do_op(q, p3, p3_ctx);

	return true;
}

static bool bif_help_0(query *q)
{
	sliter *iter = sl_first(q->pl->help);
	builtins *fn;

	while (sl_next(iter, (void**)&fn)) {
		if (fn->arity)
			fprintf(stdout, "%s/%u: %s(%s)%s%s\n", fn->name, fn->arity, fn->name, fn->help ? fn->help : "no args", fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
		else
			fprintf(stdout, "%s/%u: %s%s%s\n", fn->name, fn->arity, fn->name, fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
	}

	sl_done(iter);
	return true;
}

static bool bif_module_info_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,var);
	const char *name = C_STR(q, p1);
	module *m = find_module(q->pl, name);

	if (!m)
		return false;

	CHECKED(init_tmp_heap(q));

	for (predicate *pr = list_front(&m->predicates);
		pr; pr = list_next(pr)) {
		if (!pr->is_public)
			continue;

		cell tmp[3];
		make_instr(tmp+0, g_slash_s, NULL, 2, 2);
		SET_OP(tmp, OP_YFX);
		make_atom(tmp+1, pr->key.val_off);
		make_int(tmp+2, get_arity(&pr->key));
		append_list(q, tmp);
	}

	cell *l = end_list(q);
	CHECKED(l);
	return unify(q, p2, p2_ctx, l, q->st.cur_ctx);
}

static bool bif_source_info_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,var);

	if (!is_compound(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	if (get_arity(p1) != 2)
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	if (p1->val_off != g_slash_s)
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *f = p1 + 1;

	if (!is_atom(f))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *a = p1 + 2;

	if (!is_smallint(a))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell key = (cell){0};
	key.tag = TAG_INTERNED;
	key.val_off = f->val_off;
	set_arity(&key, get_smalluint(a));
	predicate *pr = find_predicate(q->st.m, &key);

	if (!pr || pr->is_dynamic)
		return false;

	CHECKED(init_tmp_heap(q));

	for (rule *r = pr->head; r; r = r->next) {
		cell tmp[8];
		make_instr(tmp+0, g_dot_s, NULL, 2, 7);
		make_instr(tmp+1, new_atom(q->pl, "filename"), NULL, 1, 1);
		make_cstring(tmp+2, r->filename);
		make_instr(tmp+3, g_dot_s, NULL, 2, 4);
		make_instr(tmp+4, new_atom(q->pl, "lines"), NULL, 2, 2);
		make_uint(tmp+5, r->line_num_start);
		make_uint(tmp+6, r->line_num_end);
		make_atom(tmp+7, g_nil_s);
		append_list(q, tmp);
	}

	cell *l = end_list(q);
	CHECKED(l);
	return unify(q, p2, p2_ctx, l, q->st.cur_ctx);
}

static bool bif_help_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	bool found = false, evaluable = false;

	if (!get_arity(p1)) {
		if (!is_atom(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		const char *functor = C_STR(q, p1);
		sliter *iter = sl_find_key(q->pl->help, functor);
		builtins *fn;

		while (sl_next_key(iter, (void**)&fn)) {
			if (fn->help_alt) {
				if (fn->arity)
					fprintf(stdout, "%% %s/%u: %s(%s)%s%s\n", fn->name, fn->arity, fn->name, fn->help_alt ? fn->help_alt : "no args", fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
			}

			if (fn->arity)
				fprintf(stdout, "%% %s/%u: %s(%s)%s%s\n", fn->name, fn->arity, fn->name, fn->help ? fn->help : "no args", fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
			else
				fprintf(stdout, "%% %s/%u: %s%s%s\n", fn->name, fn->arity, fn->name, fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
		}

		sl_done(iter);
		return true;
	}

	if (!is_compound(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	if (get_arity(p1) != 2)
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	if (p1->val_off != g_slash_s)
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *f = p1 + 1;

	if (!is_atom(f))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *a = p1 + 2;

	if (!is_smallint(a))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	const char *functor = C_STR(q, f);
	unsigned arity = get_smallint(a);
	builtins *fn = get_help(q->pl, functor, arity, &found, &evaluable);

	if (!found || !fn)
		return false;

	if (fn->help_alt) {
		if (fn->arity)
			fprintf(stdout, "%% %s/%u: %s(%s)%s%s\n", fn->name, fn->arity, fn->name, fn->help_alt ? fn->help_alt : "no args", fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
	}

	if (arity)
		fprintf(stdout, "%% %s/%u: %s(%s)%s%s\n%s\n", fn->name, arity, fn->name, fn->help ? fn->help : "no args", fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"", fn->desc?fn->desc:"");
	else
		fprintf(stdout, "%% %s/%u: %s%s%s\n%s\n", fn->name, arity, fn->name, fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"", fn->desc?fn->desc:"");

	return true;
}


// Trealla has no per-predicate documentation site. The generated
// predicate reference in README.md is what the GitHub Pages site
// renders, so point there.

#define DOC_URL_TREALLA "https://trealla-prolog.github.io/trealla/#predicate-reference"

// Emit a documentation URL, as an OSC 8 terminal hyperlink when we are
// talking to a terminal and as plain text otherwise.
//
// Guarded on stdin *and* stdout being ttys. stdin is the interactive
// check; stdout matters because otherwise
//
//     tpl -g "help(append/3,swi)" > notes.txt
//
// would write escape sequences into the file. Terminals that don't
// understand OSC 8 ignore it and show the text, which is why the URL
// is also the link label.

static void print_doc_url(const char *url)
{
	if (isatty(fileno(stdin)) && isatty(fileno(stdout)))
		fprintf(stdout, "\x1b]8;;%s\x1b\\%s\x1b]8;;\x1b\\", url, url);
	else
		fprintf(stdout, "%s", url);
}

static bool bif_help_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,atom);
	bool found = false, evaluable = false;
	const char *pr = C_STR(q, p2);
	char url[1024];

	if (!get_arity(p1)) {
		if (!is_atom(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		const char *functor = C_STR(q, p1);
		sliter *iter = sl_find_key(q->pl->help, functor);
		builtins *fn;

		while (sl_next_key(iter, (void**)&fn)) {
			if (fn->arity)
				fprintf(stdout, "%% %s/%u: %s(%s)%s%s\n", fn->name, fn->arity, fn->name, fn->help ? fn->help : "no args", fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
			else
				fprintf(stdout, "%% %s/%u: %s%s%s\n", fn->name, fn->arity, fn->name, fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
		}

		sl_done(iter);
		return true;
	}

	if (!is_compound(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	if (get_arity(p1) != 2)
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	if (p1->val_off != g_slash_s)
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *f = p1 + 1;

	if (!is_atom(f))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *a = p1 + 2;

	if (!is_smallint(a))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	const char *functor = C_STR(q, f);
	unsigned arity = get_smallint(a);
	builtins *fn = get_help(q->pl, functor, arity, &found, &evaluable);

	if (!found || !fn)
		return false;

	// url was only ever written for swi and tau. Any other flavour -
	// including the obvious help(F/A, trealla) - left it as whatever
	// was on the stack and printed that with %s, reading on until it
	// happened to find a zero byte.
	//
	// trealla now reports our own entry, the same as help/1, and an
	// unrecognised flavour is an error rather than garbage.

	if (!strcmp(pr, "swi"))
		snprintf(url, sizeof(url), "http://swi-prolog.org/pldoc/man?predicate=%s/%u", functor, arity);
	else if (!strcmp(pr, "tau"))
		snprintf(url, sizeof(url), "http://tau-prolog.org/documentation/prolog/builtin/%s/%u", functor, arity);
	else if (!strcmp(pr, "trealla")) {
		// Trealla has no per-predicate documentation site, so this
		// reports the local entry and links to the generated predicate
		// reference in the README, which is what the GitHub Pages site
		// renders. Deep-linking to the predicate itself would need
		// per-row anchors in util/gen_reference.py.

		if (arity)
			fprintf(stdout, "%% %s/%u: %s(%s)%s%s\n", fn->name, arity, fn->name, fn->help ? fn->help : "no args", fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
		else
			fprintf(stdout, "%% %s/%u: %s%s%s\n", fn->name, arity, fn->name, fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");

		// Only 31 of the help declarations carry a desc, so most
		// predicates would otherwise print a blank line here.

		if (fn->desc && *fn->desc)
			fprintf(stdout, "%s\n", fn->desc);

		fprintf(stdout, "%% ");
		print_doc_url(DOC_URL_TREALLA);
		fprintf(stdout, "\n");
		return true;
	} else
		return throw_error(q, p2, p2_ctx, "domain_error", "help_flavour");

	fprintf(stdout, "%% %s/%u: ", fn->name, arity);
	print_doc_url(url);
	fprintf(stdout, "\n");
	return true;
}

static bool bif_module_help_1(query *q)
{
	GET_FIRST_ARG(pm,atom);
	module *m = find_module(q->pl, C_STR(q, pm));

	if (!m)
		return false;

	sliter *iter = sl_first(q->pl->help);
	builtins *fn;

	while (sl_next(iter, (void**)&fn)) {
		if (fn->m != m)
			continue;

		if (fn->arity)
			fprintf(stdout, "%% %s/%u: %s(%s)%s%s\n", fn->name, fn->arity, fn->name, fn->help ? fn->help : "no args", fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
		else
			fprintf(stdout, "%% %s/%u: %s%s%s\n", fn->name, fn->arity, fn->name, fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
	}

	sl_done(iter);
	return true;
}

static bool bif_module_help_2(query *q)
{
	GET_FIRST_ARG(pm,atom);
	GET_NEXT_ARG(p1,any);
	bool found = false, evaluable = false;
	module *m = find_module(q->pl, C_STR(q, pm));

	if (!m)
		return false;

	if (!get_arity(p1)) {
		if (!is_atom(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		const char *functor = C_STR(q, p1);
		sliter *iter = sl_find_key(q->pl->help, functor);
		builtins *fn;

		while (sl_next_key(iter, (void**)&fn)) {
			if (fn->m != m)
				continue;

			if (fn->arity)
				fprintf(stdout, "%% %s/%u: %s(%s)%s%s\n", fn->name, fn->arity, fn->name, fn->help ? fn->help : "no args", fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
			else
				fprintf(stdout, "%% %s/%u: %s%s%s\n", fn->name, fn->arity, fn->name, fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
		}

		sl_done(iter);
		return true;
	}

	if (!is_compound(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	if (get_arity(p1) != 2)
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	if (p1->val_off != g_slash_s)
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *f = p1 + 1;

	if (!is_atom(f))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *a = p1 + 2;

	if (!is_smallint(a))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	const char *functor = C_STR(q, f);
	unsigned arity = get_smallint(a);
	builtins *fn = get_module_help(m, functor, arity, &found, &evaluable);

	if (!found || !fn)
		return false;

	if (arity)
		fprintf(stdout, "%% %s/%u: %s(%s)%s%s\n", fn->name, arity, fn->name, fn->help ? fn->help : "no args", fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
	else
		fprintf(stdout, "%% %s/%u: %s%s%s\n", fn->name, arity, fn->name, fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");

	return true;
}

static bool bif_module_help_3(query *q)
{
	GET_FIRST_ARG(pm,atom);
	GET_NEXT_ARG(p1,any);
	GET_NEXT_ARG(p2,atom);
	bool found = false, evaluable = false;
	const char *pr = C_STR(q, p2);
	char url[1024];
	module *m = find_module(q->pl, C_STR(q, pm));

	if (!m)
		return false;

	if (!get_arity(p1)) {
		if (!is_atom(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		const char *functor = C_STR(q, p1);
		sliter *iter = sl_find_key(q->pl->help, functor);
		builtins *fn;

		while (sl_next_key(iter, (void**)&fn)) {
			if (fn->m != m)
				continue;

			if (fn->arity)
				fprintf(stdout, "%% %s/%u: %s(%s)%s%s\n", fn->name, fn->arity, fn->name, fn->help ? fn->help : "no args", fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
			else
				fprintf(stdout, "%% %s/%u: %s%s%s\n", fn->name, fn->arity, fn->name, fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
		}

		sl_done(iter);
		return true;
	}

	if (!is_compound(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	if (get_arity(p1) != 2)
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	if (p1->val_off != g_slash_s)
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *f = p1 + 1;

	if (!is_atom(f))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	cell *a = p1 + 2;

	if (!is_smallint(a))
		return throw_error(q, p1, p1_ctx, "type_error", "predicate_indicator");

	const char *functor = C_STR(q, f);
	unsigned arity = get_smallint(a);
	builtins *fn = get_module_help(m, functor, arity, &found, &evaluable);

	if (!found || !fn)
		return false;

	// As in help/2: url was only written for swi and tau, and printed
	// regardless, so any other flavour printed uninitialised stack.

	if (!strcmp(pr, "swi"))
		snprintf(url, sizeof(url), "http://swi-prolog.org/pldoc/man?predicate=%s/%u", functor, arity);
	else if (!strcmp(pr, "tau"))
		snprintf(url, sizeof(url), "http://tau-prolog.org/documentation/prolog/builtin/%s/%u", functor, arity);
	else if (!strcmp(pr, "trealla"))
		snprintf(url, sizeof(url), "%s", DOC_URL_TREALLA);
	else
		return throw_error(q, p2, p2_ctx, "domain_error", "help_flavour");

	if (arity)
		fprintf(stdout, "%s/%u: %s(%s)%s%s", fn->name, arity, fn->name, fn->help ? fn->help : "no args", fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");
	else
		fprintf(stdout, "%s/%u: %s%s%s", fn->name, arity, fn->name, fn->iso?" [ISO]":"", fn->evaluable?" [EVALUABLE]":"");

	if (url[0]) {
		fprintf(stdout, " %% ");
		print_doc_url(url);
		fprintf(stdout, "\n");
	} else
		fprintf(stdout, "\n%s\n", fn->desc ? fn->desc : "");

	return true;
}

const char *dump_key(const void *k, const void *v, const void *p)
{
	query *q = (query*)p;
	cell *c = (cell*)k;
	return print_term_to_strbuf(q, c, q->st.cur_ctx, 0);
}

static bool bif_sys_first_non_octet_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,integer_or_var);
	unsigned len = C_STRLEN_UTF8(p1);
	const char *src = C_STR(q, p1);

	for (unsigned i = 0; i < len; i++) {
		int ch = get_char_utf8(&src);

		if (ch > 255) {
			cell tmp;
			make_uint(&tmp, i);
			return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
		}
	}

	return false;
}

static bool bif_trace_0(query *q)
{
	q->trace = !q->trace;
	return true;
}

static bool do_profile(query *q)
{
	fprintf(stderr, "#functor/arity,match_attempts,matched,tcos\n");

	for (module *m = list_front(&q->pl->modules);
		m; m = list_next(m)) {
		for (predicate *pr = list_front(&m->predicates);
			pr; pr = list_next(pr)) {
			for (rule *r = pr->head; r; r = r->next) {
				if (!r->attempted)
					continue;

				fprintf(stderr, "'%s/%u',%llu,%llu,%llu\n",
					C_STR(q, &pr->key), get_arity(&pr->key),
					(unsigned long long)r->attempted, (unsigned long long)r->matched, (unsigned long long)r->tcos);
			}
		}
	}

	return true;
}

bool bif_statistics_0(query *q)
{
	fprintf(stdout,
		"Goals %"PRIu64", "
		"Matches %"PRIu64","
		"(succeeded %"PRIu64").\n"

		"Active frames %u, "
		"choices %u, "
		"trails %u, "
		"slots %u, "
		"heap %u/%u.\n"

		"Backtracks %"PRIu64", "
		"Retries %"PRIu64", "
		"TCOs:%"PRIu64", "
		"Frame recovs:%"PRIu64", "
		"Frame norecovs:%"PRIu64"\n",

		q->total_inferences, q->total_matches, q->total_matched,
		q->st.fp, q->st.cp, q->st.tp, q->st.sp,
		q->st.hp, q->st.hp_num,
		q->total_backtracks, q->total_retries, q->total_tcos, q->total_recovs, q->total_no_recovs
		);
	return true;
}

static bool bif_statistics_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,list_or_var);

	if (!CMP_STRING_TO_CSTR(q, p1, "cputime") && is_var(p2)) {
		uint64_t now = cpu_time_in_usec();
		double elapsed = now - q->st.cpu_time;
		cell tmp;
		make_float(&tmp, elapsed/1000/1000);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "gctime") && is_var(p2)) {
		cell tmp;
		make_float(&tmp, 0);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "profile") && is_var(p2)) {
		return do_profile(q);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "wall") && is_var(p2)) {
		uint64_t now = wall_time_in_usec();
		cell tmp;
		make_uint(&tmp, now/1000);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "runtime")) {
		uint64_t now = cpu_time_in_usec();
		double elapsed = now - q->st.cpu_time;
		cell tmp;
		make_int(&tmp, elapsed/1000);
		allocate_list(q, &tmp);
		elapsed = now - q->time_cpu_last_started;
		q->time_cpu_last_started = now;
		make_uint(&tmp, elapsed/1000);
		append_list(q, &tmp);
		cell *l = end_list(q);
		CHECKED(l);
		return unify(q, p2, p2_ctx, l, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "frames") && is_var(p2)) {
		cell tmp;
		make_int(&tmp, q->st.fp);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "choices") && is_var(p2)) {
		cell tmp;
		make_int(&tmp, q->st.cp);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "trails") && is_var(p2)) {
		cell tmp;
		make_int(&tmp, q->st.tp);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "slots") && is_var(p2)) {
		cell tmp;
		make_int(&tmp, q->st.sp);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "heap") && is_var(p2)) {
		cell tmp;
		make_int(&tmp, q->st.hp);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	return throw_error(q, p1, p1_ctx, "domain_error", "statistics_key");
}

// True if ch is one of the characters in the UTF-8 set `set`.

static bool char_in_set(const char *set, int ch)
{
	while (*set) {
		if (get_char_utf8(&set) == ch)
			return true;
	}

	return false;
}

// split_string(+String, +SepChars, +PadChars, -SubStrings)
//
// This follows SWI's pl-string.c. The three points that are easy to get
// wrong, and were: sep and pad are character *sets*, not single chars;
// pad is stripped from BOTH ends of the input and of every field; and
// the result always has at least one element, so an all-padding input
// gives [""] rather than []. Empty fields are preserved - that is what
// the `skip_padding` flag is for, mirroring SWI's `no_skip_padding`
// label: after a separator that is not itself a pad character, and with
// another separator next, the padding skip is suppressed so the empty
// field between them survives.
//
// A nil argument is the empty text. Without that, C_STR() hands back the
// atom's name and "" (which reads as [] under any double_quotes setting)
// splits on '[' and ']'.

static bool bif_split_string_4(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,atom);
	GET_NEXT_ARG(p4,any);
	const char *src = is_nil(p1) ? "" : C_STR(q, p1);
	const size_t srclen = is_nil(p1) ? 0 : C_STRLEN(q, p1);
	const char *seps = is_nil(p2) ? "" : C_STR(q, p2);
	const char *pads = is_nil(p3) ? "" : C_STR(q, p3);

	CHECKED(init_tmp_heap(q));

	// The input, minus trailing padding. Tracked forwards so no part of
	// this needs to walk a UTF-8 sequence backwards.

	size_t end = 0;

	for (size_t off = 0; off < srclen; ) {
		const char *ptr = src + off;
		const int ch = get_char_utf8(&ptr);
		off = (size_t)(ptr - src);

		if (!char_in_set(pads, ch))
			end = off;
	}

	size_t i = 0;
	bool skip_padding = true;

	for (;;) {
		if (skip_padding) {
			while (i < end) {
				const char *ptr = src + i;

				if (!char_in_set(pads, peek_char_utf8(ptr)))
					break;

				get_char_utf8(&ptr);
				i = (size_t)(ptr - src);
			}

			if (i == end) {
				cell tmp;

				if (is_string(p1))
					make_atom(&tmp, g_nil_s);
				else
					make_atom(&tmp, g_empty_s);

				append_list(q, &tmp);
				break;
			}
		}

		skip_padding = true;

		// Scan to the next separator, remembering where the field ends
		// once its own trailing padding is discounted.

		size_t last = i, field_end = i;

		while (i < end) {
			const char *ptr = src + i;
			const int ch = peek_char_utf8(ptr);

			if (char_in_set(seps, ch))
				break;

			get_char_utf8(&ptr);
			i = (size_t)(ptr - src);

			if (!char_in_set(pads, ch))
				field_end = i;
		}

		const size_t sep_at = i;
		cell tmp;

		if (field_end > last) {
			CHECKED(make_slice(q, &tmp, p1, last, field_end-last));
		} else if (is_string(p1))
			make_atom(&tmp, g_nil_s);
		else
			make_atom(&tmp, g_empty_s);

		append_list(q, &tmp);

		if (sep_at == end)
			break;

		const char *ptr = src + sep_at;
		const int sep_ch = get_char_utf8(&ptr);
		i = (size_t)(ptr - src);

		if (!char_in_set(pads, sep_ch)
			&& (i < end) && char_in_set(seps, peek_char_utf8(src+i)))
			skip_padding = false;
	}

	cell *l = end_list(q);
	CHECKED(l);
	return unify(q, p4, p4_ctx, l, q->st.cur_ctx);
}

static bool bif_split_4(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,any);
	GET_NEXT_ARG(p4,any);

	if (is_nil(p1) || !C_STRLEN(q, p1)) {
		if (!unify(q, p3, p3_ctx, make_nil(), q->st.cur_ctx))
			return false;

		return unify(q, p4, p4_ctx, make_nil(), q->st.cur_ctx);
	}

	const char *start = C_STR(q, p1), *ptr;
	int ch = peek_char_utf8(C_STR(q, p2));

	if ((ptr = strchr_utf8(start, ch)) != NULL) {
		cell tmp;

		if (ptr != start)
			make_stringn(&tmp, start, ptr-start);
		else
			make_atom(&tmp, g_nil_s);

		if (!unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx)) {
			unshare_cell(&tmp);
			return false;
		}

		unshare_cell(&tmp);
		ptr = ptr+1;

		while (isspace((unsigned char)*ptr))
			ptr++;

		if (*ptr)
			make_stringn(&tmp, ptr, C_STRLEN(q, p1)-(ptr-start));
		else
			make_atom(&tmp, g_nil_s);

		bool ok = unify(q, p4, p4_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	}

	if (!unify(q, p3, p3_ctx, p1, p1_ctx))
		return false;

	return unify(q, p4, p4_ctx, make_nil(), q->st.cur_ctx);
}

static bool bif_sys_is_partial_string_1(query *q)
{
	GET_FIRST_ARG(p1,any);

	if (!is_iso_list(p1))
		return false;

	bool has_var, is_partial;
	scan_is_chars_list2(q, p1, p1_ctx, true, &has_var, &is_partial, NULL);
	return is_partial;
}

static bool bif_is_list_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	bool is_partial;
	return check_list(q, p1, p1_ctx, &is_partial, NULL);
}

static bool bif_is_partial_list_1(query *q)
{
	GET_FIRST_ARG(p1,any);

	if (is_var(p1))
		return true;

	bool is_partial;

	if (check_list(q, p1, p1_ctx, &is_partial, NULL))
		return false;

	return is_partial;
}

static bool bif_is_list_or_partial_list_1(query *q)
{
	GET_FIRST_ARG(p1,any);

	if (is_var(p1))
		return true;

	bool is_partial;

	if (check_list(q, p1, p1_ctx, &is_partial, NULL))
		return true;

	return is_partial;
}

static bool bif_is_bigint_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return is_bigint(p1);
}

static bool bif_load_text_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,list_or_nil);
	PROLOG_LIST_HANDLER(p2);
	const char *src = NULL;
	bool src_alloced = false;	// FIX: track owned buffer

	if (is_cstring(p1)) {
		src = C_STR(q, p1);
	} else if (scan_is_chars_list(q, p1, p1_ctx, true) > 0) {
		src = chars_list_to_string(q, p1, p1_ctx);
		CHECKED(src);
		src_alloced = true;	// FIX
	} else if (is_nil(p1)) {
		return false;
	} else
		return throw_error(q, p1, p1_ctx, "type_error", "chars");

	module *m = q->st.m;

	while (is_iso_list(p2)) {
		cell *h = PROLOG_LIST_HEAD(p2);
		cell *c = deref(q, h, p2_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (is_compound(c) && (get_arity(c) == 1)) {
			cell *name = c + 1;
			name = deref(q, name, c_ctx);

			if (!CMP_STRING_TO_CSTR(q, c, "module") && is_atom(name)) {
				const char *name_s = C_STR(q, name);
				m = find_module(q->pl, name_s);

				if (!m) {
					if (q->top->is_command)
						fprintf(stdout, "Info: created module '%s'\n", name_s);

					m = module_create(q->pl, name_s);
					CHECKED(m);
				}
			} else {
				if (src_alloced) TPL_free((void*)src);	// FIX: free owned buffer
				return throw_error(q, c, q->latest_ctx, "domain_error", "option");
			}
		} else {
			if (src_alloced) TPL_free((void*)src);	// FIX: free owned buffer
			return throw_error(q, c, q->latest_ctx, "domain_error", "option");
		}

		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;
	}

	load_text(m, src, q->st.m->filename);
	if (src_alloced) TPL_free((void*)src);	// FIX: free owned buffer
	return true;
}

static bool bif_must_be_4(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,callable);
	GET_NEXT_ARG(p4,any);

	const char *src = C_STR(q, p2);

	if (!strcmp(src, "var") && !is_var(p1))
		return throw_error2(q, p1, p1_ctx, "uninstantiation_error", "not_sufficiently_instantiated", p3);
	else if (!strcmp(src, "nonvar") && is_var(p1))
		return throw_error2(q, p1, p1_ctx, "instantiation_error", "instantiated", p3);

	if (strcmp(src, "ground") && strcmp(src, "acyclic") && is_var(p1))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

	if (!strcmp(src, "callable") && !is_callable(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "callable", p3);
	else if (!strcmp(src, "acyclic") && !is_acyclic_term(q, p1, p1_ctx))
		return throw_error(q, p1, p1_ctx, "domain_error", "acyclic_term");
	else if (!strcmp(src, "character") && !is_character(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "character", p3);
	else if (!strcmp(src, "chars")) {
		bool is_partial;

		if (!check_list(q, p1, p1_ctx, &is_partial, NULL) && !is_partial)
			return throw_error(q, p1, p1_ctx, "type_error", "list");

		if (has_vars(q, p1, p1_ctx))
			return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

		q->suspect = p1;

		if (!is_chars(q, p1, p1_ctx))
			return throw_error2(q, q->suspect, p1_ctx, "type_error", "character", p3);
	} else if (!strcmp(src, "boolean") && !is_boolean(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "boolean", p3);
	else if (!strcmp(src, "atom") && !is_atom(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "atom", p3);
	else if (!strcmp(src, "atomic") && !is_atomic(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "atomic", p3);
	else if (!strcmp(src, "not_less_than_zero") && !is_integer(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "integer", p3);
	else if (!strcmp(src, "not_less_than_zero") && is_negative(p1))
		return throw_error2(q, p1, p1_ctx, "domain_error", "not_less_than_zero", p3);
	else if (!strcmp(src, "integer") && !is_integer(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "integer", p3);
	else if (!strcmp(src, "float") && !is_float(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "float", p3);
	else if (!strcmp(src, "number") && !is_number(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "number", p3);
	else if (!strcmp(src, "ground")) {
		if (has_vars(q, p1, p1_ctx))
			return throw_error2(q, p1, p1_ctx, "instantiation_error", "ground", p3);
	} else if (!strcmp(src, "compound") && !is_structure(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "compound", p3);
	else if (is_compound(p2) && (get_arity(p2) == 1) && !strcmp(src, "list")) {
		cell *c = p2+1;
		c = deref(q, c, p2_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (!is_atom(c))
			return throw_error(q, c, c_ctx, "type_error", "atom");

		bool is_partial;

		if (!check_list(q, p1, p1_ctx, &is_partial, NULL))
			return throw_error(q, p1, p1_ctx, "type_error", "list");

		cell *l = p1;
		pl_ctx l_ctx = p1_ctx;
		PROLOG_LIST_HANDLER(l);

		while (is_iso_list(l)) {
			cell *h = PROLOG_LIST_HEAD(l);
			h = deref(q, h, l_ctx);
			pl_ctx h_ctx = q->latest_ctx;
			src = C_STR(q, c);

			if (!strcmp(src, "var" ) && !is_var(h))
				return throw_error(q, h, h_ctx, "type_error", "var");
			else if (!strcmp(src, "nonvar" ) && is_var(h))
				return throw_error(q, h, h_ctx, "type_error", "nonvar");
			else if (!strcmp(src, "character" ) && !is_character(h))
				return throw_error(q, h, h_ctx, "type_error", "character");
			else if (!strcmp(src, "boolean" ) && !is_boolean(h))
				return throw_error(q, h, h_ctx, "type_error", "boolean");
			else if (!strcmp(src, "integer" ) && !is_integer(h))
				return throw_error(q, h, h_ctx, "type_error", "integer");
			else if (!strcmp(src, "float" ) && !is_float(h))
				return throw_error(q, h, h_ctx, "type_error", "float");
			else if (!strcmp(src, "number" ) && !is_number(h))
				return throw_error(q, h, h_ctx, "type_error", "number");
			else if (!strcmp(src, "not_less_than_zero" ) && !is_number(h))
				return throw_error(q, h, h_ctx, "type_error", "number");
			else if (!strcmp(src, "not_less_than_zero" ) && is_negative(h))
				return throw_error(q, h, h_ctx, "domain_error", "not_less_than_zero");
			else if (!strcmp(src, "atom" ) && !is_atom(h))
				return throw_error(q, h, h_ctx, "type_error", "atom");
			else if (!strcmp(src, "atomic" ) && !is_atomic(h))
				return throw_error(q, h, h_ctx, "type_error", "atomic");
			else if (!strcmp(src, "ground" ) && has_vars(q, h, h_ctx))
				return throw_error(q, h, h_ctx, "instantiation_error", "ground");
			else if (!strcmp(src, "compound" ) && !is_structure(h))
				return throw_error(q, h, h_ctx, "type_error", "compound");

			l = PROLOG_LIST_TAIL(l);
			l = deref(q, l, l_ctx);
			l_ctx = q->latest_ctx;
		}
	} else if (!strcmp(src, "list")) {
		bool is_partial;

		if (!check_list(q, p1, p1_ctx, &is_partial, NULL)) {
			if (is_partial)
				return throw_error(q, p1, p1_ctx, "instantiation_error", "list");
			else
				return throw_error(q, p1, p1_ctx, "type_error", "list");
		}
	} else if (!strcmp(src, "list_or_partial_list")) {
		bool is_partial;

		if (!check_list(q, p1, p1_ctx, &is_partial, NULL) && !is_partial)
			return throw_error2(q, p1, p1_ctx, "type_error", "list", p3);
	} else if (!strcmp(src, "not_less_than_zero") && !is_integer(p1)) {
		return throw_error(q, p1, p2_ctx, "type_error", "number");
	} else if (!strcmp(src, "not_less_than_zero") && is_negative(p1)) {
		return throw_error(q, p1, p1_ctx, "domain_error", "not_less_than_zero");
	} else if (!strcmp(src, "pair")) {
		if (!is_compound(p1) || (get_arity(p1) != 2))
			return throw_error(q, p1, p1_ctx, "type_error", "pair");
		if (p1->val_off != g_minus_s)
			return throw_error(q, p1, p1_ctx, "type_error", "pair");
	}

	return true;
}

static bool do_must_be_2(query *q, cell *p2, pl_ctx p2_ctx, cell *p1, pl_ctx p1_ctx)
{
	const char *src = C_STR(q, p2);

	if (!strcmp(src, "var") && !is_var(p1))
		return throw_error(q, p1, p1_ctx, "uninstantiation_error", "not_sufficiently_instantiated");
	else if (!strcmp(src, "nonvar") && is_var(p1))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "instantiated");

	if (strcmp(src, "var") && strcmp(src, "ground") && strcmp(src, "acyclic") && is_var(p1))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

	if (!strcmp(src, "callable")) {
		if (!is_callable(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "callable");
	} else if (!strcmp(src, "acyclic")) {
		if (!is_acyclic_term(q, p1, p1_ctx))
			return throw_error(q, p1, p1_ctx, "domain_error", "acyclic_term");
	} else if (!strcmp(src, "character")) {
		if (!is_character(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "character");
	} else if (!strcmp(src, "chars")) {
		bool is_partial;

		if (!check_list(q, p1, p1_ctx, &is_partial, NULL) && !is_partial)
			return throw_error(q, p1, p1_ctx, "type_error", "list");

		if (has_vars(q, p1, p1_ctx))
			return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

		q->suspect = p1;

		if (!is_chars(q, p1, p1_ctx))
			return throw_error(q, q->suspect, p1_ctx, "type_error", "character");
	} else if (!strcmp(src, "boolean")) {
		if (!is_boolean(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "boolean");
	} else if (!strcmp(src, "atom")) {
		if (!is_atom(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "atom");
	} else if (!strcmp(src, "atomic")) {
		if (!is_atomic(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "atomic");
	} else if (!strcmp(src, "integer")) {
		if (!is_integer(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "integer");
	} else if (!strcmp(src, "float")) {
		if (!is_float(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "float");
	} else if (!strcmp(src, "number")) {
		if (!is_number(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "number");
	} else if (!strcmp(src, "not_less_than_zero")) {
		if (!is_number(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "number");
		if (is_negative(p1))
			return throw_error(q, p1, p1_ctx, "domain_error", "not_less_than_zero");
	} else if (!strcmp(src, "ground")) {
		if (has_vars(q, p1, p1_ctx))
			return throw_error(q, p1, p1_ctx, "instantiation_error", "ground");
	} else if (!strcmp(src, "compound")) {
		if (!is_structure(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "compound");
	} else if (is_compound(p2) && (get_arity(p2) == 1) && !strcmp(src, "list")) {
		cell *c = p2+1;
		c = deref(q, c, p2_ctx);
		pl_ctx c_ctx = q->latest_ctx;
		bool is_partial;

		if (!check_list(q, p1, p1_ctx, &is_partial, NULL))
			return throw_error(q, p1, p1_ctx, "type_error", "list");

		cell *l = p1;
		pl_ctx l_ctx = p1_ctx;
		PROLOG_LIST_HANDLER(l);

		while (is_iso_list(l)) {
			cell *h = PROLOG_LIST_HEAD(l);
			h = deref(q, h, l_ctx);
			pl_ctx h_ctx = q->latest_ctx;

			if (!do_must_be_2(q, c, c_ctx, h, h_ctx))
				return false;

			l = PROLOG_LIST_TAIL(l);
			l = deref(q, l, l_ctx);
			l_ctx = q->latest_ctx;
		}
	} else if (!strcmp(src, "list")) {
		bool is_partial;

		if (!check_list(q, p1, p1_ctx, &is_partial, NULL)) {
			if (is_partial)
				return throw_error(q, p1, p1_ctx, "instantiation_error", "list");
			else
				return throw_error(q, p1, p1_ctx, "type_error", "list");
		}
	} else if (!strcmp(src, "list_or_partial_list")) {
		bool is_partial;

		if (!check_list(q, p1, p1_ctx, &is_partial, NULL) && !is_partial)
			return throw_error(q, p1, p1_ctx, "type_error", "list");
	} else if (!strcmp(src, "not_less_than_zero") && !is_integer(p1)) {
		return throw_error(q, p1, p2_ctx, "type_error", "number");
	} else if (!strcmp(src, "not_less_than_zero") && is_negative(p1)) {
		return throw_error(q, p1, p1_ctx, "domain_error", "not_less_than_zero");
	} else if (!strcmp(src, "pair")) {
		if (!is_compound(p1) || (get_arity(p1) != 2))
			return throw_error(q, p1, p1_ctx, "type_error", "pair");
		if (p1->val_off != g_minus_s)
			return throw_error(q, p1, p1_ctx, "type_error", "pair");
	}

	return true;
}

static bool bif_must_be_2(query *q)
{
	GET_FIRST_ARG(p2,callable);
	GET_NEXT_ARG(p1,any);
	return do_must_be_2(q, p2, p2_ctx, p1, p1_ctx);
}

static bool bif_can_be_4(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,callable);
	GET_NEXT_ARG(p4,any);

	if (is_var(p1))
		return true;

	const char *src = C_STR(q, p2);

	if (!strcmp(src, "callable") && !is_callable(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "callable", p3);
	else if (!strcmp(src, "character") && !is_character(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "character", p3);
	else if (!strcmp(src, "boolean") && !is_boolean(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "boolean", p3);
	else if (!strcmp(src, "atom") && !is_atom(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "atom", p3);
	else if (!strcmp(src, "atomic") && !is_atomic(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "atomic", p3);
	else if (!strcmp(src, "integer") && !is_integer(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "integer", p3);
	else if (!strcmp(src, "float") && !is_float(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "float", p3);
	else if (!strcmp(src, "number") && !is_number(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "number", p3);
	else if (!strcmp(src, "compound") && !is_structure(p1))
		return throw_error2(q, p1, p1_ctx, "type_error", "compound", p3);
	else if (!strcmp(src, "term") && is_cyclic_term(q, p1, p1_ctx))
		return throw_error(q, p1, p1_ctx, "type_error", "term");
	else if (!strcmp(src, "list")) {
		bool is_partial;

		if (!check_list(q, p1, p1_ctx, &is_partial, NULL))
			return throw_error2(q, p1, p1_ctx, "type_error", "list", p3);
	} else if (!strcmp(src, "list_or_partial_list")) {
		bool is_partial;

		if (!check_list(q, p1, p1_ctx, &is_partial, NULL) && !is_partial)
			return throw_error2(q, p1, p1_ctx, "type_error", "list", p3);
	} else if (!strcmp(src, "not_less_than_zero") && !is_number(p1)) {
		return throw_error(q, p1, p2_ctx, "type_error", "integer");
	} else if (!strcmp(src, "not_less_than_zero") && is_negative(p1)) {
		return throw_error(q, p1, p1_ctx, "domain_error", "not_less_than_zero");
	} else if (!strcmp(src, "pair")) {
		if (!is_compound(p1) || (get_arity(p1) != 2))
			return throw_error(q, p1, p1_ctx, "type_error", "pair");
		if (p1->val_off != g_minus_s)
			return throw_error(q, p1, p1_ctx, "type_error", "pair");
	}

	return true;
}

static bool bif_can_be_2(query *q)
{
	GET_FIRST_ARG(p2,atom);
	GET_NEXT_ARG(p1,any);

	if (is_var(p1))
		return true;

	const char *src = C_STR(q, p2);

	if (!strcmp(src, "callable") && !is_callable(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "callable");
	else if (!strcmp(src, "character") && !is_character(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "character");
	else if (!strcmp(src, "boolean") && !is_boolean(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "boolean");
	else if (!strcmp(src, "atom") && !is_atom(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "atom");
	else if (!strcmp(src, "atomic") && !is_atomic(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "atomic");
	else if (!strcmp(src, "integer") && !is_integer(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "integer");
	else if (!strcmp(src, "float") && !is_float(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "float");
	else if (!strcmp(src, "number") && !is_number(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "number");
	else if (!strcmp(src, "compound") && !is_structure(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "compound");
	else if (!strcmp(src, "term") && is_cyclic_term(q, p1, p1_ctx))
		return throw_error(q, p1, p1_ctx, "type_error", "term");
	else if (!strcmp(src, "term") && is_cyclic_term(q, p1, p1_ctx))
		return throw_error(q, p1, p1_ctx, "type_error", "term");
	else if (!strcmp(src, "list")) {
		bool is_partial;

		if (!check_list(q, p1, p1_ctx, &is_partial, NULL))
			return throw_error(q, p1, p1_ctx, "type_error", "list");
	} else if (!strcmp(src, "list_or_partial_list")) {
		bool is_partial;

		if (!check_list(q, p1, p1_ctx, &is_partial, NULL) && !is_partial)
			return throw_error(q, p1, p1_ctx, "type_error", "list");
	} else if (!strcmp(src, "not_less_than_zero") && !is_number(p1)) {
		return throw_error(q, p1, p2_ctx, "type_error", "integer");
	} else if (!strcmp(src, "not_less_than_zero") && is_negative(p1)) {
		return throw_error(q, p1, p2_ctx, "domain_error", "not_less_than_zero");
	} else if (!strcmp(src, "pair")) {
		if (!is_compound(p1) || (get_arity(p1) != 2))
			return throw_error(q, p1, p1_ctx, "type_error", "pair");
		if (p1->val_off != g_minus_s)
			return throw_error(q, p1, p1_ctx, "type_error", "pair");
	}

	return true;
}

static bool bif_sys_skip_max_list_4(query *q)
{
	GET_FIRST_ARG(p1,integer_or_var);
	GET_NEXT_ARG(p2,integer_or_var);
	GET_NEXT_ARG(p3,any);
	GET_NEXT_ARG(p4,any);

	if (is_integer(p2) && is_negative(p2))
		return throw_error(q, p2, p2_ctx, "domain_error", "not_less_than_zero");

	if (is_atomic(p3) && !is_string(p3)) {
		cell tmp;
		make_int(&tmp, 0);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx)
			&& unify(q, p3, p3_ctx, p4, p4_ctx);
	}

	pl_int skip=0, max = is_smallint(p2) ? get_smallint(p2) : PL_INT_MAX;
	pl_ctx c_ctx = p3_ctx;
	cell tmp = {0};
	cell *c = skip_max_list(q, p3, &c_ctx, max, &skip, &tmp);

	if (!c) {
		c_ctx = p3_ctx;
		c = p3;
	}

	bool ok = unify(q, p4, p4_ctx, c, c_ctx);

	if (ok != true)
		return ok;


	unshare_cell(&tmp);

	if (!is_iso_list_or_nil(c) && !(is_cstring(c) && !strcmp(C_STR(q,c), "[]")) && !is_var(c)) {
		make_int(&tmp, -1);
		unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	make_int(&tmp, skip);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

// FIXME: not truly crypto strength

static bool bif_crypto_n_random_bytes_2(query *q)
{
	static bool s_seed = false;

	if (!s_seed) {
		srand(time(NULL));
		s_seed = true;
	}

	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,list_or_var);
	int n = get_smallint(p1);

	if (n < 1)
		return throw_error(q, p1, p1_ctx, "domain_error", "not_less_than_zero");

	CHECKED(init_tmp_heap(q));

	while (n--) {
		int i = rand() % 256;
		cell tmp;
		make_int(&tmp, i);
		append_list(q, &tmp);
	}

	cell *l = end_list(q);
	CHECKED(l);
	return unify(q, p2, p2_ctx, l, q->st.cur_ctx);
}

#if USE_OPENSSL
static bool bif_crypto_data_hash_3(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom_or_var);
	GET_NEXT_ARG(p3,list_or_nil);
	enum {is_sha256, is_sha384, is_sha512} algo = is_sha256;
	char *key = NULL;
	int keylen = 0;
	PROLOG_LIST_HANDLER(p3);

	while (is_list(p3)) {
		cell *h = PROLOG_LIST_HEAD(p3);
		h = deref(q, h, p3_ctx);
		pl_ctx h_ctx = q->latest_ctx;

		if (is_compound(h) && (get_arity(h) == 1)) {
			cell *arg = h+1;
			arg = deref(q, arg, h_ctx);
			pl_ctx arg_ctx = q->latest_ctx;

			if (!CMP_STRING_TO_CSTR(q, h, "algorithm")) {
				if (is_var(arg)) {
					cell tmp;
					make_atom(&tmp, new_atom(q->pl, "sha256"));
					unify(q, arg, arg_ctx, &tmp, q->st.cur_ctx);
					algo = is_sha256;
				} else if (!CMP_STRING_TO_CSTR(q, arg, "sha256")) {
					algo = is_sha256;
				} else if (!CMP_STRING_TO_CSTR(q, arg, "sha384")) {
					algo = is_sha384;
				} else if (!CMP_STRING_TO_CSTR(q, arg, "sha512")) {
					algo = is_sha512;
				} else
					return throw_error(q, arg, arg_ctx, "domain_error", "algorithm");
			} else if (!CMP_STRING_TO_CSTR(q, h, "hmac") && is_iso_list(arg)
				&& (scan_is_chars_list(q, arg, 0, true)) > 0) {
				key = chars_list_to_string(q, arg, 0);
			} else {
				TPL_free(key);		// may already own a buffer from an earlier option
				return throw_error(q, h, h_ctx, "domain_error", "hash_option");
			}
		} else {
			TPL_free(key);
			return throw_error(q, h, h_ctx, "domain_error", "hash_option");
		}

		p3 = PROLOG_LIST_TAIL(p3);
		p3 = deref(q, p3, p3_ctx);
		p3_ctx = q->latest_ctx;
	}

	char tmpbuf[512];
	char *dst = tmpbuf;
	*dst = '\0';
	size_t buflen = sizeof(tmpbuf);

	if (key && (algo == is_sha256)) {
		unsigned char digest[SHA256_DIGEST_LENGTH];
		unsigned digest_len = 0;
		HMAC(EVP_sha256(), key, keylen, (unsigned char*)C_STR(q, p1), C_STRLEN(q, p1), digest, &digest_len);

		for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
			size_t len = snprintf(dst, buflen, "%02x", digest[i]);
			dst += len;
			buflen -= len;
		}
	} else if (key && (algo == is_sha384)) {
		unsigned char digest[SHA384_DIGEST_LENGTH];
		unsigned digest_len = 0;
		HMAC(EVP_sha384(), key, keylen, (unsigned char*)C_STR(q, p1), C_STRLEN(q, p1), digest, &digest_len);

		for (int i = 0; i < SHA384_DIGEST_LENGTH; i++) {
			size_t len = snprintf(dst, buflen, "%02x", digest[i]);
			dst += len;
			buflen -= len;
		}
	} else if (key && (algo == is_sha512)) {
		unsigned char digest[SHA512_DIGEST_LENGTH];
		unsigned digest_len = 0;
		HMAC(EVP_sha512(), key, keylen, (unsigned char*)C_STR(q, p1), C_STRLEN(q, p1), digest, &digest_len);

		for (int i = 0; i < SHA512_DIGEST_LENGTH; i++) {
			size_t len = snprintf(dst, buflen, "%02x", digest[i]);
			dst += len;
			buflen -= len;
		}
	} else if (algo == is_sha256) {
		unsigned char digest[SHA256_DIGEST_LENGTH];
		SHA256((unsigned char*)C_STR(q, p1), C_STRLEN(q, p1), digest);

		for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
			size_t len = snprintf(dst, buflen, "%02x", digest[i]);
			dst += len;
			buflen -= len;
		}
	} else if (algo == is_sha384) {
		unsigned char digest[SHA384_DIGEST_LENGTH];
		SHA384((unsigned char*)C_STR(q, p1), C_STRLEN(q, p1), digest);

		for (int i = 0; i < SHA384_DIGEST_LENGTH; i++) {
			size_t len = snprintf(dst, buflen, "%02x", digest[i]);
			dst += len;
			buflen -= len;
		}
	} else if (algo == is_sha512) {
		unsigned char digest[SHA512_DIGEST_LENGTH];
		SHA512((unsigned char*)C_STR(q, p1), C_STRLEN(q, p1), digest);

		for (int i = 0; i < SHA512_DIGEST_LENGTH; i++) {
			size_t len = snprintf(dst, buflen, "%02x", digest[i]);
			dst += len;
			buflen -= len;
		}
	}

	if (key)
		TPL_free(key);

	cell tmp;
	make_string(&tmp, tmpbuf);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}
#endif

static int do_b64encode_2(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx)
{
	const char *str = C_STR(q, p1);
	size_t len = C_STRLEN(q, p1);
	char *dstbuf = TPL_malloc((len*3)+1);	// BASE64 can increase length x3
	CHECKED(dstbuf);
	tpl_b64_encode(str, len, &dstbuf, 0, 0);
	cell tmp;
	make_string(&tmp, dstbuf);
	TPL_free(dstbuf);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static int do_b64decode_2(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx)
{
	const char *str = C_STR(q, p2);
	size_t len = C_STRLEN(q, p2);
	char *dstbuf = TPL_malloc(len+1);
	CHECKED(dstbuf);
	tpl_b64_decode(str, len, &dstbuf);
	cell tmp;
	make_string(&tmp, dstbuf);
	TPL_free(dstbuf);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_base64_3(query *q)
{
	GET_FIRST_ARG(p1,list_or_atom_or_var);
	GET_NEXT_ARG(p2,list_or_atom_or_var);

	if (is_atom(p1))
		return do_b64encode_2(q, p1, p1_ctx, p2, p2_ctx);
	else if (is_list(p2)) {
		PROLOG_LIST_HANDLER(p2);
		SB(pr);

		while (is_list(p2)) {
			cell *h = PROLOG_LIST_HEAD(p2);
			h = deref(q, h, p2_ctx);
			SB_sprintf(pr, "%s", C_STR(q, h));
			p2 = PROLOG_LIST_TAIL(p2);
			p2 = deref(q, p2, p2_ctx);
			p2_ctx = q->latest_ctx;
		}

		cell tmp;
		make_string(&tmp, SB_cstr(pr));
		SB_free(pr);
		bool ok = do_b64decode_2(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	} else if (is_atom(p2))
		return do_b64decode_2(q, p1, p1_ctx, p2, p2_ctx);

	return throw_error(q, p1, p1_ctx, "instantiation_error", "atom");
}

char *url_encode(const char *src, int len, char *dstbuf, size_t dstlen)
{
	char *dst = dstbuf;

	// As per RFC3986 (2005)

	while (len-- > 0) {
		if (*src == ' ') {
			*dst++ = '+';
			src++;
		} else if (!isalnum((unsigned char)*src) && (*src != '-') && (*src != '_') && (*src != '.') && (*src != '~')) {
			const unsigned char* src2 = (unsigned char*)src;
			dst += snprintf(dst, dstlen, "%%%02X", *src2);
			src++;
		} else
			*dst++ = *src++;
	}

	*dst = '\0';
	return dstbuf;
}

char *url_decode(const char *src, char *dstbuf)
{
	char *dst = dstbuf;

	while (*src) {
		if (*src == '%') {
			src++;
			unsigned ch = 0;
			sscanf(src, "%02X", &ch);
			src += 2;
			*dst++ = (unsigned char)ch;
		} else if (*src == '+') {
			*dst++ = ' ';
			src++;
		} else
			*dst++ = *src++;
	}

	*dst = '\0';
	return dstbuf;
}

static bool do_urlencode_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,var);
	const char *str = C_STR(q, p1);
	size_t len = C_STRLEN(q, p1);
	char *dstbuf = TPL_malloc((len*3)+1);	// URL's can increase length x3
	CHECKED(dstbuf);
	url_encode(str, len, dstbuf, (len*3)+1);
	cell tmp;

	if (is_string(p1))
		make_string(&tmp, dstbuf);
	else
		make_cstring(&tmp, dstbuf);

	TPL_free(dstbuf);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool do_urldecode_2(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,atom);
	const char *str = C_STR(q, p2);
	size_t len = C_STRLEN(q, p2);
	char *dstbuf = TPL_malloc(len+1);
	CHECKED(dstbuf);
	url_decode(str, dstbuf);
	cell tmp;

	if (is_string(p1))
		make_string(&tmp, dstbuf);
	else
		make_cstring(&tmp, dstbuf);

	TPL_free(dstbuf);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_urlenc_3(query *q)
{
	GET_FIRST_ARG(p1,atom_or_var);
	GET_NEXT_ARG(p2,atom_or_var);

	if ((is_atom(p1) || is_string(p1)) && is_var(p2))
		return do_urlencode_2(q);
	else if (is_var(p1) && (is_atom(p2) || is_string(p2)))
		return do_urldecode_2(q);

	return throw_error(q, p1, p1_ctx, "instantiation_error", "atom");
}

static bool bif_atom_lower_2(query *q)
{
	GET_FIRST_ARG(p1,iso_atom);
	GET_NEXT_ARG(p2,iso_atom_or_var);
	const char *src = C_STR(q, p1);
	size_t len = substrlen_utf8(src, C_STRLEN(q, p1));
	char *tmps = TPL_malloc((len*MAX_BYTES_PER_CODEPOINT)+1);
	CHECKED(tmps);
	char *dst = tmps;

	while (len--) {
		int ch = get_char_utf8(&src);
		ch = towlower(ch);
		dst += put_char_utf8(dst, ch);
	}

	*dst = '\0';
	cell tmp;
	make_cstringn(&tmp, tmps, C_STRLEN(q, p1));
	TPL_free(tmps);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_atom_upper_2(query *q)
{
	GET_FIRST_ARG(p1,iso_atom);
	GET_NEXT_ARG(p2,iso_atom_or_var);
	const char *src = C_STR(q, p1);
	size_t len = substrlen_utf8(src, C_STRLEN(q, p1));
	char *tmps = TPL_malloc((len*MAX_BYTES_PER_CODEPOINT)+1);
	CHECKED(tmps);
	char *dst = tmps;

	while (len--) {
		int ch = get_char_utf8(&src);
		ch = towupper(ch);
		dst += put_char_utf8(dst, ch);
	}

	*dst = '\0';
	cell tmp;
	make_cstringn(&tmp, tmps, C_STRLEN(q, p1));
	TPL_free(tmps);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_string_lower_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom_or_var);
	const char *src = C_STR(q, p1);
	size_t len = substrlen_utf8(src, C_STRLEN(q, p1));
	char *tmps = TPL_malloc((len*MAX_BYTES_PER_CODEPOINT)+1);
	CHECKED(tmps);
	char *dst = tmps;

	while (len--) {
		int ch = get_char_utf8(&src);
		ch = towlower(ch);
		dst += put_char_utf8(dst, ch);
	}

	*dst = '\0';
	cell tmp;
	make_stringn(&tmp, tmps, C_STRLEN(q, p1));
	TPL_free(tmps);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_string_upper_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom_or_var);
	const char *src = C_STR(q, p1);
	size_t len = substrlen_utf8(src, C_STRLEN(q, p1));
	char *tmps = TPL_malloc((len*MAX_BYTES_PER_CODEPOINT)+1);
	CHECKED(tmps);
	char *dst = tmps;

	while (len--) {
		int ch = get_char_utf8(&src);
		ch = towupper(ch);
		dst += put_char_utf8(dst, ch);
	}

	*dst = '\0';
	cell tmp;
	make_stringn(&tmp, tmps, C_STRLEN(q, p1));
	TPL_free(tmps);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

#define FNV_OFFSET_64 0xCBF29CE484222325ULL
#define FNV_PRIME_64  0x100000001B3ULL

__attribute__((no_sanitize("unsigned-integer-overflow")))
static uint64_t fnv1a_64(const void* data, size_t size)
{
	const unsigned char* bytes = (const unsigned char*)data;
	unsigned long long hash = FNV_OFFSET_64;

	for (size_t i = 0; i < size; i++) {
		hash ^= bytes[i];
		hash *= FNV_PRIME_64;
	}

	return hash;
}

static bool bif_term_hash_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,integer_or_var);

	if (is_bigint(p2))
		return throw_error(q, p2, p2_ctx, "domain_error", "small_integer_range");

	if (is_var(p1))
		return true;

	cell tmp;

	if (is_smallint(p1)) {
		char tmpbuf[256];
		snprintf(tmpbuf, sizeof(tmpbuf), "%"PRId64"", (int64_t)get_smallint(p1));
		make_int(&tmp, fnv1a_64(tmpbuf, strlen(tmpbuf)));
	} else if (is_atom(p1)) {
		make_int(&tmp, fnv1a_64(C_STR(q, p1), C_STRLEN(q, p1)));
	} else {
		char *tmpbuf = print_term_to_strbuf(q, p1, p1_ctx, 1);
		make_int(&tmp, fnv1a_64(tmpbuf, strlen(tmpbuf)));
		TPL_free(tmpbuf);
	}

	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_hex_chars_2(query *q)
{
	GET_FIRST_ARG(p1,integer_or_var);
	GET_NEXT_ARG(p2,atom_or_var);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");
	if (is_var(p1) && is_var(p2))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "atom");

	if (is_var(p2)) {
		char tmpbuf[256];
		char *dst = tmpbuf;

		if (is_bigint(p1)) {
			size_t len = mp_int_string_len(&p1->val_bigint->ival, 16) -1;
			dst = TPL_malloc(len+10);
			CHECKED(dst);
			mp_int_to_string(&p1->val_bigint->ival, 16, dst, len+1);
		} else {
			snprintf(tmpbuf, sizeof(tmpbuf), "%"PRIx64"", (uint64_t)get_smallint(p1));
		}

		cell tmp;
		make_string(&tmp, dst);
		if (dst != tmpbuf) TPL_free(dst);
		bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	}

	char *src = DUP_STRING(q, p2);
	mpz_t v2;
	mp_int_init(&v2);
	mp_int_read_cstring(&v2, 16, (char*)src, NULL);
	TPL_free(src);
	mp_small val;
	cell tmp = {0};

	if (mp_int_to_int(&v2, &val) == MP_RANGE) {
		tmp.tag = TAG_INT;
		tmp.val_bigint = TPL_malloc(sizeof(bigint));
		CHECKED(tmp.val_bigint);
		tmp.val_bigint->refcnt = 1;
		mp_int_init_copy(&tmp.val_bigint->ival, &v2);
		tmp.flags |= FLAG_INT_BIG | FLAG_MANAGED | FLAG_INT_BIG;
	} else {
		make_uint(&tmp, (unsigned long long)val);
	}

	mp_int_clear(&v2);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_octal_chars_2(query *q)
{
	GET_FIRST_ARG(p1,integer_or_var);
	GET_NEXT_ARG(p2,atom_or_var);

	if (is_var(p1) && is_var(p2))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "atom");

	if (is_var(p2)) {
		char tmpbuf[256];
		char *dst = tmpbuf;

		if (is_bigint(p1)) {
			size_t len = mp_int_string_len(&p1->val_bigint->ival, 8) -1;
			dst = TPL_malloc(len+10);
			CHECKED(dst);
			mp_int_to_string(&p1->val_bigint->ival, 8, dst, len+1);
		} else {
			snprintf(tmpbuf, sizeof(tmpbuf), "%"PRIo64"", (uint64_t)get_smallint(p1));
		}

		cell tmp;
		make_string(&tmp, dst);
		if (dst != tmpbuf) TPL_free(dst);
		bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	}

	char *src = DUP_STRING(q, p2);
	mpz_t v2;
	mp_int_init(&v2);
	mp_int_read_cstring(&v2, 8, (char*)src, NULL);
	TPL_free(src);
	mp_small val;
	cell tmp = {0};

	if (mp_int_to_int(&v2, &val) == MP_RANGE) {
		tmp.tag = TAG_INT;
		tmp.val_bigint = TPL_malloc(sizeof(bigint));
		CHECKED(tmp.val_bigint);
		tmp.val_bigint->refcnt = 1;
		mp_int_init_copy(&tmp.val_bigint->ival, &v2);
		tmp.flags |= FLAG_INT_BIG | FLAG_MANAGED;
	} else {
		make_uint(&tmp, (unsigned long long)val);
	}

	mp_int_clear(&v2);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_string_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return is_string(p1);
}

static bool bif_uuid_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	uuid u;
	uuid_gen(q->pl, &u);
	char tmpbuf[128];
	uuid_to_buf(&u, tmpbuf, sizeof(tmpbuf));
	cell tmp;
	make_string(&tmp, tmpbuf);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_atomic_concat_3(query *q)
{
	GET_FIRST_ARG(p1,atomic);
	GET_NEXT_ARG(p2,atomic);
	GET_NEXT_ARG(p3,atom_or_var);
	char *src1 = print_term_to_strbuf(q, p1, p1_ctx, 1);
	char *src2 = print_term_to_strbuf(q, p2, p2_ctx, 1);
	SB(pr);
	SB_strcat(pr, src1);
	SB_strcat(pr, src2);
	TPL_free(src1);
	TPL_free(src2);
	cell tmp;
	make_cstringn(&tmp, SB_cstr(pr), SB_strlen(pr));
	SB_free(pr);
	bool ok = unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

// Split mode: Atom is bound and List isn't, so break Atom apart on
// each occurrence of Sep. Pieces are always atoms, and an empty piece
// is '' - so 'a--b' split on '-' gives [a,'',b].

static bool do_atomic_list_concat_split(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, cell *p3, pl_ctx p3_ctx)
{
	char *sep_alloc = NULL;
	const char *sep, *src;
	size_t sep_len, src_len;

	if (is_atom(p2)) {
		sep = C_STR(q, p2);
		sep_len = C_STRLEN(q, p2);
	} else {
		sep = sep_alloc = print_term_to_strbuf(q, p2, p2_ctx, 1);
		CHECKED(sep_alloc);
		sep_len = strlen(sep);
	}

	if (!sep_len) {
		TPL_free(sep_alloc);
		return throw_error(q, p2, p2_ctx, "domain_error", "non_empty_atom");
	}

	src = C_STR(q, p3);
	src_len = C_STRLEN(q, p3);
	CHECKED(init_tmp_heap(q), TPL_free(sep_alloc));
	const char *start = src, *end = src + src_len;

	while (true) {
		const char *ptr = NULL;

		for (const char *s = start; (size_t)(end - s) >= sep_len; s++) {
			if (!memcmp(s, sep, sep_len)) {
				ptr = s;
				break;
			}
		}

		cell tmp;
		make_cstringn(&tmp, start, (ptr ? ptr : end) - start);
		append_list(q, &tmp);

		if (!ptr)
			break;

		start = ptr + sep_len;
	}

	TPL_free(sep_alloc);
	cell *l = end_list(q);
	CHECKED(l);

	// end_list() took its own reference to every piece, so the ones
	// make_cstringn() gave us are now redundant and must be released
	// or the strings outlive the query...

	cell *c = l;

	while (is_iso_list(c)) {
		cell *h = c + 1;
		c = h + h->num_cells;
		unshare_cell(h);
	}

	return unify(q, p1, p1_ctx, l, q->st.cur_ctx);
}

// Join mode: the list is proper, so run the elements together with Sep
// between them. A var element means we can't join; whether that's an
// error or a cue to split instead is the caller's call - atomic_list_-
// concat/2 has no separator to split on, so for it it's always an error.

static bool do_atomic_list_concat_join(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, cell *p3, pl_ctx p3_ctx, bool can_split)
{
	cell *p1_orig = p1;
	pl_ctx p1_orig_ctx = p1_ctx;
	PROLOG_LIST_HANDLER(p1);
	SB(pr);

	while (is_list(p1)) {
		cell *h = PROLOG_LIST_HEAD(p1);
		h = deref(q, h, p1_ctx);

		if (is_var(h)) {
			SB_free(pr);

			if (!can_split || is_var(p3))
				return throw_error(q, h, q->latest_ctx, "instantiation_error", "atomic");

			return do_atomic_list_concat_split(q, p1_orig, p1_orig_ctx, p2, p2_ctx, p3, p3_ctx);
		}

		if (!is_atomic(h)) {
			SB_free(pr);
			return throw_error(q, h, q->latest_ctx, "type_error", "atomic");
		}

		q->parens = true;
		char *dst = print_term_to_strbuf(q, h, q->latest_ctx, 1);
		q->parens = false;
		SB_strcat(pr, dst);
		TPL_free(dst);

		p1 = PROLOG_LIST_TAIL(p1);
		p1 = deref(q, p1, p1_ctx);
		p1_ctx = q->latest_ctx;

		if (is_list(p1)) {
			q->parens = true;
			dst = print_term_to_strbuf(q, p2, p2_ctx, 1);
			q->parens = false;
			SB_strcat(pr, dst);
			TPL_free(dst);
		}
	}

	cell tmp;
	make_cstring(&tmp, SB_cstr(pr));
	SB_free(pr);
	bool ok = unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_atomic_list_concat_2(query *q)
{
	GET_FIRST_ARG(p1,iso_list_or_nil_or_var);
	GET_NEXT_ARG(p2,atom_or_var);
	bool is_partial;

	// There's no separator here, so nothing to split on: a list that
	// isn't fully there is an error, not a request to split...

	if (!check_list(q, p1, p1_ctx, &is_partial, NULL)) {
		if (is_partial)
			return throw_error(q, p1, p1_ctx, "instantiation_error", "not_sufficiently_instantiated");

		return throw_error(q, p1, p1_ctx, "type_error", "list");
	}

	cell tmp;
	make_atom(&tmp, g_empty_s);
	return do_atomic_list_concat_join(q, p1, p1_ctx, &tmp, q->st.cur_ctx, p2, p2_ctx, false);
}

static bool bif_atomic_list_concat_3(query *q)
{
	GET_FIRST_ARG(p1,iso_list_or_nil_or_var);
	GET_NEXT_ARG(p2,atomic);
	GET_NEXT_ARG(p3,atom_or_var);
	bool is_partial;

	if (!check_list(q, p1, p1_ctx, &is_partial, NULL)) {
		if (!is_partial)
			return throw_error(q, p1, p1_ctx, "type_error", "list");

		if (is_var(p3))
			return throw_error(q, p3, p3_ctx, "instantiation_error", "not_sufficiently_instantiated");

		return do_atomic_list_concat_split(q, p1, p1_ctx, p2, p2_ctx, p3, p3_ctx);
	}

	return do_atomic_list_concat_join(q, p1, p1_ctx, p2, p2_ctx, p3, p3_ctx, true);
}

static bool bif_replace_4(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,atom);
	GET_NEXT_ARG(p4,var);
	size_t srclen = C_STRLEN(q, p1);
	const char *src = C_STR(q, p1);
	const char *s1 = C_STR(q, p2);
	const char *s2 = C_STR(q, p3);
	size_t s1len = C_STRLEN(q, p2);
	size_t s2len = C_STRLEN(q, p3);
	SB(pr);

	while (srclen > 0) {
		if (!strncmp(src, s1, s1len)) {
			SB_strcatn(pr, s2, s2len);
			src += s1len;
			srclen -= s1len;
		} else {
			SB_strcatn(pr, src, 1);
			src++;
			srclen--;
		}
	}

	cell tmp;

	if (SB_strlen(pr))
		make_stringn(&tmp, SB_cstr(pr), SB_strlen(pr));
	else
		make_atom(&tmp, g_nil_s);

	SB_free(pr);
	bool ok = unify(q, p4, p4_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static void load_properties(module *m);

static bool bif_sys_load_properties_0(query *q)
{
	load_properties(q->st.m);
	return true;
}

static void load_flags(query *q);

static bool bif_sys_load_flags_0(query *q)
{
	load_flags(q);
	return true;
}

static void load_ops(query *q);

static bool bif_sys_load_ops_0(query *q)
{
	load_ops(q);
	return true;
}

static bool bif_sys_ops_dirty_0(query *q)
{
	bool ok = q->ops_dirty;
	q->ops_dirty = false;
	return ok;
}

static void do_template(char *tmpbuf, const char *name, unsigned arity, const char *help, bool function, bool quote)
{
	SB(t);

	if (quote) {
		SB_sprintf(t, "template('%s'", name);
	} else {
		SB_sprintf(t, "template(%s", name);
	}

	if (arity)
		SB_strcat(t, "(");

	char tmpbuf1[256], tmpbuf2[256], tmpbuf3[256];
	const char *src = help + (function?1:0);

	for (unsigned i = 0; i < arity; i++) {
		sscanf(src, "%255[^,],%s255", tmpbuf1, tmpbuf2);
		tmpbuf1[sizeof(tmpbuf1)-1] = tmpbuf2[sizeof(tmpbuf2)-1] = 0;

		if (i > 0)
			SB_strcat(t, ",");

		SB_strcat(t, tmpbuf1);
		memcpy(tmpbuf3, tmpbuf2, sizeof(tmpbuf3));
		src = tmpbuf3 + (function?1:0);
	}

	if (arity)
		SB_strcat(t, ")");

	if (function) {
		SB_sprintf(t, ",%s", src);
	}

	strcpy(tmpbuf, SB_cstr(t));
	SB_free(t);
}

static bool bif_sys_predicate_property_2(query *q)
{
	GET_FIRST_ARG(p1,callable);
	GET_NEXT_ARG(p2,atom_or_var);
	cell tmp;
	bool found = false, evaluable = false;

	if ((p1->val_off == g_colon_s) && (get_arity(p1) == 2)) {
		cell *cm = p1 + 1;
		cm = deref(q, cm, p1_ctx);

		if (!is_atom(cm) && !is_var(cm))
			return throw_error(q, cm, p1_ctx, "type_error", "callable");

		if (!is_var(cm)) {
			module *m = find_module(q->pl, C_STR(q, cm));
			if (m) q->st.m = m;
		}

		p1 += 2;
		p1 = deref(q, p1, p1_ctx);
		p1_ctx = q->latest_ctx;

		if (!is_callable(p1))
			return throw_error(q, p1, p1_ctx, "type_error", "callable");
	}

	if (get_builtin_term(q->st.m, p1, &found, &evaluable), found) {
		if (evaluable)
			return false;

		make_atom(&tmp, new_atom(q->pl, "built_in"));

		if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
			return true;

		make_atom(&tmp, new_atom(q->pl, "static"));

		if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
			return true;

		make_atom(&tmp, new_atom(q->pl, "dynamic"));

		if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
			return false;

		return throw_error(q, p2, p2_ctx, "domain_error", "predicate_property");
	}

	predicate *pr = find_predicate(q->st.m, p1);

	if (!pr)
		return false;

	if (pr->is_builtin) {
		make_atom(&tmp, new_atom(q->pl, "built_in"));

		if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
			return true;
	}

	if (!pr->is_dynamic) {
		make_atom(&tmp, new_atom(q->pl, "static"));

		if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
			return true;
	}

	if (pr->is_dynamic) {
		make_atom(&tmp, new_atom(q->pl, "dynamic"));

		if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
			return true;
	}

	if (pr->is_multifile) {
		make_atom(&tmp, new_atom(q->pl, "multifile"));

		if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
			return true;
	}

	if (pr->is_public) {
		make_atom(&tmp, new_atom(q->pl, "public"));

		if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
			return true;
	}

	if (pr->is_public) {
		make_atom(&tmp, new_atom(q->pl, "exported"));

		if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
			return true;
	}

	make_atom(&tmp, new_atom(q->pl, "meta_predicate"));

	if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
		return true;

	make_atom(&tmp, new_atom(q->pl, "visible"));

	if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
		return true;

	return false;
}

static bool bif_sys_evaluable_property_2(query *q)
{
	GET_FIRST_ARG(p1,callable);
	GET_NEXT_ARG(p2,atom_or_var);
	cell tmp;
	bool found = false, evaluable = false;

	if (CMP_STRING_TO_CSTR(q, p2, "built_in")
		&& CMP_STRING_TO_CSTR(q, p2, "static")
		&& CMP_STRING_TO_CSTR(q, p2, "dynamic")
		&& CMP_STRING_TO_CSTR(q, p2, "foreign")
		)
		return throw_error(q, p2, p2_ctx, "domain_error", "evaluable_property");

	if (get_builtin_term(q->st.m, p1, &found, &evaluable), found) {
		if (!evaluable
			&& CMP_STRING_TO_CSTR(q, p1, "integer")	// both evaluable & predicate
			&& CMP_STRING_TO_CSTR(q, p1, "float")	// both evaluable & predicate
			)
			return false;

		make_atom(&tmp, new_atom(q->pl, "built_in"));

		if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
			return true;

		make_atom(&tmp, new_atom(q->pl, "static"));

		if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
			return true;

		make_atom(&tmp, new_atom(q->pl, "dynamic"));

		if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
			return false;

		make_atom(&tmp, new_atom(q->pl, "foreign"));

		if (unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
			return false;

		return throw_error(q, p2, p2_ctx, "domain_error", "evaluable_property");
	}

	return false;
}

static bool bif_char_type_2(query *q)
{
	GET_FIRST_ARG(p1,atom_or_integer);
	GET_NEXT_ARG(p2,atom_or_compound);
	int ch;

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (is_atom(p1)) {
		if (strlen_utf8(C_STR(q, p1)) != 1)
			return false;

		ch = peek_char_utf8(C_STR(q, p1));
	} else
		ch = get_smallint(p1);

	if (!CMP_STRING_TO_CSTR(q, p2, "alpha"))
		return iswalpha(ch);
	else if (!CMP_STRING_TO_CSTR(q, p2, "alphabetic"))
		return iswalpha(ch);
	else if (!CMP_STRING_TO_CSTR(q, p2, "alnum"))
		return iswalpha(ch) || iswdigit(ch);
	else if (!CMP_STRING_TO_CSTR(q, p2, "alphanumeric"))
		return iswalpha(ch) || iswdigit(ch);
	else if (!CMP_STRING_TO_CSTR(q, p2, "prolog"))
		return iswalpha(ch) || iswdigit(ch) || iswgraph(ch);
	// 'ch' is a full codepoint, so the wide forms: the narrow ctype
	// functions are undefined for anything outside unsigned char, and on
	// some libcs that is a table overrun and a crash rather than the
	// harmless false it reads as. iswdigit and iswxdigit are ASCII-only
	// in every locale by definition, so this is not a widening of what
	// they match. There is no iswoctdigit, hence the range test.
	else if (!CMP_STRING_TO_CSTR(q, p2, "hexadecimal_digit"))
		return iswxdigit(ch);
	else if (!CMP_STRING_TO_CSTR(q, p2, "octal_digit"))
		return (ch >= '0') && (ch <= '7');
	else if (!CMP_STRING_TO_CSTR(q, p2, "decimal_digit"))
		return iswdigit(ch);
	else if (!CMP_STRING_TO_CSTR(q, p2, "numeric"))
		return iswdigit(ch);
	else if (!CMP_STRING_TO_CSTR(q, p2, "whitespace"))
		return is_space_utf8(ch);
	else if (!CMP_STRING_TO_CSTR(q, p2, "lower") && !get_arity(p2))
		return iswlower(ch);
	else if (!CMP_STRING_TO_CSTR(q, p2, "upper") && !get_arity(p2))
		return iswupper(ch);
	else if (!CMP_STRING_TO_CSTR(q, p2, "lower") && get_arity(p2)) {
		cell *arg21 = deref(q, p2+1, p2_ctx);
		pl_ctx arg21_ctx = q->latest_ctx;
		char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
		put_char_utf8(tmpbuf, towlower(ch));
		cell tmp;
		make_string(&tmp, tmpbuf);
		bool ok = unify(q, arg21, arg21_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	} else if (!CMP_STRING_TO_CSTR(q, p2, "upper") && get_arity(p2)) {
		cell *arg21 = deref(q, p2+1, p2_ctx);
		pl_ctx arg21_ctx = q->latest_ctx;
		char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
		put_char_utf8(tmpbuf, towupper(ch));
		cell tmp;
		make_string(&tmp, tmpbuf);
		bool ok = unify(q, arg21, arg21_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	} else if (!CMP_STRING_TO_CSTR(q, p2, "graphic"))
		return (ch == '#') || (ch == '$') || (ch == '&')
		|| (ch == '*') || (ch == '+') || (ch == '-')
		|| (ch == '.') || (ch == '/') || (ch == ':')
		|| (ch == '<') || (ch == '=') || (ch == '>')
		|| (ch == '?') || (ch == '@') || (ch == '^')
		|| (ch == '~');
	else if (!CMP_STRING_TO_CSTR(q, p2, "graphic_token"))
		return (ch == '#') || (ch == '$') || (ch == '&')
		|| (ch == '*') || (ch == '+') || (ch == '-')
		|| (ch == '.') || (ch == '/') || (ch == ':')
		|| (ch == '<') || (ch == '=') || (ch == '>')
		|| (ch == '?') || (ch == '@') || (ch == '^')
		|| (ch == '~');
	else if (!CMP_STRING_TO_CSTR(q, p2, "ascii_graphic"))
		return iswgraph(ch) && (ch < 128);
	else if (!CMP_STRING_TO_CSTR(q, p2, "ascii"))
		return ch < 128;
	else if (!CMP_STRING_TO_CSTR(q, p2, "ascii_punctuation"))
		return iswpunct(ch);
	else if (!CMP_STRING_TO_CSTR(q, p2, "octet"))
		return ch < 256;
	else if (!CMP_STRING_TO_CSTR(q, p2, "layout"))
		return iswspace(ch) || (ch == '\t') || (ch == '\v') || (ch == '\f') || (ch == '\r') || (ch == '\n');
	else if (!CMP_STRING_TO_CSTR(q, p2, "exponent"))
		return (ch == 'e') || (ch == 'E');
	else if (!CMP_STRING_TO_CSTR(q, p2, "sign"))
		return (ch == '-') || (ch == '+');
	else if (!CMP_STRING_TO_CSTR(q, p2, "meta"))
		return (ch == '\'') || (ch == '"') || (ch == '`') || (ch == '\\');
	else if (!CMP_STRING_TO_CSTR(q, p2, "solo"))
		return (ch == '|') || (ch == '!') || (ch == '%')
			|| (ch == '[') || (ch == ']')
			|| (ch == '{') || (ch == '}')
			|| (ch == '(') || (ch == ')')
			;

	return false;
}

static bool bif_sys_lt_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (is_bigint(p2))
		return throw_error(q, p2, p2_ctx, "domain_error", "small_integer_range");

	pl_int num = get_smallint(p1);

	if (num < get_smallint(p2)) {
		set_smallint(p1, num+1);
		return true;
	}

	drop_choice(q);
	return true;
}

static bool bif_limit_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,callable);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	cell *tmp = prepare_call(q, CALL_NOSKIP, p2, p2_ctx, 4);
	pl_idx num_cells = p2->num_cells;
	make_instr(tmp+num_cells++, g_fail_s, bif_sys_lt_2, 2, 2);
	make_int(tmp+num_cells++, 1);
	make_int(tmp+num_cells++, get_smallint(p1));
	make_call(q, tmp+num_cells);
	q->st.instr = tmp;
	return true;
}

static bool bif_sys_gt_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (is_bigint(p2))
		return throw_error(q, p2, p2_ctx, "domain_error", "small_integer_range");

	pl_int num = get_smallint(p1);

	if (num <= get_smallint(p2)) {
		set_smallint(p1, num+1);
		return false;
	}

	return true;
}

static bool bif_offset_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,callable);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	cell *tmp = prepare_call(q, CALL_NOSKIP, p2, p2_ctx, 4);
	pl_idx num_cells = p2->num_cells;
	make_instr(tmp+num_cells++, g_fail_s, bif_sys_gt_2, 2, 2);
	make_int(tmp+num_cells++, 1);
	make_int(tmp+num_cells++, get_smallint(p1));
	make_call(q, tmp+num_cells);
	q->st.instr = tmp;
	return true;
}

static bool bif_sys_ne_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (is_bigint(p2))
		return throw_error(q, p2, p2_ctx, "domain_error", "small_integer_range");

	pl_int num = get_smallint(p1);

	if (num != get_smallint(p2)) {
		set_smallint(p1, num+1);
		return false;
	}

	drop_choice(q);
	return true;
}

static bool bif_sys_incr_2(query *q)
{
	GET_FIRST_ARG(p1, integer_or_var);
	GET_NEXT_ARG(p2, integer);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	pl_int n = get_smallint(p2);
	n++;
	set_smallint(p2, n);

	if (is_integer(p1))
		return get_smallint(p1) == n;

	return unify(q, p1, p1_ctx, p2, q->st.cur_ctx);
}

static bool bif_call_nth_2(query *q)
{
	GET_FIRST_ARG(p1,callable);
	GET_NEXT_ARG(p2,integer_or_var);

	if (is_bigint(p2))
		return throw_error(q, p2, p2_ctx, "domain_error", "small_integer_range");

	if (is_integer(p2) && is_zero(p2))
		return false;

	if (is_integer(p2) && is_negative(p2))
		return throw_error(q, p2, p2_ctx, "domain_error", "not_less_than_zero");

	if (is_var(p2)) {
		cell *tmp = prepare_call(q, CALL_NOSKIP, p1, p1_ctx, 6);
		pl_idx num_cells = p1->num_cells;
		make_instr(tmp+num_cells++, g_sys_incr_s, bif_sys_incr_2, 2, 2);
		GET_RAW_ARG(2,p2_raw);
		tmp[num_cells] = *p2_raw;
		tmp[num_cells++].num_cells = 1;
		make_int(tmp+num_cells++, 0);
		make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
		make_uint(tmp+num_cells++, q->st.cp);
		make_call(q, tmp+num_cells);
		CHECKED(push_fail_on_retry_with_barrier(q));
		q->st.instr = tmp;
		return true;
	}

	cell *tmp = prepare_call(q, CALL_NOSKIP, p1, p1_ctx, 7);
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_sys_ne_s, bif_sys_ne_2, 2, 2);
	make_int(tmp+num_cells++, 1);
	make_int(tmp+num_cells++, get_smallint(p2));
	make_instr(tmp+num_cells++, g_cut_s, bif_iso_cut_0, 0, 0);
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_call(q, tmp+num_cells);
	CHECKED(push_fail_on_retry_with_barrier(q));
	q->st.instr = tmp;
	return true;
}

static bool bif_string_concat_3(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,atom_or_var);
	SB(pr);
	SB_strcpy(pr, C_STR(q, p1));
	SB_strcat(pr, C_STR(q, p2));
	const char *src = SB_cstr(pr);
	cell tmp;
	make_string(&tmp, src);
	SB_free(pr);
	int ok  = unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_string_length_2(query *q)
{
	GET_FIRST_ARG(p1,atom_or_list_or_nil);
	GET_NEXT_ARG(p2,integer_or_var);

	if (is_interned(p1) && !CMP_STRING_TO_CSTR(q, p1, "[]")) {
		cell tmp;
		make_int(&tmp, 0);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	if (is_atom(p1)) {
		cell tmp;
		make_int(&tmp, C_STRLEN_UTF8(p1));
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	size_t tmp_len;

	if (q->st.m->flags.double_quote_chars
		&& !is_cyclic_term(q, p1, p1_ctx)
		&& (tmp_len = scan_is_chars_list(q, p1, p1_ctx, false)) > 0) {
		cell tmp;
		make_int(&tmp, tmp_len);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	return throw_error(q, p1, p1_ctx, "type_error", "chars");
}

static bool bif_numlist_3(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);
	GET_NEXT_ARG(p3,var);
	long long from = get_smallint(p1);
	long long to = get_smallint(p2);

	if (from > to)
		return false;

	long long cnt = (to - from) + 1;
	CHECKED(init_tmp_heap(q));

	while (cnt--) {
		cell tmp;
		make_int(&tmp,  from++);
		cell *l = append_list(q, &tmp);
		CHECKED(l);
		l->flags |= FLAG_INTERNED_GROUND;
	}

	cell *l = end_list(q);
	CHECKED(l);
	l->flags |= FLAG_INTERNED_GROUND;
	return unify(q, p3, p3_ctx, l, q->st.cur_ctx);
}

// module:goal

bool bif_iso_qualify_2(query *q)
{
	GET_FIRST_ARG(p1,atom_or_var);
	GET_NEXT_ARG(p2,callable);
	module *m = q->st.m;

	if (is_cstring(p2))
		convert_to_literal(m, p2);

	if (is_atom(p1)) {
		m = find_module(q->pl, C_STR(q, p1));

		if (!m && strcmp(C_STR(q, p1), "loader"))
			return throw_error(q, p1, q->st.cur_ctx, "existence_error", "module");

		if (!m)
			m = q->st.m;
	}

	bool status;

	if (!call_check(q, p2, &status, false))
		return status;

	cell *tmp = prepare_call(q, CALL_NOSKIP, p2, p2_ctx, 4);
	CHECKED(tmp);
	pl_idx num_cells = 0;

	// Resolve the goal in the module it was qualified with, not in the
	// caller. Looking it up in q->st.m pins the caller's predicate of
	// the same name/arity, and the engine prefers a set 'match' over
	// resolving the goal itself, so the qualification is lost whenever
	// the calling module happens to define that name.

	if (!is_builtin(p2))
		tmp[num_cells].match = search_predicate(m, p2);

	num_cells += p2->num_cells;
	make_instr(tmp+num_cells++, g_true_s, bif_iso_true_0, 0, 0); // see query fact matching
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_call(q, tmp+num_cells);
	CHECKED(push_fail_on_retry_with_barrier(q));
	q->st.instr = tmp;
	q->st.m = m;
	return true;
}

static bool bif_current_module_1(query *q)
{
	GET_FIRST_ARG(p1,atom_or_var);

	if (!q->retry) {
		if (is_atom(p1)) {
			const char *name = C_STR(q, p1);
			return find_module(q->pl, name) ? true : false;
		}

		CHECKED(push_choice(q));
		module *m = q->current_m = list_front(&q->pl->modules);
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, m->name));
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	if (!q->current_m)
		return false;

	module *m = q->current_m = list_next(q->current_m);

	if (!m)
		return false;

	CHECKED(push_choice(q));
	cell tmp;
	make_atom(&tmp, new_atom(q->pl, m->name));
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_use_module_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	if (!is_atom(p1) && !is_compound(p1)) return false;
	CHECKED(init_tmp_heap(q));
	cell *tmp = clone_term_to_tmp(q, q->st.instr, q->st.cur_ctx);
	return do_use_module_1(q->st.m, tmp);
}

static bool bif_use_module_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,list_or_nil);
	CHECKED(init_tmp_heap(q));
	cell *tmp = clone_term_to_tmp(q, q->st.instr, q->st.cur_ctx);
	return do_use_module_2(q->st.m, tmp);
}

static bool bif_multifile_1(query *q)
{
	GET_FIRST_ARG(p1,compound);

	if (p1->val_off == g_colon_s) {
		const char *mod = C_STR(q, p1+1);
		p1 += 2;
		const char *name = C_STR(q, p1+1);
		unsigned arity = get_smalluint(p1+2);

		if (!is_multifile_in_db(q->pl, mod, name, arity)) {
			//fprintf(stderr, "Error: not multifile %s:%s/%u\n", mod, name, arity);
			//return true;
		}
	} else if (p1->val_off == g_slash_s) {
		const char *name = C_STR(q, p1+1);
		unsigned arity = get_smalluint(p1+2);
		set_multifile_in_db(q->st.m, name, arity);
	} else
		return false;

	return true;
}

static bool bif_meta_predicate_1(query *q)
{
	GET_FIRST_ARG(p1,compound);
	set_meta_predicate_in_db(q->st.m, p1);
	return true;
}

static bool bif_prolog_load_context_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom_or_var);

	if (CMP_STRING_TO_CSTR(q, p1, "module"))
		return false;

	cell tmp;
	make_atom(&tmp, new_atom(q->pl, q->st.m->name));
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_strip_module_3(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,atom_or_var);
	GET_NEXT_ARG(p3,any);

	if (p1->val_off == g_colon_s) {
		cell *cm = deref(q, p1+1, p1_ctx);
		pl_ctx cm_ctx = q->latest_ctx;

		if (!unify(q, p2, p2_ctx, cm, cm_ctx))
			return false;

		cell *ct = deref(q, p1+2, p1_ctx);
		pl_ctx ct_ctx = q->latest_ctx;
		return unify(q, p3, p3_ctx, ct, ct_ctx);
	}

#if 0
	cell tmp;
	make_atom(&tmp, new_atom(q->pl, q->st.m->name));

	if (!unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
		return false;
#endif

	return unify(q, p3, p3_ctx, p1, p1_ctx);
}

bool bif_sys_module_1(query *q)
{
	GET_FIRST_ARG(p1,atom_or_var);

	if (is_var(p1)) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, q->st.m->name));
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	const char *name = C_STR(q, p1);
	module *m = find_module(q->pl, name);

	if (!strcmp(name, "loader"))
		return true;

	if (!m) {
		if (q->top->is_command && !q->run_init)
			fprintf(stdout, "Info: created module '%s'\n", name);

		m = module_create(q->pl, name);
		CHECKED(m);
	}

	q->st.m = m;
	return true;
}

static bool bif_sys_modules_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	CHECKED(init_tmp_heap(q));

	for (module *m = list_front(&q->pl->modules);
		m; m = list_next(m)) {
		if (m->orig)
			continue;

		cell tmp;
		make_atom(&tmp,  new_atom(q->pl, m->name));
		append_list(q, &tmp);
	}

	cell *l = end_list(q);
	CHECKED(l);
	return unify(q, p1, p1_ctx, l, q->st.cur_ctx);
}

static bool bif_using_0(query *q)
{
	module *m = q->st.m;
	fprintf(stdout, "%% %s --> [", m->name);

	for (unsigned i = 0; i < m->idx_used; i++) {
		if (i) fprintf(stdout, "%s", ",");
		fprintf(stdout, "%s", m->used[i]->name);
	}

	fprintf(stdout, "].\n");
	return true;
}

static bool bif_sys_det_length_rundown_2(query *q)
{
	GET_FIRST_ARG(p1,list_or_var);
	GET_NEXT_ARG(p2,integer);

	if (is_bigint(p2) || (get_smallint(p2) > INT32_MAX))
		return throw_error(q, p2, p2_ctx, "resource_error", "memory");

	unsigned n = get_smalluint(p2);
	int var_num = create_vars(q, n);
	CHECKED(var_num != -1);
	cell *l = alloc_heap(q, n*2+1);
	CHECKED(l);
	cell *save_l = l;

	while (n) {
		l->tag = TAG_INTERNED;
		l->val_off = g_dot_s;
		l->num_cells = n-- * 2 + 1;
		l->flags = 0;
		set_arity(l, 2);
		l++;
		make_ref(l, var_num++, q->st.cur_ctx);
		l->flags |= FLAG_VAR_ANON | FLAG_VAR_VOID;
		l++;
	}

	make_atom(l, g_nil_s);
	GET_FIRST_ARG(xp1,list_or_var);
	return unify(q, xp1, xp1_ctx, save_l, q->st.cur_ctx);
}

static bool bif_sys_memberchk_3(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,list_or_nil_or_var);
	GET_NEXT_ARG(p3,var);
	PROLOG_LIST_HANDLER(p2);

	while (is_list(p2)) {
		CHECKED(push_choice(q));
		cell *h = PROLOG_LIST_HEAD(p2);
		h = deref(q, h, p2_ctx);
		pl_ctx h_ctx = q->latest_ctx;

		if (unify(q, p1, p1_ctx, h, h_ctx)) {
			drop_choice(q);
			unify(q, p3, p3_ctx, make_nil(), q->st.cur_ctx);
			return true;
		}

		retry_choice(q);
		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;
	}

	if (is_nil(p2))
		return false;

	unify(q, p3, p3_ctx, p2, p2_ctx);
	return true;
}

bool bif_sys_make_string_2(query *q)
{
	GET_FIRST_ARG(p1,iso_list);
	GET_NEXT_ARG(p2,var);
	bool has_vars, is_partial;
	size_t len = scan_is_chars_list2(q, p1, p1_ctx, true, &has_vars, &is_partial, NULL);

	if (has_vars || !len)
		return false;

	char *src = chars_list_to_string(q, p1, p1_ctx);
	CHECKED(src);
	cell tmp;
	make_stringn(&tmp, src, len);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	TPL_free(src);
	return ok;
}

bool bif_sys_create_var_1(query *q)
{
	GET_FIRST_ARG(p1,any);

	if (!is_var(p1))
		return true;

	unsigned var_num = create_vars(q, 1);
	const frame *f = GET_FRAME(p1_ctx);
	slot *e = get_slot(q, f, p1->var_num);
	cell tmp;
	make_var(&tmp, g_anon_s, var_num);
	e->c = tmp;
	return true;
}

bool bif_sys_jump_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	q->st.instr += get_smallint(p1);
	return true;
}

bool bif_sys_jump_if_nil_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,integer);

	if (is_nil(p1))
		q->st.instr += get_smallint(p2);

	return true;
}

static bool bif_sys_integer_in_radix_3(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,smallint);
	GET_NEXT_ARG(p3,var);

	if (!is_positive(p1) && !is_zero(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "integer");

	if (!is_positive(p2) || (get_smallint(p2) == 1))
		return throw_error(q, p2, p2_ctx, "domain_error", "integer");

	int radix = get_smallint(p2);

	cell tmp;

	if (is_bigint(p1)) {
		size_t len = mp_int_string_len(&p1->val_bigint->ival, radix) - 1;
		char *dst = TPL_malloc(len+1);
		mp_int_to_string(&p1->val_bigint->ival, radix, dst, len+1);
		make_string(&tmp, dst);
		TPL_free(dst);
	} else {
		char tmpbuf[256];
		sprint_int(tmpbuf, sizeof(tmpbuf), get_smallint(p1), radix);
		make_string(&tmp, tmpbuf);
	}

	bool ok = unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

bool bif_sys_reset_handler_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	cell tmp;
	make_uint(&tmp, (pl_uint)q->st.cp);
	CHECKED(push_reset_handler(q));
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_iso_compare_3(query *q)
{
	GET_FIRST_ARG(p1,atom_or_var);
	GET_NEXT_ARG(p2,any);
	GET_NEXT_ARG(p3,any);

	if (is_atom(p1)) {
		if (CMP_STRING_TO_CSTR(q, p1, "<")
			&& CMP_STRING_TO_CSTR(q, p1, ">")
			&& CMP_STRING_TO_CSTR(q, p1, "="))
			return throw_error(q, p1, p1_ctx, "domain_error", "order");
	}

	int status = compare(q, p2, p2_ctx, p3, p3_ctx);
	cell tmp;
	make_atom(&tmp, (status == 0)?g_eq_s:status<0?g_lt_s:g_gt_s);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_sys_counter_1(query *q)
{
	GET_FIRST_ARG(p1,integer_or_var);
	pl_uint n = 0;

	if (is_smallint(p1))
		n = get_smalluint(p1);

	cell tmp;
	make_uint(&tmp, n+1);
	GET_FIRST_RAW_ARG(p1_raw,var);
	reset_var(q, p1_raw, p1_raw_ctx, &tmp, q->st.cur_ctx);
	return true;
}

static bool bif_sys_countall_2(query *q)
{
	GET_FIRST_ARG(p1,callable);
	GET_NEXT_ARG(p2,var);

	CHECKED(init_tmp_heap(q));
	cell *tmp2 = clone_term_to_tmp(q, p1, p1_ctx);
	CHECKED(tmp2);
	bool status;

	if (!call_check(q, tmp2, &status, false))
		return status;

	cell n;
	make_uint(&n, 0);
	reset_var(q, p2, p2_ctx, &n, q->st.cur_ctx);
	cell *tmp = prepare_call(q, CALL_NOSKIP, tmp2, q->st.cur_ctx, 4);
	CHECKED(tmp);
	pl_idx num_cells = tmp2->num_cells;
	make_instr(tmp+num_cells++, g_sys_counter_s, bif_sys_counter_1, 1, 1);
	make_ref(tmp+num_cells++, p2->var_num, p2_ctx);
	make_instr(tmp+num_cells++, g_fail_s, bif_iso_fail_0, 0, 0);
	make_call(q, tmp+num_cells);
	CHECKED(push_succeed_on_retry_with_barrier(q, 0));
	q->st.instr = tmp;
	return true;
}

// Compare two integer cells by value, across the small/big divide.

static int cmp_int_cells(const cell *c1, const cell *c2)
{
	if (is_bigint(c1) && is_bigint(c2))
		return mp_int_compare(&c1->val_bigint->ival, &c2->val_bigint->ival);

	if (is_bigint(c1))
		return mp_int_compare_value(&c1->val_bigint->ival, get_smallint(c2));

	if (is_bigint(c2))
		return -mp_int_compare_value(&c2->val_bigint->ival, get_smallint(c1));

	pl_int v1 = get_smallint(c1), v2 = get_smallint(c2);
	return v1 < v2 ? -1 : v1 > v2 ? 1 : 0;
}

// Make an integer cell from an mpz, demoting to a smallint when it fits.
// On success the caller owns the cell and must unshare_cell() it.

static bool make_int_from_mpz(cell *tmp, mpz_t *v)
{
	*tmp = (cell){0};
	mp_small val;

	if (mp_int_to_int(v, &val) == MP_RANGE) {
		tmp->tag = TAG_INT;
		tmp->val_bigint = TPL_malloc(sizeof(bigint));

		if (!tmp->val_bigint)
			return false;

		tmp->val_bigint->refcnt = 1;

		if (mp_int_init_copy(&tmp->val_bigint->ival, v) == MP_MEMORY) {
			TPL_free(tmp->val_bigint);
			return false;
		}

		tmp->flags |= FLAG_INT_BIG | FLAG_MANAGED;
	} else
		make_int(tmp, (pl_int)val);

	return true;
}

static bool bif_between_3(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);
	GET_NEXT_ARG(p3,integer_or_var);

	// Fast path: all bounds are smallints, so the counter can hold the
	// value itself. This is the hot path (loops over small ranges).

	if (!is_bigint(p1) && !is_bigint(p2) && !is_bigint(p3)) {
		if (!q->retry) {
			if (get_smallint(p1) > get_smallint(p2))
				return false;

			if (!is_var(p3)) {
				if (get_smallint(p3) > get_smallint(p2))
					return false;

				if (get_smallint(p3) < get_smallint(p1))
					return false;

				return true;
			}

			if (get_smallint(p1) != get_smallint(p2)) {
				q->st.cnt = get_smallint(p1);
				CHECKED(push_choice(q));
			}

			return unify(q, p3, p3_ctx, p1, p1_ctx);
		}

		cell tmp;
		make_int(&tmp, ++q->st.cnt);

		if (q->st.cnt != get_smallint(p2))
			CHECKED(push_choice(q));

		return unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	}

	// General path: a bound (or the value) is a bigint. Rather than keep a
	// bigint counter in the query state, q->st.cnt holds the *offset* from
	// Low and each value is recomputed as Low+offset. An int64 offset is
	// ample: no enumeration will ever reach 2^63 steps. (#1105)

	if (!q->retry) {
		if (cmp_int_cells(p1, p2) > 0)
			return false;

		if (!is_var(p3)) {
			if (cmp_int_cells(p3, p2) > 0)
				return false;

			if (cmp_int_cells(p3, p1) < 0)
				return false;

			return true;
		}

		if (cmp_int_cells(p1, p2) != 0) {
			q->st.cnt = 0;
			CHECKED(push_choice(q));
		}

		return unify(q, p3, p3_ctx, p1, p1_ctx);
	}

	q->st.cnt++;

	mpz_t v;

	if (is_bigint(p1))
		mp_int_init_copy(&v, &p1->val_bigint->ival);
	else
		mp_int_init_value(&v, get_smallint(p1));

	mp_int_add_value(&v, q->st.cnt, &v);

	cell tmp;

	if (!make_int_from_mpz(&tmp, &v)) {
		mp_int_clear(&v);
		return throw_error(q, p1, p1_ctx, "resource_error", "memory");
	}

	mp_int_clear(&v);

	if (cmp_int_cells(&tmp, p2) != 0) {
		if (!push_choice(q)) {
			unshare_cell(&tmp);
			return false;
		}
	}

	bool ok = unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

void format_property(module *m, char *tmpbuf, size_t buflen, const char *name, unsigned arity, const char *type, bool function)
{
	tmpbuf[0] = '\0';
	char *dst = tmpbuf;

	if (needs_quoting(m, name, strlen(name))) {
		char *dst2 = formatted(name, strlen(name), false, false);
		dst += snprintf(dst, buflen-(dst-tmpbuf), "'$predicate_property'(%s, '%s'", function?"function":"predicate", dst2);
		TPL_free(dst2);
	} else
		dst += snprintf(dst, buflen-(dst-tmpbuf), "'$predicate_property'(%s, %s", function?"function":"predicate", name);

	if (arity) {
		dst += snprintf(dst, buflen-(dst-tmpbuf), "(");

		for (unsigned i = 0; i < arity; i++) {
			if (i > 0)
				dst += snprintf(dst, buflen-(dst-tmpbuf), ",");

			dst += snprintf(dst, buflen-(dst-tmpbuf), "_");
		}

		dst += snprintf(dst, buflen-(dst-tmpbuf), ")");
	}

	dst += snprintf(dst, buflen-(dst-tmpbuf), ", (%s)).\n", type);
}

void format_template(module *m, char *tmpbuf, size_t buflen, const char *name, unsigned arity, const builtins *ptr, bool function, bool alt)
{
	tmpbuf[0] = '\0';

	if (!ptr->help || !*ptr->help)
		return;

	if (alt && (!ptr->help_alt || !*ptr->help_alt))
		return;

	char *dst = tmpbuf;
	bool quote = needs_quoting(m, name, strlen(name));

	if (quote) {
		char *dst2 = formatted(name, strlen(name), false, false);
		dst += snprintf(dst, buflen-(dst-tmpbuf), "'$predicate_property'(%s, '%s'", function?"function":"predicate", dst2);
		TPL_free(dst2);
	} else
		dst += snprintf(dst, buflen-(dst-tmpbuf), "'$predicate_property'(%s, %s", function?"function":"predicate", name);

	if (arity) {
		dst += snprintf(dst, buflen-(dst-tmpbuf), "(");

		for (unsigned i = 0; i < arity; i++) {
			if (i > 0)
				dst += snprintf(dst, buflen-(dst-tmpbuf), ",");

			dst += snprintf(dst, buflen-(dst-tmpbuf), "_");
		}

		dst += snprintf(dst, buflen-(dst-tmpbuf), ")");
	}

	char tmpbuf2[256];
	do_template(tmpbuf2, name, ptr->arity, alt?ptr->help_alt:ptr->help, function, quote);
	dst += snprintf(dst, buflen-(dst-tmpbuf), ", (%s))).\n", tmpbuf2);
}

static void load_properties(module *m)
{
	if (m->loaded_properties)
		return;

	m->loaded_properties = true;
	SB_alloc(pr, 1024*64);
	char tmpbuf[1024];

	format_property(m, tmpbuf, sizeof(tmpbuf), "!", 0, "choice_construct", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "true", 0, "choice_construct", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "fail", 0, "choice_construct", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), ",", 2, "choice_construct", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), ";", 2, "choice_construct", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "->", 2, "choice_construct", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "*->", 2, "choice_construct", false); SB_strcat(pr, tmpbuf);

	format_property(m, tmpbuf, sizeof(tmpbuf), "\\+", 1, "meta_predicate((\\+0))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "catch", 3, "meta_predicate(catch(0,?,0))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "reset", 3, "meta_predicate(parser_reset(0,?,?))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), ",", 2, "meta_predicate((0,0))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), ";", 2, "meta_predicate((0;0))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "->", 2, "meta_predicate((0->0))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "*->", 2, "meta_predicate((0*->0))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "if", 3, "meta_predicate(if(0,0,0))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "once", 1, "meta_predicate(once(0))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "ignore", 1, "meta_predicate(ignore(0))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "call", 1, "meta_predicate(call(0))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "call_task", 1, "meta_predicate(call_task(0))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "findall", 3, "meta_predicate(findall(?,0,-))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "findnsols", 4, "meta_predicate(findnsols(?,?,0,-))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "|", 2, "meta_predicate(((:)|(+)))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "time", 1, "meta_predicate(time(0))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "call_nth", 2, "meta_predicate(call_nth(0,?))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "asserta", 1, "meta_predicate(asserta(:))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "asserta", 2, "meta_predicate(asserta(:,-))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "assertz", 1, "meta_predicate(assertz(:))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "assertz", 2, "meta_predicate(assertz(:,-))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "retract", 1, "meta_predicate(retract(:))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "retract", 2, "meta_predicate(retract(:,?))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "retractall", 1, "meta_predicate(retractall(:))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "current_predicate", 1, "meta_predicate(current_predicate(:))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "predicate_property", 2, "meta_predicate(predicate_property(:,?))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "abolish", 1, "meta_predicate(abolish(:))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "clause", 2, "meta_predicate(clause(:,?))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "clause", 3, "meta_predicate(clause(:,?,?))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "bb_b_put", 2, "meta_predicate(bb_b_put(:,?))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "bb_put", 2, "meta_predicate(bb_put(:,?))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "bb_get", 2, "meta_predicate(bb_get(:,?))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "bb_update", 3, "meta_predicate(bb_update(:,?,?))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "bb_delete", 2, "meta_predicate(bb_delete(:,?))", false); SB_strcat(pr, tmpbuf);

#if USE_THREADS
	format_property(m, tmpbuf, sizeof(tmpbuf), "thread_create", 3, "meta_predicate(thread_create(0,-,?))", false); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "thread_signal", 2, "meta_predicate(thread_signal(+,0))", false); SB_strcat(pr, tmpbuf);
#endif

	for (int i = 2; i <= 7; i++) {
		char metabuf[1024];
		char *dst2 = metabuf;
		dst2 += snprintf(dst2, sizeof(metabuf), "meta_predicate(call(%d", i-1);

		for (int j = 1; j < i; j++)
			dst2 += snprintf(dst2, sizeof(metabuf)-(dst2-metabuf), ",?");


		snprintf(dst2, sizeof(metabuf)-(dst2-metabuf), "))");
		format_property(m, tmpbuf, sizeof(tmpbuf), "call", i, metabuf, false); SB_strcat(pr, tmpbuf);
	}

	for (int i = 2; i <= 8; i++) {
		char metabuf[1024];
		char *dst2 = metabuf;
		dst2 += snprintf(dst2, sizeof(metabuf), "meta_predicate(call_task(%d", i-1);

		for (int j = 1; j < i; j++)
			dst2 += snprintf(dst2, sizeof(metabuf)-(dst2-metabuf), ",?");


		snprintf(dst2, sizeof(metabuf)-(dst2-metabuf), "))");
		format_property(m, tmpbuf, sizeof(tmpbuf), "call_task", i, metabuf, false); SB_strcat(pr, tmpbuf);
	}

	for (const builtins *ptr = g_atts_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
	}

	for (const builtins *ptr = g_bboard_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
	}

	for (const builtins *ptr = g_csv_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
	}

	for (const builtins *ptr = g_database_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
	}

	for (const builtins *ptr = g_evaluable_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
	}

	format_property(m, tmpbuf, sizeof(tmpbuf), "float", 1, "built_in", true); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "float", 1, "static", true); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "integer", 1, "built_in", true); SB_strcat(pr, tmpbuf);
	format_property(m, tmpbuf, sizeof(tmpbuf), "integer", 1, "static", true); SB_strcat(pr, tmpbuf);

	for (const builtins *ptr = g_ffi_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "foreign", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
	}

	for (const builtins *ptr = g_iso_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
 	}

	for (const builtins *ptr = g_misc_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
 	}

	for (const builtins *ptr = g_net_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
 	}

	for (const builtins *ptr = g_uri_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
 	}

	for (const builtins *ptr = g_os_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
	}

	for (const builtins *ptr = g_other_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
	}

	for (const builtins *ptr = g_control_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
	}

	for (const builtins *ptr = g_posix_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
	}

	for (const builtins *ptr = g_sort_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
	}

	for (const builtins *ptr = g_sregex_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
	}

	for (const builtins *ptr = g_streams_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
 	}

	for (const builtins *ptr = g_tasks_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
	}

	for (const builtins *ptr = g_threads_bifs; ptr->name; ptr++) {
		sl_app(m->pl->biftab, ptr->name, ptr);
		if (ptr->name[0] == '$') continue;
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "built_in", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "static", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf);
		if (ptr->iso) { format_property(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, "iso", ptr->evaluable?true:false); SB_strcat(pr, tmpbuf); }
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, false); SB_strcat(pr, tmpbuf);
		format_template(m, tmpbuf, sizeof(tmpbuf), ptr->name, ptr->arity, ptr, ptr->evaluable?true:false, true); SB_strcat(pr, tmpbuf);
 	}

	parser *p = parser_create(m);
	p->srcptr = SB_cstr(pr);
	p->is_consulting = true;
	tokenize(p, false, false);
	parser_destroy(p);
	SB_free(pr);
}

static void load_flags(query *q)
{
	cell tmp;
	make_atom(&tmp, new_atom(q->pl, "$current_prolog_flag"));
	set_arity(&tmp, 2);

	if (do_abolish(q, &tmp, &tmp, false) != true)
		return;

	module *m = q->st.m;
	SB_alloc(pr, 1024*8);

	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "double_quotes", m->flags.double_quote_atom?"atom":m->flags.double_quote_chars?"chars":m->flags.double_quote_codes?"codes":"???");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "char_conversion", m->flags.char_conversion?"on":"off");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "occurs_check", m->flags.occurs_check==OCCURS_CHECK_TRUE?"true":m->flags.occurs_check==OCCURS_CHECK_FALSE?"false":"error");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "character_escapes", m->flags.character_escapes?"true":"false");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "strict_iso", m->flags.strict_iso?"on":"off");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "debug", m->flags.debug?"on":"off");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "unknown", m->flags.unknown == UNK_ERROR?"error":m->flags.unknown == UNK_WARNING?"warning":m->flags.unknown == UNK_CHANGEABLE?"changeable":"fail");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "encoding", "'UTF-8'");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "unix", host_is_unix()?"true":"false");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "os", host_os());
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "rationals", HAS_RATIONALS?"true":"false");
#if USE_THREADS
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "threads", "true");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %u).\n", "os_threads", g_max_os_threads ? g_max_os_threads : MAX_ACTUAL_THREADS);
#else
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "threads", "false");
#endif
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "global_bb", q->pl->global_bb?"true":"false");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "tabling", q->pl->tabling?"true":"false");

	if (q->pl->tbl_max_answer_size) {
		SB_sprintf(pr, "'$current_prolog_flag'(%s, %u).\n", "max_table_answer_size", q->pl->tbl_max_answer_size);
	} else {
		SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "max_table_answer_size", "infinite");
	}

	if (q->pl->tbl_max_subgoal_size) {
		SB_sprintf(pr, "'$current_prolog_flag'(%s, %u).\n", "max_table_subgoal_size", q->pl->tbl_max_subgoal_size);
	} else {
		SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "max_table_subgoal_size", "infinite");
	}

	if (q->pl->tbl_max_answers_for_subgoal) {
		SB_sprintf(pr, "'$current_prolog_flag'(%s, %u).\n", "max_answers_for_subgoal", q->pl->tbl_max_answers_for_subgoal);
	} else {
		SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "max_answers_for_subgoal", "infinite");
	}

	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "verbose", q->pl->quiet?"false":"true");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "dialect", "trealla");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "bounded", "false");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %d).\n", "max_procedure_arity", MAX_PROCEDURE_ARITY);
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "max_arity", "unbounded");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %u).\n", "cpu_count", g_cpu_count);
	SB_sprintf(pr, "'$current_prolog_flag'(%s, %s).\n", "integer_rounding_function", "toward_zero");
	SB_sprintf(pr, "'$current_prolog_flag'(%s, [max_depth(%u),quoted(%s),double_quotes(%s)]).\n", "answer_write_options", (unsigned)q->pl->def_max_depth, q->pl->def_quoted?"true":"false", q->pl->def_double_quotes?"true":"false");

	parser *p = parser_create(m->pl->user_m);
	p->srcptr = SB_cstr(pr);
	p->is_consulting = true;
	tokenize(p, false, false);
	parser_destroy(p);
	SB_free(pr);
}

static void load_ops(query *q)
{
	SB_alloc(pr, 1024*8);
	sliter *iter = sl_first(q->st.m->ops);
	op_table *ptr;

	while (sl_next(iter, (void**)&ptr)) {
		char specifier[80], name[1024];

		if (!ptr->priority || !ptr->specifier)
			continue;

		if (ptr->specifier == OP_FX)
			strcpy(specifier, "fx");
		else if (ptr->specifier == OP_FY)
			strcpy(specifier, "fy");
		else if (ptr->specifier == OP_YF)
			strcpy(specifier, "yf");
		else if (ptr->specifier == OP_XF)
			strcpy(specifier, "xf");
		else if (ptr->specifier == OP_YFX)
			strcpy(specifier, "yfx");
		else if (ptr->specifier == OP_XFY)
			strcpy(specifier, "xfy");
		else if (ptr->specifier == OP_XFX)
			strcpy(specifier, "xfx");

		bool quote = needs_quoting(q->st.m, ptr->name, strlen(ptr->name));

		if (quote) {
			char *dst2 = formatted(ptr->name, strlen(ptr->name), false, false);
			snprintf(name, sizeof(name), "%s", dst2);
			TPL_free(dst2);
		} else
			snprintf(name, sizeof(name), "%s", ptr->name);

		if (quote) {
			SB_sprintf(pr, "'$op'( '%s', %s, %u).\n", name, specifier, ptr->priority);
		} else {
			SB_sprintf(pr, "'$op'( (%s), %s, %u).\n", name, specifier, ptr->priority);
		}
	}

	sl_done(iter);
	iter = sl_first(q->st.m->defops);

	while (sl_next(iter, (void**)&ptr)) {
		char specifier[80];

		if (!ptr->specifier)
			continue;

		if (ptr->specifier == OP_FX)
			strcpy(specifier, "fx");
		else if (ptr->specifier == OP_FY)
			strcpy(specifier, "fy");
		else if (ptr->specifier == OP_YF)
			strcpy(specifier, "yf");
		else if (ptr->specifier == OP_XF)
			strcpy(specifier, "xf");
		else if (ptr->specifier == OP_YFX)
			strcpy(specifier, "yfx");
		else if (ptr->specifier == OP_XFY)
			strcpy(specifier, "xfy");
		else if (ptr->specifier == OP_XFX)
			strcpy(specifier, "xfx");


		char *dst2 = formatted(ptr->name, strlen(ptr->name), false, false);
		SB_sprintf(pr, "'$op'('%s', %s, %u).\n", dst2, specifier, ptr->priority);
		TPL_free(dst2);
	}

	sl_done(iter);
	parser *p = parser_create(q->pl->user_m);
	p->srcptr = SB_cstr(pr);
	p->is_consulting = true;
	tokenize(p, false, false);
	parser_destroy(p);
	//printf("*** %s load_ops %s\n", q->st.m->name, SB_cstr(pr));
	SB_free(pr);
}

builtins g_iso_bifs[] =
{
	{":", 2, bif_iso_qualify_2, "+atom,:callable", true, false, BLAH},
	{"=..", 2, bif_iso_univ_2, "+term,?list", true, false, BLAH},
	{"=", 2, bif_iso_unify_2, "+term,+term", true, false, BLAH},
	{"\\=", 2, bif_iso_notunifiable_2, "+term,+term", true, false, BLAH},

	{"repeat", 0, bif_iso_repeat_0, NULL, true, false, BLAH},
	{"atom", 1, bif_iso_atom_1, "+term", true, false, BLAH},
	{"atomic", 1, bif_iso_atomic_1, "+term", true, false, BLAH},
	{"number", 1, bif_iso_number_1, "+term", true, false, BLAH},
	{"compound", 1, bif_iso_compound_1, "+term", true, false, BLAH},
	{"var", 1, bif_iso_var_1, "+term", true, false, BLAH},
	{"nonvar", 1, bif_iso_nonvar_1, "+term", true, false, BLAH},
	{"ground", 1, bif_iso_ground_1, "+term", true, false, BLAH},
	{"callable", 1, bif_iso_callable_1, "+term", true, false, BLAH},
	{"char_code", 2, bif_iso_char_code_2, "?atom,?integer", true, false, BLAH},
	{"atom_chars", 2, bif_iso_atom_chars_2, "?atom,?list", true, false, BLAH},
	{"atom_codes", 2, bif_iso_atom_codes_2, "?atom,?list", true, false, BLAH},
	{"number_chars", 2, bif_iso_number_chars_2, "?number,?list", true, false, BLAH},
	{"number_codes", 2, bif_iso_number_codes_2, "?number,?list", true, false, BLAH},
	{"arg", 3, bif_iso_arg_3, "+integer,+term,?term", true, false, BLAH},
	{"functor", 3, bif_iso_functor_3, "?term,?atom,?integer", true, false, BLAH},
	{"copy_term", 2, bif_iso_copy_term_2, "+term,?term", true, false, BLAH},
	{"term_variables", 2, bif_iso_term_variables_2, "+term,-list", true, false, BLAH},
	{"atom_length", 2, bif_iso_atom_length_2, "?list,?integer", true, false, BLAH},
	{"atom_concat", 3, bif_iso_atom_concat_3, "+atom,+atom,?atom", true, false, BLAH},
	{"sub_atom", 5, bif_iso_sub_atom_5, "+atom,?before,?length,?after,?atom", true, false, BLAH},
	{"sub_string", 5, bif_iso_sub_string_5, "+character_list,?before,?length,?after,?character_list", true, false, BLAH},
	{"current_rule", 1, bif_iso_current_rule_1, "-term", true, false, BLAH},
	{"end_of_file", 0, NULL, NULL, true, false, BLAH},
	{"$halt", 0, bif_iso_halt_0, NULL, true, false, BLAH},
	{"$halt", 1, bif_iso_halt_1, "+integer", true, false, BLAH},
	{"set_prolog_flag", 2, bif_iso_set_prolog_flag_2, "+atom,+term", true, false, BLAH},
	{"op", 3, bif_iso_op_3, "?integer,?atom,+atom", true, false, BLAH},
	{"findall", 3, bif_iso_findall_3, "+term,:callable,-list", true, false, BLAH},
	{"current_predicate", 1, bif_iso_current_predicate_1, "+predicate_indicator", true, false, BLAH},
	{"acyclic_term", 1, bif_iso_acyclic_term_1, "+term", true, false, BLAH},
	{"compare", 3, bif_iso_compare_3, "+atom,+term,+term", true, false, BLAH},
	{"unify_with_occurs_check", 2, bif_iso_unify_with_occurs_check_2, "+term,+term", true, false, BLAH},

	{0}
};

builtins g_other_bifs[] =
{
	{"trace", 0, bif_trace_0, NULL, false, false, BLAH},
	{"statistics", 0, bif_statistics_0, NULL, false, false, BLAH},
	{"statistics", 2, bif_statistics_2, "+atom,-term", false, false, BLAH},
	{"current_module", 1, bif_current_module_1, "-atom", false, false, BLAH},
	{"prolog_load_context", 2, bif_prolog_load_context_2, "+atom,?term", false, false, BLAH},
	{"strip_module", 3, bif_strip_module_3, "+callable,?atom,?callable", false, false, BLAH},
	{"using", 0, bif_using_0, NULL, false, false, BLAH},
	{"use_module", 1, bif_use_module_1, "+term", false, false, BLAH},
	{"use_module", 2, bif_use_module_2, "+term,+list", false, false, BLAH},
	{"module_info", 2, bif_module_info_2, "+atom,-list", false, false, BLAH},
	{"source_info", 2, bif_source_info_2, "+predicate_indicator,-list", false, false, BLAH},
	{"multifile", 1, bif_multifile_1, "+term", false, false, BLAH},
	{"meta_predicate", 1, bif_meta_predicate_1, "+term", false, false, BLAH},
	{"$findnsols", 4, bif_findnsols_4, "+integer,+term,:callable,-list", false, false, BLAH},

	{"help", 2, bif_help_2, "+predicate_indicator,+atom", false, false, BLAH},
	{"help", 1, bif_help_1, "+predicate_indicator", false, false, BLAH},
	{"help", 0, bif_help_0, NULL, false, false, BLAH},
	{"module_help", 3, bif_module_help_3, "+atom,+predicate_indicator,+atom", false, false, BLAH},
	{"module_help", 2, bif_module_help_2, "+atom,+predicate_indicator", false, false, BLAH},
	{"module_help", 1, bif_module_help_1, "+atom", false, false, BLAH},

	{"string_codes", 2, bif_string_codes_2, "+string,-list", false, false, BLAH},
	{"term_singletons", 2, bif_term_singletons_2, "+term,-list", false, false, BLAH},
	{"string", 1, bif_string_1, "+term", false, false, BLAH},
	{"atomic_concat", 3, bif_atomic_concat_3, "+atomic,+atomic,?atomic", false, false, BLAH},
	{"atomic_list_concat", 2, bif_atomic_list_concat_2, "+list,?atom", false, false, BLAH},
	{"atomic_list_concat", 3, bif_atomic_list_concat_3, "?list,+atomic,?atom", false, false, BLAH},
	{"replace", 4, bif_replace_4, "+string,+integer,+integer,-string", false, false, BLAH},
	{"split_string", 4, bif_split_string_4, "+string,+atom,+atom,-list", false, false, BLAH},
	{"split", 4, bif_split_4, "+string,+string,?string,?string", false, false, BLAH},
	{"is_list_or_partial_list", 1, bif_is_list_or_partial_list_1, "+term", false, false, BLAH},
	{"is_partial_list", 1, bif_is_partial_list_1, "+term", false, false, BLAH},
	{"is_list", 1, bif_is_list_1, "+term", false, false, BLAH},
	{"is_bigint", 1, bif_is_bigint_1, "+term", false, false, BLAH},
	{"list", 1, bif_is_list_1, "+term", false, false, BLAH},
	{"term_hash", 2, bif_term_hash_2, "+term,?integer", false, false, BLAH},
	{"base64", 3, bif_base64_3, "?string,?string,+list", false, false, BLAH},
	{"urlenc", 3, bif_urlenc_3, "?string,?string,+list", false, false, BLAH},
	{"atom_lower", 2, bif_atom_lower_2, "?atom,?atom", false, false, BLAH},
	{"atom_upper", 2, bif_atom_upper_2, "?atom,?atom", false, false, BLAH},
	{"string_lower", 2, bif_string_lower_2, "?string,?string", false, false, BLAH},
	{"string_upper", 2, bif_string_upper_2, "?string,?string", false, false, BLAH},
	{"hex_bytes", 2, bif_hex_bytes_2, "?string,?list", false, false, BLAH},
	{"hex_chars", 2, bif_hex_chars_2, "?integer,?string", false, false, BLAH},
	{"octal_chars", 2, bif_octal_chars_2, "?integer,?string", false, false, BLAH},
	{"$char_type", 2, bif_char_type_2, "+character,+term", false, false, BLAH},
	{"$code_type", 2, bif_char_type_2, "+integer,+term", false, false, BLAH},
	{"uuid", 1, bif_uuid_1, "-string", false, false, BLAH},
	{"call_nth", 2, bif_call_nth_2, ":callable,+integer", false, false, BLAH},
	{"limit", 2, bif_limit_2, "+integer,:callable", false, false, BLAH},
	{"offset", 2, bif_offset_2, "+integer,+callable", false, false, BLAH},
	{"unifiable", 3, bif_sys_unifiable_3, "+term,+term,-list", false, false, BLAH},
	{"string_concat", 3, bif_string_concat_3, "+string,+string,?string", false, false, BLAH},
	{"string_length", 2, bif_string_length_2, "+string,?integer", false, false, BLAH},
	{"crypto_n_random_bytes", 2, bif_crypto_n_random_bytes_2, "+integer,-codes", false, false, BLAH},
	{"cyclic_term", 1, bif_cyclic_term_1, "+term", false, false, BLAH},
	{"load_text", 2, bif_load_text_2, "+string,+list", false, false, BLAH},
	{"between", 3, bif_between_3, "+integer,+integer,?integer", false, false, BLAH},
	{"numlist", 3, bif_numlist_3, "+integer,+integer,-list", false, false, BLAH},
	{"nb_setarg", 3, bif_nb_setarg_3, "+integer,+term,+integer", false, false, BLAH},

	{"must_be", 4, bif_must_be_4, "+term,+atom,+term,?any", false, false, BLAH},
	{"must_be", 2, bif_must_be_2, "+atom,+term", false, false, BLAH},
	{"can_be", 4, bif_can_be_4, "+term,+atom,+term,?any", false, false, BLAH},
	{"can_be", 2, bif_can_be_2, "+atom,+term,", false, false, BLAH},

	{"duplicate_term", 2, bif_duplicate_term_2, "+term,?term", false, false, BLAH},
	{"copy_term_with_attributes", 2, bif_duplicate_term_2, "+term,?term", false, false, BLAH},
	{"copy_term_nat", 2, bif_copy_term_nat_2, "+term,?term", false, false, BLAH},

#if USE_OPENSSL
	{"crypto_data_hash", 3, bif_crypto_data_hash_3, "?string,?string,?list", false, false, BLAH},
#endif

	{"$clone_term", 2, bif_sys_clone_term_2, "+term,?term", false, false, BLAH},
	{"$module", 1, bif_sys_module_1, "?atom", false, false, BLAH},
	{"$modules", 1, bif_sys_modules_1, "-list", false, false, BLAH},
	{"$countall", 2, bif_sys_countall_2, "@callable,-integer", false, false, BLAH},
	{"$counter", 1, bif_sys_counter_1, NULL, false, false, BLAH},
	{"$legacy_current_prolog_flag", 2, bif_sys_current_prolog_flag_2, "+atom,?term", true, false, BLAH},
	{"$legacy_predicate_property", 2, bif_sys_predicate_property_2, "+callable,?string", false, false, BLAH},
	{"$legacy_evaluable_property", 2, bif_sys_evaluable_property_2, "+callable,?string", false, false, BLAH},
	{"$det_length_rundown", 2, bif_sys_det_length_rundown_2, "?list,+integer", false, false, BLAH},
	{"$memberchk", 3, bif_sys_memberchk_3, "?term,?list,-term", false, false, BLAH},
	{"$is_partial_string", 1, bif_sys_is_partial_string_1, "+string", false, false, BLAH},
	{"$load_properties", 0, bif_sys_load_properties_0, NULL, false, false, BLAH},
	{"$load_flags", 0, bif_sys_load_flags_0, NULL, false, false, BLAH},
	{"$load_ops", 0, bif_sys_load_ops_0, NULL, false, false, BLAH},
	{"$ops_dirty", 0, bif_sys_ops_dirty_0, NULL, false, false, BLAH},
	{"$list", 1, bif_sys_list_1, "-list", false, false, BLAH},
	{"$queue", 1, bif_sys_queue_1, "+term", false, false, BLAH},
	{"$incr", 2, bif_sys_incr_2, "@integer,+integer", false, false, BLAH},
	{"$first_non_octet", 2, bif_sys_first_non_octet_2, "+chars,-integer", false, false, BLAH},
	{"$skip_max_list", 4, bif_sys_skip_max_list_4, "?integer,?integer?,?term,?term", false, false, BLAH},
	{"$integer_in_radix", 3, bif_sys_integer_in_radix_3, "+integer,+integer,-string", false, false, BLAH},
	{"$jump", 1, bif_sys_jump_1, NULL, false, false, BLAH},
	{"$jump_if_nil", 2, bif_sys_jump_if_nil_2, "+term,+integer", false, false, BLAH},
	{"$lt", 2, bif_sys_lt_2, NULL, false, false, BLAH},
	{"$gt", 2, bif_sys_gt_2, NULL, false, false, BLAH},
	{"$ge", 2, bif_findnsols_ge_2, NULL, false, false, BLAH},
	{"$ne", 2, bif_sys_ne_2, NULL, false, false, BLAH},
	{"$undo", 2, bif_sys_undo_1, "+var", true, false, BLAH},
	{"$create_var", 1, bif_sys_create_var_1, "-var", false, false, BLAH},
	{"$make_string", 2, bif_sys_make_string_2, "+list,-string", false, false, BLAH},

	{0}
};
