% multifile(M:N/A) called as a goal checked whether N/A was multifile in M and
% ignored the answer, so it did nothing; it now makes N/A multifile in M,
% creating M if need be, as SWI does. multifile/1 for an ISO builtin succeeded
% having made nothing, and is now refused as SWI refuses it.

:- initialization(main).

main :-
	check(existing_module, (multifile(user:qa/1), predicate_property(user:qa(_), multifile))),
	check(new_module, (multifile(qmod:qb/2), predicate_property(qmod:qb(_,_), multifile))),
	check(only_in_its_module, \+ predicate_property(user:qb(_,_), multifile)),
	check(module_from_variable, (M = qmod2, multifile(M:qc/1), predicate_property(qmod2:qc(_), multifile))),
	check(unqualified_still, (multifile(qd/1), predicate_property(qd(_), multifile))),
	check(builtin_refused, catch((multifile(findall/3), fail),
		error(permission_error(modify, static_procedure, findall/3), _), true)),
	check(qualified_builtin_refused, catch((multifile(user:findall/3), fail),
		error(permission_error(modify, static_procedure, findall/3), _), true)).

check(Name, Goal) :-
	(	\+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->	format("~w ok~n", [Name])
	;	format("~w FAILED~n", [Name])
	).
