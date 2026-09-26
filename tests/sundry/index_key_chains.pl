% Key chains: an indexed predicate changed while it is being iterated keeps the logical update
% view and database order, through both idx1 and idx2 chains, retract, clause/2 and key churn.

:- dynamic(q/2).
:- dynamic(r/1).

:- initialization(main).

setup :-
	retractall(q(_, _)),
	forall(between(1, 600, I), (A is I mod 3, B is I mod 7, assertz(q(A, B-I)))),
	once(q(0, _)).

firsts(L, N, F) :- length(F0, N), append(F0, _, L), !, F = F0.
firsts(L, _, L).

% idx1 chain, changed mid-walk: the walk sees the clauses as they were when it started.

t1 :-
	setup,
	findall(I, (q(1, _-I), (I =:= 4 -> assertz(q(1, x-1000)), asserta(q(1, x-0)), retract(q(1, _-7)) ; true)), L),
	length(L, N), firsts(L, 4, F),
	findall(I, q(1, _-I), L2), length(L2, N2), firsts(L2, 4, F2), last(L2, Z),
	format("t1 ~w ~w | ~w ~w ~w~n", [N, F, N2, F2, Z]).

% idx2 chain: first argument unbound, second bound.

t2 :-
	setup,
	findall(A-I, (q(A, 3-I), (I =:= 10 -> assertz(q(9, 3-2000)), asserta(q(8, 3-1)), retract(q(_, 3-17)) ; true)), L),
	length(L, N), firsts(L, 3, F),
	findall(A-I, q(A, 3-I), L2), length(L2, N2), firsts(L2, 3, F2), last(L2, Z),
	format("t2 ~w ~w | ~w ~w ~w~n", [N, F, N2, F2, Z]).

% retract/1 walking a key's chain, then the key refilled.

t3 :-
	setup,
	findall(I, retract(q(2, _-I)), L), length(L, N), firsts(L, 3, F),
	( q(2, _) -> E = some ; E = none ),
	assertz(q(2, a-1)), asserta(q(2, b-2)),
	findall(X, q(2, X), L2),
	format("t3 ~w ~w ~w ~w~n", [N, F, E, L2]).

% clause/2 on a chained key, and asserta order.

t4 :-
	setup,
	asserta(q(0, y-(-1))), asserta(q(0, y-(-2))),
	findall(X, clause(q(0, X), true), L), length(L, N), firsts(L, 3, F),
	format("t4 ~w ~w~n", [N, F]).

% A clause with a variable key turns the index off; retracting it turns it back on.

t5 :-
	setup,
	assertz(q(_, v-999)),
	findall(I, q(1, _-I), L1), length(L1, N1),
	retract(q(V, v-999)), var(V),
	findall(I, q(1, _-I), L2), length(L2, N2),
	format("t5 ~w ~w~n", [N1, N2]).

% Mixed key kinds in one index: atoms, integers, floats, compounds and lists.

t6 :-
	retractall(r(_)),
	forall(between(1, 600, I),
		(	M is I mod 5,
			(	M =:= 0 -> K = a
			;	M =:= 1 -> K is I mod 4
			;	M =:= 2 -> K is (I mod 4) * 0.5
			;	M =:= 3 -> K = f(I mod 3)
			;	K = [I mod 2]
			),
			assertz(r(K)))),
	findall(C, (member(K, [a, 1, 1.0, 0.5, f(1), f(_), [1], [_], b]), findall(x, r(K), Xs), length(Xs, C)), Cs),
	format("t6 ~w~n", [Cs]).

% Both key arguments bound: the shorter chain is walked when short, and the composite index serves two
% long ones once enough lookups have asked for it.

:- dynamic(b/3).

t7 :-
	retractall(b(_, _, _)),
	forall(between(1, 600, I), (A is I mod 40, B is I mod 5, assertz(b(A, B, I)))),
	findall(I, b(3, 3, I), S), length(S, NS), firsts(S, 3, FS),
	( b(3, 9, _) -> M = found ; M = none ),
	findall(N, (between(1, 150, J), A is J mod 40, B is J mod 5, findall(x, b(A, B, _), Xs), length(Xs, N)), Ns),
	sum_list(Ns, Total),
	findall(I, (b(7, 2, I), (I =:= 47 -> assertz(b(7, 2, 9000)), retract(b(7, 2, 247)) ; true)), W),
	findall(I, b(7, 2, I), W2),
	format("t7 ~w ~w ~w ~w ~w ~w~n", [NS, FS, M, Total, W, W2]).

% Keys with a single clause: a second clause for one, while it is being walked and not; one retracted
% and its key used again before it has gone; asserta onto one.

:- dynamic(u/2).

t8 :-
	retractall(u(_, _)),
	forall(between(1, 600, I), assertz(u(I, aa))),
	findall(X, (u(5, X), assertz(u(5, bb))), L1),
	findall(X, u(5, X), L2),
	findall(X, (u(7, X), retract(u(7, aa)), assertz(u(7, cc))), L3),
	findall(X, u(7, X), L4),
	asserta(u(9, zz)), findall(X, u(9, X), L5),
	retract(u(11, aa)), assertz(u(11, dd)), asserta(u(11, ee)), findall(X, u(11, X), L6),
	findall(K, (between(1, 20, K), \+ u(K, _)), Missing),
	format("t8 ~w ~w ~w ~w ~w ~w ~w~n", [L1, L2, L3, L4, L5, L6, Missing]).

main :- t1, t2, t3, t4, t5, t6, t7, t8.
