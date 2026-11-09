% multifile/1 called as a goal now walks its argument as dynamic/1 does. Conjunctions, lists and
% N//A failed; a negative or too large arity succeeded having made nothing; and every bad part
% was type_error(predicate_indicator, Spec), where SWI names the part.

:- initialization(main).

st(1).

main :-
	check(conjunction, (multifile((h1/1, h2/2)), predicate_property(h2(_,_), multifile))),
	check(list, (multifile([h3/1, h4/0]), predicate_property(h4, multifile))),
	check(empty_list, multifile([])),
	check(dcg, (multifile(h5//1), predicate_property(h5(_,_,_), multifile))),
	check(qualified_conjunction, (multifile(hm:(h6/1, h7/1)), predicate_property(hm:h7(_), multifile))),
	check(static_with_clauses, (multifile(st/1), predicate_property(st(_), multifile))),
	error(multifile(_), instantiation_error),
	error(multifile(h/_), instantiation_error),
	error(multifile(_:h/1), instantiation_error),
	error(multifile(foo), type_error(predicate_indicator, foo)),
	error(multifile(h/1 as incremental), type_error(predicate_indicator, h/1 as incremental)),
	error(multifile(1:h/1), type_error(atom, 1)),
	error(multifile(h/(-1)), domain_error(not_less_than_zero, -1)),
	error(multifile(h/256), representation_error(max_arity)),
	error(multifile([h8/1|bar]), type_error(predicate_indicator, bar)).

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
