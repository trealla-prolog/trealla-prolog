% process_wait/3 blocked in waitpid(), which saw no time limit, Ctrl-C or thread_cancel/1, and a thread_signal/2's
% SIGUSR2 brought it back EINTR, which failed the wait. It now waits in slices: a signal runs during the wait,
% which goes on unless the signal throws, and a time limit or a cancel stops it.

:- initialization(main).

:- dynamic(event/1).

st(T, Name) :-
	( catch(thread_property(T, status(St)), _, fail) -> true ; St = gone ),
	format("~w: ~q~n", [Name, St]).

main :-
	process_create('/bin/sleep', ['1'], [process(P1)]),
	thread_create((process_wait(P1, S1), assertz(event(done(S1)))), T1, []),
	sleep(0.3), thread_signal(T1, assertz(event(signal))),
	thread_join(T1, J1), findall(E, event(E), Es), writeq(signal_then_wait(J1, Es)), nl,
	process_create('/bin/sleep', ['5'], [process(P2)]),
	thread_create(catch(process_wait(P2, _), stop, writeln(wait_abandoned)), T2, []),
	sleep(0.3), thread_signal(T2, throw(stop)), thread_join(T2, J2), writeq(thrown(J2)), nl,
	process_kill(P2), process_wait(P2, _),
	process_create('/bin/sleep', ['5'], [process(P3)]),
	get_time(W0),
	catch(call_with_time_limit(0.3, process_wait(P3, _)), error(time_limit_exceeded(_, _), _), writeln(time_limit)),
	get_time(W1), ( W1 - W0 < 2 -> writeln(stopped_in_time) ; writeln(too_slow) ),
	process_kill(P3), process_wait(P3, _),
	process_create('/bin/sleep', ['5'], [process(P4)]),
	thread_create(process_wait(P4, _), T4, [at_exit(writeln(at_exit_ran))]),
	sleep(0.3), thread_cancel(T4), sleep(0.5), st(T4, cancelled),
	process_kill(P4), process_wait(P4, _).
