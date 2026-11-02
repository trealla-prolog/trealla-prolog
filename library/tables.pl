/**
SWI's library(tables): XSB's predicates for inspecting and abolishing
tables, over library(tabling). As in SWI, a table's answers are read
through ret/N of its call's variables, moded arguments last, and a
delay list is a list of goals, the latest first.
*/

:- module(tables, [
	abolish_all_tables/0, abolish_module_tables/1, abolish_table_pred/1,
	abolish_table_call/1, abolish_table_call/2, abolish_table_subgoals/2,
	tfindall/3, 't not'/1,
	get_call/3, get_calls/3, get_returns/2,
	get_returns_and_dls/3, get_returns_and_tvs/3,
	get_returns_for_call/2, get_residual/2,
	set_pil_on/0, set_pil_off/0,
	op(900, fy, tnot)]).

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- meta_predicate(abolish_table_pred(:)).
:- meta_predicate(abolish_table_call(:)).
:- meta_predicate(abolish_table_call(:, +)).
:- meta_predicate(abolish_table_subgoals(:, +)).
:- meta_predicate(tfindall(+, 0, -)).
:- meta_predicate('t not'(0)).
:- meta_predicate(get_call(:, -, -)).
:- meta_predicate(get_calls(:, -, -)).
:- meta_predicate(get_returns_for_call(:, :)).
:- meta_predicate(get_returns_and_dls(+, -, :)).
:- meta_predicate(get_residual(:, -)).

abolish_all_tables :-
	tabling:abolish_all_tables.

% Tables are not keyed by module here: a module's are those of the
% predicates it defines.

abolish_module_tables(M) :-
	must_be(atom, M),
	'$tbl_handles'(Hs),
	forall(( member(T, Hs), '$tbl_variant'(T, V), functor(V, N, A),
	         current_predicate(M:N/A), \+ predicate_property(M:V, imported_from(_)) ),
	       '$tbl_abolish_table'(T)).

abolish_table_pred(PI0) :-
	strip_module(PI0, _, PI),
	(  nonvar(PI), PI = N/A, atom(N), integer(A) -> functor(H, N, A)
	;  callable(PI) -> functor(PI, N, A), functor(H, N, A)
	;  throw(error(type_error(callable_or_predicate_indicator, PI0), abolish_table_pred/1))
	),
	abolish_table_subgoals(H).

abolish_table_call(Head) :-
	abolish_table_subgoals(Head).

abolish_table_call(Head, Options) :-
	abolish_table_subgoals(Head, Options).

% Both options abolish only the tables named, as in SWI.

abolish_table_subgoals(Head, Options) :-
	must_be(list, Options),
	(  Options == [] -> true
	;  memberchk(abolish_tables_transitively, Options) -> true
	;  memberchk(abolish_tables_singly, Options) -> true
	;  throw(error(domain_error([abolish_tables_transitively,abolish_tables_singly], Options), abolish_table_subgoals/2))
	),
	abolish_table_subgoals(Head).

tfindall(Template, Goal, Answers) :-
	findall(Template, Goal, Answers).

't not'(Goal) :-
	tnot(Goal).

set_pil_on.
set_pil_off.

% get_call/3 finds the table of a variant of CallTerm, get_calls/3 each
% one whose call unifies with it.

get_call(Goal0, T, Ret) :-
	strip_module(Goal0, _, Goal),
	current_table(Goal, T),
	tabling:tbl_return(Goal, Ret).

get_calls(Goal0, T, Ret) :-
	strip_module(Goal0, _, Goal),
	'$tbl_handles'(Hs0),
	reverse(Hs0, Hs),
	member(T, Hs),
	'$tbl_variant'(T, V),
	V = Goal,
	tabling:tbl_return(V, Ret).

get_returns(T, Ret) :-
	'$tbl_variant'(T, V),
	tabling:tbl_return(V, Ret),
	tabling:tbl_answer(T, V, _).

get_returns_and_tvs(T, Ret, TV) :-
	'$tbl_variant'(T, V),
	tabling:tbl_return(V, Ret),
	tabling:tbl_answer(T, V, Es),
	(  Es == [] -> TV = t ; TV = u ).

get_returns_and_dls(T, Ret, DLs0) :-
	strip_module(DLs0, _, DLs),
	'$tbl_variant'(T, V),
	tabling:tbl_return(V, Ret),
	tabling:tbl_answer(T, V, Es),
	tabling:tbl_delay_lists(Es, DLs).

% As in SWI, read as a call would, so a conditional answer makes the
% caller's conditional too.

get_returns_for_call(Call0, Answer0) :-
	strip_module(Call0, _, Call),
	strip_module(Answer0, _, Answer),
	current_table(Call, T),
	'$tbl_variant'(T, V),
	'$tbl_get_answer'(T, V),
	Answer = V.

% Each answer of a table whose call unifies with CallTerm, once for each
% of its delay lists; [] for one that holds outright.

get_residual(Goal0, DL) :-
	strip_module(Goal0, _, Goal),
	'$tbl_handles'(Hs0),
	reverse(Hs0, Hs),
	member(T, Hs),
	'$tbl_variant'(T, V),
	V = Goal,
	tabling:tbl_answer(T, V, Es),
	tabling:tbl_delay_lists(Es, DLs),
	(  DLs == [] -> DL = [] ; member(DL, DLs) ).
