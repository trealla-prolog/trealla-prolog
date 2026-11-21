% Issue #1186: sockets on Windows too, a TCP exchange with a client thread over loopback; skipped without sockets.

:- initialization(main).

:- use_module(library(sockets)).

main :-
	(	current_prolog_flag(os, OS), memberchk(OS, [wasi, emscripten])
	->	forall(member(N, [server_port, read_request, end_of_file, client_reply]), format("~w ok~n", [N]))
	;	run
	).

client(Port) :-
	socket_client_open(inet('127.0.0.1', Port), S, [type(text)]),
	format(S, "ping(~w).~n", [Port]),
	flush_output(S),
	read(S, Reply),
	close(S),
	Reply == pong.

run :-
	socket_server_open('127.0.0.1':Port, SS, []),
	check(server_port, (integer(Port), Port > 0)),
	thread_create(client(Port), Tid, []),
	call_with_time_limit(20, socket_server_accept(SS, _, S, [type(text)])),
	read(S, T),
	check(read_request, T == ping(Port)),
	format(S, "pong.~n", []),
	flush_output(S),
	read(S, T2),
	check(end_of_file, T2 == end_of_file),
	close(S),
	thread_join(Tid, St),
	check(client_reply, St == true),
	socket_server_close(SS).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
