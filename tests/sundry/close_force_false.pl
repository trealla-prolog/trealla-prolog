% close/2 refused force(false), one of the two close options ISO has, with
% domain_error(close_option, force(false)). Anything else is still refused.

:- initialization(main).

main :-
	check(force_false, (open('tmp_close_force_false.txt', write, S), close(S, [force(false)]))),
	check(force_true, (open('tmp_close_force_false.txt', write, S), close(S, [force(true)]))),
	check(force_other_refused, (open('tmp_close_force_false.txt', write, S),
		catch((close(S, [force(fail)]), fail), error(domain_error(close_option, force(fail)), _), true),
		close(S))),
	delete_file('tmp_close_force_false.txt').

check(Name, Goal) :-
	(	\+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->	format("~w ok~n", [Name])
	;	format("~w FAILED~n", [Name])
	).
