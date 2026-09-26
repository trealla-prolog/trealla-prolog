% A clause retracted while a variable still holds one of its compound arguments must outlive that binding:
% it was freed on backtracking to a choicepoint newer than the binding, and the variable read freed cells.

:- dynamic(p/2).

:- initialization(main).

filler(M) :- assertz(p(z, filler(M, overwrite, [x, y, z], g(4, 5, 6)))).

% Retracted by retract/1 itself, with member/2's choicepoint newer than the binding to Y.

t1 :-
	retractall(p(_, _)),
	assertz(p(k, f(a, text, g(1, 2, 3)))),
	(	p(k, Y), member(M, [1, 2, 3]),
		( M == 1 -> retract(p(k, _)) ; true ),
		filler(M), write(t1(M, Y)), nl, fail
	;	true
	).

% Reclaimed when a cut releases the walker, not by retract/1.

t2 :-
	retractall(p(_, _)),
	assertz(p(k, f(b, text, g(7, 8, 9)))), assertz(p(k, other)),
	(	( p(k, Y), retract(p(k, f(_, _, _))) -> true ; true ),
		member(M, [1, 2]), filler(M), write(t2(M, Y)), nl, fail
	;	true
	).

main :- t1, t2.
