% multifile/1 called as a goal read Name/Arity by fixed offsets without
% dereferencing, so an indicator built from variables quietly did nothing, a
% malformed one made a predicate from whatever cells were there, and M:PI with
% PI bound elsewhere read past the end of the term.

:- initialization(main).

main :-
	check(var_indicator, (N = vi, A = 1, multifile(N/A), predicate_property(vi(_), multifile))),
	check(literal_indicator, (multifile(li/2), predicate_property(li(_,_), multifile))),
	check(qualified_var_pi, (PI = qv/1, multifile(user:PI))),
	check(bad_name_refused, catch((multifile(1/1), fail),
		error(type_error(predicate_indicator, 1/1), _), true)),
	check(bad_arity_refused, (N = ba, A = x, catch((multifile(N/A), fail),
		error(type_error(predicate_indicator, ba/x), _), true))).

check(Name, Goal) :-
	(	\+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->	format("~w ok~n", [Name])
	;	format("~w FAILED~n", [Name])
	).
