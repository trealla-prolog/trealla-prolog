% close/2 and absolute_file_name/3 read an option's argument without
% dereferencing it, so force(F), relative_to(D) or expand(E) with the value
% bound elsewhere was refused or ignored: relative_to(D) resolved against the
% working directory instead of D, with no error.

:- initialization(main).

main :-
	check(close_force, (open('tmp_stream_option_vars.txt', write, S), F = true, close(S, [force(F)]))),
	check(close_bad_option_refused, (open('tmp_stream_option_vars.txt', write, S), F = maybe,
		catch((close(S, [force(F)]), fail), error(domain_error(close_option, force(maybe)), _), true),
		close(S))),
	check(relative_to, (absolute_file_name(foo, A0, [relative_to('/usr')]),
		D = '/usr', absolute_file_name(foo, A1, [relative_to(D)]), A0 == A1)),
	check(expand, (	catch(absolute_file_name('$HOME/x', A0, [expand(true)]), _, fail)
		->	E = true, absolute_file_name('$HOME/x', A1, [expand(E)]), A0 == A1
		;	true
		)),
	delete_file('tmp_stream_option_vars.txt').

check(Name, Goal) :-
	(	\+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->	format("~w ok~n", [Name])
	;	format("~w FAILED~n", [Name])
	).
