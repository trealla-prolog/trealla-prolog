#include <stdlib.h>
#include <stdio.h>
#include <float.h>

#include "prolog.h"
#include "query.h"

#ifndef DBL_DECIMAL_DIG
#define DBL_DECIMAL_DIG DBL_DIG
#endif

static bool bif_map_create_2(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p4,list_or_nil);

	// Options are validated BEFORE new_stream() hands out a slot, so no
	// error exit below has anything to unwind. Previously the slot and
	// its alias skiplist were taken first and every exit had to release
	// them by hand - see bif_threads.c for the same change.

	cell *alias = NULL;
	PROLOG_LIST_HANDLER(p4);

	while (is_list(p4)) {
		cell *h = PROLOG_LIST_HEAD(p4);
		cell *c = deref(q, h, p4_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (is_var(c))
			return throw_error(q, c, q->latest_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

		cell *name = c + 1;
		name = deref(q, name, c_ctx);

		if (!CMP_STRING_TO_CSTR(q, c, "alias")) {
			if (is_var(name))
				return throw_error(q, name, q->latest_ctx, "instantiation_error", "stream_option");

			if (!is_atom(name))
				return throw_error(q, c, c_ctx, "domain_error", "stream_option");

			if (get_named_stream(q->pl, C_STR(q, name), C_STRLEN(q, name)) >= 0)
				return throw_error(q, c, c_ctx, "permission_error", "open,source_sink");

			alias = name;
		} else
			return throw_error(q, c, c_ctx, "domain_error", "stream_option");

		p4 = PROLOG_LIST_TAIL(p4);
		p4 = deref(q, p4, p4_ctx);
		p4_ctx = q->latest_ctx;

		if (is_var(p4))
			return throw_error(q, p4, p4_ctx, "instantiation_error", "args_not_sufficiently_instantiated");
	}

	// Commit.

	int n = new_stream(q->pl);

	if (n < 0)
		return throw_error(q, p1, p1_ctx, "resource_error", "too_many_streams");

	stream *str = &q->pl->streams[n];
	if (!str->alias) str->alias = sl_create((void*)fake_strcmp, (void*)keyfree, NULL);
	str->keyval = sl_create((void*)fake_strcmp, (void*)fake_free, NULL);
	CHECKED(str->keyval);
	str->is_map = true;

	if (alias) {
		sl_app(str->alias, DUP_STRING(q, alias), NULL);
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, C_STR(q, alias)));
		unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	} else {
		cell tmp;
		make_int(&tmp, n);
		tmp.flags |= FLAG_INT_STREAM | FLAG_INT_MAP;
		unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	return true;
}

static bool bif_map_set_3(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (!str->is_map)
		return throw_error(q, pstr, pstr_ctx, "type_error", "not_a_map");

	GET_NEXT_ARG(p1,atomic);
	GET_NEXT_ARG(p2,atomic);
	char *key;

	if (is_integer(p1)) {
		char tmpbuf[128];
		snprintf(tmpbuf, sizeof(tmpbuf), "%lld", (long long unsigned)get_smallint(p1));
		key = TPL_strdup(tmpbuf);
	} else if (is_atom(p1))
		key = DUP_STRING(q, p1);
	else
		return throw_error(q, p1, p1_ctx, "type_error", "integer");

	CHECKED(key);
	char *val = NULL;

	if (is_integer(p2)) {
		char tmpbuf[128];
		snprintf(tmpbuf, sizeof(tmpbuf), "%lld", (long long unsigned)get_smallint(p2));
		val = TPL_strdup(tmpbuf);
	} else if (is_float(p2)) {
		char tmpbuf[128];
		snprintf(tmpbuf, sizeof(tmpbuf), "%.*lg", DBL_DECIMAL_DIG, get_float(p2));
		val = TPL_strdup(tmpbuf);
	} else if (is_atom(p2))
		val = DUP_STRING(q, p2);
	else {
		TPL_free(key);
		return throw_error(q, p2, p2_ctx, "type_error", "integer");
	}

	CHECKED(val);

	// Replace rather than accumulate. sl_app() inserts *after* equal
	// keys (it is the assertz half of the pair asserta/assertz use for
	// the clause index), while sl_get() returns the *first* match - so
	// without this delete a second map_set/3 on the same key was never
	// visible, map_count/2 counted the key once per set, and the old
	// key and value were held until map_close/1.
	//
	// sl_del() frees the stored key and value via fake_free(); the key
	// we just built is untouched and is handed to sl_app() below.

	sl_del(str->keyval, key);
	sl_app(str->keyval, key, val);
	return true;
}

static bool bif_map_get_3(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (!str->is_map)
		return throw_error(q, pstr, pstr_ctx, "type_error", "not_a_map");

	GET_NEXT_ARG(p1,atomic);
	GET_NEXT_ARG(p2,atomic_or_var);
	char *key;
	char tmpbuf[128];

	if (is_integer(p1)) {
		snprintf(tmpbuf, sizeof(tmpbuf), "%lld", (long long unsigned)get_smallint(p1));
		key = tmpbuf;
	} else if (is_atom(p1))
		key = DUP_STRING(q, p1);
	else
		return throw_error(q, p2, p2_ctx, "type_error", "integer");

	CHECKED(key);
	char *val = NULL;

	if (!sl_get(str->keyval, key, (void*)&val)) {
		if (key != tmpbuf) TPL_free(key);
		return false;
	}

	cell tmp;
	const char *src = val;
	int all_digs = 1, floaties = 0;

	if (*src == '-')
		src++;

	while (*src) {
		if ((*src == '.') || (*src == 'e') || (*src == 'E')
			|| (*src == '+') || (*src == '-'))
			floaties++;
		else if (!isdigit((unsigned char)*src)) {
			all_digs = 0;
			break;
		}

		src++;
	}

	if (all_digs && !floaties) {
		pl_int v = strtoll(val, NULL, 10);
		make_int(&tmp, v);
	} else if (all_digs && floaties) {
		pl_flt v = strtod(val, NULL);
		make_float(&tmp, v);
	} else
		make_cstring(&tmp, val);

	if (key != tmpbuf) TPL_free(key);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_map_del_2(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (!str->is_map)
		return throw_error(q, pstr, pstr_ctx, "type_error", "not_a_map");

	GET_NEXT_ARG(p1,atomic);
	char *key;

	if (is_integer(p1)) {
		char tmpbuf[128];
		snprintf(tmpbuf, sizeof(tmpbuf), "%lld", (long long unsigned)get_smallint(p1));
		key = TPL_strdup(tmpbuf);
	} else if (is_atom(p1))
		key = DUP_STRING(q, p1);
	else
		return throw_error(q, p1, p1_ctx, "type_error", "integer");

	CHECKED(key);

	// sl_del() frees the key that is *stored*, not the one used to
	// look it up, so this one is ours to release.

	sl_del(str->keyval, key);
	TPL_free(key);
	return true;
}

static bool bif_map_list_2(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (!str->is_map)
		return throw_error(q, pstr, pstr_ctx, "type_error", "not_a_map");

	GET_NEXT_ARG(p1,list_or_var);
	sliter *iter = sl_first(str->keyval);
	char *val = NULL;
	CHECKED(init_tmp_heap(q));

	while (sl_next(iter, (void**)&val)) {
		void *key = sl_key(iter);
		cell tmpk, tmpv;
		const char *src = key;
		int all_digs = 1;

		while (*src) {
			if (!isdigit((unsigned char)*src)) {
				all_digs = 0;
				break;
			}

			src++;
		}

		if (all_digs) {
			pl_int v = strtoll(key, NULL, 10);
			make_int(&tmpk, v);
		} else
			make_cstring(&tmpk, key);

		src = val;
		src = val;
		all_digs = 1; int floaties = 0;

		if (*src == '-')
			src++;

		while (*src) {
			if ((*src == '.') || (*src == 'e') || (*src == 'E')
				|| (*src == '+') || (*src == '-'))
				floaties++;
			else if (!isdigit((unsigned char)*src)) {
				all_digs = 0;
				break;
			}

			src++;
		}

		if (all_digs && !floaties) {
			pl_int v = strtoll(val, NULL, 10);
			make_int(&tmpv, v);
		} else if (all_digs && floaties) {
			pl_flt v = strtod(val, NULL);
			make_float(&tmpv, v);
		} else
			make_cstring(&tmpv, val);

		cell tmp2[3];
		make_instr(tmp2+0, g_colon_s, NULL, 2, 2);
		tmp2[1] = tmpk;
		tmp2[2] = tmpv;
		SET_OP(tmp2, OP_YFX);
		append_list(q, tmp2);
	}

	cell *tmp = end_list(q);
	sl_done(iter);
	return unify(q, p1, p1_ctx, tmp, q->st.cur_ctx);
}

static bool bif_map_count_2(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (!str->is_map)
		return throw_error(q, pstr, pstr_ctx, "type_error", "not_a_map");

	GET_NEXT_ARG(p1,var);
	cell tmp;
	make_int(&tmp, sl_count(str->keyval));
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_map_close_1(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (!str->is_map)
		return throw_error(q, pstr, pstr_ctx, "type_error", "not_a_map");

	return bif_iso_close_1(q);
}

// An engine runs in its own query, with its own frames and slots, so a
// term held on one side cannot be dereferenced against the other: its
// variables name frames the peer never had, and reading them ran off the
// end of the peer's slot array. Ship terms over the boundary the way
// thread messages do - flatten to a detached image with the variables
// renumbered from zero, then import that into the receiver.

static cell *engine_detach_term(query *q, cell *c, pl_ctx c_ctx)
{
	c = deref(q, c, c_ctx);
	c_ctx = q->latest_ctx;

	if (!init_tmp_heap(q))
		return NULL;

	cell *tmp = clone_term_to_tmp(q, c, c_ctx);

	if (!tmp)
		return NULL;

	rebase_term(q, tmp, 0, false);
	cell *img = TPL_malloc(sizeof(cell) * tmp->num_cells);

	if (!img)
		return NULL;

	dup_cells(img, tmp, tmp->num_cells);
	return img;
}

static bool bif_engine_create_4(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,callable);
	GET_NEXT_ARG(p3,atom_or_var);
	GET_NEXT_ARG(p4,list_or_nil);

	int n = new_stream(q->pl);

	if (n < 0)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "too_many_streams");

	stream *str = &q->pl->streams[n];
	if (!str->alias) str->alias = sl_create((void*)fake_strcmp, (void*)keyfree, NULL);
	bool is_alias = false;
	PROLOG_LIST_HANDLER(p4);

	while (is_list(p4)) {
		cell *h = PROLOG_LIST_HEAD(p4);
		cell *c = deref(q, h, p4_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (is_var(c))
			{ unwind_stream(q, n); return throw_error(q, c, q->latest_ctx, "instantiation_error", "args_not_sufficiently_instantiated"); }

		cell *name = c + 1;
		name = deref(q, name, c_ctx);

		if (!CMP_STRING_TO_CSTR(q, c, "alias")) {
			if (is_var(name))
				{ unwind_stream(q, n); return throw_error(q, name, q->latest_ctx, "instantiation_error", "stream_option"); }

			if (!is_atom(name))
				{ unwind_stream(q, n); return throw_error(q, c, c_ctx, "domain_error", "stream_option"); }

			if (get_named_stream(q->pl, C_STR(q, name), C_STRLEN(q, name)) >= 0)
				{ unwind_stream(q, n); return throw_error(q, c, c_ctx, "permission_error", "open,source_sink"); }

			sl_app(str->alias, DUP_STRING(q, name), NULL);
			cell tmp;
			make_atom(&tmp, new_atom(q->pl, C_STR(q, name)));

			if (!unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx))
				{ unwind_stream(q, n); return false; }

			is_alias = true;
		} else {
			{ unwind_stream(q, n); return throw_error(q, c, c_ctx, "domain_error", "stream_option"); }
		}

		p4 = PROLOG_LIST_TAIL(p4);
		p4 = deref(q, p4, p4_ctx);
		p4_ctx = q->latest_ctx;

		if (is_var(p4))
			{ unwind_stream(q, n); return throw_error(q, p4, p4_ctx, "instantiation_error", "args_not_sufficiently_instantiated"); }
	}

	if (is_atom(p3)) {
		if (get_named_stream(q->pl, C_STR(q, p3), C_STRLEN(q, p3)) >= 0)
			{ unwind_stream(q, n); return throw_error(q, q->st.instr, q->st.cur_ctx, "permission_error", "open,source_sink"); }

		sl_app(str->alias, DUP_STRING(q, p3), NULL);
	} else if (!is_alias) {
		cell tmp2;
		make_int(&tmp2, n);
		tmp2.flags |= FLAG_INT_STREAM | FLAG_INT_MAP;
		unify(q, p3, p3_ctx, &tmp2, q->st.cur_ctx);
	}

	str->first_time = str->is_engine = true;
	str->cur_yield = NULL;

	str->engine = query_create(q->st.m);
	CHECKED(str->engine);
	str->engine->cur_engine = n;
	str->engine->is_engine = true;
	str->engine->trace = q->trace;

	cell *p0 = copy_term_to_heap(q, q->st.instr, q->st.cur_ctx, false);
	CHECKED(p0);
	unify(q, q->st.instr, q->st.cur_ctx, p0, q->st.cur_ctx);

	query *save_q = q;
	q = str->engine;		// Operating in engine now

	GET_FIRST_ARG0(xp1,any,p0);
	GET_NEXT_ARG(xp2,callable);

	cell *tmp = prepare_call(q, CALL_NOSKIP, xp2, xp2_ctx, 1);
	CHECKED(tmp);
	make_call_engine(q, tmp+xp2->num_cells, save_q->st.instr);
	CHECKED(push_fail_on_retry_with_barrier(q));
	q->st.instr = tmp;
	str->pattern = clone_term_to_heap(q, xp1, xp1_ctx);
	CHECKED(str->pattern);
	return true;
}

static bool bif_engine_next_2(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	GET_NEXT_ARG(p1,any);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (!str->is_engine)
		return throw_error(q, pstr, pstr_ctx, "existence_error", "not_an_engine");

	bool was_first_time = str->first_time;

	if (str->first_time) {
		str->first_time = false;

		// engine_create() already gave frame 0 its variables: pass their count, not a fixed cap, or execute() re-lays frame 0 wrongly.
		execute(str->engine, str->engine->st.instr, get_frame(str->engine, 0)->actual_slots);
	}

	if (str->cur_yield) {
		cell *tmp = import_term(q, str->cur_yield, q->st.cur_ctx);
		CHECKED(tmp);
		free_detached_term(str->cur_yield);
		str->cur_yield = NULL;
		return unify(q, p1, p1_ctx, tmp, q->st.cur_ctx);
	}

	if (!was_first_time) {
		if (!query_redo(str->engine))
			return false;
	}

	// The fail-on-retry barrier is the engine's bottom choicepoint. If it
	// was consumed, the goal has no answer rather than an unbound pattern.

	if (!str->engine->st.cp)
		return false;

	cell *img = engine_detach_term(str->engine, str->pattern, 0);
	CHECKED(img);
	cell *tmp = import_term(q, img, q->st.cur_ctx);
	free_detached_term(img);
	CHECKED(tmp);
	return unify(q, p1, p1_ctx, tmp, q->st.cur_ctx);
}

static bool bif_engine_yield_1(query *q)
{
	GET_FIRST_ARG(p1,any);

	if (!q->is_engine)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "permission_error", "not_an_engine");

	stream *str = &q->pl->streams[q->cur_engine];

	if (q->retry && str->cur_yield)
		return do_yield(q, 0);
	else if (q->retry)
		return true;

	free_detached_term(str->cur_yield);
	str->cur_yield = engine_detach_term(q, p1, p1_ctx);
	CHECKED(str->cur_yield);
	return do_yield(q, 0);
}

static bool bif_engine_post_2(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	GET_NEXT_ARG(p1,any);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (!str->is_engine)
		return throw_error(q, pstr, pstr_ctx, "existence_error", "not_an_engine");

	free_detached_term(str->cur_yield);
	str->cur_yield = engine_detach_term(q, p1, p1_ctx);
	CHECKED(str->cur_yield);
	return true;
}

static bool bif_engine_fetch_1(query *q)
{
	GET_FIRST_ARG(p1,any);

	if (!q->is_engine)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "existence_error", "not_an_engine");

	stream *str = &q->pl->streams[q->cur_engine];

	if (!str->cur_yield)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "existence_error", "no_data");

	cell *tmp = import_term(q, str->cur_yield, q->st.cur_ctx);
	CHECKED(tmp);
	free_detached_term(str->cur_yield);
	str->cur_yield = NULL;
	return unify(q, p1, p1_ctx, tmp, q->st.cur_ctx);
}

static bool bif_engine_self_1(query *q)
{
	GET_FIRST_ARG(p1,any);

	if (!q->is_engine)
		return false;

	cell tmp2;
	make_int(&tmp2, q->cur_engine);
	tmp2.flags |= FLAG_INT_STREAM | FLAG_INT_MAP;
	return unify(q, p1, p1_ctx, &tmp2, q->st.cur_ctx);
}

static bool bif_is_engine_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	int n = get_stream(q, p1);

	if (n < 0)
		return false;

	stream *str = &q->pl->streams[n];
	return str->is_engine;
}

static bool bif_engine_destroy_1(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (!str->is_engine)
		return throw_error(q, pstr, pstr_ctx, "existence_error", "not_an_engine");

	return bif_iso_close_1(q);
}

builtins g_misc_bifs[] =
{
	{"map_create", 2, bif_map_create_2, "--stream,+list", false, false, BLAH},
	{"map_set", 3, bif_map_set_3, "+stream,+atomic,+atomic", false, false, BLAH},
	{"map_get", 3, bif_map_get_3, "+stream,+atomic,-atomic", false, false, BLAH},
	{"map_del", 2, bif_map_del_2, "+stream,+atomic", false, false, BLAH},
	{"map_list", 2, bif_map_list_2, "+stream,-list", false, false, BLAH},
	{"map_count", 2, bif_map_count_2, "+stream,-integer", false, false, BLAH},
	{"map_close", 1, bif_map_close_1, "+stream", false, false, BLAH},

	{"$engine_create", 4, bif_engine_create_4, "+term,:callable,?stream,+list", false, false, BLAH},
	{"engine_next", 2, bif_engine_next_2, "+stream,-term", false, false, BLAH},
	{"is_engine", 1, bif_is_engine_1, "+term", false, false, BLAH},
	{"engine_self", 1, bif_engine_self_1, "--stream", false, false, BLAH},
	{"engine_yield", 1, bif_engine_yield_1, "+term", false, false, BLAH},
	{"engine_post", 2, bif_engine_post_2, "+stream,+term", false, false, BLAH},
	{"engine_fetch", 1, bif_engine_fetch_1, "-term", false, false, BLAH},
	{"engine_destroy", 1, bif_engine_destroy_1, "+stream", false, false, BLAH},

	{0}
};
