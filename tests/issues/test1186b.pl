% Issue #1186 review: end_of_stream on an engine or alias/2 stream crashed, make_directory_path/1 passed over a file,
% posix_realpath/2 kept '\' on Windows, and relative_file_name/3 saw c: and C: as two drives.

:- initialization(main).

:- use_module(library(filesex)).

main :-
	engine_create(X, member(X, [a]), E),
	check(engine_end_of_stream, (stream_property(E, end_of_stream(A)), A == not)),
	engine_destroy(E),
	alias(7, tmp1186b),
	check(alias_end_of_stream, (stream_property(tmp1186b, end_of_stream(B)), B == not)),
	F = 'tmp1186b.txt',
	open(F, write, S), close(S),
	check(mkpath_on_file, catch((make_directory_path(F), fail), error(existence_error(file, _), _), true)),
	check(mkpath_on_dir, make_directory_path('.')),
	check(realpath_slashes, (posix_realpath(F, R), \+ sub_atom(R, _, _, _, '\\'))),
	delete_file(F),
	(	current_prolog_flag(os, windows)
	->	check(rel_drive_case, (relative_file_name('c:/a/b/c', 'C:/a/x/f', R1), R1 == '../b/c'))
	;	check(rel_drive_case, true)
	).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, Err, (format("~w threw ~q~n", [Name,Err]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
