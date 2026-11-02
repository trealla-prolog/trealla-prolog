% copy_term/2 kept a cycle back to the whole term, but cut one closing on an inner subterm or reached through an
% argument to a fresh variable, so P = g(h(P)), copy_term(x(P), C) gave the acyclic x(g(h(_))).

:- use_module(library(freeze)).
:- initialization(main).

try(Name, Goal) :-
	write(Name), write(': '),
	catch((Goal -> write(ok) ; write(failed)), error(E, _), write(E)), nl.

main :-
	try(inner, (P = g(Q), Q = h(P), copy_term(x(P), x(C)), C = g(h(C1)), C1 == C)),
	try(inner_eq, (P2 = g(h(P2)), copy_term(x(P2), x(C2)), C2 == P2)),
	try(deep, (D = a(b(c(d(D)))), copy_term(w(D), w(F)), F = a(b(c(d(F1)))), F1 == F)),
	try(list_head, (L = [a|L], copy_term(f([L]), f([C3])), C3 = [a|C4], C4 == C3)),
	try(list_tail, (L2 = [a,b|L2], copy_term(f(L2), f(C5)), C5 = [a,b|C6], C6 == C5)),
	try(two_cycles, (A = f(A), B = g(B), copy_term(p(A, B), p(CA, CB)), CA = f(CA1), CA1 == CA, CB = g(CB1), CB1 == CB)),
	try(shared_cycle, (A2 = f(A2), copy_term(p(A2, A2), p(E1, E2)), E1 == E2, E1 = f(E3), E3 == E1)),
	try(cycle_and_var, (A3 = f(A3, V), copy_term(x(A3, V), x(G1, W)), G1 = f(G2, W2), G2 == G1, W2 == W, var(W), W \== V)),
	try(cycle_and_frozen, (A4 = f(A4, V2), freeze(V2, true), copy_term(x(A4), x(H1)), H1 = f(H2, _), H2 == H1)),
	try(root_bound, (R = f(R), copy_term(R, f(Y)), Y = f(Y1), Y1 == Y)),
	try(root_var, (R2 = f(R2), copy_term(R2, Z), Z = f(Z1), Z1 == Z)),
	try(nat, (P3 = g(h(P3)), copy_term_nat(x(P3), x(C7)), C7 = g(h(C8)), C8 == C7)),
	try(acyclic_shared, (S = [1,2], copy_term(t(S, S, [S|S]), T), T == t(S, S, [S|S]))),
	try(fresh_vars, (copy_term(f(X1, X1, X2), f(K1, K2, K3)), K1 == K2, K1 \== K3, var(K1), K1 \== X1)).
