% Issue #1150: must_be/2 and can_be/2 accepted any type.
%
% A type neither builtin knows fell through every check and succeeded,
% where it should be a type_error(type, Type).
%
% https://github.com/trealla-prolog/trealla/issues/1150

:- initialization(main).

main :-
	check(must_be(nontype, 0)),
	check(can_be(nontype, 0)),

	% the type is checked before the term

	check(must_be(nontype, _)),
	check(can_be(nontype, _)),

	check(must_be(1, 0)),
	check(can_be(1, 0)),
	check(must_be(integer(x), a)),
	check(can_be(integer(x), a)),
	check(must_be(list(nontype), [a])),
	check(must_be(_, 0)),
	check(can_be(_, 0)),
	check(must_be(list(_), [a])),

	% known types behave as before

	check(must_be(integer, 0)),
	check(must_be(integer, a)),
	check(can_be(integer, _)),
	check(must_be(list(integer), [1])),
	check(must_be(list(integer), [a])),
	check(must_be(list(list(integer)), [[1]])),
	check(can_be(list(integer), [1])),

	% assoc is a builtin type, as library(assoc) relies on it

	check(must_be(assoc, t)),
	check(must_be(assoc, t(k,v,<,t,t))),
	check(must_be(assoc, t(k,v))),
	check(must_be(assoc, foo)),
	check(must_be(assoc, 1)),
	check(can_be(assoc, _)),
	check(can_be(assoc, t)),
	check(can_be(assoc, foo)).

check(Goal) :-
	(	catch(Goal, E, true)
	->	(	var(E)
		->	show(Goal, ok)
		;	show(Goal, threw(E))
		)
	;	show(Goal, failed)
	).

show(Goal, Outcome) :-
	copy_term(Goal-Outcome, G-O),
	numbervars(G-O, 0, _),
	format("~q ~q~n", [G,O]).
