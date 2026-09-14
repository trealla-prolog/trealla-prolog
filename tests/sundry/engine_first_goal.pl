% An engine began its goal holding back the advance that only call/N wants, so
% the goal's first builtin ran twice.

:- initialization(main).
:- dynamic(seen/1).

g :- write(a), write(b).

main :-
	engine_create(x, write(once), E1), engine_next(E1, _), engine_destroy(E1), nl,
	engine_create(x, (write(a), write(b)), E2), engine_next(E2, _), engine_destroy(E2), nl,
	engine_create(x, g, E3), engine_next(E3, _), engine_destroy(E3), nl,
	engine_create(x, assertz(seen(x)), E4), engine_next(E4, _), engine_destroy(E4),
	findall(S, seen(S), Ss), length(Ss, C), write(seen(C)), nl.
