#include <stdlib.h>
#include <stdio.h>
#include <float.h>

#include "prolog.h"
#include "query.h"

#ifndef DBL_DECIMAL_DIG
#define DBL_DECIMAL_DIG DBL_DIG
#endif

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

		// Every option is name(Arg): anything else has no argument to read.

		if (!is_compound(c) || (get_arity(c) != 1))
			{ unwind_stream(q, n); return throw_error(q, c, c_ctx, "domain_error", "engine_option"); }

		cell *name = c + 1;
		name = deref(q, name, c_ctx);

		if (!CMP_STRING_TO_CSTR(q, c, "alias")) {
			if (is_var(name))
				{ unwind_stream(q, n); return throw_error(q, name, q->latest_ctx, "instantiation_error", "engine_option"); }

			if (!is_atom(name))
				{ unwind_stream(q, n); return throw_error(q, name, q->latest_ctx, "type_error", "atom"); }

			if (get_named_stream(q->pl, C_STR(q, name), C_STRLEN(q, name)) >= 0)
				{ unwind_stream(q, n); return throw_error(q, name, q->latest_ctx, "permission_error", "create,engine"); }

			sl_app(str->alias, DUP_STRING(q, name), NULL);
			cell tmp;
			make_atom(&tmp, new_atom(q->pl, C_STR(q, name)));

			if (!unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx))
				{ unwind_stream(q, n); return false; }

			is_alias = true;
		} else if (!CMP_STRING_TO_CSTR(q, c, "stack")) {
			// SWI-Prolog's stack(Bytes), accepted for portability but not enforced.
		} else {
			{ unwind_stream(q, n); return throw_error(q, c, c_ctx, "domain_error", "engine_option"); }
		}

		p4 = PROLOG_LIST_TAIL(p4);
		p4 = deref(q, p4, p4_ctx);
		p4_ctx = q->latest_ctx;

		if (is_var(p4))
			{ unwind_stream(q, n); return throw_error(q, p4, p4_ctx, "instantiation_error", "args_not_sufficiently_instantiated"); }
	}

	if (is_atom(p3)) {
		if (get_named_stream(q->pl, C_STR(q, p3), C_STRLEN(q, p3)) >= 0)
			{ unwind_stream(q, n); return throw_error(q, p3, p3_ctx, "permission_error", "create,engine"); }

		sl_app(str->alias, DUP_STRING(q, p3), NULL);
	} else if (!is_alias) {
		cell tmp2;
		make_int(&tmp2, n);
		tmp2.flags |= FLAG_INT_STREAM | FLAG_INT_ENGINE;
		unify(q, p3, p3_ctx, &tmp2, q->st.cur_ctx);
	}

	str->first_time = str->is_engine = true;
	str->cur_yield = str->cur_post = NULL;

	str->engine = query_create(q->st.m);
	CHECKED(str->engine);
	str->engine->cur_engine = n;
	str->engine->is_engine = true;
	str->engine->trace = q->trace;

	// A context is a frame index, meaningless in the engine: clone the call whole and number its variables from 0 for the engine's frame 0.

	CHECKED(init_tmp_heap(q));
	cell *p0 = clone_term_to_tmp(q, q->st.instr, q->st.cur_ctx);
	CHECKED(p0);
	unsigned num_vars = rebase_term(q, p0, 0, false);

	query *save_q = q;
	q = str->engine;		// Operating in engine now

	CHECKED(create_vars(q, num_vars) >= 0);
	GET_FIRST_RAW_ARG0(xp1,any,p0);
	GET_NEXT_RAW_ARG(xp2,callable);

	// Not CALL_NOSKIP: execute() starts on the goal itself, so no proceed() needs holding back and the first builtin ran twice.

	cell *tmp = prepare_call(q, CALL_SKIP, xp2, xp2_ctx, 1);
	CHECKED(tmp);
	make_call_engine(q, tmp+xp2->num_cells, save_q->st.instr);
	str->pattern = alloc_heap(q, xp1->num_cells);
	CHECKED(str->pattern);
	dup_cells(str->pattern, xp1, xp1->num_cells);
	CHECKED(push_fail_on_retry_with_barrier(q));
	q->st.instr = tmp;
	return true;
}

// An engine argument's stream slot, -1 for a term that can't name an engine or -2 for one naming none: SWI-Prolog's errors, not the stream ones.

static int get_engine_stream(query *q, cell *p)
{
	if (!is_atom(p) && !((p->tag == TAG_INT) && (p->flags & FLAG_INT_STREAM)))
		return -1;

	int n = get_stream(q, p);
	return (n >= 0) && q->pl->streams[n].is_engine ? n : -2;
}

static bool bif_engine_next_2(query *q)
{
	GET_FIRST_ARG(pstr,any);
	GET_NEXT_ARG(p1,any);
	int n = get_engine_stream(q, pstr);

	if (n < 0)
		return throw_error(q, pstr, pstr_ctx, n == -1 ? "type_error" : "existence_error", "engine");

	stream *str = &q->pl->streams[n];

	// As in SWI-Prolog, asking again once the goal has finished is an error, not another failure.

	if (str->engine->engine_done)
		return throw_error(q, pstr, pstr_ctx, "existence_error", "engine");

	if (str->first_time) {
		str->first_time = false;

		// engine_create() already gave frame 0 its variables: pass their count, not a fixed cap, or execute() re-lays frame 0 wrongly.
		execute(str->engine, str->engine->st.instr, get_frame(str->engine, 0)->actual_slots);
	} else if (!query_redo(str->engine)) {
		str->engine->engine_done = true;
		return false;
	}

	// An error the goal didn't catch belongs to the caller: rethrow it from the ball as throw/1 printed it.

	if (str->engine->engine_ball) {
		char *ball = str->engine->engine_ball;
		str->engine->engine_ball = NULL;
		str->engine->engine_done = true;
		bool ok = find_exception_handler(q, ball);
		TPL_free(ball);
		return ok;
	}

	// Stopped at an engine_yield/1, whose term is the answer: the next call resumes just after it.

	if (str->engine->yielded) {
		cell *tmp = import_term(q, str->cur_yield, q->st.cur_ctx);
		CHECKED(tmp);
		free_detached_term(str->cur_yield);
		str->cur_yield = NULL;
		return unify(q, p1, p1_ctx, tmp, q->st.cur_ctx);
	}

	// The fail-on-retry barrier is the engine's bottom choicepoint. If it
	// was consumed, the goal has no answer rather than an unbound pattern.

	if (!str->engine->st.cp) {
		str->engine->engine_done = true;
		return false;
	}

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

	// Resumed by engine_next/2, which has already taken the term.

	if (q->retry)
		return true;

	stream *str = &q->pl->streams[q->cur_engine];
	free_detached_term(str->cur_yield);
	str->cur_yield = engine_detach_term(q, p1, p1_ctx);
	CHECKED(str->cur_yield);

	// Not do_yield(), which suspends only a task: stop start() here, leaving a choicepoint for query_redo() to resume.

	CHECKED(push_choice(q));
	q->yielded = true;
	return false;
}

static bool bif_engine_post_2(query *q)
{
	GET_FIRST_ARG(pstr,any);
	GET_NEXT_ARG(p1,any);
	int n = get_engine_stream(q, pstr);

	if (n < 0)
		return throw_error(q, pstr, pstr_ctx, n == -1 ? "type_error" : "existence_error", "engine");

	stream *str = &q->pl->streams[n];
	free_detached_term(str->cur_post);
	str->cur_post = engine_detach_term(q, p1, p1_ctx);
	CHECKED(str->cur_post);
	return true;
}

static bool bif_engine_fetch_1(query *q)
{
	GET_FIRST_ARG(p1,any);

	if (!q->is_engine)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "existence_error", "not_an_engine");

	stream *str = &q->pl->streams[q->cur_engine];

	if (!str->cur_post) {
		cell self;
		make_int(&self, q->cur_engine);
		self.flags |= FLAG_INT_STREAM | FLAG_INT_ENGINE;
		return throw_error(q, &self, q->st.cur_ctx, "existence_error", "term,delivery");
	}

	cell *tmp = import_term(q, str->cur_post, q->st.cur_ctx);
	CHECKED(tmp);
	free_detached_term(str->cur_post);
	str->cur_post = NULL;
	return unify(q, p1, p1_ctx, tmp, q->st.cur_ctx);
}

static bool bif_engine_self_1(query *q)
{
	GET_FIRST_ARG(p1,any);

	if (!q->is_engine)
		return false;

	cell tmp2;
	make_int(&tmp2, q->cur_engine);
	tmp2.flags |= FLAG_INT_STREAM | FLAG_INT_ENGINE;
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
	GET_FIRST_ARG(pstr,any);
	int n = get_engine_stream(q, pstr);

	if (n < 0)
		return throw_error(q, pstr, pstr_ctx, n == -1 ? "type_error" : "existence_error", "engine");

	return bif_iso_close_1(q);
}

builtins g_misc_bifs[] =
{
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
