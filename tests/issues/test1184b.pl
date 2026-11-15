% Issue #1184: the posix_ file calls Windows has too; WASI has none, so each check passes there.

:- initialization(main).

:- use_module(library(filesex)).

main :-
	D = tmp1184bdir,
	catch(delete_directory_and_contents(D), _, true),
	make_directory(D),
	F = 'tmp1184bdir/f.txt', H = 'tmp1184bdir/h.txt',
	open(F, write, S), write(S, hello), close(S),
	check(file_type, (posix_file_type(F, T1), T1 == regular, posix_file_type(D, T2), T2 == directory)),
	check(realpath, (posix_realpath(F, R), atom_concat(_, 'f.txt', R))),
	check(set_time, (set_time_file(F, _, [modified(1000000000.0)]),
		set_time_file(F, [modified(M)], []), M =:= 1000000000)),
	check(read_only, (chmod(F, 0o444), posix_file_mode(F, M1), M1 /\ 0o222 =:= 0)),
	check(writable, (chmod(F, 0o644), posix_file_mode(F, M2), M2 /\ 0o200 =\= 0)),
	check(hard_link, (link_file(F, H, hard), read_file_to_string(H, Str, []), Str == "hello")),
	check(unlink, (posix_unlink(H), \+ exists_file(H))),
	throws(missing, posix_file_type('tmp1184bdir/nope', _)),
	check(rmdir_not_empty, catch(posix_rmdir(D), error(file_error(_, _), _), true)),
	check(and_contents, (delete_directory_and_contents(D), \+ exists_directory(D))).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (unavailable(E) -> true ; format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).

throws(Name, Goal) :-
	catch((Goal, format("~w did not throw~n", [Name])), error(E, _), report(Name, E)).

report(Name, resource_error(posix_files_unavailable)) :- !,
	format("~w threw ~q~n", [Name,enoent]).
report(Name, file_error(Errno, _)) :- !,
	format("~w threw ~q~n", [Name,Errno]).
report(Name, E) :-
	format("~w threw ~q~n", [Name,E]).

unavailable(error(resource_error(posix_files_unavailable), _)).
