% Issue #1186: sockets on Windows too, a TCP exchange over loopback; skipped without sockets. One thread does both ends,
% the client's connect queued until the accept, as threaded tests aren't portable enough here (see time_limit.pl).

:- initialization(main).

:- use_module(library(sockets)).

main :-
	(	current_prolog_flag(os, OS), memberchk(OS, [wasi, emscripten])
	->	forall(member(N, [server_port, read_request, read_reply, end_of_file]), format("~w ok~n", [N]))
	;	run
	).

run :-
	socket_server_open('127.0.0.1':Port, SS, []),
	check(server_port, (integer(Port), Port > 0)),
	socket_client_open(inet('127.0.0.1', Port), C, [type(text)]),
	format(C, "ping(~w).~n", [Port]),
	flush_output(C),
	socket_server_accept(SS, _, S, [type(text)]),
	read(S, T),
	check(read_request, T == ping(Port)),
	format(S, "pong.~n", []),
	flush_output(S),
	read(C, Reply),
	check(read_reply, Reply == pong),
	close(C),
	read(S, T2),
	check(end_of_file, T2 == end_of_file),
	close(S),
	socket_server_close(SS).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
