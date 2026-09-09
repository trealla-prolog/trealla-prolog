% Issue #1149: must_be(predicate_indicator, PI) accepted anything.
%
% predicate_indicator was not one of the types must_be/2 knows, and an
% unknown type simply succeeds - so a partial indicator and a negative
% arity both passed the check.
%
% https://github.com/trealla-prolog/trealla/issues/1149

:- initialization(main).

main :-
	check(must_be(predicate_indicator, _/_)),
	check(must_be(predicate_indicator, p/_)),
	check(must_be(predicate_indicator, _/3)),
	check(must_be(predicate_indicator, p/3)),
	check(must_be(predicate_indicator, p/0)),
	check(must_be(predicate_indicator, p/(-3))),
	check(must_be(predicate_indicator, p/a)),
	check(must_be(predicate_indicator, 1/3)),
	check(must_be(predicate_indicator, f(a)/3)),
	check(must_be(predicate_indicator, foo)),

	% a wrong part outranks a missing one

	check(must_be(predicate_indicator, _/(-3))),

	% can_be/2 accepts what a substitution could still complete

	check(can_be(predicate_indicator, _)),
	check(can_be(predicate_indicator, _/_)),
	check(can_be(predicate_indicator, p/3)),
	check(can_be(predicate_indicator, p/(-3))),
	check(can_be(predicate_indicator, foo)),

	% the four-argument forms name the caller in the error

	check(must_be(p/3, predicate_indicator, foo/1, _)),
	check(must_be(p/(-3), predicate_indicator, foo/1, _)),
	check(can_be(p/(-3), predicate_indicator, foo/1, _)).

check(Goal) :-
	(	catch(Goal, E, true)
	->	(	var(E)
		->	show(Goal, ok)
		;	show(Goal, threw(E))
		)
	;	show(Goal, failed)
	).

% The goal and its outcome are numbervar'd together, so the report says
% which variable is which without depending on the numbers this run
% happened to hand out.

show(Goal, Outcome) :-
	copy_term(Goal-Outcome, G-O),
	numbervars(G-O, 0, _),
	format("~q ~q~n", [G,O]).
