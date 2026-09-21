% Two terms that agree down to a depth and differ below it must not unify. Recursion past
% MAX_UNIFY_DEPTH used to return true, so they unified: x matched y under 1000 levels of f/1.
%
% Failing and throwing resource_error(stack) are both fine here - the limit depends on the C
% stack a build has, and a WASM build has far less. Succeeding is not.

:- initialization(main).

mk(0, Leaf, Leaf) :- !.
mk(N, Leaf, f(T)) :- M is N - 1, mk(M, Leaf, T).

unifies(D, R) :-
	mk(D, x, A), mk(D, y, B),
	(	catch(A = B, error(resource_error(stack), _), (R = too_deep, fail)) -> R = yes
	;	( var(R) -> R = no ; true )
	).

main :-
	unifies(20, R1),
	unifies(1000, R2),
	unifies(4000, R3),
	(	R1 == no, R2 \== yes, R3 \== yes
	->	format("deep_unify: ok~n")
	;	format("deep_unify: ~w ~w ~w~n", [R1, R2, R3])
	).
