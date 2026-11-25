% Issue #1187: no string/1, as it told a packed string from the equal list of characters.

:- initialization(main).

main :-
	check(equal_terms, "a" == [a]),
	check(not_builtin, \+ predicate_property(string(_), built_in)),
	check(not_defined, catch((string("a"), fail), error(existence_error(procedure, string/1), _), true)),
	check(user_may_define, (assertz(string(x)), string(x))).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
