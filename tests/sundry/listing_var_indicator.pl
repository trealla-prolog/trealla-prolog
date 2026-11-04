% listing/1 read the two halves of Name/Arity without dereferencing them, so an
% indicator built from variables - which is what a goal read at run time and
% metacalled hands it - raised instantiation_error instead of listing anything,
% and every wrong indicator raised that too rather than its own error (#1183).

:- initialization(main).

:- dynamic(empty/1).

a(1).
a(2).

main :-
	check(var_indicator, (Name = a, Arity = 1, listing(Name/Arity))),
	check(module_qualified, (M = user, Name = a, Arity = 1, listing(M:Name/Arity))),
	check(partly_var, (Arity = 1, listing(a/Arity))),
	check(bad_arity, (Name = a, Arity = bar, catch((listing(Name/Arity), fail),
		error(type_error(integer, bar), _), true))),
	check(bad_name, (Name = 1, Arity = 1, catch((listing(Name/Arity), fail),
		error(type_error(atom, 1), _), true))),
	check(builtin_refused, (Name = atom_length, Arity = 2, catch((listing(Name/Arity), fail),
		error(permission_error(access, private_procedure, atom_length/2), _), true))),
	check(unbound, catch((listing(_), fail), error(instantiation_error, _), true)),
	check(no_clauses, listing(empty/1)).

check(Name, Goal) :-
	(	\+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->	format("~w ok~n", [Name])
	;	format("~w FAILED~n", [Name])
	).
