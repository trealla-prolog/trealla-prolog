% listing/1 took a bigint arity as whatever its cells read as a small integer,
% which failed or, by chance, listed some other arity; and a negative arity
% ran into -1, its mark for any arity, so listing(a/(-1)) listed every a/N.
% As in SWI, the first is now a representation error and the second a domain
% error, checked before // adds its two.

:- initialization(main).

a(1).
a(x, y).

main :-
	check(bigint_refused, catch((listing(a/100000000000000000000), fail),
		error(representation_error(max_arity), _), true)),
	check(negative_refused, catch((listing(a/(-1)), fail),
		error(domain_error(not_less_than_zero, -1), _), true)),
	check(negative_dcg_refused, catch((listing(a//(-2)), fail),
		error(domain_error(not_less_than_zero, -2), _), true)),
	check(ordinary_arity_lists, listing(a/2)).

check(Name, Goal) :-
	(	\+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->	format("~w ok~n", [Name])
	;	format("~w FAILED~n", [Name])
	).
