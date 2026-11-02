% An enumeration of a table's answers that outlives the table: the
% table is dropped by a change and read again, or abolished, while an
% engine or the query itself is still walking its answers. The table
% is retired rather than freed, and the enumeration goes on with the
% old answers, as SWI's does. Each case was a use after free or an
% error before. Results match SWI. Engines have had tables of their own
% since (tabling_engines.pl), so 1, 2, 6 and 7 now check that neither
% side disturbs the other's.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

:- dynamic(level/1).
:- incremental(level/1).
:- table counted_to/1 as incremental.

level(0).

counted_to(X) :- level(B), between(1, 50, K), X is B*100+K.

next_level :-
	retract(level(B)),
	B1 is B+1,
	assertz(level(B1)).

reset_level :-
	retractall(level(_)),
	assertz(level(0)),
	abolish_all_tables.

check(Name, Got, Want) :-
	(	Got == Want ->
		write(Name), write(': ok')
	;	write(Name), write(': FAILED '), write(Got)
	),
	nl.

% 1. An engine enumerates the table; the query reads it again after a
% change. The drop used to see only the query's own choicepoints, so it
% freed the answers the engine was walking.

test_engine_enumerates :-
	reset_level,
	engine_create(X, counted_to(X), E),
	engine_next(E, A1),
	next_level,
	findall(Y, counted_to(Y), Ys), length(Ys, N),
	engine_next(E, A2),
	engine_next(E, A3),
	engine_destroy(E),
	check('engine enumerates', [A1,N,A2,A3], [1,50,2,3]).

% 2. The other way round: the query enumerates, an engine reads it
% again after a change.

test_engine_rereads :-
	reset_level,
	findall(X-N,
		(	counted_to(X), X < 3, next_level,
			engine_create(Ys, findall(Y, counted_to(Y), Ys), E),
			engine_next(E, Ys), engine_destroy(E),
			length(Ys, N)
		), L),
	check('engine rereads', L, [1-50,2-50]).

% 3. abolish_all_tables/0 inside a loop over a table's answers. The next
% answer used to raise type_error(table_handle, _).

test_abolish_all :-
	reset_level,
	findall(X, ( counted_to(X), X < 3, abolish_all_tables ), L),
	check('abolish_all_tables', L, [1,2]).

% 4. abolish_table/1 of the predicate being enumerated, the same.

test_abolish_table :-
	reset_level,
	findall(X, ( counted_to(X), X < 3, abolish_table(counted_to/1) ), L),
	check('abolish_table', L, [1,2]).

% 5. abolish_table/1 of another predicate used to stop an unrelated
% enumeration after its next answer.

:- table p/1, r/1.

p(X) :- between(1, 5, X).
r(X) :- between(1, 3, X).

test_abolish_other :-
	abolish_all_tables,
	findall(X-Y, ( p(X), abolish_table(r/1), r(Y), Y < 2 ), L),
	check('abolish_table of another', L, [1-1,2-1,3-1,4-1,5-1]).

% 6. An engine enumerates; abolish_all_tables/0 and a new table for the
% variant come between its answers.

test_engine_abolish_all :-
	reset_level,
	engine_create(X, counted_to(X), E),
	engine_next(E, A1),
	abolish_all_tables,
	findall(Y, counted_to(Y), Ys), length(Ys, N),
	engine_next(E, A2),
	engine_destroy(E),
	check('engine over abolish_all_tables', [A1,N,A2], [1,50,2]).

% 7. Many engines, each left part way through a table that is then
% replaced, and destroyed in turn: the retired tables are freed as the
% engines go, and the reads stay right.

test_many_engines :-
	reset_level,
	findall(E,
		(	between(1, 20, _),
			engine_create(X, counted_to(X), E),
			engine_next(E, _),
			next_level,
			findall(_, counted_to(_), _)
		), Es),
	findall(A, ( member(E, Es), engine_next(E, A), engine_destroy(E) ), As),
	findall(Y, counted_to(Y), Ys),
	length(Ys, N), last(Ys, Last),
	numlist(0, 19, Bs), findall(A, ( member(B, Bs), A is B*100+2 ), Want),
	check('many engines', As-N-Last, Want-50-2050).

main :-
	test_engine_enumerates,
	test_engine_rereads,
	test_abolish_all,
	test_abolish_table,
	test_abolish_other,
	test_engine_abolish_all,
	test_many_engines.
