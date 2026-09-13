% A frame that binds its own variables, returns and is recovered leaves
% their trail entries behind. When a smaller frame was later pushed at the
% same index and an outer variable's entry sat on top, backtracking looked
% the stale entries up past the smaller frame's slots, landed on the slots
% at the bottom of the stack, and unbound variables there.

:- initialization(main).

p :- A = 1, B = 2, C = 3, D = 4, E = 5, F = 6, A+B+C+D+E+F > 0.
q(_).

t_pair(R) :- X = 1, Y = 2, ( member(_, [a,b]), p, Z = z, q(Z), fail ; true ), R = X-Y.

t_atom(R) :- X = hello, ( member(_, [a,b,c]), p, W = w, q(W), fail ; true ), ( atom(X) -> R = X ; R = lost(X) ).

main :-
	forall(
		member(T, [t_pair, t_atom]),
		(	G =.. [T, R],
			(	catch(G, E, R = uncaught(E)) -> true ; R = failed ),
			write(T), write(': '), writeq(R), nl
		)
	).
