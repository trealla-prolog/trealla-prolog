% A mutex keeps the tasks of one thread apart, not only threads: the
% mutex records which task holds it, as the guard under it is recursive.
% Builds without threads have no mutexes, and print the same lines.

:- initialization(main).

report(Name, Got, Expect) :-
	(	Got == Expect
	->	format("~w: ok~n", [Name])
	;	format("~w: FAILED got ~q wanted ~q~n", [Name,Got,Expect])
	).

wait_for(K, N) :-
	(	bb_get(K, true) -> true
	;	N > 0 -> task_yield, N1 is N - 1, wait_for(K, N1)
	;	true
	).

% Two tasks that sleep inside with_mutex/2 never both get in.

sleepy_bump(0) :- !.
sleepy_bump(N) :-
	with_mutex(m, (bb_get(count, C), sleep(0.001), C1 is C + 1, bb_put(count, C1))),
	N1 is N - 1,
	sleepy_bump(N1).

sleepers :-
	bb_put(count, 0),
	task_create(sleepy_bump(50), _),
	task_create(sleepy_bump(50), _),
	task_wait,
	bb_get(count, C),
	report(sleepers, C, 100).

% Nor do two preempted by task_time_slice. The countdown puts most goals inside.

countdown(0) :- !.
countdown(N) :- N1 is N - 1, countdown(N1).

sliced_bump(0) :- !.
sliced_bump(N) :-
	with_mutex(m, (bb_get(count, C), countdown(20), C1 is C + 1, bb_put(count, C1))),
	N1 is N - 1,
	sliced_bump(N1).

sliced :-
	bb_put(count, 0),
	set_prolog_flag(task_time_slice, 1),
	task_create(sliced_bump(10000), _),
	task_create(sliced_bump(10000), _),
	task_wait,
	set_prolog_flag(task_time_slice, 0),
	bb_get(count, C),
	report(sliced, C, 20000).

% The task holding a mutex can take it again.

relock :-
	task_create((with_mutex(m, with_mutex(m, true)) -> bb_put(rep, ok) ; bb_put(rep, failed)), _),
	task_wait,
	bb_get(rep, R),
	report(relock, R, ok).

% Another task can neither take it nor unlock it.

holder :-
	mutex_lock(m),
	bb_put(locked, true),
	wait_for(tried, 1000),
	mutex_unlock(m).

intruder :-
	wait_for(locked, 1000),
	( mutex_trylock(m) -> T = got_it ; T = refused ),
	catch((mutex_unlock(m), U = unlocked), error(E, _), U = E),
	bb_put(intrusion, T-U),
	bb_put(tried, true).

not_mine :-
	bb_put(locked, false),
	bb_put(tried, false),
	task_create(holder, _),
	task_create(intruder, _),
	task_wait,
	bb_get(intrusion, R),
	report(not_mine, R, refused-permission_error(mutex_unlock,not_locked_by_me,m)).

% A task cancelled while holding one leaves it free.

sleeper :- mutex_lock(m), bb_put(held, true), sleep(30).

retry_lock(0, failed) :- !.
retry_lock(N, R) :- ( mutex_trylock(m) -> R = got ; task_yield, N1 is N - 1, retry_lock(N1, R) ).

canceller :-
	task_create(sleeper, Id),
	wait_for(held, 1000),
	( mutex_trylock(m) -> mutex_unlock(m), Before = got ; Before = refused ),
	task_cancel(Id),
	retry_lock(1000, After),
	( After == got -> mutex_unlock(m) ; true ),
	bb_put(cancelled, Before-After).

cancelled :-
	bb_put(held, false),
	task_create(canceller, _),
	task_wait,
	bb_get(cancelled, R),
	report(cancelled, R, refused-got).

% The thread's own query cannot wait for a task of its thread to let go.

lender :- mutex_lock(m), task_end_wait, task_yield, mutex_unlock(m).

thread_waits :-
	task_create(lender, _),
	task_wait,
	catch((mutex_lock(m), mutex_unlock(m), E = locked), error(E0, _), E = E0),
	task_wait,
	( mutex_trylock(m) -> mutex_unlock(m), After = got ; After = refused ),
	report(thread_waits, E-After, permission_error(lock,mutex,m)-got).

% A task waits for another thread to let go.

other_thread :-
	mutex_lock(m),
	bb_put(thread_holds, true),
	sleep(0.05),
	bb_put(order, thread_first),
	mutex_unlock(m).

across_threads :-
	bb_put(thread_holds, false),
	bb_put(order, task_first),
	thread_create(other_thread, T, []),
	task_create((wait_for(thread_holds, 1000000), with_mutex(m, (bb_get(order, O), bb_put(seen, O)))), _),
	task_wait,
	thread_join(T, _),
	bb_get(seen, S),
	report(across_threads, S, thread_first).

checks([sleepers, sliced, relock, not_mine, cancelled, thread_waits, across_threads]).

main :-
	checks(Cs),
	(	catch(mutex_create(_, [alias(m)]), _, fail)
	->	maplist(call, Cs)
	;	forall(member(C, Cs), format("~w: ok~n", [C]))		% no mutexes in this build
	).
