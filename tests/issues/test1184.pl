% Issue #1184: delete_directory/1 is a portable builtin, and filesex deletes without the posix_ layer.

:- initialization(main).

:- use_module(library(filesex)).

main :-
	D = tmp1184dir,
	catch(delete_directory_and_contents(D), _, true),
	make_directory(D),
	check(deletes_empty, (delete_directory(D), \+ exists_directory(D))),
	make_directory(D),
	check(chars_path, (atom_chars(D, Cs), delete_directory(Cs), \+ exists_directory(D))),
	throws(missing, delete_directory(D)),
	open(D, write, S), close(S),
	throws(plain_file, delete_directory(D)),
	delete_file(D),
	make_directory_path('tmp1184dir/a/b'),
	open('tmp1184dir/a/f', write, S2), close(S2),
	throws(not_empty, delete_directory(D)),
	check(file_type, (filesex:'$fx_file_type'('tmp1184dir/a', T1), T1 == directory)),
	check(fallback_taken, filesex:'$fx_posix'(throw(error(resource_error(posix_files_unavailable), x)), true)),
	throws(other_errors_pass, filesex:'$fx_posix'(throw(error(resource_error(other), x)), true)),
	check(and_contents, (delete_directory_and_contents(D), \+ exists_directory(D))),
	throws(not_atom, delete_directory(42)).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).

throws(Name, Goal) :-
	catch((Goal, format("~w did not throw~n", [Name])), error(E, _), format("~w threw ~q~n", [Name,E])).
