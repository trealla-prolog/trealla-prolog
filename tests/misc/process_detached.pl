% process_create/3's detached(Bool): only true starts a new session, so the child leads its own process group;
% detached(false), like no option, leaves it in ours. detached(_) used to start one whatever its argument.

:- initialization(main).

leads_own_group(Opts) :-
	process_create('/bin/sh', ['-c', 'ps -o pgid= -p $$; echo $$'], [stdout(pipe(S)), process(P)|Opts]),
	read_line_to_codes(S, Cs1),
	read_line_to_codes(S, Cs2),
	close(S),
	process_wait(P, _),
	number_codes(Pgid, Cs1),
	number_codes(Pid, Cs2),
	Pgid =:= Pid.

check(Name, Goal) :-
	(   catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).

main :-
	check(default_stays, \+ leads_own_group([])),
	check(false_stays, \+ leads_own_group([detached(false)])),
	check(true_detaches, leads_own_group([detached(true)])).
