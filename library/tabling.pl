% Tabling: the (:- table)/1 directive and its driver. Data structures
% (variant/answer tries, worklists, scheduling) are native - see the
% '$tbl_*' builtins in src/tabling.c; suspension uses reset/shift.
%
% The "tabling" prolog flag (default true) gates it at call time:
% when false, tabled predicates run as PLAIN calls - no memoization,
% no termination guarantees (left recursion loops again), which makes
% A/B comparison of a program with and without tabling a one-liner.
%
% Helpers precede the user:term_expansion clauses at the bottom, which
% become active the moment they load.

:- module(tabling, [start_tabling/2,
	abolish_all_tables/0, abolish_table/1, current_table/2,
	incremental/1, tnot/1, call_delays/2, undefined/0,
	delays_residual_program/2, call_residual_program/2, answer_residual/2,
	op(1150, fx, table)]).

:- use_module(library(dcgs)).
:- use_module(library(lists)).

:- dynamic('$tabled'/1).
:- dynamic('$tbl_moded_spec'/5).
:- dynamic('$tbl_incremental_spec'/2).
:- dynamic('$tbl_shared_spec'/2).

% incremental(+Spec) - ":- incremental(q/1)" marks a DYNAMIC predicate
% as one whose assert/retract invalidates the tables that consulted it.
% `incremental` is not a known directive, so the loader runs it as an
% ordinary goal and this needs no parser support (unlike SWI's
% ":- dynamic q/1 as incremental", whose `as` lands in the parser's
% predicate-indicator error path).
%
% Deliberately NOT a prefix operator, so the parens are required. At a
% priority loose enough for ":- incremental q/1" it could no longer
% appear as the right operand of `as` (xfx 700, max 699), which is
% exactly where ":- table p/1 as incremental" needs it - and it would
% make a common word an operator for every user of this library.
%
% Both halves must opt in: this one, and ":- table p/1 as incremental"
% on the table side. A table only collects dependencies if it is
% incremental, and only on predicates that are.

incremental(Spec) :-
	(  var(Spec) ->
	   throw(error(instantiation_error, incremental/1))
	;  incremental_(Spec)
	).

incremental_((A,B)) :- !,
	incremental_(A),
	incremental_(B).
incremental_(Name//Arity) :-
	atom(Name), integer(Arity), Arity >= 0, !,
	Arity2 is Arity + 2,
	incremental_(Name/Arity2).
incremental_(Name/Arity) :-
	atom(Name), integer(Arity), Arity >= 0, !,
	'$tbl_set_pred_incremental'(Name, Arity).
incremental_(Spec) :-
	throw(error(type_error(predicate_indicator, Spec), incremental/1)).

abolish_all_tables :-
	'$tbl_abolish_all_tables'.

% abolish_table(+Spec) drops every table of ONE predicate, where Spec
% takes the same shapes as the (:- table) directive: Name/Arity,
% Name//Arity for a DCG non-terminal, or a comma-conjunction of those.
%
% A non-incremental table does not notice assert/retract, so this is how to invalidate one without abolishing every table.

abolish_table(Spec) :-
	(  var(Spec) ->
	   throw(error(instantiation_error, abolish_table/1))
	;  abolish_table_(Spec)
	).

abolish_table_((A,B)) :- !,
	abolish_table_(A),
	abolish_table_(B).
abolish_table_(Name//Arity) :-
	atom(Name), integer(Arity), Arity >= 0, !,
	Arity2 is Arity + 2,
	abolish_table_(Name/Arity2).
abolish_table_(Name/Arity) :-
	atom(Name), integer(Arity), Arity >= 0, !,
	functor(Test, Name, Arity),
	(  '$tabled'(Test) ->
	   '$tbl_abolish'(Name, Arity)
	;  % Silently doing nothing here hides a typo, and the caller
	   % believes stale answers were dropped when they were not.
	   throw(error(existence_error(table, Name/Arity), abolish_table/1))
	).
abolish_table_(Spec) :-
	throw(error(type_error(predicate_indicator, Spec), abolish_table/1)).

% current_table(?Variant, -Trie) enumerates tables, complete or not; as in SWI a bound Variant is a deterministic variant lookup.

current_table(M:Variant, Trie) :-
	atom(M), !,
	current_table(Variant, Trie).
current_table(Variant, Trie) :-
	var(Variant), !,
	'$tbl_handles'(Handles),
	member(Trie, Handles),
	'$tbl_variant'(Trie, Variant).
current_table(Variant, Trie) :-
	'$tbl_handles'(Handles),
	member(Trie, Handles),
	'$tbl_variant'(Trie, V),
	variant(V, Variant), !,
	V = Variant.

% --- driver ---

start_tabling(Wrapper, Worker) :-
	(  current_prolog_flag(tabling, false) ->
	   call(Worker)
	;  native_start_tabling(Wrapper, Worker)
	).

%
% NB. every shift/1 sits in final-call position of its own clause: the
% continuation capture walks pending goals frame by frame, and keeping
% shifts out of compiled control constructs keeps the captured
% continuations exact.

native_start_tabling(Wrapper, Worker) :-
	'$tbl_variant_table'(Wrapper, T, S),
	% A fresh table needs its mode spec (":- table path(_,_,min)")
	% installed before anything can be added to it - S == fresh means
	% this is the FIRST call for this variant, the only time the table
	% is still empty, so the lookup only ever happens once per table.
	%
	% '$tbl_has_specs' guards it because this sits on the driver's
	% per-table path: in deeply nested tabling (a fresh variant called
	% from inside another) the apply_table_specs/2 frame cost ~1 KB per
	% nesting level, measured as a 15% peak-RSS regression on a
	% 50k-deep benchmark. Most programs declare no specs at all, so the
	% flag turns that into one builtin call.
	(  S == fresh, '$tbl_has_specs' ->
	   apply_table_specs(Wrapper, T)
	;  true
	),
	start_tabling_(S, T, Wrapper, Worker).

apply_table_specs(Wrapper, T) :-
	functor(Wrapper, Name, Arity),
	(  '$tbl_moded_spec'(Name, Arity, Mask, Op, _) ->
	   '$tbl_set_moded'(T, Mask, Op)
	;  true
	),
	(  '$tbl_incremental_spec'(Name, Arity) ->
	   '$tbl_set_incremental'(T)
	;  true
	),
	(  '$tbl_shared_spec'(Name, Arity) ->
	   '$tbl_set_shared'(T, Wrapper)
	;  true
	).

start_tabling_(complete, T, Wrapper, _Worker) :-
	'$tbl_get_answer'(T, Wrapper).
start_tabling_(active, T, Wrapper, _Worker) :-
	shift(call_info(Wrapper, T)).
% A fresh variant is COMPLETED in its own (possibly nested) SCC rather
% than suspending the consumer. Suspension needs the consumer's
% continuation to be capturable, which it is not when the tabled call
% sits inside findall/3, setof/3 or any other collector holding state in
% C. Completing the subgoal side-steps that entirely.
%
% If the SCC turns out to depend on an outer one (a genuine cycle across
% the nesting) its tables are merged into the parent by '$tbl_pop_scc'
% and the consumer suspends after all, deferring to the outer leader.
%
% On an exception escaping, tables created under this SCC are left
% half-built and marked active; without a rollback a later call would
% suspend on a table nobody is going to complete (and silently fail).

start_tabling_(fresh, T, Wrapper, Worker) :-
	'$tbl_push_scc'(T),
	catch(( '$tbl_set_status'(T, active),
	        (  catch(reset(Worker, Ball0, Cont), E, worker_error(E)),
	           delim_(Cont, Ball0, T, Wrapper),
	           !
	        ;  true
	        ),
	        completion
	      ), Ball,
	      ( '$tbl_reset_incomplete', '$tbl_pop_scc'(_), throw(Ball) )),
	'$tbl_pop_scc'(Escaped),
	run_scc_(Escaped, T, Wrapper).

run_scc_(true, T, Wrapper) :-
	shift(call_info(Wrapper, T)).
run_scc_(false, T, Wrapper) :-
	'$tbl_get_answer'(T, Wrapper).

% One producer step. A completed worker records an answer and a shifted
% worker a dependency on the table it suspended on, and both FAIL for
% the producer to go on - except an answer that settles a ground call:
% the step succeeds and the producer is cut, its other clauses unrun.
% That is early completion, how SWI and XSB stop on p29, whose rest
% would never end. dep/4 packs
% SourceCall, continuation and target wrapper in ONE term so they keep
% sharing variables through the image copy. A tnot/1 that has to wait
% shifts neg_call/1 instead, a negative suspension resumed only at the
% fixpoint (item 6) but kept in the same shape.
%
% Exceptions raised by a worker fail that producer branch, as in the
% reference (Desouter) library. Eg. a worker calling setof/3 with its
% output argument bound to a non-list: Trealla's ISO-strict setof
% throws where lenient systems fail, and tabling must not surface a
% difference. tnot/1's own errors are the exception, and so is a moded
% argument bound at call: failing the branch would turn a misuse into a
% wrong answer, as SWI does not.

delim(T, Wrapper, Worker) :-
	catch(reset(Worker, Ball, Cont), E, worker_error(E)),
	delim_(Cont, Ball, T, Wrapper),
	!.

worker_error(E) :-
	(  misuse_error(E) ->
	   throw(E)
	;  '$tbl_note_exception',
	   fail
	).

misuse_error(E) :-
	(  subsumes_term(error(permission_error(tnot, _, _), tnot/1), E)
	;  subsumes_term(error(instantiation_error, tnot/1), E)
	;  subsumes_term(error(type_error(callable, _), tnot/1), E)
	;  subsumes_term(error(uninstantiation_error(_), _/_), E),
	   E = error(_, Name/Arity),
	   '$tbl_moded_spec'(Name, Arity, _, _, _)
	), !.

delim_(none, _, T, Wrapper) :-
	'$tbl_add_answer'(T, Wrapper).
delim_(cont(C), call_info(Answer, SrcT), T, Wrapper) :-
	'$tbl_add_suspension'(SrcT, dep(call_info(Answer, SrcT), C, Wrapper, T)).
delim_(cont(C), neg_call(SrcT), T, Wrapper) :-
	'$tbl_add_neg_suspension'(SrcT, dep(call_info(_, SrcT), C, Wrapper, T)).

% Run to fixpoint: drain tables until no work remains, then complete
% every table created under this leader. With no other work left, the
% tnot/1 calls still waiting come off the worklist as one more batch
% (item 6): each resumes with the negation in its delay list, which can
% give conditional answers and so more work. Simplification and answer
% completion settle them at completion.

completion :-
	(  '$tbl_pop_worklist'(T) ->
	   (  '$tbl_wkl_work'(T, Answer, dep(call_info(Answer, _), C, W, TT)),
	      delim(TT, W, C),
	      fail
	   ;  true
	   ),
	   completion
	;  % A fixpoint in which some worker threw may have collected only
	   % part of the answers; caching that as complete would make the
	   % error permanent. Roll those tables back so a later call
	   % recomputes instead.
	   (  '$tbl_saw_exception' ->
	      '$tbl_reset_incomplete'
	   ;  '$tbl_scc_escaped' ->
	      % This SCC depends on an outer one, so this fixpoint saw only
	      % part of the answers. Leave the tables active for
	      % '$tbl_pop_scc' to merge into the parent, which completes
	      % them once it has seen the rest. Marking them complete here
	      % caches a partial table permanently.
	      true
	   ;  '$tbl_mark_all_complete'
	   )
	).

% --- tnot/1: tabled negation, well-founded semantics ---
%
% DESIGN-tabling-phase2.md item 6, stages A to C. Goal's table is
% completed first if it can be, then negated: existentially when Goal
% is not ground, as in SWI. An unconditional answer makes tnot/1 fail
% and no answer at all makes it succeed. A table that cannot complete
% first is in a loop through this negation, so the caller suspends on
% it and the leader delays the negation at its fixpoint: the answers it
% leads to are conditional until simplification and answer completion
% decide them, and those they cannot decide are undefined. A complete
% table holding only such answers delays the negation the same way.
%
% Every shift/1 below is in final-call position along the whole path
% from tnot/1, as the driver's are, and the paths that may shift branch
% by clause rather than by if-then-else.

tnot(Goal) :-
	tnot_goal(Goal, G),
	functor(G, Name, Arity),
	functor(Test, Name, Arity),
	(  '$tabled'(Test) ->
	   true
	;  tnot_culprit(Goal, Name/Arity, PI),
	   throw(error(permission_error(tnot, non_tabled_procedure, PI), tnot/1))
	),
	current_prolog_flag(tabling, Flag),
	tnot_(Flag, G, Name).

tnot_goal(Goal, _) :-
	var(Goal), !,
	throw(error(instantiation_error, tnot/1)).
tnot_goal(M:Goal, G) :- !,
	(  var(M) ->
	   throw(error(instantiation_error, tnot/1))
	;  tnot_goal(Goal, G)
	).
tnot_goal(Goal, Goal) :-
	callable(Goal), !.
tnot_goal(Goal, _) :-
	throw(error(type_error(callable, Goal), tnot/1)).

tnot_culprit(M:_, PI, M:PI) :-
	M \== user, !.
tnot_culprit(_, PI, PI).

tnot_(false, G, _) :-
	\+ G.
tnot_(true, G, Name) :-
	'$tbl_variant_table'(G, T, S),
	tnot_status_(S, T, G, Name).

tnot_status_(complete, T, _, _) :-
	'$tbl_answer_state'(T, A),
	tnot_complete_(A, T).
tnot_status_(active, T, _, _) :-
	'$tbl_answer_state'(T, A),
	tnot_incomplete_(A, T).
% A copy of start_tabling_(fresh, ...) that negates where that one
% answers: kept apart so the positive path gains no frame per level.
tnot_status_(fresh, T, G, Name) :-
	(  '$tbl_has_specs' ->
	   apply_table_specs(G, T)
	;  true
	),
	G =.. [_|Args],
	atom_concat(Name, ' tabled', WrapName),
	Worker =.. [WrapName|Args],
	'$tbl_push_scc'(T),
	catch(( '$tbl_set_status'(T, active),
	        (  catch(reset(Worker, Ball0, Cont), E, worker_error(E)),
	           delim_(Cont, Ball0, T, G),
	           !
	        ;  true
	        ),
	        completion
	      ), Ball,
	      ( '$tbl_reset_incomplete', '$tbl_pop_scc'(_), throw(Ball) )),
	'$tbl_pop_scc'(Escaped),
	'$tbl_answer_state'(T, A),
	tnot_fresh_(Escaped, A, T).

tnot_fresh_(false, A, T) :-
	tnot_complete_(A, T).
tnot_fresh_(true, A, T) :-
	tnot_incomplete_(A, T).

tnot_complete_(none, _).
tnot_complete_(conditional, T) :-
	'$tbl_delay_neg'(T).

tnot_incomplete_(none, T) :-
	shift(neg_call(T)).
tnot_incomplete_(conditional, T) :-
	shift(neg_call(T)).

% call_delays(:Goal, -Delays) - Goal, with Delays the conjunction of
% literals its answer is conditional on, the latest first: true when it
% is unconditional, as in SWI. Delays still undecided once their tables
% are complete are undefined.

call_delays(Goal, Delays) :-
	(  nb_current('$tbl_delays', D0) -> true ; D0 = [] ),
	b_setval('$tbl_delays', []),
	call(Goal),
	b_getval('$tbl_delays', D),
	append(D, D0, D1),
	b_setval('$tbl_delays', D1),
	delays_goals(D, Delays).

delays_goals([], true).
delays_goals([E|Es], Delays) :-
	'$tbl_delem_goal'(E, G),
	delays_goals_(Es, G, Delays).

delays_goals_([], G, G).
delays_goals_([E|Es], G0, Delays) :-
	'$tbl_delem_goal'(E, G),
	delays_goals_(Es, (G, G0), Delays).

% --- the residual program (item 6 stage D), as in SWI's library(wfs) ---
%
% delays_residual_program(+Delays, -Program): Program lists, as
% Head :- Body, each undefined answer Delays depends on, directly or
% through the answers its own delays name. Body is the answer's delay
% lists, disjoined. Answers that hold outright do not appear, nor does
% undefined/0's own clause.

delays_residual_program(Delays, Program) :-
	delays_elems(Delays, Es, []),
	residual_program(Es, [], _, Program, []).

% call_residual_program(:Goal, -Program): Goal, with the residual program
% of its delays.

call_residual_program(Goal, Program) :-
	call_delays(Goal, Delays),
	delays_residual_program(Delays, Program).

% answer_residual(:Goal, -Residual): an answer already in a table of Goal,
% with Residual its delays, true if it holds outright. Tables are read,
% never created, and the caller's answer stays unconditional.

answer_residual(Goal0, Residual) :-
	(  nonvar(Goal0), Goal0 = _:Goal -> true ; Goal = Goal0 ),
	(  callable(Goal) -> true
	;  var(Goal) -> throw(error(instantiation_error, answer_residual/2))
	;  throw(error(type_error(callable, Goal0), answer_residual/2))
	),
	functor(Goal, Name, Arity),
	functor(Test, Name, Arity),
	(  '$tabled'(Test) -> true
	;  throw(error(permission_error(answer_residual, non_tabled_procedure, Goal0), answer_residual/2))
	),
	'$tbl_handles'(Hs0),
	reverse(Hs0, Hs),
	member(T, Hs),
	'$tbl_variant'(T, Goal),
	(  nb_current('$tbl_delays', D0) -> true ; D0 = [] ),
	b_setval('$tbl_delays', []),
	'$tbl_get_answer'(T, Goal),
	b_getval('$tbl_delays', D),
	b_setval('$tbl_delays', D0),
	(  D = [E] -> answer_body(E, Residual) ; Residual = true ).

delays_elems(G) -->
	{ var(G), throw(error(instantiation_error, delays_residual_program/2)) }.
delays_elems(_:G) --> !,
	delays_elems(G).
delays_elems((A, B)) --> !,
	delays_elems(A),
	delays_elems(B).
delays_elems((A ; B)) --> !,
	delays_elems(A),
	delays_elems(B).
delays_elems(true) --> !.
delays_elems(G) -->
	(  { catch('$tbl_goal_delem'(G, E), _, fail) } -> [E] ; [] ).

% Each answer once: a negation stands for its table's undefined answers.

residual_program([], Done, Done) --> [].
residual_program([E|Es], Done0, Done) -->
	(  { memberchk(E, Done0) } ->
	   { Done1 = Done0 }
	;  { E = neg(_) } ->
	   { '$tbl_delem_answers'(E, As) },
	   residual_program(As, [E|Done0], Done1)
	;  residual_clause(E, [E|Done0], Done1)
	),
	residual_program(Es, Done1, Done).

% Once per head too, as in SWI: the same answer can be reached through
% tables of different calls.

residual_clause(E, Done0, Done) -->
	{ '$tbl_delem_goal'(E, Head),
	  answer_delays(E, 0, DLs) },
	(  { DLs == [] ; Head == undefined ; member(h(H), Done0), H =@= Head } ->
	   { Done = Done0 }
	;  { dls_body(DLs, Body),
	     dls_elems(DLs, Es) },
	   [(Head :- Body)],
	   residual_program(Es, [h(Head)|Done0], Done)
	).

answer_delays(E, N, DLs) :-
	(  '$tbl_answer_delays'(E, N, DL) ->
	   DLs = [DL|DLs1],
	   N1 is N + 1,
	   answer_delays(E, N1, DLs1)
	;  DLs = []
	).

% The elements in the order the body shows them.

dls_elems([], []).
dls_elems([DL|DLs], Es) :-
	reverse(DL, Es1),
	dls_elems(DLs, Es2),
	append(Es1, Es2, Es).

answer_body(E, Body) :-
	answer_delays(E, 0, DLs),
	(  DLs == [] -> Body = true ; dls_body(DLs, Body) ).

dls_body([DL], Body) :- !,
	delays_goals(DL, Body).
dls_body([DL|DLs], (Body ; Bodies)) :-
	delays_goals(DL, Body),
	dls_body(DLs, Bodies).

% The toplevel's side, by way of '$tbl_dump_delays'/1: an undefined
% answer's residual program before its bindings, and its delays after
% them in place of true, or just undefined if the flag
% toplevel_list_wfs_residual_program is false. Clauses are laid out as
% SWI lays them out.

dump_delays_(program) :-
	(  current_prolog_flag(toplevel_list_wfs_residual_program, true),
	   nb_current('$tbl_delays', Es),
	   residual_program(Es, [], _, Program, []),
	   Program \== [] ->
	   write('% WFS residual program'), nl,
	   list_residual(Program)
	;  true
	).
dump_delays_(goal) :-
	(  current_prolog_flag(toplevel_list_wfs_residual_program, true),
	   nb_current('$tbl_delays', Es) ->
	   delays_goals(Es, G),
	   residual_goal(G)
	;  write(undefined)
	).

list_residual([]).
list_residual([(Head :- Body)|Cs]) :-
	\+ \+ ( numbervars(Head-Body, 0, _),
	        write('    '), residual_goal(Head), write(' :-'), nl,
	        write('        '), residual_body(Body, 8), write('.'), nl
	      ),
	list_residual(Cs).

% Body from the cursor, its later lines at Indent.

residual_body((A, B), I) :- !,
	residual_body(A, I), write(','), nl,
	tab(I), residual_body(B, I).
residual_body((A ; B), I) :- !,
	I4 is I + 4,
	write('(   '), residual_body(A, I4),
	residual_disj(B, I, I4), nl,
	tab(I), write(')').
residual_body(G, _) :-
	residual_goal(G).

residual_disj((A ; B), I, I4) :- !,
	nl, tab(I), write(';   '), residual_body(A, I4),
	residual_disj(B, I, I4).
residual_disj(A, I, I4) :-
	nl, tab(I), write(';   '), residual_body(A, I4).

residual_goal(G) :-
	write_term(G, [quoted(true), numbervars(true)]).

% undefined/0, as in SWI and XSB: a tabled atom that is neither true nor
% false. Tabled by hand, since this library's own clauses load before
% its term_expansion does.

'$tabled'(undefined).

undefined :-
	start_tabling(undefined, 'undefined tabled').

'undefined tabled' :-
	tnot(undefined).

% --- (:- table f/N) directive + clause renaming ---

rename_head(Head, NewHead) :-
	functor(Head, Name, Arity),
	functor(Test, Name, Arity),
	'$tabled'(Test),
	atom_concat(Name, ' tabled', WrapName),
	Head =.. [_|Args],
	NewHead =.. [WrapName|Args].

wrappers(Var) --> { var(Var), !, throw(error(instantiation_error, (table)/1)) }.
wrappers((A,B)) --> !, wrappers(A), wrappers(B).
% A DCG non-terminal Name//Arity tables the underlying Name/(Arity+2).
% The clauses themselves reach the rename hook already DCG-translated,
% so only the arity mapping is needed here.
wrappers(Name//Arity) -->
	{ atom(Name), integer(Arity), Arity >= 0, !,
	  Arity2 is Arity + 2 },
	wrappers(Name/Arity2).
% Re-tabling an already-tabled predicate is a no-op rather than a
% second wrapper (harmless but wasteful, and it would re-mark).
wrappers(Name/Arity) -->
	{ atom(Name), integer(Arity), Arity >= 0,
	  functor(Test, Name, Arity),
	  '$tabled'(Test), ! },
	[].
wrappers(Name/Arity) -->
	{ atom(Name), integer(Arity), Arity >= 0, !,
	  functor(Head, Name, Arity),
	  atom_concat(Name, ' tabled', WrapName),
	  Head =.. [Name|Args],
	  WrappedHead =.. [WrapName|Args] },
	% The wrapper rule must precede the '$tabled' marker: each emitted
	% element is itself term_expanded, and once the marker is asserted
	% the rename rule would rewrite the wrapper's own head.
	[ (Head :- tabling:start_tabling(Head, WrappedHead)),
	  tabling:'$tabled'(Head) ].

% ":- table Spec as Options" takes `incremental` and `shared`; anything else is a domain_error, not silently ignored.
%
% Must precede the mode-spec clause below. `p/1 as incremental` is a
% compound whose functor is `as`/2, so that clause matched it - tabling
% a predicate literally named `as`/2 and leaving p/1 untabled, silently.

wrappers(Spec as Options) --> !,
	{ table_spec_name_arity(Spec, Name, Arity),
	  table_options_list(Options, Os),
	  % Publication promises a completed table is never written again;
	  % invalidation does exactly that. Together they would let one
	  % thread free answers another is walking, so refuse rather than
	  % offer a combination that is silently unsafe.
	  (  memberchk(incremental, Os), memberchk(shared, Os) ->
	     throw(error(domain_error(table_option, incremental_and_shared), (table)/1))
	  ;  true
	  ),
	  forall(member(O, Os), table_option(O, Name, Arity)) },
	wrappers(Spec).

table_options_list(Options, Os) :-
	(  var(Options) ->
	   throw(error(instantiation_error, (table)/1))
	;  comma_list_(Options, Os)
	).

comma_list_((A,B), [A|Rest]) :- !, comma_list_(B, Rest).
comma_list_(A, [A]).

table_option(incremental, Name, Arity) :- !,
	(  '$tbl_incremental_spec'(Name, Arity) -> true
	;  assertz(tabling:'$tbl_incremental_spec'(Name, Arity)),
	   '$tbl_specs_declared',
	   '$tbl_incremental_declared'
	).
table_option(shared, Name, Arity) :- !,
	(  '$tbl_shared_spec'(Name, Arity) -> true
	;  assertz(tabling:'$tbl_shared_spec'(Name, Arity)),
	   '$tbl_specs_declared'
	).
table_option(Option, _, _) :-
	throw(error(domain_error(table_option, Option), (table)/1)).

% The Name/Arity a table spec ultimately names, for the option clauses
% above - they have to record the spec BEFORE handing off to the
% clauses that actually build the wrapper.

table_spec_name_arity(Name//Arity, Name, Arity2) :-
	atom(Name), integer(Arity), Arity >= 0, !,
	Arity2 is Arity + 2.
table_spec_name_arity(Name/Arity, Name, Arity) :-
	atom(Name), integer(Arity), Arity >= 0, !.
table_spec_name_arity(Spec, Name, Arity) :-
	compound(Spec), !,
	functor(Spec, Name, Arity).
table_spec_name_arity(Spec, _, _) :-
	throw(error(type_error(predicate_indicator, Spec), (table)/1)).

% Answer subsumption (item 2 - DESIGN-tabling-phase2.md): a mode spec
% like path(_,_,min) - same functor/arity as the predicate itself, so
% it can never be confused with Name/Arity or Name//Arity above, which
% require the OUTER functor to literally be '/' or '//'. Excluding
% those shapes here too (and `as`/2, handled just above) means a
% malformed Name/Arity (eg. foo/bar, Arity not an integer) still falls
% through to no clause matching at all - today's behaviour - rather
% than being silently misread as a mode spec for a predicate
% confusingly named '/'.
%
% The modes are SWI's. An argument written `_`, `index` or `+` is part
% of the dedup KEY; the others are moded, and a new answer for a key
% joins the stored one in each: `first` (or `-`) keeps the stored
% value, `last` takes the new one, `min` and `max` compare in standard
% order, `sum` adds, lattice(PI) calls PI(Old, New, Joined) and po(PI)
% keeps the old value if PI(Old, New) holds. Anything else is a
% domain_error, where it used to be taken as part of the key. No moded
% argument (eg. path(_,_,_), pure documentation of arity) is plain
% tabling, matching SWI's leniency. A moded argument must be unbound
% when called, as in SWI: bound, it would make a table of its own and
% answer as if that value were the best.

wrappers(Spec) -->
	{ compound(Spec),
	  functor(Spec, Name, Arity),
	  \+ (Name == (/), Arity == 2),
	  \+ (Name == (//), Arity == 2),
	  \+ (Name == (as), Arity == 2),
	  !,
	  functor(Test, Name, Arity) },
	(  { '$tabled'(Test) } ->
	   []
	;  { Spec =.. [Name|SpecArgs],
	     table_modes(SpecArgs, 1, Modes) },
	   (  { Modes == [] } ->
	      wrappers(Name/Arity)
	   ;  { moded_spec(Name, Arity, Modes) },
	      moded_wrapper(Name, Arity, Modes)
	   )
	).

table_modes([], _, []).
table_modes([A|As], I, Modes) :-
	I1 is I + 1,
	(  index_mode(A) ->
	   Modes = Rest
	;  table_mode(A, M) ->
	   Modes = [I-M|Rest]
	;  throw(error(domain_error(tabled_mode, A), (table)/1))
	),
	table_modes(As, I1, Rest).

index_mode(A) :- var(A), !.
index_mode(index).
index_mode(+).

table_mode(M, _) :- var(M), !, fail.
table_mode(first, first).
table_mode(-, first).
table_mode(last, last).
table_mode(min, min).
table_mode(max, max).
table_mode(sum, sum).
table_mode(lattice(PI), lattice(G)) :- mode_pred(PI, 3, lattice_arity, G).
table_mode(po(PI), po(G)) :- mode_pred(PI, 2, po_arity, G).

% The predicate a lattice/1 or po/1 mode names, as Module:Name: given as
% Name, Name/Arity, a head of that arity, or any of these qualified. A
% wrong arity is SWI's domain_error.

mode_pred(PI, _, _, _) :- var(PI), !, fail.
mode_pred(M:PI, N, Kind, M:Name) :- !, atom(M), mode_pred(PI, N, Kind, _:Name).
mode_pred(PI, N, Kind, M:Name) :-
	(  atom(PI) -> Name = PI
	;  PI = Name/N0 -> atom(Name), integer(N0)
	;  compound(PI), functor(PI, Name, N0)
	),
	(  var(N0) -> true
	;  N0 == N -> true
	;  throw(error(domain_error(Kind, N0), (table)/1))
	),
	(  prolog_load_context(module, M) -> true ; M = user ).

% Mask has a bit per moded argument. C does a single first, last, min or
% max itself and leaves the rest to '$tbl_moded_join'.

moded_spec(Name, Arity, Modes) :-
	mode_mask(Modes, 0, Mask),
	(  Modes = [_-M], memberchk(M, [first, last, min, max]) -> Op = M ; Op = join ),
	assertz(tabling:'$tbl_moded_spec'(Name, Arity, Mask, Op, Modes)),
	'$tbl_specs_declared'.

mode_mask([], Mask, Mask).
mode_mask([P-_|Modes], Mask0, Mask) :-
	(  P > 62 -> throw(error(representation_error(moded_argument_position), (table)/1)) ; true ),
	Mask1 is Mask0 \/ (1 << (P - 1)),
	mode_mask(Modes, Mask1, Mask).

% Plain tabling's wrapper, which first checks the moded arguments are
% unbound. See wrappers(Name/Arity) above for why the marker comes last.

moded_wrapper(Name, Arity, Modes) -->
	{ functor(Head, Name, Arity),
	  atom_concat(Name, ' tabled', WrapName),
	  Head =.. [Name|Args],
	  WrappedHead =.. [WrapName|Args],
	  findall(P, member(P-_, Modes), Ps),
	  moded_args(Ps, Head, Vs),
	  vars_test(Vs, Test) },
	[ (Head :- (Test -> true ; tabling:moded_unbound(Vs, Name/Arity)), tabling:start_tabling(Head, WrappedHead)),
	  tabling:'$tabled'(Head) ].

moded_args([], _, []).
moded_args([P|Ps], Head, [V|Vs]) :-
	arg(P, Head, V),
	moded_args(Ps, Head, Vs).

% The check inline, as SWI has it: var/1 is all a correct call pays.

vars_test([V], var(V)) :- !.
vars_test([V|Vs], (var(V), Test)) :-
	vars_test(Vs, Test).

moded_unbound([], _).
moded_unbound([V|Vs], PI) :-
	(  var(V) -> moded_unbound(Vs, PI)
	;  throw(error(uninstantiation_error(V), PI))
	).

% '$tbl_moded_join'(+Handle, +New, +Old): continued from '$tbl_add_answer'
% when New arrives for a key that already holds Old in a table whose
% modes C does not join itself. Replaces Old if the join differs from
% it, and fails either way, as '$tbl_add_answer' does: a moded call is
% never ground, so its producer always goes on.

'$tbl_moded_join'(T, New, Old) :-
	functor(New, Name, Arity),
	'$tbl_moded_spec'(Name, Arity, _, _, Modes),
	New =.. [F|Args],
	join_args(Args, 1, Modes, Old, Joined0),
	Joined =.. [F|Joined0],
	Joined \== Old,
	'$tbl_moded_replace'(T, Joined),
	fail.

join_args([], _, _, _, []).
join_args([A|As], I, Modes, Old, [J|Js]) :-
	(  memberchk(I-M, Modes) ->
	   arg(I, Old, O),
	   mode_join(M, O, A, J)
	;  J = A
	),
	I1 is I + 1,
	join_args(As, I1, Modes, Old, Js).

mode_join(first, O, _, O).
mode_join(last, _, N, N).
mode_join(min, O, N, J) :- ( O @< N -> J = O ; J = N ).
mode_join(max, O, N, J) :- ( O @> N -> J = O ; J = N ).
mode_join(sum, O, N, J) :- J is O + N.
mode_join(lattice(G), O, N, J) :- call(G, O, N, J), !.
mode_join(po(G), O, N, J) :- ( call(G, O, N) -> J = O ; J = N ).

% Active from here on - keep these clauses LAST.

user:term_expansion((:- table Preds), Clauses) :-
	nonvar(Preds),
	phrase(wrappers(Preds), Clauses).
user:term_expansion((Head :- Body), (NewHead :- Body)) :-
	nonvar(Head),
	rename_head(Head, NewHead).
% Module-qualified clause heads of tabled predicates (eg. user:q(a)).
% The qualifier is STRIPPED from the renamed clause: M:Head clauses get
% their bodies wrapped in a module-qualify barrier by the loader, and a
% tabled call suspending under that barrier truncates the captured
% continuation (same unsupported class as suspending in an if-then-else
% condition). Stripping is exact when M is the loading module - the
% overwhelmingly common case - and cross-module definition of tabled
% predicates is unsupported regardless (table identity is not
% module-keyed).
user:term_expansion((M:Head :- Body), (NewHead :- Body)) :-
	nonvar(M), nonvar(Head),
	rename_head(Head, NewHead).
user:term_expansion(M:Head, NewHead) :-
	nonvar(M), nonvar(Head),
	Head \= (_ :- _),
	rename_head(Head, NewHead).
user:term_expansion(Head, NewHead) :-
	nonvar(Head),
	Head \= (:- _), Head \= (_ :- _),
	rename_head(Head, NewHead).
