% An engine runs a goal on its own stacks and hands back its
% answers one at a time, so an endless generator costs only
% what is asked of it.
naturals(N) :- naturals_from(0, N).

naturals_from(N, N).
naturals_from(N0, N) :- N1 is N0 + 1, naturals_from(N1, N).

prime(P) :-
	naturals(P), P >= 2,
	\+ ( between(2, P, D), D * D =< P, P mod D =:= 0 ).

take(0, _, []) :- !.
take(K, E, [X|Xs]) :-
	engine_next(E, X),
	K1 is K - 1,
	take(K1, E, Xs).

first_primes(K, Ps) :-
	engine_create(P, prime(P), E),
	take(K, E, Ps),
	engine_destroy(E).

% Two generators side by side; each picks up where it left off.
primes_and_squares(Ps, Ss, More) :-
	engine_create(P, prime(P), E1),
	engine_create(S, (naturals(N), S is N * N), E2),
	take(5, E1, Ps), take(5, E2, Ss), take(5, E1, More),
	engine_destroy(E1), engine_destroy(E2).
%?- first_primes(10, Ps).
%?- primes_and_squares(Ps, Ss, More).
