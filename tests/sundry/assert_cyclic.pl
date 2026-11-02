% A cyclic term was asserted with its cycle cut to a fresh variable, so X = f(X), assertz(g(X)) stored g(f(_)).
% It is refused as SWI does, while acyclic terms sharing subterms still go in whole.

:- initialization(main).

:- dynamic(s/1).

try(Name, Goal) :-
	write(Name), write(': '),
	catch((Goal -> write(ok) ; write(failed)), error(E, _), write(E)), nl.

main :-
	try(asserta_1, (X1 = f(X1), asserta(a(X1)))),
	try(assertz_1, (X2 = f(X2), assertz((b :- c(X2))))),
	try(asserta_2, (X3 = [a|X3], asserta(c(X3), _))),
	try(assertz_2, (X4 = [a|X4], assertz(c(X4), _))),
	try(inner, (X5 = f(g(X5)), assertz(d(x, [1,2|X5])))),
	try(spine, (L = [1,2|T], T = [3|L], assertz(e(L)))),
	try(recordz, (X6 = f(X6), recordz(k, X6))),
	try(none_created, \+ current_predicate(a/1)),
	try(shared, (B = f(Q, Q), Q = g(R, R), R = h(S, S), S = [1,2,3], assertz(s(B)), s(B2), B2 == B)),
	try(shared_tail, (T1 = [x,y], assertz(s([T1, T1|T1])), s([_,_,x,y]))),
	try(shared_var, (assertz(s(f(P, P, [P|P]))), s(f(m, M2, [M3|M4])), M2 == m, M3 == m, M4 == m)).
