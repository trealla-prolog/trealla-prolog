% The bb_* predicates read the two halves of Module:Key without dereferencing
% them, so a qualified key built from variables raised instantiation_error, and
% so did a bad module named that way, instead of its existence_error.

:- initialization(main).

main :-
	check(bb_b_put, (M = user, K = k1, bb_b_put(M:K, 1), bb_get(user:k1, 1))),
	check(bb_put, (M = user, K = k2, bb_put(M:K, 2), bb_get(user:k2, 2))),
	check(bb_get, (bb_put(user:k3, 3), M = user, K = k3, bb_get(M:K, 3))),
	check(bb_delete, (bb_put(user:k4, 4), M = user, K = k4, bb_delete(M:K, 4), \+ bb_get(user:k4, _))),
	check(bb_update, (bb_put(user:k5, 5), M = user, K = k5, bb_update(M:K, 5, 6), bb_get(user:k5, 6))),
	check(bb_is_live, (bb_b_put(user:k6, 6), M = user, K = k6, '$bb_is_live'(M:K))),
	check(bad_module, (M = no_such_module, K = k, catch((bb_put(M:K, 1), fail),
		error(existence_error(module, no_such_module), _), true))),
	check(unbound_key, (M = user, catch((bb_put(M:_, 1), fail), error(instantiation_error, _), true))).

check(Name, Goal) :-
	(	\+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->	format("~w ok~n", [Name])
	;	format("~w FAILED~n", [Name])
	).
