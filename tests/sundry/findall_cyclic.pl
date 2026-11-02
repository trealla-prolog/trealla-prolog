% findall/3 refused a cyclic answer with type_error(acyclic_term), since its queue has no slot to hold a cycle, and
% bagof/3 and setof/3 failed on a cyclic goal. The queue now records where each cycle was cut and the drain rejoins
% it, so both work as in SWI; test0989 has the issue's own cases.

:- initialization(main).

try(Name, Goal) :-
	write(Name), write(': '),
	catch((Goal -> write(ok) ; write(failed)), error(E, _), write(E)), nl.

cyclic(V) :- V0 = f(V0), V = V0.

is_cyclic(V) :- V = f(W), W == V.

p(1). p(2). p(3).

f(_, 1, a).
f(_, 2, b).

first([H|_], H).

main :-
	try(inner, (findall(x(P), P = g(h(P)), [x(C1)]), C1 = g(h(D1)), D1 == C1)),
	try(inner_before, (P2 = g(h(P2)), findall(x(P2), true, [x(C2)]), C2 = g(h(D2)), D2 == C2)),
	try(several, (findall(N-V, (p(N), V = s(N, V)), [1-A1, 2-A2, 3-A3]), A1 = s(1, B1), B1 == A1, A2 = s(2, B2), B2 == A2, A3 = s(3, B3), B3 == A3)),
	try(mixed, (findall(T, (p(N2), (N2 =:= 2 -> T = f(T) ; T = n(N2))), [n(1), F, n(3)]), is_cyclic(F))),
	try(with_var, (findall(V2-W, V2 = f(V2, W), [V3-W3]), V3 = f(V4, W4), V4 == V3, W4 == W3, var(W3), W3 \== W)),
	try(two_cycles, (findall(p(A4, B4), (A4 = f(A4), B4 = g(B4)), [p(C3, C4)]), is_cyclic(C3), C4 = g(D4), D4 == C4)),
	try(occurs_check, (cyclic(Cy), set_prolog_flag(occurs_check, true), (findall(V5, V5 = Cy, [C5]) -> R = ok ; R = no), set_prolog_flag(occurs_check, false), R == ok, is_cyclic(C5))),
	try(findnsols_chunks, (findall(Ch, findnsols(2, V6, (p(N3), V6 = s(N3, V6)), Ch), [[X1, X2], [X3]|_]), X1 = s(1, Y1), Y1 == X1, X2 = s(2, Y2), Y2 == X2, X3 = s(3, Y3), Y3 == X3)),
	try(nested, (findall(L, findall(V7, cyclic(V7), L), [[C6]]), is_cyclic(C6))),
	try(bagof_cyclic_goal, (G = f(G, Y4, Z4), findall(Z4-L2, bagof(Y4, G, L2), LL), LL == [a-[1], b-[2]])),
	try(setof_cyclic_goal, (G2 = f(G2, Y5, Z5), findall(Z5-L3, setof(Y5, G2, L3), LL2), LL2 == [a-[1], b-[2]])),
	try(bagof_cyclic_witness, (Lc = [1,2|Lc], bagof(H, first(Lc, H), Hs), Hs == [1])),
	try(bagof_cyclic_grouping, (findall(Wt-Ls, bagof(N4, (member(Wt-N4, [Ka-1, Kb-2, Ka-3]), Ka = f(Ka), Kb = g(Kb)), Ls), WLs), length(WLs, 2))),
	try(acyclic, (findall(X-f(X), member(X, [1,2]), Ps), Ps == [1-f(1), 2-f(2)])),
	try(fresh_vars, (findall(Z, member(Z, [Q, Q]), [Z1, Z2]), Z1 \== Z2, Z1 \== Q)).
