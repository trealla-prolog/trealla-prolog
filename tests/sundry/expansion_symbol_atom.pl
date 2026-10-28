% A term_expansion/2 or goal_expansion/2 result that is a bare symbol atom, such as
% -, is reparsed with the full stop on a line of its own, so it is not read as the
% atom -. and the rest of the file still loads.

:- initialization(main).

term_expansion(make_minus, (-)).
goal_expansion(minus_goal, (-)).

make_minus.

:- dynamic(t/0).
t :- minus_goal.

after_expansion.

main :-
	check(term_expansion, catch(-, _, fail)),
	check(goal_expansion, (clause(t, B), B == (-))),
	check(rest_loaded, after_expansion).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
