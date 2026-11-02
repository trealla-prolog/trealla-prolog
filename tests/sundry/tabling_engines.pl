% An engine has tables of its own, as in SWI. It used the main thread's,
% from whichever thread ran it, so an engine reading a table its creator
% was still computing found no answers in it, a query reading a table an
% engine had left part-built found none either, and an engine in another
% thread raced with the main thread's tables. Results match SWI.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

runs(N) :- flag(runs, N, N).

% 1. Each engine computes its own tables, wherever it is created.

:- table counted/1.
counted(X) :- flag(runs, N, N+1), between(1, 3, X).

test_own_tables :-
	findall(X, counted(X), _), runs(N1),
	engine_create(X, counted(X), E), engine_next(E, _), engine_destroy(E), runs(N2),
	thread_create((findall(X, counted(X), _), runs(N3), thread_send_message(main, n(N3))), T1),
	thread_get_message(n(N3)), thread_join(T1, _),
	thread_create((engine_create(X, counted(X), E2), engine_next(E2, _), engine_destroy(E2),
		runs(N4), thread_send_message(main, n(N4))), T2),
	thread_get_message(n(N4)), thread_join(T2, _),
	write('own tables': [N1, N2, N3, N4]), nl.

% 2. An engine reading the table its creator is computing computes its
% own, rather than finding the creator's empty.

:- table p/1.
p(X) :- member(X, [1,2,3]).
p(X) :- flag(depth, D, D), D < 1, flag(depth, _, D+1),
	engine_create(Ys, findall(Y, p(Y), Ys), E), engine_next(E, Ys), engine_destroy(E),
	length(Ys, N), X is 10+N.

test_creator_computing :-
	findall(X, p(X), L), msort(L, S),
	write('creator computing': S), nl.

% 3. current_table/2 in an engine lists its tables only.

:- table r/1, s/1.
r(X) :- between(1, 3, X).
s(X) :- between(1, 2, X).

tables(PIs) :-
	findall(N/A, (current_table(V, _), functor(V, N, A), memberchk(N, [r,s])), PIs0),
	msort(PIs0, PIs).

test_current_table :-
	findall(X, r(X), _),
	engine_create(V0-V1, (tables(V0), s(_), tables(V1)), E),
	engine_next(E, V0-V1), engine_destroy(E),
	tables(Main),
	write('current_table': [V0, V1, Main]), nl.

% 4. An engine yielding inside a tabled evaluation leaves its table
% part-built; the query computes its own meanwhile, and destroying an
% engine there frees its tables.

:- table y/1.
y(X) :- between(1, 3, X).
y(X) :- engine_self(_), engine_yield(mid), y(Z), Z < 3, X is Z + 10.
y(X) :- y(Z), Z > 10, Z < 12, X is Z + 100.

test_yield :-
	engine_create(X, y(X), E), engine_next(E, A1),
	findall(Y, y(Y), Ys0), msort(Ys0, Ys),
	findall(Z, engine_next(E, Z), Zs0), msort(Zs0, Zs),
	engine_create(W, y(W), E2), engine_next(E2, B1), engine_destroy(E2),
	engine_create(W2, y(W2), E3), engine_next(E3, C1),
	findall(Y, y(Y), Ys2), length(Ys2, N2),
	engine_destroy(E3),
	write(yield: [A1, Ys, Zs, B1, C1, N2]), nl.

% 5. abolish_all_tables/0 in an engine leaves the query's tables.

test_abolish :-
	flag(runs, _, 0),
	findall(X, counted(X), _),
	engine_create(x, abolish_all_tables, E), engine_next(E, _), engine_destroy(E),
	findall(X, counted(X), _), runs(N),
	write(abolish: N), nl.

main :-
	test_own_tables,
	test_creator_computing,
	test_current_table,
	test_yield,
	test_abolish.
