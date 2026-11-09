% abolish/1,2 now check Name/Arity with the helper dynamic/1 uses. A builtin's name built at run
% time read as some other atom, so abolish(N/2) for N = atom_length succeeded having done nothing;
% it is refused now, and an unbound part is an instantiation error before any type error, as ISO
% orders them. An arity no procedure can have still leaves nothing to abolish.

:- initialization(main).

:- dynamic(ab1/1).
:- dynamic(ab2/3).
ab1(1).
ab2(1, 2, 3).

main :-
	check(literal, (abolish(ab1/1), \+ catch(ab1(_), _, fail))),
	check(dcg, (abolish(ab2//1), \+ catch(ab2(_,_,_), _, fail))),
	check(built_name, (assertz(ab3(1)), atom_concat(ab, '3', N), abolish(N/1), \+ catch(ab3(_), _, fail))),
	check(too_big_arity, abolish(ab4/300)),
	check(bigint_arity, (A is 2^70, abolish(ab4/A))),
	error((atom_concat(atom_, length, N1), abolish(N1/2)), permission_error(modify, static_procedure, atom_length/2)),
	error((atom_concat(atom_, length, N2), abolish(N2, 2)), permission_error(modify, static_procedure, atom_length/2)),
	error(abolish(atom_length/2), permission_error(modify, static_procedure, atom_length/2)),
	error(abolish(1/_), instantiation_error),
	error(abolish(ab/_), instantiation_error),
	error(abolish(1/1), type_error(atom, 1)),
	error(abolish(ab/a), type_error(integer, a)),
	error(abolish(ab/(-1)), domain_error(not_less_than_zero, -1)).

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
