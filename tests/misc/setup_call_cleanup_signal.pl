% A thread signal queued during setup_call_cleanup/3's Setup waits until the
% cleanup is registered, so it cannot throw past a lock Setup took (#1180).
% Signals run again as soon as Setup fails or throws, and inside the body.

:- initialization(main).

:- dynamic(log/1).

note(X) :- assertz(log(X)).

signal(G) :- thread_self(Self), thread_signal(Self, G).

% A signal queued now runs before the next goal unless it is held.
signals_run(R) :- catch((signal(throw(late)), true, R = held), late, R = run).

run(G, R) :-
	retractall(log(_)),
	( catch(G, E, true) -> true ; E = failed ),
	( var(E) -> E = true ; true ),
	findall(X, log(X), Xs),
	signals_run(S),
	R = E-Xs-S.

t_queued(R) :- run(setup_call_cleanup((note(setup), signal(throw(stop))), note(body), note(cleanup)), R).
t_nested(R) :- run(setup_call_cleanup(setup_call_cleanup((signal(throw(stop)), note(s1)), note(b1), note(c1)), note(b2), note(c2)), R).
t_setup_fails(R) :- run(setup_call_cleanup(fail, note(body), note(cleanup)), R).
t_setup_throws(R) :- run(setup_call_cleanup(throw(oops), note(body), note(cleanup)), R).
t_unbound_cleanup(R) :- run(setup_call_cleanup(note(setup), note(body), _), R).
t_nondet(R) :- run(once(setup_call_cleanup(true, member(X, [a,b]), note(cleanup(X)))), R).
t_call_cleanup(R) :- run(call_cleanup(note(body), note(cleanup)), R).

% A worker blocked in Setup gets the signal once Setup is done.
blocked_worker(Main) :-
	retractall(log(_)),
	catch(setup_call_cleanup((thread_send_message(Main, waiting), thread_get_message(go), note(setup)), note(body), note(cleanup)), E, true),
	findall(X, log(X), Xs),
	thread_send_message(Main, result(E-Xs)).

t_blocked_setup(R) :-
	thread_self(Main),
	thread_create(blocked_worker(Main), T, []),
	thread_get_message(Main, waiting),
	thread_signal(T, throw(stop)),
	thread_send_message(T, go),
	thread_get_message(Main, result(R)),
	thread_join(T, _).

% A worker blocked in the body is still interrupted there.
body_worker(Main) :-
	retractall(log(_)),
	catch(setup_call_cleanup(note(setup), (thread_send_message(Main, waiting), thread_get_message(go), note(body)), note(cleanup)), E, true),
	findall(X, log(X), Xs),
	thread_send_message(Main, result(E-Xs)).

t_blocked_body(R) :-
	thread_self(Main),
	thread_create(body_worker(Main), T, []),
	thread_get_message(Main, waiting),
	thread_signal(T, throw(stop)),
	thread_get_message(Main, result(R)),
	thread_join(T, _).

% The issue's case: with_mutex/2 interrupted as it takes the lock.
mutex_worker(Main) :-
	thread_send_message(Main, ready),
	catch(with_mutex(scc_lock, true), E, true),
	mutex_property(scc_lock, status(S)),
	thread_send_message(Main, result(E-S)).

t_with_mutex(R) :-
	mutex_create(_, [alias(scc_lock)]),
	thread_self(Main),
	mutex_lock(scc_lock),
	thread_create(mutex_worker(Main), T, []),
	thread_get_message(Main, ready),
	sleep(0.01),
	thread_signal(T, throw(stop)),
	mutex_unlock(scc_lock),
	thread_get_message(Main, result(R)),
	thread_join(T, _),
	mutex_destroy(scc_lock).

main :-
	forall(
		member(T, [t_queued, t_nested, t_setup_fails, t_setup_throws, t_unbound_cleanup, t_nondet, t_call_cleanup, t_blocked_setup, t_blocked_body, t_with_mutex]),
		(	G =.. [T, R],
			(	catch(G, E, R = uncaught(E)) -> true ; R = failed ),
			format("~w: ~q~n", [T, R])
		)
	).
