% Issue #1185: prolog_to_os_filename/2, and paths handed to Prolog use '/' on Windows too.

:- initialization(main).

main :-
	(current_prolog_flag(os, windows) -> OS = 'a\\b\\c' ; OS = 'a/b/c'),
	check(to_os, (prolog_to_os_filename('a/b/c', X1), X1 == OS)),
	check(from_os, (prolog_to_os_filename(X2, OS), X2 == 'a/b/c')),
	check(both_bound, prolog_to_os_filename('a/b/c', OS)),
	check(mismatch_fails, \+ prolog_to_os_filename(abc, abd)),
	throws(both_unbound, prolog_to_os_filename(_, _)),
	throws(not_text, prolog_to_os_filename(1, _)),
	working_directory(W, W),
	atom_chars(WA, W),
	check(cwd_absolute, is_absolute_file_name(WA)),
	check(cwd_no_backslash, \+ sub_atom(WA, _, _, _, '\\')),
	absolute_file_name('x/../y', A),
	check(abs_absolute, is_absolute_file_name(A)),
	check(abs_no_backslash, \+ sub_atom(A, _, _, _, '\\')),
	check(abs_name, sub_atom(A, _, _, 0, '/y')),
	check(relative_not_absolute, \+ is_absolute_file_name('x/y')),
	check(slash_absolute, is_absolute_file_name('/x')),
	(	current_prolog_flag(os, windows)
	->	check(drive_absolute, (is_absolute_file_name('C:/x'), is_absolute_file_name('C:\\x')))
	;	check(drive_absolute, \+ is_absolute_file_name('C:/x'))
	).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).

throws(Name, Goal) :-
	catch((Goal, format("~w did not throw~n", [Name])), error(E, _), format("~w threw ~q~n", [Name,E])).
