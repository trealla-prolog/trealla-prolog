/** SWI-Prolog compatible socket interface.

Emulates the interface documented at
<https://www.swi-prolog.org/pldoc/man?section=socket> on top of Trealla's
networking builtins. See docs/socket-swi-design.md for the full design.

Note that `library(sockets)` (plural) also exists and is unrelated: it is
Trealla's own, Scryer-shaped socket library. Both drive the same builtins;
they differ only in the interface they present. Use whichever suits.

## Differences from SWI worth knowing

  * A Socket is a Prolog-side handle, not an OS handle. Trealla has no bif
    that creates an unbound, unconnected socket, so `tcp_socket/1` records
    intent and the real socket appears later, at `tcp_connect/2` or
    `tcp_bind/2`.

  * The underlying bif binds and listens in one step, so `tcp_bind/2` does
    both and `tcp_listen/2` only validates. Bind errors and the ephemeral
    port therefore surface at `tcp_bind/2`, which is where SWI puts them.
    The one visible difference is that a socket bound but never listened
    is nonetheless listening, where SWI would refuse connections to it.
    `BackLog` is ignored - the underlying layer hardcodes `SOMAXCONN`.

  * Trealla's socket streams are bidirectional, so a "stream pair" here is
    one stream. `tcp_open_socket/3` returns the same stream twice. Code
    that splits a pair with `stream_pair/3` will not work; code that uses
    the pair directly with `format/3`, `read_term/3` and `close/1` will.

  * `udp_receive/4` and `udp_send/4` carry text as UTF-8, so a byte over
    127 goes out as more than one byte. Pass `encoding(octet)` for a
    binary protocol. Under it `udp_send/4` takes a list of byte values,
    but `udp_receive/4` still honours `as/1`, which defaults to `string` -
    so a binary protocol wants `[encoding(octet), as(codes)]` to get the
    bytes back as a list. `encoding(iso_latin_1)` is rejected rather than
    quietly approximated.

  * `udp_receive/4` with `as(term)` interns the functor and atom names of
    every term it parses, and those are never reclaimed - roughly one new
    symbol per distinct datagram. Reading terms from an untrusted peer is
    therefore a slow memory leak. SWI has the same property. Ordinary
    atom construction does not cost anything permanent; parsing does.
*/

:- module(socket, [
	tcp_socket/1,
	udp_socket/1,
	unix_domain_socket/1,
	socket_create/2,
	udp_receive/4,
	udp_send/4,
	tcp_bind/2,
	tcp_listen/2,
	tcp_accept/3,
	tcp_connect/2,
	tcp_connect/3,
	tcp_connect/4,
	tcp_open_socket/2,
	tcp_open_socket/3,
	tcp_close_socket/1,
	tcp_setopt/2,
	tcp_getopt/2,
	tcp_host_to_address/2,
	host_address/3,
	gethostname/1,
	ip_name/2
	]).

:- use_module(library(error)).
:- use_module(library(lists)).

% --- handle state ------------------------------------------------------
%
% '$socket'(Id) is opaque to callers. The state lives here rather than in
% the term so that connecting mutates the socket, as it does in SWI.
%
% Phase is one of:
%
%   fresh              created; no OS socket exists yet
%   connected(Stream)  '$client' has run, or this came from an accept
%   listening(Stream)  '$server' has run
%   closed             torn down
%
% Nothing here is thread-safe yet; sockets created concurrently on
% different threads would race on the counter. Single-threaded use is
% correct. See docs/socket-swi-design.md §9.

:- dynamic('$sock'/4).
:- dynamic('$sock_counter'/1).

'$sock_counter'(0).

'$new_id'(Id) :-		% sig_atomic/1: no task_time_slice slice between the read and the write
	sig_atomic((retract('$sock_counter'(Id0)), Id is Id0 + 1, assertz('$sock_counter'(Id)))).

'$sock_get'(Socket, Domain, Type, Phase) :-
	(  nonvar(Socket), Socket = '$socket'(Id)
	-> true
	;  var(Socket)
	-> throw(error(instantiation_error, socket/2))
	;  throw(error(type_error(socket, Socket), socket/2))
	),
	(  '$sock'(Id, Domain, Type, Phase)
	-> true
	;  throw(error(existence_error(socket, Socket), socket/2))
	).

'$sock_set'(Socket, Phase) :-
	Socket = '$socket'(Id),
	retract('$sock'(Id, D, T, _)),
	assertz('$sock'(Id, D, T, Phase)).

% The stream behind a socket, whatever way it was materialised.

'$sock_stream'(Socket, Stream) :-
	'$sock_get'(Socket, _, _, Phase),
	(  Phase = connected(Stream) -> true
	;  Phase = listening(Stream) -> true
	;  Phase == closed
	-> throw(error(existence_error(stream, Socket), socket/2))
	;  throw(error(permission_error(access, socket, Socket), socket/2))
	).

% --- errors ------------------------------------------------------------
%
% SWI reports error(socket_error(Code, Message), _). Trealla's builtins
% raise ordinary ISO errors and in places simply fail, so the errno is
% usually not recoverable - we can wrap what we are given but cannot
% invent a code we were never told. An operation that merely fails
% becomes socket_error(unknown, ...) rather than silently failing, since
% SWI callers expect an exception.

'$sock_call'(Goal, Context) :-
	(  catch(Goal, E, '$sock_rethrow'(E, Context))
	-> true
	;  throw(error(socket_error(unknown, 'operation failed'), Context))
	).

% The builtins put the culprit where SWI puts a message, so the message
% is rederived from the code and the context is the library predicate the
% caller actually invoked.

'$sock_rethrow'(error(socket_error(C,M), Ctx), Context) :- !,
	(  '$errmsg'(C, Msg)
	-> throw(error(socket_error(C,Msg), Context))
	;  throw(error(socket_error(C,M), Ctx))
	).
'$sock_rethrow'(error(existence_error(_, _), _), Context) :- !,
	throw(error(socket_error(enoent, 'no such host or file'), Context)).
'$sock_rethrow'(E, _) :-
	throw(E).

'$errmsg'(eaddrinuse,    'address already in use').
'$errmsg'(eaddrnotavail, 'cannot assign requested address').
'$errmsg'(eafnosupport,  'address family not supported').
'$errmsg'(eacces,        'permission denied').
'$errmsg'(econnrefused,  'connection refused').
'$errmsg'(econnreset,    'connection reset by peer').
'$errmsg'(ehostunreach,  'no route to host').
'$errmsg'(enetunreach,   'network is unreachable').
'$errmsg'(etimedout,     'connection timed out').
'$errmsg'(epipe,         'broken pipe').
'$errmsg'(eagain,        'resource temporarily unavailable').
'$errmsg'(einval,        'invalid argument').
'$errmsg'(emfile,        'too many open files').
'$errmsg'(enfile,        'too many open files in system').
'$errmsg'(enoent,        'no such host or file').
'$errmsg'(eisconn,       'socket is already connected').
'$errmsg'(unknown,       'operation failed').

% --- addresses ---------------------------------------------------------
%
% Converts SWI's address terms into the Host:Port atom the builtins take.
%
%   ip(A,B,C,D)             IPv4
%   ip(A,B,C,D,E,F,G,H)     IPv6
%   Host:Port               hostname or IP text, plus port
%   Port                    integer, meaning "any interface"

'$addr_atom'(Addr, _) :-
	var(Addr), !,
	throw(error(instantiation_error, socket/2)).
'$addr_atom'(Host:Port, Atom) :- !,
	'$host_atom'(Host, H),
	must_be(integer, Port),
	atomic_list_concat([H, ':', Port], Atom).
'$addr_atom'(Port, Atom) :-
	integer(Port), !,
	atomic_list_concat([':', Port], Atom).
'$addr_atom'(Addr, Addr) :-
	atom(Addr), !.
'$addr_atom'(Addr, _) :-
	throw(error(domain_error(socket_address, Addr), socket/2)).

'$host_atom'(H, H) :- atom(H), !.
'$host_atom'(ip(A,B,C,D), H) :- !,
	atomic_list_concat([A,'.',B,'.',C,'.',D], H).
'$host_atom'(ip(A,B,C,D,E,F,G,I), H) :- !,
	maplist('$hex4', [A,B,C,D,E,F,G,I], Hs),
	atomic_list_concat(Hs, ':', H).
'$host_atom'(H, _) :-
	throw(error(domain_error(ip_address, H), socket/2)).

'$hex4'(N, H) :- must_be(integer, N), format(atom(H), "~16r", [N]).

%% ip_name(?IP, ?Name).
%
% Between an `ip/4` or `ip/8` term and its textual form.

ip_name(IP, Name) :-
	nonvar(IP), !,
	'$host_atom'(IP, Name).
ip_name(IP, Name0) :-
	'$must_be_text'(Name0, Name),
	atom_chars(Name, Cs),
	'$parse_ip4'(Cs, IP0), !,
	IP = IP0.
ip_name(_, Name) :-
	throw(error(domain_error(ip_address, Name), ip_name/2)).

% Parsed arithmetically off the characters rather than by splitting into
% sub-atoms with atomic_list_concat/3. This runs on the receive path for
% every packet, so it stays allocation-light.

'$parse_ip4'(Cs, ip(A,B,C,D)) :-
	'$ip_octet'(Cs, A, ['.'|R1]),
	'$ip_octet'(R1, B, ['.'|R2]),
	'$ip_octet'(R2, C, ['.'|R3]),
	'$ip_octet'(R3, D, []).

'$ip_octet'([C|Cs], N, Rest) :-
	'$digit_val'(C, V),
	'$ip_octet_'(Cs, V, N, Rest),
	N >= 0, N =< 255.

'$ip_octet_'([C|Cs], Acc, N, Rest) :-
	'$digit_val'(C, V), !,
	Acc2 is (Acc * 10) + V,
	Acc2 =< 255,
	'$ip_octet_'(Cs, Acc2, N, Rest).
'$ip_octet_'(Cs, N, N, Cs).

'$digit_val'(C, V) :-
	char_code(C, Code),
	Code >= 0'0, Code =< 0'9,
	V is Code - 0'0.

% A wildcard server socket comes back AF_INET6, so an IPv4 peer arrives
% v4-mapped as ::ffff:127.0.0.1. Callers expect SWI's ip/4. The address
% arrives as an atom from both '$peer_addr' and '$udp_recv', but chars
% are accepted too so the parser has one entry point.

'$normalise_peer'(Host, IP) :-
	'$peer_chars'(Host, Cs),
	'$strip_v4mapped'(Cs, Cs1),
	'$parse_ip4'(Cs1, IP0), !,
	IP = IP0.
'$normalise_peer'(Host, Host).

'$peer_chars'(H, Cs) :- atom(H), !, atom_chars(H, Cs).
'$peer_chars'(H, H).

'$strip_v4mapped'([':',':',f,f,f,f,':'|Cs], Cs) :- !.
'$strip_v4mapped'(Cs, Cs).

% --- creation ----------------------------------------------------------

%% socket_create(-Socket, +Options).
%
% Options: `domain(inet|inet6|unix)`, `type(stream|dgram)`.

socket_create(Socket, Options) :-
	must_be(var, Socket),
	must_be(list, Options),
	(  memberchk(domain(D0), Options) -> D = D0 ; D = inet ),
	(  memberchk(type(T0), Options)   -> T = T0 ; T = stream ),
	'$new_id'(Id),
	assertz('$sock'(Id, D, T, fresh)),
	Socket = '$socket'(Id).

%% tcp_socket(-Socket).

tcp_socket(Socket) :- socket_create(Socket, []).

%% udp_socket(-Socket).

udp_socket(Socket) :- socket_create(Socket, [type(dgram)]).

%% unix_domain_socket(-Socket).

unix_domain_socket(Socket) :- socket_create(Socket, [domain(unix)]).

% Builtin options implied by the handle's domain and type.

'$sock_opts'(_, dgram, [udp(true)]) :- !.
'$sock_opts'(_, _, []).

% --- udp ---------------------------------------------------------------
%
% A UDP socket need not be bound before sending, so one that has never
% seen tcp_bind/2 is materialised here on an ephemeral port, which is
% what the send would have done implicitly anyway.

'$udp_stream'(Socket, Stream, Ctx) :-
	'$sock_get'(Socket, Domain, Type, Phase),
	(  Type == dgram
	-> true
	;  throw(error(permission_error(udp, socket, Socket), Ctx))
	),
	(  Phase = listening(S)
	-> Stream = S
	;  Phase = connected(S)
	-> Stream = S
	;  Phase == fresh
	-> '$sock_opts'(Domain, Type, Opts),
	   '$sock_call'('$server'(_, Stream, Opts), Ctx),
	   '$sock_set'(Socket, listening(Stream))
	;  throw(error(existence_error(socket, Socket), Ctx))
	).

%% udp_receive(+Socket, -Data, -From, +Options).
%
% From is Ip:Port with Ip an ip/4 or ip/8 term, as SWI has it - note that
% udp_send/4 takes a *hostname* in that position, which is SWI's
% asymmetry, not ours.
%
% Options: as(atom|codes|string|chars|term), default string;
% max_message_size(+Bytes), default 4096; encoding(octet|utf8|text);
% timeout(+Milliseconds).
%
% Without timeout/1 the receive blocks indefinitely, as before. With it,
% udp_receive/4 FAILS if nothing arrives in time - note fails, not throws,
% so that a retry is an if-then-else. timeout(0) polls and returns at once,
% which is the non-blocking case; nothing about the socket's own flags is
% changed either way, so other operations on the stream are unaffected.

udp_receive(Socket, Data, From, Options) :-
	must_be(list, Options),
	'$udp_as'(Options, As, udp_receive/4),
	'$udp_enc'(Options, Enc, udp_receive/4),
	'$udp_stream'(Socket, Stream, udp_receive/4),
	'$udp_bifopts'(Options, Enc, BifOpts),
	% A timeout is an outcome, not a fault: it fails rather than throwing,
	% so a retransmit loop is an if-then-else and not a catch/3.
	catch('$udp_recv'(Stream, Raw, Host, Port, BifOpts), E, true),
	(	var(E) -> true
	;	E = error(socket_error(timeout, _), _) -> fail
	;	'$sock_rethrow'(E, udp_receive/4)
	),
	'$udp_data'(Enc, Raw, As, Data),
	'$normalise_peer'(Host, Ip),
	From = Ip:Port.

%% udp_send(+Socket, +Data, +To, +Options).
%
% To is Host:Port. Under encoding(octet) Data must be a list of byte
% values; otherwise an atom, string, chars/codes list, number or - with
% as(term) - any term.

udp_send(Socket, Data, To, Options) :-
	must_be(list, Options),
	'$udp_enc'(Options, Enc, udp_send/4),
	'$udp_as'(Options, As, udp_send/4),
	'$udp_stream'(Socket, Stream, udp_send/4),
	'$send_addr'(To, Host, Port),
	'$udp_payload'(Enc, As, Data, Payload),
	'$udp_bifopts'(Options, Enc, BifOpts),
	'$sock_call'('$udp_send'(Stream, Payload, Host, Port, BifOpts), udp_send/4).

'$send_addr'(To, _, _) :-
	var(To), !,
	throw(error(instantiation_error, udp_send/4)).
'$send_addr'(Host0:Port, Host, Port) :- !,
	must_be(integer, Port),
	'$host_atom'(Host0, Host).
'$send_addr'(To, _, _) :-
	throw(error(domain_error(socket_address, To), udp_send/4)).

% Only the options the bifs understand are passed down; the rest are
% interpreted here.

'$udp_bifopts'(Options, Enc, BifOpts) :-
	(  memberchk(max_message_size(N), Options)
	-> Rest0 = [max_message_size(N)]
	;  Rest0 = []
	),
	(  memberchk(timeout(T), Options)
	-> must_be(integer, T),
	   Rest = [timeout(T)|Rest0]
	;  Rest = Rest0
	),
	(  Enc == octet
	-> BifOpts = [encoding(octet)|Rest]
	;  BifOpts = Rest
	).

'$udp_as'(Options, As, Ctx) :-
	(  memberchk(as(A), Options)
	-> (  '$udp_as_type'(A)
	   -> As = A
	   ;  throw(error(domain_error(udp_as, A), Ctx))
	   )
	;  As = string
	).

'$udp_as_type'(atom).
'$udp_as_type'(codes).
'$udp_as_type'(chars).
'$udp_as_type'(string).
'$udp_as_type'(term).

% iso_latin_1 is rejected rather than quietly approximated - the layer
% below is UTF-8 or raw bytes, and nothing here would do the transcoding.

'$udp_enc'(Options, Enc, Ctx) :-
	(  memberchk(encoding(E), Options)
	-> (  E == octet
	   -> Enc = octet
	   ;  ( E == utf8 ; E == text )
	   -> Enc = text
	   ;  throw(error(domain_error(encoding, E), Ctx))
	   )
	;  Enc = text
	).

% Under octet the bif hands back byte values; otherwise a string.

% No atom is built unless as(atom) was asked for - read_term_from_atom/3
% accepts a string, which is what the text path already holds, so the
% term case needs no intermediate either.
%
% as(term) is the one option that costs something permanent. Reading a
% term interns the functor and atom names it contains, and unlike
% ordinary atom construction that *does* grow the symbol table: measured
% at 950 new symbols for 1000 distinct datagrams. Parsing terms from an
% untrusted peer is therefore a slow memory leak, and is documented as
% such rather than prevented - SWI behaves the same way.

'$udp_data'(octet, Raw, As, Data) :- !,
	(  As == codes
	-> Data = Raw
	;  As == atom
	-> atom_codes(Data, Raw)
	;  string_codes(S, Raw),
	   '$udp_text'(As, S, Data)
	).
'$udp_data'(text, Raw, As, Data) :-
	(  As == atom
	-> atom_chars(Data, Raw)
	;  '$udp_text'(As, Raw, Data)
	).

'$udp_text'(string, S, S).
'$udp_text'(chars, S, S).
'$udp_text'(codes, S, C) :- string_codes(S, C).
'$udp_text'(term, S, T) :- read_term_from_atom(S, T, []).

'$udp_payload'(octet, _, Data, Data) :- !,
	must_be(list, Data).
'$udp_payload'(text, As, Data, Payload) :-
	(  ( atom(Data) ; is_list(Data) )
	-> Payload = Data
	;  number(Data)
	-> number_codes(Data, Payload)
	;  As == term
	-> format(string(Payload), "~q", [Data])
	;  throw(error(type_error(text, Data), udp_send/4))
	).

% --- server ------------------------------------------------------------

%% tcp_bind(+Socket, ?Address).
%
% Binds AND listens - see the module header. Address may leave the port
% unbound to request an ephemeral one, in which case it is unified with
% the port actually assigned.

tcp_bind(Socket, Address) :-
	'$sock_get'(Socket, Domain, Type, Phase),
	(  Phase == fresh
	-> true
	;  throw(error(permission_error(bind, socket, Socket), tcp_bind/2))
	),
	'$bind_spec'(Domain, Address, Spec),
	'$sock_opts'(Domain, Type, Opts),
	'$sock_call'('$server'(Spec, Stream, Opts), tcp_bind/2),
	'$sock_set'(Socket, listening(Stream)).

% What to hand '$server'. A wholly or partly unbound address is passed
% through as a TERM rather than flattened to an atom, because that is how
% the bif reports the port it actually bound.

'$bind_spec'(unix, Path0, Spec) :- !,
	'$must_be_text'(Path0, Path),
	atom_concat('unix://', Path, Spec).
'$bind_spec'(_, Address, Spec) :-
	var(Address), !,
	Spec = Address.
'$bind_spec'(_, Host:Port, Spec) :-
	var(Port), !,
	'$host_atom'(Host, H),
	Spec = H:Port.
'$bind_spec'(_, Address, Spec) :-
	'$addr_atom'(Address, Spec).

%% tcp_listen(+Socket, +BackLog).
%
% The bind already listened, so this validates and returns. BackLog is
% accepted and ignored.

tcp_listen(Socket, BackLog) :-
	must_be(integer, BackLog),
	'$sock_get'(Socket, _, _, Phase),
	(  Phase = listening(_)
	-> true
	;  throw(error(permission_error(listen, socket, Socket), tcp_listen/2))
	).

%% tcp_accept(+Socket, -Slave, -Peer).
%
% Blocks until a client connects. Slave is a new socket, already
% materialised - the stream exists before the handle does, so
% tcp_open_socket/2 on it is a lookup.

tcp_accept(Socket, Slave, Peer) :-
	'$sock_get'(Socket, Domain, Type, Phase),
	(  Phase = listening(Stream)
	-> true
	;  throw(error(permission_error(accept, socket, Socket), tcp_accept/3))
	),
	'$sock_call'('$accept'(Stream, Client), tcp_accept/3),
	(  catch('$peer_addr'(Client, Addr, _), _, fail)
	-> '$normalise_peer'(Addr, Peer)
	;  Peer = ip(0,0,0,0)
	),
	'$new_id'(Id),
	assertz('$sock'(Id, Domain, Type, connected(Client))),
	Slave = '$socket'(Id).

% --- client ------------------------------------------------------------

%% tcp_connect(+Socket, +Address).
%
% Connects an existing socket. This is where the OS socket is actually
% created - see the module header.

tcp_connect(Socket, Address) :-
	'$sock_get'(Socket, Domain, Type, Phase),
	(  Phase == fresh
	-> true
	;  throw(error(permission_error(connect, socket, Socket), tcp_connect/2))
	),
	'$connect_addr'(Domain, Address, Atom),
	'$sock_opts'(Domain, Type, Opts),
	'$sock_call'('$client'(Atom, _, _, Stream, Opts), tcp_connect/2),
	'$sock_set'(Socket, connected(Stream)).

'$connect_addr'(unix, Path0, Atom) :- !,
	'$must_be_text'(Path0, Path),
	atom_concat('unix://', Path, Atom).
'$connect_addr'(_, Address, Atom) :-
	'$addr_atom'(Address, Atom).

%% tcp_connect(+Address, -StreamPair, +Options).
%
% The modern form: creates the socket, connects, and hands back the
% stream in one step. Options are accepted for compatibility.

tcp_connect(Address, StreamPair, Options) :-
	must_be(list, Options),
	tcp_socket(Socket),
	catch(tcp_connect(Socket, Address),
	      E,
	      ( catch(tcp_close_socket(Socket), _, true), throw(E) )),
	tcp_open_socket(Socket, StreamPair).

%% tcp_connect(+Socket, +Address, -Read, -Write).
%
% Deprecated in SWI. Read and Write are the same stream here.

tcp_connect(Socket, Address, Read, Write) :-
	tcp_connect(Socket, Address),
	tcp_open_socket(Socket, Read, Write).

% --- streams -----------------------------------------------------------

%% tcp_open_socket(+Socket, -StreamPair).
%
% Trealla socket streams are bidirectional, so the "pair" is one stream.

tcp_open_socket(Socket, StreamPair) :-
	'$sock_stream'(Socket, StreamPair).

%% tcp_open_socket(+Socket, -Read, -Write).

tcp_open_socket(Socket, Read, Write) :-
	'$sock_stream'(Socket, Stream),
	Read = Stream,
	Write = Stream.

%% tcp_close_socket(+Socket).
%
% Safe on a socket that never materialised, and on one already closed -
% SWI's is not fussy here either, and leaking a handle is worse.

tcp_close_socket(Socket) :-
	'$sock_get'(Socket, _, _, Phase),
	(  Phase = connected(S) -> catch(close(S), _, true)
	;  Phase = listening(S) -> catch(close(S), _, true)
	;  true
	),
	Socket = '$socket'(Id),
	retractall('$sock'(Id, _, _, _)).

% --- options -----------------------------------------------------------

%% tcp_setopt(+Socket, +Option).
%
% `reuseaddr` is already unconditionally set by the underlying layer, so
% it is accepted and ignored rather than refused. `dispatch/1` is GUI
% related and has no meaning here. Options needing setsockopt calls that
% Trealla does not expose are refused rather than silently dropped.

tcp_setopt(Socket, Option) :-
	'$sock_get'(Socket, _, _, _),
	must_be(nonvar, Option),
	'$setopt'(Option, Socket).

'$setopt'(reuseaddr, _) :- !.
'$setopt'(dispatch(_), _) :- !.
'$setopt'(nodelay, S) :- !, '$setopt'(nodelay(true), S).
'$setopt'(nodelay(_), _) :- !.
'$setopt'(Option, _) :-
	throw(error(domain_error(socket_option, Option), tcp_setopt/2)).

%% tcp_getopt(+Socket, ?Option).
%
% Only `file_no/1`, which is what SWI documents.

tcp_getopt(Socket, file_no(F)) :- !,
	'$sock_stream'(Socket, Stream),
	stream_property(Stream, file_no(F)).
tcp_getopt(_, Option) :-
	throw(error(domain_error(socket_option, Option), tcp_getopt/2)).

% --- names -------------------------------------------------------------

%% gethostname(-Host).

gethostname(Host) :- '$current_host'(Host).

%% tcp_host_to_address(?Host, ?Address).
%
% Resolution is one-way here: name to address. SWI's reverse direction
% needs a PTR lookup that Trealla does not expose.

tcp_host_to_address(Host0, Address) :-
	nonvar(Host0), !,
	'$must_be_text'(Host0, Host),
	(  '$host_address'(Host, Atom)
	-> '$normalise_peer'(Atom, Address)
	;  throw(error(existence_error(host, Host), tcp_host_to_address/2))
	).
tcp_host_to_address(_, _) :-
	throw(error(instantiation_error, tcp_host_to_address/2)).

% host_address(+Host, -Address, +Options): each address the resolver gives
% for Host on backtracking, as tcp_host_to_address/2 gives the first, so a
% caller can vet them all before connecting to one. SWI returns sockaddr
% dicts; this returns the addresses. Options can be domain(inet|inet6).

host_address(Host0, Address, Options) :-
	'$must_be_text'(Host0, Host),
	must_be(list, Options),
	(	memberchk(domain(D), Options) -> true ; true ),
	'$host_addresses'(Host, Atoms),
	(	Atoms == [] -> throw(error(existence_error(host, Host), host_address/3)) ; true ),
	member(Atom, Atoms),
	'$normalise_peer'(Atom, Address),
	(	D == inet -> Address = ip(_,_,_,_)
	;	D == inet6 -> Address \= ip(_,_,_,_)
	;	true
	).
