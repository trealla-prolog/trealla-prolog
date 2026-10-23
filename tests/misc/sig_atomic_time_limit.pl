% sig_atomic/1 and setup_call_cleanup/3's Setup hold a time limit that runs
% out inside them, as in SWI. It throws as soon as they are done, unless its
% own call_with_time_limit/2 has ended first (#1179).

:- initialization(main).

spin(T) :- get_time(T0), repeat, get_time(N), N-T0 > T, !.

t(Name, G) :-
	( catch(G, E, true) -> ( var(E) -> R = true ; limit(E) -> R = time_limit_exceeded ; R = E ) ; R = failed ),
	format("~w: ~q~n", [Name, R]).

limit(time_limit_exceeded).
limit(error(time_limit_exceeded(_,_),_)).

mutex(R-S) :-
	mutex_create(M),
	t(mutex, call_with_time_limit(0.05, setup_call_cleanup((mutex_lock(M), spin(0.1)), true, mutex_unlock(M)))),
	( mutex_property(M, status(locked(_,_))) -> S = locked ; S = unlocked ),
	R = done.

handoff(V-Q) :-
	thread_self(S),
	nb_setval(saved, []),
	thread_send_message(S, hello),
	t(handoff, call_with_time_limit(0.05, (sig_atomic((thread_get_message(S, M), spin(0.1), nb_setval(saved, [M]))), spin(0.01)))),
	nb_getval(saved, V),
	( thread_peek_message(S, Q) -> true ; Q = empty ).

blocked(Held) :-
	thread_self(S),
	get_time(T0),
	t(blocked, call_with_time_limit(0.05, (sig_atomic(\+ thread_get_message(S, never, [timeout(0.2)])), spin(0.01)))),
	get_time(T1),
	( T1-T0 > 0.15 -> Held = held ; Held = cut_short ).

main :-
	mutex(R1), format("mutex after: ~q~n", [R1]),
	handoff(R2), format("handoff after: ~q~n", [R2]),
	blocked(R3), format("blocked after: ~q~n", [R3]),
	t(held_then_thrown, call_with_time_limit(0.05, (sig_atomic(spin(0.1)), spin(0.01)))),
	t(inner_dropped, sig_atomic(call_with_time_limit(0.05, spin(0.2)))),
	t(outer_survives_inner, call_with_time_limit(0.05, (sig_atomic((spin(0.1), call_with_time_limit(1, true))), spin(0.01)))),
	t(unheld, call_with_time_limit(0.05, spin(0.2))),
	t(nothing_stray, spin(0.1)).
