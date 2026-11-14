% A clause runs from its own cells, so nb_setarg/3 on a term written in it
% changed the clause: the next run, or the next pass of a failure-driven loop,
% started from the value set last. SWI builds each run's own term.

:- initialization(main).
:- dynamic(asserted/1).

ground_lit(X) :- N = n(0), arg(1, N, X), nb_setarg(1, N, 5).
nonground_lit(X) :- N = n(Y), Y = 0, arg(1, N, X), nb_setarg(1, N, 5).
loop :- between(1, 3, _), N = n(0), arg(1, N, X), write(X), nb_setarg(1, N, 9), fail.
loop :- nl.
rec(0, []) :- !.
rec(K, [X|Xs]) :- N = n(0), nb_setarg(1, N, K), K1 is K-1, rec(K1, Xs), arg(1, N, X).
meta(X) :- once((N = n(0), arg(1, N, X), nb_setarg(1, N, 5))).
kept(Xs) :- N = n(0), findall(X, ((true ; true), arg(1, N, X), nb_setarg(1, N, 5)), Xs).
count(C) :- N = n(0), forall(member(_, [a,b,c]), (arg(1, N, C0), C1 is C0+1, nb_setarg(1, N, C1))), arg(1, N, C).

main :-
	assertz((asserted(X) :- N = n(0), arg(1, N, X), nb_setarg(1, N, 5))),
	forall(
		member(G, [ground_lit, nonground_lit, rec(3), meta, kept, count, asserted]),
		(	call(G, A), call(G, B),
			writeq(G-[A,B]), nl
		)
	),
	loop, loop.
