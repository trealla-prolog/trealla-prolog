% A last call reused its caller's frame while the caller's clause still had an alternative, so a cut in the callee removed that alternative.

:- initialization(main).

g03 :- g03a.
g03.
g03a :- member(_, [a,b]), !.

h03 :- h03a.
h03.
h03a :- between(1, 2, _), !.

k03 :- true, k03a.
k03.
k03a :- ( true ; true ), !.

p03(X) :- p03a(X).
p03(9).
p03a(X) :- between(1, 2, X), !.

r03(X) :- X = 1, r03a.
r03(9).
r03a :- ( true ; true ), !.

count(G, N) :- findall(x, G, L), length(L, N).

main :-
	forall(member(G, [g03, h03, k03]), (count(G, N), write(G), write(': '), write(N), nl)),
	findall(X, p03(X), LP), write('p03: '), writeq(LP), nl,
	findall(X, r03(X), LR), write('r03: '), writeq(LR), nl.
