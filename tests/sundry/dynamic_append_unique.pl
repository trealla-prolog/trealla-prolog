% A dynamic predicate whose clauses loaded from the file were all rules
% lost the clauses asserted after them: check_unique() had marked the
% lone rule unique when the load ended, nothing cleared that on assertz,
% and a call took the rule as its last clause, leaving no choicepoint for
% the rest. Results match SWI.

:- initialization(main).

:- dynamic(t/2).
t(X, Y) :- e(X, Y).
e(1, 2).

:- dynamic(u/1).
u(X) :- X = 1.

main :-
	assertz(t(1, 9)), assertz(t(5, 5)),
	findall(Q, t(1, Q), L1), write(L1), nl,
	findall(P-Q, t(P, Q), L2), write(L2), nl,
	(t(5, 5) -> write(yes) ; write(no)), nl,
	assertz(u(2)),
	findall(X, u(X), L3), write(L3), nl,
	(u(2) -> write(yes) ; write(no)), nl.
