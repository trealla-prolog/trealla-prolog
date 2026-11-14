% nb_setarg/3 copied a bigint value without holding it, so once backtracking
% freed the original the term read freed memory. Over a compound argument it
% wrote one cell where the compound had several, leaving a corrupt term.

:- initialization(main).

big_kept(N) :- functor(N, n, 1), ( between(1, 3, _), X is 2^100+1, nb_setarg(1, N, X), fail ; true ), _ is 3^90*7.
big_replaced(N) :- functor(N, n, 1), X is 2^100, nb_setarg(1, N, X), Y is 2^99, nb_setarg(1, N, Y), _ is 2^98.
small_over_big(N) :- functor(N, n, 1), X is 2^100, nb_setarg(1, N, X), nb_setarg(1, N, 7).
compound_arg(R) :- N = f(g(1), 2), arg(1, N, G), catch(nb_setarg(1, N, 5), error(E, _), true), R = N-G-E.
var_arg(R) :- functor(N, f, 2), N = f(g(1), 2), arg(1, N, G), nb_setarg(1, N, 5), R = N-G.

main :-
	forall(
		member(T, [big_kept, big_replaced, small_over_big, compound_arg, var_arg]),
		(	call(T, R),
			writeq(T-R), nl
		)
	).
