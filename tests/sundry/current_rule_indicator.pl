% current_rule/1 named the whole indicator in its errors, and an unbound name or arity was a type
% error; a negative or bigint arity was read as a small one; and M:PI was refused, a module being
% looked for inside the name atom. Each part is now checked and named, and M:PI looks in M.

:- initialization(main).

r(1).

main :-
	check(user_predicate, current_rule(r/1)),
	check(builtin, current_rule(atom_length/2)),
	check(library, current_rule(append/3)),
	check(dcg, current_rule(phrase//1)),
	check(qualified, current_rule(lists:append/3)),
	check(no_such_module, \+ current_rule(nomod:append/3)),
	check(no_such_predicate, \+ current_rule(r/2)),
	check(too_big_arity, \+ current_rule(r/300)),
	check(bigint_arity, (A is 2^64 + 1, \+ current_rule(r/A))),
	check(built_name, (atom_concat(atom_, length, N), current_rule(N/2))),
	error(current_rule(_), instantiation_error),
	error(current_rule(r/_), instantiation_error),
	error(current_rule(_/1), instantiation_error),
	error(current_rule(_:r/1), instantiation_error),
	error(current_rule(r), type_error(predicate_indicator, r)),
	error(current_rule(1/1), type_error(atom, 1)),
	error(current_rule(1:r/1), type_error(atom, 1)),
	error(current_rule(r/a), type_error(integer, a)),
	error(current_rule(r/(-1)), domain_error(not_less_than_zero, -1)),
	error((G = current_rule(r/a), call(G)), type_error(integer, a)).

check(Name, Goal) :-
	(	\+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->	format("~w ok~n", [Name])
	;	format("~w FAILED~n", [Name])
	).

error(Goal, Expected) :-
	catch((Goal, R = succeeded), error(E, _), R = E),
	(	subsumes_term(Expected, R)
	->	format("~q ok~n", [Expected])
	;	format("~q: expected ~q, got ~q~n", [Goal, Expected, R])
	).
