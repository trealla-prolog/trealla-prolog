:- module(zmq, [
	zmq_context/1,
	zmq_context_destroy/1,
	zmq_open/3,
	zmq_close/1,
	zmq_bind/2,
	zmq_connect/2,
	zmq_unbind/2,
	zmq_disconnect/2,
	zmq_send/2,
	zmq_send/3,
	zmq_recv/2,
	zmq_recv/3,
	zmq_send_multipart/2,
	zmq_recv_multipart/2,
	zmq_subscribe/2,
	zmq_unsubscribe/2,
	zmq_set_option/2,
	zmq_last_endpoint/2,
	zmq_version/1,

	zmq_ctx_new/1,
	zmq_ctx_term/2,
	zmq_ctx_shutdown/2,
	zmq_socket/3,
	zmq_close/2,
	zmq_bind/3,
	zmq_connect/3,
	zmq_unbind/3,
	zmq_disconnect/3,
	zmq_send/5,
	zmq_setsockopt/5,
	zmq_getsockopt/5,
	zmq_msg_init/2,
	zmq_msg_recv/4,
	zmq_msg_data/2,
	zmq_msg_size/2,
	zmq_msg_more/2,
	zmq_msg_close/2,
	zmq_errno/1,
	zmq_strerror/2,
	zmq_version/4,
	zmq_has/2
	]).

% ZeroMQ messaging (https://zeromq.org)
%
% UBUNTU: sudo apt install libzmq3-dev
% MACOS: brew install zeromq
%
%	?- zmq_context(C), zmq_open(C, rep, S), zmq_bind(S, 'tcp://*:5555'),
%		zmq_recv(S, Msg), zmq_send(S, "World").
%
% Messages are text, sent as UTF-8 and received as strings, NULs and all. A receive that times
% out (rcvtimeo) or finds nothing waiting (dontwait) fails, as does a send that can't go yet;
% other errors are error(zmq_error(Errno, Message), _), Message an atom.
%
% The zmq_* predicates of other arities are the C functions.

:- use_module(library(lists)).

:- use_foreign_module('libzmq.so', [
	zmq_ctx_new([], ptr),
	zmq_ctx_term([ptr], sint),
	zmq_ctx_shutdown([ptr], sint),
	zmq_socket([ptr,sint], ptr),
	zmq_close([ptr], sint),
	zmq_bind([ptr,cstr], sint),
	zmq_connect([ptr,cstr], sint),
	zmq_unbind([ptr,cstr], sint),
	zmq_disconnect([ptr,cstr], sint),
	zmq_send([ptr,ptr,ptr,sint], sint),					% size_t as ptr, its width
	zmq_setsockopt([ptr,sint,ptr,ptr], sint),
	zmq_getsockopt([ptr,sint,ptr,ptr], sint),
	zmq_msg_init([ptr], sint),
	zmq_msg_recv([ptr,ptr,sint], sint),
	zmq_msg_data([ptr], ptr),
	zmq_msg_size([ptr], ptr),
	zmq_msg_more([ptr], sint),
	zmq_msg_close([ptr], sint),
	zmq_errno([], sint),
	zmq_strerror([sint], ccstr),
	zmq_version([-sint,-sint,-sint], void),
	zmq_has([cstr], sint)
	]).

zmq_version(Major-Minor-Patch) :-
	zmq_version(Major, Minor, Patch).

zmq_context(Ctx) :-
	zmq_ctx_new(Ctx),
	nonzero_(Ctx, zmq_context/1).

% Waits for sockets still open, and for messages still going out of closed ones as linger allows

zmq_context_destroy(Ctx) :-
	must_be_(integer, Ctx, zmq_context_destroy/1),
	zmq_ctx_term(Ctx, Rc),
	rc_(Rc, zmq_context_destroy/1).

% Socket, a new one of Type (pair, pub, sub, req, rep, dealer, router, pull, push, xpub, xsub
% or stream) in Ctx

zmq_open(Ctx, Type, Socket) :-
	must_be_(integer, Ctx, zmq_open/3),
	must_be_(atom, Type, zmq_open/3),
	(	socket_type_(Type, T) -> true
	;	throw(error(domain_error(zmq_socket_type, Type), zmq_open/3))
	),
	zmq_socket(Ctx, T, Socket),
	nonzero_(Socket, zmq_open/3).

socket_type_(pair, 0).
socket_type_(pub, 1).
socket_type_(sub, 2).
socket_type_(req, 3).
socket_type_(rep, 4).
socket_type_(dealer, 5).
socket_type_(router, 6).
socket_type_(pull, 7).
socket_type_(push, 8).
socket_type_(xpub, 9).
socket_type_(xsub, 10).
socket_type_(stream, 11).

zmq_close(Socket) :-
	must_be_(integer, Socket, zmq_close/1),
	zmq_close(Socket, Rc),
	rc_(Rc, zmq_close/1).

zmq_bind(Socket, Endpoint) :- endpoint_(zmq_bind, Socket, Endpoint, zmq_bind/2).
zmq_connect(Socket, Endpoint) :- endpoint_(zmq_connect, Socket, Endpoint, zmq_connect/2).
zmq_unbind(Socket, Endpoint) :- endpoint_(zmq_unbind, Socket, Endpoint, zmq_unbind/2).
zmq_disconnect(Socket, Endpoint) :- endpoint_(zmq_disconnect, Socket, Endpoint, zmq_disconnect/2).

endpoint_(F, Socket, Endpoint, Ctx) :-
	must_be_(integer, Socket, Ctx),
	must_be_(text, Endpoint, Ctx),
	text_atom_(Endpoint, E),
	call(F, Socket, E, Rc),
	rc_(Rc, Ctx).

% Options: more, that further parts of this message follow, and dontwait

zmq_send(Socket, Text) :-
	send_(Socket, Text, 0, zmq_send/2).

zmq_send(Socket, Text, Options) :-
	must_be_(list, Options, zmq_send/3),
	flags_(Options, 0, Flags, zmq_send/3),
	send_(Socket, Text, Flags, zmq_send/3).

send_(Socket, Text, Flags, Ctx) :-
	must_be_(integer, Socket, Ctx),
	text_(Text, T, Ctx),
	setup_call_cleanup(
		'$text_to_pointer'(T, P, Len),
		send_rc_(Socket, P, Len, Flags, Rc, E),
		'$free_struct_pointer'(P)),
	(	Rc >= 0 -> true
	;	errno_(E, Ctx)
	).

% zmq_errno() read at once, before anything else can change errno

send_rc_(Socket, P, Len, Flags, Rc, E) :-
	zmq_send(Socket, P, Len, Flags, Rc),
	(	Rc < 0 -> zmq_errno(E) ; E = 0 ).

flags_([], F, F, _).
flags_([O|Os], F0, F, Ctx) :-
	must_be_(atom, O, Ctx),
	(	flag_(O, B) -> F1 is F0 \/ B
	;	throw(error(domain_error(zmq_option, O), Ctx))
	),
	flags_(Os, F1, F, Ctx).

flag_(dontwait, 1).
flag_(more, 2).

% Options: dontwait

zmq_recv(Socket, Text) :-
	recv_(Socket, 0, Text, _, zmq_recv/2).

zmq_recv(Socket, Text, Options) :-
	must_be_(list, Options, zmq_recv/3),
	flags_(Options, 0, Flags, zmq_recv/3),
	(	Flags /\ 2 =:= 0 -> true
	;	throw(error(domain_error(zmq_option, more), zmq_recv/3))
	),
	recv_(Socket, Flags, Text, _, zmq_recv/3).

% A zmq_msg_t is 64 bytes, pointer-aligned, which a block of eight uint64 is

recv_(Socket, Flags, Text, More, Ctx) :-
	must_be_(integer, Socket, Ctx),
	setup_call_cleanup(
		'$array_to_pointer'(uint64, [0,0,0,0,0,0,0,0], M),
		(	zmq_msg_init(M, _),
			zmq_msg_recv(M, Socket, Flags, Rc),
			(	Rc >= 0
			->	zmq_msg_data(M, P),
				zmq_msg_size(M, N),
				'$pointer_to_text'(P, N, Text0),
				zmq_msg_more(M, More),
				zmq_msg_close(M, _)
			;	zmq_errno(E),
				zmq_msg_close(M, _)
			)
		),
		'$free_struct_pointer'(M)),
	(	Rc >= 0 -> Text = Text0
	;	errno_(E, Ctx)
	).

% Each part of List as one message

zmq_send_multipart(Socket, Parts) :-
	must_be_(list, Parts, zmq_send_multipart/2),
	(	Parts = [] -> throw(error(domain_error(non_empty_list, []), zmq_send_multipart/2))
	;	true
	),
	send_parts_(Parts, Socket).

send_parts_([P], Socket) :- !,
	send_(Socket, P, 0, zmq_send_multipart/2).
send_parts_([P|Ps], Socket) :-
	send_(Socket, P, 2, zmq_send_multipart/2),
	send_parts_(Ps, Socket).

zmq_recv_multipart(Socket, Parts) :-
	recv_(Socket, 0, P, More, zmq_recv_multipart/2),
	(	More =:= 0 -> Parts = [P]
	;	Parts = [P|Ps],
		zmq_recv_multipart(Socket, Ps)
	).

zmq_subscribe(Socket, Prefix) :-
	zmq_set_option(Socket, subscribe(Prefix)).

zmq_unsubscribe(Socket, Prefix) :-
	zmq_set_option(Socket, unsubscribe(Prefix)).

% Option: linger(Ms), rcvtimeo(Ms), sndtimeo(Ms), sndhwm(N), rcvhwm(N), subscribe(Prefix),
% unsubscribe(Prefix) or routing_id(Id)

zmq_set_option(Socket, Option) :-
	must_be_(integer, Socket, zmq_set_option/2),
	must_be_(compound, Option, zmq_set_option/2),
	(	Option =.. [Name, V], option_(Name, Code, Type) -> true
	;	throw(error(domain_error(zmq_option, Option), zmq_set_option/2))
	),
	set_option_(Type, Socket, Code, V).

option_(routing_id, 5, text).
option_(subscribe, 6, text).
option_(unsubscribe, 7, text).
option_(linger, 17, int).
option_(sndhwm, 23, int).
option_(rcvhwm, 24, int).
option_(rcvtimeo, 27, int).
option_(sndtimeo, 28, int).

set_option_(int, Socket, Code, V) :-
	must_be_(integer, V, zmq_set_option/2),
	setup_call_cleanup(
		'$array_to_pointer'(sint, [V], P),
		setsockopt_(Socket, Code, P, 4, E),
		'$free_struct_pointer'(P)),
	rc_(E, zmq_set_option/2).
set_option_(text, Socket, Code, V) :-
	text_(V, T, zmq_set_option/2),
	setup_call_cleanup(
		'$text_to_pointer'(T, P, Len),
		setsockopt_(Socket, Code, P, Len, E),
		'$free_struct_pointer'(P)),
	rc_(E, zmq_set_option/2).

setsockopt_(Socket, Code, P, Len, E) :-
	zmq_setsockopt(Socket, Code, P, Len, Rc),
	(	Rc =:= 0 -> E = 0 ; zmq_errno(E) ).

% The endpoint Socket last bound to, with the port a wildcard (tcp://127.0.0.1:*) chose

zmq_last_endpoint(Socket, Endpoint) :-
	must_be_(integer, Socket, zmq_last_endpoint/2),
	length(Zs, 256),
	maplist(=(0), Zs),
	setup_call_cleanup(
		(	'$array_to_pointer'(uint8, Zs, B),
			'$array_to_pointer'(ptr, [256], L)
		),
		(	zmq_getsockopt(Socket, 32, B, L, Rc),
			rc_(Rc, zmq_last_endpoint/2),
			'$pointer_to_array'(ptr, L, 1, [N]),
			N1 is max(N - 1, 0),					% less its NUL
			'$pointer_to_text'(B, N1, S)
		),
		(	'$free_struct_pointer'(B),
			'$free_struct_pointer'(L)
		)),
	atom_chars(Endpoint, S).

nonzero_(P, _) :- P =\= 0, !.
nonzero_(_, Ctx) :- errno_(Ctx).

% 0, or an errno already read

rc_(0, _) :- !.
rc_(E, Ctx) :- E > 0, !, errno_(E, Ctx).
rc_(_, Ctx) :- errno_(Ctx).

errno_(Ctx) :-
	zmq_errno(E),
	errno_(E, Ctx).

% EAGAIN, a timeout or nothing ready, fails; its number is the system's

errno_(E, _) :- eagain_(E), !, fail.
errno_(E, Ctx) :-
	zmq_strerror(E, Msg),
	throw(error(zmq_error(E, Msg), Ctx)).

eagain_(E) :-
	current_prolog_flag(os, OS),
	(	memberchk(OS, [macos, ios, freebsd, openbsd, netbsd, dragonfly]) -> E =:= 35
	;	E =:= 11
	).

text_(T, T, _) :- atom(T), !.
text_(T, T, _) :- string(T), !.
text_(T, A, _) :- number(T), !, number_codes(T, Cs), atom_codes(A, Cs).
text_(T, _, Ctx) :- var(T), !, throw(error(instantiation_error, Ctx)).
text_(T, S, _) :- is_list(T), catch(format(string(S), "~s", [T]), _, fail), !.
text_(T, _, Ctx) :- throw(error(type_error(text, T), Ctx)).

text_atom_(T, A) :- atom(T), !, A = T.
text_atom_(T, A) :- atom_chars(A, T).

must_be_(_, X, Ctx) :- var(X), !, throw(error(instantiation_error, Ctx)).
must_be_(integer, X, Ctx) :- !, (integer(X) -> true ; throw(error(type_error(integer, X), Ctx))).
must_be_(atom, X, Ctx) :- !, (atom(X) -> true ; throw(error(type_error(atom, X), Ctx))).
must_be_(compound, X, Ctx) :- !, (compound(X) -> true ; throw(error(type_error(compound, X), Ctx))).
must_be_(list, X, Ctx) :- !, (is_list(X) -> true ; throw(error(type_error(list, X), Ctx))).
must_be_(text, X, Ctx) :- ((atom(X) ; string(X)) -> true ; throw(error(type_error(text, X), Ctx))).
