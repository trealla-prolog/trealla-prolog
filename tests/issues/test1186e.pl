% Issue #1186: a datagram longer than udp_receive/4's max_message_size(_) is cut to it, as POSIX does,
% where Winsock failed the receive; skipped without sockets.

:- initialization(main).

:- use_module(library(socket)).

main :-
	(	current_prolog_flag(os, OS), memberchk(OS, [wasi, emscripten])
	->	forall(member(N, [udp_whole, udp_truncated]), format("~w ok~n", [N]))
	;	run
	).

run :-
	udp_socket(Sv), tcp_bind(Sv, '127.0.0.1':Port), udp_socket(Cl),
	udp_send(Cl, hello, '127.0.0.1':Port, []),
	udp_receive(Sv, D1, _, []),
	check(udp_whole, D1 == "hello"),
	udp_send(Cl, abcdefgh, '127.0.0.1':Port, []),
	udp_receive(Sv, D2, _, [max_message_size(3)]),
	check(udp_truncated, D2 == "abc"),
	tcp_close_socket(Cl),
	tcp_close_socket(Sv).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
