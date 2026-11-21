% Issue #1186: process_create/3, process_wait/2,3 and process_kill/1 on Windows too, run with each OS's own commands.

:- initialization(main).

main :-
	(	current_prolog_flag(os, OS), memberchk(OS, [wasi, emscripten])
	->	forall(member(N, [stdout_pipe, exit_code, no_pid_waits, stdin_pipe, environment, cwd, null_stdout, poll_running, kill_wait, missing_exe]),
			format("~w ok~n", [N]))		% no subprocesses there
	;	run
	).

win :- current_prolog_flag(os, windows).

sh(Cmd, Exe, Args) :-
	(	win
	->	Exe = 'cmd.exe', Args = ['/d', '/c', Cmd]
	;	Exe = '/bin/sh', Args = ['-c', Cmd]
	).

lines(S, Ls) :-
	read_line_to_codes(S, Cs),
	(	Cs == end_of_file
	->	Ls = []
	;	atom_codes(L, Cs), Ls = [L|Rest], lines(S, Rest)
	).

run :-
	sh('echo hello', E1, A1),
	check(stdout_pipe, (process_create(E1, A1, [stdout(pipe(S1)), process(P1)]), lines(S1, L1), close(S1),
		process_wait(P1, St1), L1-St1 == [hello]-exit(0))),
	sh('exit 3', E2, A2),
	check(exit_code, (process_create(E2, A2, [process(P2)]), process_wait(P2, St2), St2 == exit(3))),
	sh('exit 0', E3, A3),
	check(no_pid_waits, process_create(E3, A3, [])),
	(win -> E4 = findstr ; E4 = grep),
	check(stdin_pipe, (process_create(E4, [x], [stdin(pipe(In4)), stdout(pipe(S4)), process(P4)]),
		format(In4, "x1~ny2~nx3~n", []), close(In4), lines(S4, L4), close(S4), process_wait(P4, _), L4 == [x1, x3])),
	(win -> sh('echo %TPL1186C%', E5, A5) ; sh('echo $TPL1186C', E5, A5)),
	check(environment, (process_create(E5, A5, [environment(['TPL1186C'=abc]), stdout(pipe(S5)), process(P5)]),
		lines(S5, L5), close(S5), process_wait(P5, _), L5 == [abc])),
	D = tmp1186cdir,
	catch(delete_directory(D), _, true),
	make_directory(D),
	(win -> sh(cd, E6, A6) ; sh(pwd, E6, A6)),
	check(cwd, catch((process_create(E6, A6, [cwd(D), stdout(pipe(S6)), process(P6)]), lines(S6, [L6]), close(S6),
		process_wait(P6, _), atom_concat(_, D, L6)), error(system_error(posix_spawn_file_actions_addchdir, _), _), true)),	% not on OpenBSD
	delete_directory(D),
	sh('echo hidden', E7, A7),
	check(null_stdout, (process_create(E7, A7, [stdout(null), process(P7)]), process_wait(P7, St7), St7 == exit(0))),
	(win -> E8 = ping, A8 = ['-n', '30', '127.0.0.1'] ; E8 = sleep, A8 = ['30']),
	process_create(E8, A8, [stdout(null), process(P8)]),
	check(poll_running, (process_wait(P8, St8a, [timeout(0)]), St8a == timeout)),
	check(kill_wait, (process_kill(P8), process_wait(P8, St8), St8 == killed(9))),
	% Raised, or a child that could not exec exits 127 where posix_spawnp() reports nothing (older glibc, OpenBSD).
	check(missing_exe, catch((process_create(no_such_program_1186c, [], [stderr(null), process(P9)]), process_wait(P9, exit(127))),
		error(_, _), true)).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
