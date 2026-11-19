% Issue #1185: filesex path arithmetic knows drive letters on Windows and nowhere else.

:- initialization(main).

:- use_module(library(filesex)).

main :-
	check(tilde_relative, \+ is_absolute_file_name('~/x')),
	(	current_prolog_flag(os, windows)
	->	check(dfp_drive_abs, (directory_file_path(a, 'C:/b', P1), P1 == 'C:/b')),
		check(dfp_drive_root, (directory_file_path(D2, F2, 'C:/b'), D2 == 'C:/', F2 == b)),
		check(dfp_drive_dir, (directory_file_path(D3, F3, 'C:/a/b'), D3 == 'C:/a', F3 == b)),
		check(rel_same_drive, (relative_file_name('C:/a/b/c', 'C:/a/x/f', R1), R1 == '../b/c')),
		check(rel_other_drive, (relative_file_name('D:/a/b', 'C:/a/x/f', R2), R2 == 'D:/a/b')),
		check(rel_back, (relative_file_name(P4, 'C:/a/x/f', '../b/c'), P4 == 'C:/a/b/c'))
	;	check(dfp_drive_abs, (directory_file_path(a, 'C:/b', P1), P1 == 'a/C:/b')),
		check(dfp_drive_root, (directory_file_path(D2, F2, 'C:/b'), D2 == 'C:', F2 == b)),
		check(dfp_drive_dir, (directory_file_path(D3, F3, 'C:/a/b'), D3 == 'C:/a', F3 == b)),
		check(rel_same_drive, (relative_file_name('/a/b/c', '/a/x/f', R1), R1 == '../b/c')),
		check(rel_other_drive, (relative_file_name('/d/a/b', '/c/a/x/f', R2), R2 == '../../../d/a/b')),
		check(rel_back, (relative_file_name(P4, '/a/x/f', '../b/c'), P4 == '/a/b/c'))
	).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
