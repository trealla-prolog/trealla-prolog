% nb_setval/2, bb_put/2 and bb_update/3 stored a cyclic term with its cycle cut to a fresh variable, so reading it
% back gave another term. Their copies have nowhere to keep a cycle, so they refuse it, while b_setval/2 and bb_b_put/2,
% whose values refer back to the caller's term, keep it.

:- use_module(library(freeze)).
:- initialization(main).

try(Name, Goal) :-
	write(Name), write(': '),
	catch((Goal -> write(ok) ; write(failed)), error(E, _), write(E)), nl.

cyclic(V) :- V0 = f(V0), V = V0.

is_cyclic(V) :- V = f(W), W == V.

main :-
	try(nb_setval, (cyclic(V1), nb_setval(k1, V1))),
	try(nb_setval_inner, (A = g(B), B = h(A), nb_setval(k1, x(A)))),
	try(nb_setval_spine, (L = [1,2|L], nb_setval(k1, L))),
	try(nb_setval_frozen, (X = f(X, Y), freeze(Y, true), nb_setval(k1, X))),
	try(nb_unchanged, (nb_setval(k2, a), cyclic(V2), catch(nb_setval(k2, V2), _, true), nb_getval(k2, a))),
	try(bb_put, (cyclic(V3), bb_put(k3, V3))),
	try(bb_update, (bb_put(k4, a), cyclic(V4), bb_update(k4, _, V4))),
	try(bb_unchanged, (bb_get(k4, a))),
	try(b_setval, (cyclic(V5), b_setval(k5, V5), b_getval(k5, W5), is_cyclic(W5))),
	try(bb_b_put, (cyclic(V6), bb_b_put(k6, V6), bb_get(k6, W6), is_cyclic(W6))),
	try(nb_shared, (S = [1,2], T = p(S, S, [S|S]), nb_setval(k7, T), nb_getval(k7, T2), T2 == T)),
	try(nb_frozen, (Z = g(F), freeze(F, true), nb_setval(k8, Z), nb_getval(k8, g(F2)), frozen(F2, G), G \== true)).
