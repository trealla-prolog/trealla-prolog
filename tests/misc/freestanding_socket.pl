:- initialization(main).

% The UDP subset of library(socket) that a network image embeds in place of
% the hosted one, checked against stand-ins for the net_udp_* builtins.
%
% Those builtins exist only in a board image, so without this the shim could
% be exercised only on a Raspberry Pi. The division is deliberate: the board
% proves the driver and the wire, and this proves everything above them -
% handles, ephemeral ports, the octet and text paths, and what happens when
% the four sockets a freestanding stack has are all spoken for.

:- use_module('../../library/freestanding/socket').

% A loopback in place of the stack: a datagram sent to a port is a datagram
% waiting on that port. Four sockets, as many as NET_UDP_SOCKETS in
% src/net/net.h, so that running out of them is testable.

:- dynamic(bound/1).
:- dynamic(queued/4).
:- dynamic(value/2).

net_udp_open(Port, Port) :-
	\+ bound(Port),
	findall(P, bound(P), Ports),
	length(Ports, Count),
	Count < 4,
	!,
	assertz(bound(Port)).
net_udp_open(Port, _) :-
	throw(error(permission_error(open, udp_port, Port), net_udp_open/2)).

net_udp_close(Port) :-
	retractall(bound(Port)).

net_udp_send(Source, _Host, Destination, Bytes) :-
	must_be(list, Bytes),
	assertz(queued(Destination, '10.0.0.9', Source, Bytes)).

net_udp_recv(Port, Host, From, Bytes, _Timeout) :-
	retract(queued(Port, Host, From, Bytes)),
	!.

% Sockets outlive the check that made them, and a check's own variables must
% not: a goal is copied before it runs, so that a binding from one check
% cannot satisfy or spoil the next.

remember(Key, Value) :-
	retractall(value(Key, _)),
	assertz(value(Key, Value)).

recall(Key, Value) :-
	value(Key, Value).

check(Name, Goal0) :-
	copy_term(Goal0, Goal),
	(	catch(Goal, Error, (format("FAIL ~w: ~q~n", [Name, Error]), fail))
	->	format("ok   ~w~n", [Name])
	;	format("FAIL ~w~n", [Name])
	).

main :-
	check(ephemeral_bind, (
		udp_socket(S1), tcp_bind(S1, P1),
		integer(P1), P1 >= 49152, P1 =< 65535,
		remember(s1, S1-P1))),
	check(distinct_ports, (
		recall(s1, _-P1), udp_socket(S2), tcp_bind(S2, P2),
		P2 \== P1, remember(s2, S2-P2))),
	check(text_roundtrip, (
		recall(s1, S1-P1), recall(s2, S2-P2),
		udp_send(S1, "héllo", ip(10,0,0,9):P2, []),
		udp_receive(S2, Data, From, [timeout(0)]),
		Data == "héllo", From == ip(10,0,0,9):P1)),
	check(text_is_utf8_on_the_wire, (
		recall(s1, S1-_), recall(s2, _-P2),
		udp_send(S1, 'é', '10.0.0.9':P2, []),
		queued(P2, _, _, Bytes), Bytes == [0xC3, 0xA9],
		retractall(queued(_, _, _, _)))),
	check(octet_as_codes, (
		recall(s1, S1-_), recall(s2, S2-P2),
		udp_send(S1, [0, 255, 128], '10.0.0.9':P2, [encoding(octet)]),
		udp_receive(S2, Data, _, [encoding(octet), as(codes)]),
		Data == [0, 255, 128])),
	check(octet_as_atom, (
		recall(s1, S1-_), recall(s2, S2-P2),
		udp_send(S1, [104, 105], '10.0.0.9':P2, [encoding(octet)]),
		udp_receive(S2, Data, _, [encoding(octet), as(atom)]),
		Data == hi)),
	check(timeout_fails_not_throws, (
		recall(s2, S2-_),
		\+ udp_receive(S2, _, _, [timeout(0)]))),
	check(bad_utf8_becomes_replacement, (
		recall(s2, S2-P2),
		assertz(queued(P2, '10.0.0.9', 1, [0xC3, 0x41])),
		udp_receive(S2, Data, _, [as(codes), timeout(0)]),
		Data == [0xFFFD, 0x41])),
	check(bind_to_a_named_port, (
		udp_socket(S3), tcp_bind(S3, 69), bound(69), remember(s3, S3))),
	check(taken_port_refused, (
		udp_socket(S4),
		catch((tcp_bind(S4, 69), fail),
			error(permission_error(bind, udp_port, 69), _), true))),
	check(sockets_run_out, (
		udp_socket(S5), tcp_bind(S5, _), udp_socket(S6),
		catch((tcp_bind(S6, _), fail),
			error(resource_error(udp_sockets), _), true))),
	check(rebinding_refused, (
		recall(s3, S3),
		catch((tcp_bind(S3, 70), fail),
			error(permission_error(bind, socket, _), _), true))),
	check(close_frees_the_port, (
		recall(s3, S3), tcp_close_socket(S3), \+ bound(69))),
	check(closed_socket_is_gone, (
		recall(s3, S3),
		catch((udp_receive(S3, _, _, [timeout(0)]), fail),
			error(existence_error(socket, _), _), true))),
	check(sending_binds_an_unbound_socket, (
		recall(s1, S1-_), tcp_close_socket(S1),
		udp_socket(S7), recall(s2, _-P2),
		udp_send(S7, abc, '10.0.0.9':P2, []),
		socket:'$udp_sock'(_, bound(_)))),
	check(a_host_that_is_not_an_address, (
		udp_socket(S8),
		catch((udp_send(S8, x, foo(1):9, []), fail),
			error(domain_error(ipv4_address, _), _), true))),
	halt.
