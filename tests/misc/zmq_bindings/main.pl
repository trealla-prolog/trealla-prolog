% library(zmq): request-reply over TCP on a port the system picks, text with a NUL and non-ASCII characters,
% empty messages, timeouts and dontwait failing, multipart push-pull and filtered pub-sub over inproc, and
% errors. Run by ../zmq_bindings.sh, which skips it where libzmq or the FFI is missing. Strings are shown as
% atoms.

:- use_module(library(zmq)).
:- use_module(library(lists)).
:- initialization(main).

show(S, A) :- string(S), !, atom_string(A, S).
show(T, A) :- compound(T), !, T =.. [F|As], maplist(show, As, Bs), A =.. [F|Bs].
show(T, T).

% A zmq_error without its errno, which is the system's own for most

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (   var(E) -> show(Show, S), format("~w: ~q~n", [Label, S])
	    ;   E = error(zmq_error(_, M), Ctx) -> format("~w: raised ~q~n", [Label, error(zmq_error('_', M), Ctx)])
	    ;   format("~w: raised ~q~n", [Label, E])
	    )
	;   format("~w: failed~n", [Label])
	).

% Published until one gets through, as a subscription reaches the publisher some time after connecting

pub_until(Pub, Sub, N, Msg) :-
	N > 0,
	zmq_send(Pub, "sport 1"),
	zmq_send(Pub, "news 2"),
	(	zmq_recv(Sub, Msg) -> true
	;	N1 is N - 1,
		pub_until(Pub, Sub, N1, Msg)
	).

main :-
	zmq_context(C),
	zmq_open(C, rep, Rep),
	zmq_bind(Rep, 'tcp://127.0.0.1:*'),
	zmq_last_endpoint(Rep, EP),
	t(endpoint, atom_concat('tcp://127.0.0.1:', _, EP), yes),
	zmq_open(C, req, Req),
	zmq_connect(Req, EP),
	t(request, (zmq_send(Req, "héllo\0\wörld"), zmq_recv(Rep, M1), length(M1, L1)), L1-M1),
	t(same_text, M1 == "héllo\0\wörld", yes),
	t(empty_reply, (zmq_send(Rep, ''), zmq_recv(Req, M2)), M2),
	t(timeout, (zmq_set_option(Rep, rcvtimeo(50)), zmq_recv(Rep, _)), x),
	t(dontwait, zmq_recv(Rep, _, [dontwait]), x),
	t(wrong_state, zmq_send(Rep, x), x),
	t(number_and_atom, (zmq_send(Req, 42), zmq_recv(Rep, M3), zmq_send(Rep, ok), zmq_recv(Req, M4)), M3/M4),
	zmq_open(C, push, Push),
	zmq_open(C, pull, Pull),
	zmq_bind(Pull, 'inproc://pipe'),
	zmq_connect(Push, 'inproc://pipe'),
	t(multipart, (zmq_send_multipart(Push, [a, "b", 42, [c,d], ""]), zmq_recv_multipart(Pull, Ps)), Ps),
	t(more_option, (zmq_send(Push, x, [more]), zmq_send(Push, y), zmq_recv_multipart(Pull, Ps2)), Ps2),
	zmq_open(C, pub, Pub),
	zmq_open(C, sub, Sub),
	zmq_bind(Pub, 'inproc://news'),
	zmq_connect(Sub, 'inproc://news'),
	zmq_subscribe(Sub, "news"),
	zmq_set_option(Sub, rcvtimeo(50)),
	t(filtered, pub_until(Pub, Sub, 100, M5), M5),
	t(bad_type, zmq_open(C, nosuch, _), x),
	t(bad_transport, zmq_bind(Push, 'bogus://x'), x),
	t(bad_option, zmq_set_option(Push, nosuch(1)), x),
	t(bad_flag, zmq_recv(Pull, _, [more]), x),
	t(empty_multipart, zmq_send_multipart(Push, []), x),
	maplist(zmq_set_option_(linger(0)), [Rep, Req, Push, Pull, Pub, Sub]),
	maplist(zmq_close, [Rep, Req, Push, Pull, Pub, Sub]),
	t(destroyed, zmq_context_destroy(C), yes).

zmq_set_option_(O, S) :- zmq_set_option(S, O).
