% Standard order put a bigint or rational after any atom, string or compound when it was on the left, so
% compare/3 gave > both ways round, a negative bigint came after a rational both ways round, and
% list_to_assoc/2 refused mixed keys as not unique. A float comes first, then integers and rationals by value.

:- initialization(main).
:- use_module(library(assoc)).

c(X, Y) :- compare(O1, X, Y), compare(O2, Y, X), writeq(c(X, Y) = O1/O2), nl.

main :-
	B is 2^70, N is -(2^70), R is 1 rdiv 3, H is (2^71+1) rdiv 2,
	c(B, a), c(B, f(x)), c(B, "s"), c(B, 1.0e30), c(B, 1), c(N, R), c(B, R), c(B, H),
	c(R, a), c(R, f(x)), c(R, "s"), c(R, 0.5), c(R, 0), c(R, 1),
	Q is 1 rdiv 2, M is -1 rdiv 3,
	msort([1, Q, 0.5, B, 1.0e30, M, a, f(x), 0, N, g(y, z)], L), writeq(L), nl,
	Ks = [a, B, Q, f(x), 0, N, b, g(y), 1.5],
	findall(K-I, nth1(I, Ks, K), Ps), list_to_assoc(Ps, T),
	findall(V, (member(K, Ks), get_assoc(K, T, V)), Vs), writeq(Vs), nl.
