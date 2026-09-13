% A caller's variable bound to a structure from a callee's head pinned the
% caller's frame rather than the callee's, so the callee's frame was later
% reused by its own tail call and the caller's term lost its bindings.

:- initialization(main).

% The caller's own variable.
own(R) :- own_(a, R).
own_(a, f(X)) :- X = 1, own_(b, 0).
own_(b, Y) :- Z = 3, Y+Z =:= 3.

% A variable from a frame older than the caller.
older(R) :- older_mid(R).
older_mid(R) :- older_(a, R), true.
older_(a, f(X)) :- X = 1, older_(b, 0).
older_(b, Y) :- Z = 3, Y+Z =:= 3.

% Like numbervars/3: list elements bound to '$VAR'(N) from the head.
number_(L) :- length(L, 3), number_list(L, 0, _).
number_list([], N, N).
number_list(['$VAR'(N0)|Vs], N0, N) :- N1 is N0+1, number_list(Vs, N1, N).

main :-
	forall(
		member(T, [own, older, number_]),
		(	G =.. [T, R],
			(	catch(G, E, R = uncaught(E)) -> true ; R = failed ),
			write(T), write(': '), writeq(R), nl
		)
	).
