% engine_create/3 gave the engine variables numbered in the caller's frame, so
% answers came out wrong, and from 998 variables on it read past the engine's slots.

:- initialization(main).

check(G) :-
	(	catch(G, E, (write(G), write(' THREW '), writeq(E), nl, fail))
	->	true
	;	write(G), write(' FAILED'), nl
	).

many(N) :-
	length(L, N),
	engine_create(L, maplist(=(x), L), E),
	engine_next(E, R),
	engine_destroy(E),
	length(R, Len),
	last(R, X),
	write(many(N, Len, X)), nl.

answers :-
	engine_create(X, member(X, [a,b,c]), E),
	engine_next(E, A1),
	engine_next(E, A2),
	engine_next(E, A3),
	\+ engine_next(E, _),
	engine_destroy(E),
	write(answers([A1,A2,A3])), nl.

shared :-
	engine_create(X-Y, (X = 1, Y = X), E),
	engine_next(E, R),
	engine_destroy(E),
	write(shared(R)), nl.

bound_in_caller :-
	X = 21,
	engine_create(Y, Y is X * 2, E),
	engine_next(E, R),
	engine_destroy(E),
	write(bound_in_caller(R)), nl.

untouched :-
	length(L, 3),
	engine_create(L, maplist(=(x), L), E),
	engine_next(E, _),
	engine_destroy(E),
	(	maplist(var, L)
	->	write(untouched), nl
	;	write(bound(L)), nl
	).

main :-
	check(many(1)),
	check(many(2)),
	check(many(3)),
	check(many(997)),
	check(many(998)),
	check(many(3000)),
	check(answers),
	check(shared),
	check(bound_in_caller),
	check(untouched).
