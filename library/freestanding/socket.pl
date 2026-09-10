/*  library(socket) for a freestanding image: the UDP subset, over the
	network stack's net_udp_* builtins (src/net/bif_net_stack.c).

	A network image embeds this under the name `socket`, in place of the
	hosted library/socket.pl, which is built on streams a freestanding image
	does not have. It exists so that library(tftp), written against
	library(socket), runs on a board unchanged. Every meaning here is the
	hosted library's; what is missing is everything that is not UDP.

	Differences worth knowing:

	  * The stack holds few sockets (NET_UDP_SOCKETS in src/net/net.h), and
	    tcp_bind/2 throws resource_error(udp_sockets) when they run out.
	  * Addresses are dotted IPv4 quads or ip/4 terms. There is no resolver,
	    so a host name is a domain_error rather than a lookup.
	  * as(term) is not offered: a datagram is data, and parsing an untrusted
	    peer's terms interns atoms that are never reclaimed.
	  * Text is UTF-8, as hosted. Malformed sequences decode to U+FFFD rather
	    than raising, since the bytes came from the network.
*/

:- module(socket, [
	udp_socket/1,
	tcp_bind/2,
	udp_send/4,
	udp_receive/4,
	tcp_close_socket/1
	]).

:- use_module(library(lists)).

:- dynamic('$udp_sock'/2).
:- dynamic('$udp_next_id'/1).
:- dynamic('$udp_last_port'/1).

%% udp_socket(-Socket).
%
% A handle only: the port arrives at tcp_bind/2, or on first use.

udp_socket(Socket) :-
	must_be(var, Socket),
	(	retract('$udp_next_id'(Id)) -> true ; Id = 1 ),
	Next is Id + 1,
	assertz('$udp_next_id'(Next)),
	assertz('$udp_sock'(Id, fresh)),
	Socket = '$socket'(Id).

%% tcp_bind(+Socket, ?Address).
%
% Address is Port or Host:Port, the host being ignored because a board has
% the one address. An unbound port asks for an ephemeral one and is unified
% with the port assigned.

tcp_bind(Socket, Address) :-
	'$udp_phase'(Socket, Phase, tcp_bind/2),
	(	Phase == fresh
	->	true
	;	throw(error(permission_error(bind, socket, Socket), tcp_bind/2))
	),
	'$bind_port'(Address, Port),
	'$udp_open'(Port, tcp_bind/2),
	'$udp_set'(Socket, bound(Port)).

'$bind_port'(Address, Port) :-
	var(Address), !,
	Address = Port.
'$bind_port'(_:Port, Port) :- !.
'$bind_port'(Port, Port) :-
	must_be(integer, Port).

% Ephemeral ports rotate through the dynamic range, so consecutive transfers
% get different ports - which is what TFTP's transfer identifiers ask for.
% A port refused as taken costs one try; more tries than the stack has
% sockets means every one is in use.

'$udp_open'(Port, Ctx) :-
	var(Port), !,
	'$udp_ephemeral'(8, Port, Ctx).
'$udp_open'(Port, Ctx) :-
	(	'$udp_try_open'(Port, Ctx)
	->	true
	;	throw(error(permission_error(bind, udp_port, Port), Ctx))
	).

'$udp_ephemeral'(Tries, Port, Ctx) :-
	(	retract('$udp_last_port'(Last)) -> true ; Last = 65535 ),
	Candidate is 49152 + (Last - 49151) mod 16384,
	assertz('$udp_last_port'(Candidate)),
	(	'$udp_try_open'(Candidate, Ctx)
	->	Port = Candidate
	;	Tries > 1
	->	Left is Tries - 1,
		'$udp_ephemeral'(Left, Port, Ctx)
	;	throw(error(resource_error(udp_sockets), Ctx))
	).

% Fails if the port is taken; any other error is the caller's to see.

'$udp_try_open'(Port, Ctx) :-
	catch(net_udp_open(Port, _), error(E, _),
		(	functor(E, permission_error, _)
		->	fail
		;	throw(error(E, Ctx))
		)).

%% udp_send(+Socket, +Data, +To, +Options).
%
% To is Host:Port. Under encoding(octet) Data must be a list of byte values;
% otherwise an atom, number, or code or char list, sent as UTF-8.

udp_send(Socket, Data, To, Options) :-
	must_be(list, Options),
	'$udp_enc'(Options, Enc, udp_send/4),
	% Everything checkable is checked before a socket is bound, so a bad
	% call cannot spend one of the few the stack has.
	'$send_addr'(To, Host, DstPort),
	'$udp_payload'(Enc, Data, Bytes),
	'$udp_port'(Socket, Port, udp_send/4),
	(	net_udp_send(Port, Host, DstPort, Bytes)
	->	true
	;	% The one way a send fails: the peer never answered ARP.
		throw(error(socket_error(ehostunreach, 'No route to host'), udp_send/4))
	).

'$send_addr'(To, _, _) :-
	var(To), !,
	throw(error(instantiation_error, udp_send/4)).
'$send_addr'(Host0:Port, Host, Port) :- !,
	must_be(integer, Port),
	'$host_atom'(Host0, Host).
'$send_addr'(To, _, _) :-
	throw(error(domain_error(socket_address, To), udp_send/4)).

'$host_atom'(ip(A,B,C,D), Host) :- !,
	format(atom(Host), "~d.~d.~d.~d", [A,B,C,D]).
'$host_atom'(Host, Host) :-
	atom(Host), !.
'$host_atom'(Host, _) :-
	throw(error(domain_error(ipv4_address, Host), udp_send/4)).

'$udp_payload'(octet, Data, Data) :- !,
	must_be(list, Data).
'$udp_payload'(text, Data, Bytes) :-
	'$text_codes'(Data, Codes),
	'$utf8_bytes'(Codes, Bytes).

'$text_codes'(Data, Codes) :-
	(	atom(Data) -> atom_codes(Data, Codes)
	;	number(Data) -> number_codes(Data, Codes)
	;	Data == [] -> Codes = []
	;	is_list(Data), Data = [H|_], integer(H) -> Codes = Data
	;	is_list(Data) -> maplist(char_code, Data, Codes)
	;	throw(error(type_error(text, Data), udp_send/4))
	).

%% udp_receive(+Socket, -Data, -From, +Options).
%
% From is ip(A,B,C,D):Port. Options: as(atom|codes|string|chars), default
% string; encoding(octet|utf8|text); timeout(+Milliseconds), under which
% the receive FAILS if nothing arrives - fails, not throws, so a retry is an
% if-then-else. Without it the receive waits for as long as it takes.

udp_receive(Socket, Data, From, Options) :-
	must_be(list, Options),
	'$udp_as'(Options, As, udp_receive/4),
	'$udp_enc'(Options, Enc, udp_receive/4),
	'$udp_port'(Socket, Port, udp_receive/4),
	(	memberchk(timeout(Ms), Options)
	->	must_be(integer, Ms),
		net_udp_recv(Port, Host, FromPort, Bytes, Ms)
	;	'$udp_wait'(Port, Host, FromPort, Bytes)
	),
	'$udp_data'(Enc, Bytes, As, Data),
	'$host_ip'(Host, Ip),
	From = Ip:FromPort.

% In slices, so waiting forever is an ordinary loop over a builtin that
% returns rather than one call that never does.

'$udp_wait'(Port, Host, FromPort, Bytes) :-
	repeat,
	net_udp_recv(Port, Host, FromPort, Bytes, 1000),
	!.

'$udp_data'(octet, Bytes, As, Data) :- !,
	'$codes_as'(As, Bytes, Data).
'$udp_data'(text, Bytes, As, Data) :-
	'$utf8_codes'(Bytes, Codes),
	'$codes_as'(As, Codes, Data).

'$codes_as'(codes, Codes, Codes).
'$codes_as'(atom, Codes, Atom) :- atom_codes(Atom, Codes).
'$codes_as'(string, Codes, String) :- string_codes(String, Codes).
'$codes_as'(chars, Codes, String) :- string_codes(String, Codes).

'$host_ip'(Host, ip(A,B,C,D)) :-
	atom_codes(Host, Codes),
	'$dotted'(Codes, [A,B,C,D]).

'$dotted'(Codes, [N|Ns]) :-
	(	append(Digits, [0'.|Rest], Codes)
	->	number_codes(N, Digits),
		'$dotted'(Rest, Ns)
	;	number_codes(N, Codes),
		Ns = []
	).

%% tcp_close_socket(+Socket).

tcp_close_socket(Socket) :-
	'$udp_phase'(Socket, Phase, tcp_close_socket/1),
	(	Phase = bound(Port) -> net_udp_close(Port) ; true ),
	Socket = '$socket'(Id),
	retract('$udp_sock'(Id, _)).

% --- handles -----------------------------------------------------------
%
% A socket never bound is bound to an ephemeral port on first use, which is
% what the hosted library does too.

'$udp_port'(Socket, Port, Ctx) :-
	'$udp_phase'(Socket, Phase, Ctx),
	(	Phase = bound(Bound)
	->	Port = Bound
	;	'$udp_open'(Port, Ctx),
		'$udp_set'(Socket, bound(Port))
	).

'$udp_phase'(Socket, _, Ctx) :-
	var(Socket), !,
	throw(error(instantiation_error, Ctx)).
'$udp_phase'('$socket'(Id), Phase, _) :-
	'$udp_sock'(Id, Phase0), !,
	Phase = Phase0.
'$udp_phase'(Socket, _, Ctx) :-
	throw(error(existence_error(socket, Socket), Ctx)).

'$udp_set'('$socket'(Id), Phase) :-
	retract('$udp_sock'(Id, _)),
	assertz('$udp_sock'(Id, Phase)).

% --- options -----------------------------------------------------------

'$udp_as'(Options, As, Ctx) :-
	(	memberchk(as(A), Options)
	->	(	memberchk(A, [atom, codes, chars, string])
		->	As = A
		;	throw(error(domain_error(udp_as, A), Ctx))
		)
	;	As = string
	).

'$udp_enc'(Options, Enc, Ctx) :-
	(	memberchk(encoding(E), Options)
	->	(	E == octet
		->	Enc = octet
		;	( E == utf8 ; E == text )
		->	Enc = text
		;	throw(error(domain_error(encoding, E), Ctx))
		)
	;	Enc = text
	).

% --- UTF-8 -------------------------------------------------------------

'$utf8_bytes'([], []).
'$utf8_bytes'([C|Cs], Bytes) :-
	(	C < 0x80
	->	Bytes = [C|Rest]
	;	C < 0x800
	->	B1 is 0xC0 \/ (C >> 6),
		B2 is 0x80 \/ (C /\ 0x3F),
		Bytes = [B1,B2|Rest]
	;	C < 0x10000
	->	B1 is 0xE0 \/ (C >> 12),
		B2 is 0x80 \/ ((C >> 6) /\ 0x3F),
		B3 is 0x80 \/ (C /\ 0x3F),
		Bytes = [B1,B2,B3|Rest]
	;	B1 is 0xF0 \/ (C >> 18),
		B2 is 0x80 \/ ((C >> 12) /\ 0x3F),
		B3 is 0x80 \/ ((C >> 6) /\ 0x3F),
		B4 is 0x80 \/ (C /\ 0x3F),
		Bytes = [B1,B2,B3,B4|Rest]
	),
	'$utf8_bytes'(Cs, Rest).

'$utf8_codes'([], []).
'$utf8_codes'([B|Bs], [C|Cs]) :-
	(	B < 0x80
	->	C = B, Rest = Bs
	;	B >= 0xF0, B < 0xF8, Bs = [B2,B3,B4|Rest], '$cont'([B2,B3,B4])
	->	C is ((B /\ 0x07) << 18) \/ ((B2 /\ 0x3F) << 12)
			\/ ((B3 /\ 0x3F) << 6) \/ (B4 /\ 0x3F)
	;	B >= 0xE0, B < 0xF0, Bs = [B2,B3|Rest], '$cont'([B2,B3])
	->	C is ((B /\ 0x0F) << 12) \/ ((B2 /\ 0x3F) << 6) \/ (B3 /\ 0x3F)
	;	B >= 0xC0, B < 0xE0, Bs = [B2|Rest], '$cont'([B2])
	->	C is ((B /\ 0x1F) << 6) \/ (B2 /\ 0x3F)
	;	C = 0xFFFD, Rest = Bs
	),
	'$utf8_codes'(Rest, Cs).

'$cont'([]).
'$cont'([B|Bs]) :-
	B /\ 0xC0 =:= 0x80,
	'$cont'(Bs).
