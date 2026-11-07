% process_create/3 read each Key=Value in env/1 and environment/1 without
% dereferencing either side, so a pair built from variables was dropped and the
% child never saw it.

:- initialization(main).
:- use_module(library(lists)).

main :-
	K = 'TPL_ENV_VAR', V = from_a_variable,
	process_create('/usr/bin/env', [], [env([K=V]), stdout(pipe(Out)), process(Pid)]),
	read_lines(Out, Ls), close(Out), process_wait(Pid, exit(0)),
	(	memberchk("TPL_ENV_VAR=from_a_variable", Ls) -> write(env_ok) ; write(env_missing) ), nl,
	K2 = 'TPL_ENVIRONMENT_VAR', V2 = 42,
	process_create('/usr/bin/env', [], [environment([K2=V2]), stdout(pipe(Out2)), process(Pid2)]),
	read_lines(Out2, Ls2), close(Out2), process_wait(Pid2, exit(0)),
	(	memberchk("TPL_ENVIRONMENT_VAR=42", Ls2) -> write(environment_ok) ; write(environment_missing) ), nl.

read_lines(S, Ls) :-
	read_line_to_string(S, L),
	( L == end_of_file -> Ls = [] ; Ls = [L|Ls1], read_lines(S, Ls1) ).
