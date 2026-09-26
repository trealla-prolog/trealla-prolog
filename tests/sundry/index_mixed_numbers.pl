% An indexed predicate keyed on rationals mixed with floats or bigints lost clauses to lookups,
% because index_cmpkey_() ordered those pairs inconsistently.

:- dynamic(p/1).

:- initialization(main).

mk(rat, I, K) :- K is I rdiv 7.
mk(float, I, K) :- K is I * 1.5.
mk(big, I, K) :- K is 10^30 + I.
mk(int, I, K) :- K = I.

key(A, B, I, K) :- ( I mod 2 =:= 0 -> mk(A, I, K) ; mk(B, I, K) ).

% 600 clauses, past the threshold at which a predicate is indexed.

run(A, B) :-
	retractall(p(_)),
	forall(between(1, 600, I), (key(A, B, I, K), assertz(p(K)))),
	findall(K, (between(1, 600, I), key(A, B, I, K), \+ p(K)), Ks),
	length(Ks, N),
	format("~w/~w: missing ~w~n", [A, B, N]).

main :-
	forall(member(A-B, [rat-float, rat-big, big-float, rat-int, float-int, big-int]), run(A, B)).
