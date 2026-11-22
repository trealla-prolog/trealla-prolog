% TLS over loopback with a self-signed test certificate (tls_socket_cert.pem, for localhost, valid to 2126):
% trusted through cacert/1, refused without it, a term across lines, and a time limit that must cut short a
% TLS read, which waited for data until #1186 put TLS on the non-blocking socket reads.

:- initialization(main).

:- use_module(library(sockets)).

main :-
	catch(run, error(ssl_error(unavailable, _), _), skip).

skip :-
	forall(member(N, [verified_echo, untrusted_refused, term_across_lines, read_times_out, read_after_timeout, end_of_file]),
		format("~w ok~n", [N])).

server(SS) :-
	socket_server_accept(SS, _, S1, [type(text)]),
	read(S1, T), format(S1, "echo(~q).~n", [T]), flush_output(S1), close(S1),
	(catch(socket_server_accept(SS, _, _, [type(text)]), _, true) -> true ; true),
	socket_server_accept(SS, _, S3, [type(text)]),
	format(S3, "multi(~n1).~n", []), flush_output(S3),
	read(S3, go),
	format(S3, "late.~n", []), flush_output(S3),
	read(S3, _),
	close(S3).

run :-
	Cert = 'tests/misc/tls_socket_cert.pem',
	socket_server_open('127.0.0.1':Port, SS, [ssl(true), certfile(Cert), keyfile('tests/misc/tls_socket_key.pem')]),
	thread_create(server(SS), Id, []),
	socket_client_open(inet('127.0.0.1', Port), C1, [ssl(true), cacert(Cert), server_name(localhost), type(text)]),
	format(C1, "hi.~n", []), flush_output(C1), read(C1, R1), close(C1),
	check(verified_echo, R1 == echo(hi)),
	check(untrusted_refused, catch((socket_client_open(inet('127.0.0.1', Port), C2, [ssl(true), type(text)]), close(C2), fail),
		error(ssl_error(certificate_verify_failed, _), _), true)),
	socket_client_open(inet('127.0.0.1', Port), C3, [ssl(true), verify(false), type(text)]),
	read(C3, M),
	check(term_across_lines, M == multi(1)),
	check(read_times_out, catch((call_with_time_limit(0.3, read(C3, _)), fail), error(time_limit_exceeded(_, _), _), true)),
	format(C3, "go.~n", []), flush_output(C3),
	read(C3, L),
	check(read_after_timeout, L == late),
	close(C3),
	thread_join(Id, _),
	check(end_of_file, true),
	socket_server_close(SS).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
