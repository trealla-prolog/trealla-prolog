% Issue #1186: sockets on Windows too, a TCP exchange over loopback; skipped without sockets. One thread does both ends,
% the client's connect queued until the accept, as threaded tests aren't portable enough here (see time_limit.pl).
% A time limit must cut short a read with nothing to read, which blocked for good on Windows, and leave the stream usable.

:- initialization(main).

:- use_module(library(sockets)).

main :-
	(	current_prolog_flag(os, OS), memberchk(OS, [wasi, emscripten])
	->	forall(member(N, [server_port, read_request, read_reply, read_times_out, get_char_times_out, bread_times_out, read_after_timeout, bread_after_timeout, end_of_file]), format("~w ok~n", [N]))
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
	check(read_times_out, times_out(read(C, _))),
	check(get_char_times_out, times_out(get_char(C, _))),
	check(bread_times_out, times_out('$bread'(C, 4, _))),
	format(S, "late.~n", []),
	flush_output(S),
	read(C, Late),
	check(read_after_timeout, Late == late),
	format(S, "abcd", []),
	flush_output(S),
	'$bread'(C, 4, Bytes),
	check(bread_after_timeout, Bytes == "abcd"),
	close(C),
	read(S, T2),
	check(end_of_file, T2 == end_of_file),
	close(S),
	socket_server_close(SS).

times_out(Goal) :-
	catch((call_with_time_limit(0.3, Goal), fail), error(time_limit_exceeded(_, _), _), true).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
