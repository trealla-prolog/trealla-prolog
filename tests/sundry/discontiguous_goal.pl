% discontiguous/1 called as a goal raised existence_error, being known only as a directive; it
% now walks its argument as dynamic/1 and multifile/1 do, as in SWI. predicate_property(P,
% discontiguous) failed even after the directive, though enumerating the properties found it.

:- initialization(main).

:- discontiguous(d0/1).

st(1).

main :-
	check(directive_property, predicate_property(d0(_), discontiguous)),
	check(plain, (discontiguous(d1/1), predicate_property(d1(_), discontiguous))),
	check(conjunction, (discontiguous((d2/1, d3/2)), predicate_property(d3(_,_), discontiguous))),
	check(list, (discontiguous([d4/1, d5/0]), predicate_property(d5, discontiguous))),
	check(empty_list, discontiguous([])),
	check(dcg, (discontiguous(d6//1), predicate_property(d6(_,_,_), discontiguous))),
	check(qualified, (discontiguous(dm:(d7/1, d8/1)), predicate_property(dm:d8(_), discontiguous))),
	check(only_in_its_module, \+ predicate_property(user:d8(_), discontiguous)),
	check(static_with_clauses, (discontiguous(st/1), predicate_property(st(_), discontiguous))),
	check(not_discontiguous, \+ predicate_property(main, discontiguous)),
	error(discontiguous(_), instantiation_error),
	error(discontiguous(d/_), instantiation_error),
	error(discontiguous(foo), type_error(predicate_indicator, foo)),
	error(discontiguous(1/1), type_error(atom, 1)),
	error(discontiguous(d/a), type_error(integer, a)),
	error(discontiguous(d/(-1)), domain_error(not_less_than_zero, -1)),
	error(discontiguous(d/256), representation_error(max_arity)),
	error(discontiguous(atom_length/2), permission_error(modify, static_procedure, atom_length/2)),
	error((G = discontiguous((d9/1, foo)), call(G)), type_error(predicate_indicator, foo)).

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
