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
	abolish_all_tables/0, abolish_table/1,
	incremental/1,
	op(1150, fx, table)]).

:- use_module(library(dcgs)).
:- use_module(library(lists)).

:- dynamic('$tabled'/1).
:- dynamic('$tbl_subsumptive_spec'/4).
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
	(  '$tbl_subsumptive_spec'(Name, Arity, Pos, Op) ->
	   '$tbl_set_subsumptive'(T, Pos, Op)
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
	run_scc(T, Wrapper, Worker).

run_scc(T, Wrapper, Worker) :-
	'$tbl_push_scc'(T),
	catch(( activate(T, Wrapper, Worker),
	        completion
	      ), Ball,
	      ( '$tbl_reset_incomplete', '$tbl_pop_scc'(_), throw(Ball) )),
	'$tbl_pop_scc'(Escaped),
	run_scc_(Escaped, T, Wrapper).

run_scc_(true, T, Wrapper) :-
	shift(call_info(Wrapper, T)).
run_scc_(false, T, Wrapper) :-
	'$tbl_get_answer'(T, Wrapper).

activate(T, Wrapper, Worker) :-
	'$tbl_set_status'(T, active),
	(  delim(T, Wrapper, Worker),
	   fail
	;  true
	).

% One producer step. A completed worker records an answer
% ('$tbl_add_answer' FAILS on duplicates, driving the loop); a shifted
% worker records a dependency on the table it suspended on. dep/4 packs
% SourceCall, continuation and target wrapper in ONE term so they keep
% sharing variables through the image copy.
%
% Exceptions raised by a worker fail that producer branch, as in the
% reference (Desouter) library. Eg. a worker calling setof/3 with its
% output argument bound to a non-list: Trealla's ISO-strict setof
% throws where lenient systems fail, and tabling must not surface a
% difference.

delim(T, Wrapper, Worker) :-
	catch(reset(Worker, Ball, Cont), _, ('$tbl_note_exception', fail)),
	(  Cont == none ->
	   '$tbl_add_answer'(T, Wrapper)
	;  Cont = cont(C),
	   Ball = call_info(_, SrcT),
	   '$tbl_add_suspension'(SrcT, dep(Ball, C, Wrapper, T))
	).

% Run to fixpoint: drain tables until no work remains, then complete
% every table created under this leader.

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
	   '$tbl_specs_declared'
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
% Exactly one argument may be `min` or `max` (aggregated); every other
% argument, `_` by convention, is part of the dedup KEY. Recommended by
% the design doc as the first cut: monotone, so the fixpoint argument
% stays simple - a general user-supplied lattice is not attempted.
% Zero markers (eg. path(_,_,_), pure documentation of arity) degrades
% to plain tabling, matching SWI's leniency.

wrappers(Spec) -->
	{ compound(Spec),
	  functor(Spec, Name, Arity),
	  \+ (Name == (/), Arity == 2),
	  \+ (Name == (//), Arity == 2),
	  \+ (Name == (as), Arity == 2),
	  !,
	  functor(Test, Name, Arity),
	  ( '$tabled'(Test) -> true
	  ; Spec =.. [Name|SpecArgs],
	    agg_positions(SpecArgs, 1, AggPositions),
	    ( AggPositions = [] ->
	         true
	    ; AggPositions = [Pos-Op] ->
	         % Keyed on the ORIGINAL Name/Arity, not the renamed
	         % 'Name tabled' wrapper: start_tabling(Head, WrappedHead)
	         % passes the user-facing Head as Wrapper and the renamed
	         % goal as Worker - the other way round from what those
	         % variable names might suggest - and apply_subsumption_spec
	         % below looks it up by functor(Wrapper, ...).
	         assertz(tabling:'$tbl_subsumptive_spec'(Name, Arity, Pos, Op)),
	         '$tbl_specs_declared'
	    ; throw(error(domain_error(table_mode_spec, Spec), (table)/1))
	    )
	  ) },
	wrappers(Name/Arity).

agg_positions([], _, []).
agg_positions([A|As], I, Positions) :-
	I1 is I + 1,
	(  A == min -> Positions = [I-min|Rest]
	;  A == max -> Positions = [I-max|Rest]
	;  Positions = Rest
	),
	agg_positions(As, I1, Rest).

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
