% task_cancel/1 - cross-thread-safe cooperative task cancellation.
% Everything here is single-threaded (same-thread cancel only);
% cancelling a task from a real, different OS thread is what motivated
% the design (see the comment on cancel_requested in src/internal.h -
% writing the task's flags directly from a foreign thread would race
% with the packed bool:1 flags start() mutates constantly) but needs a
% real thread to actually exercise cross-thread, so belongs in
% tests/misc.
%
% Cancellation is cooperative, not preemptive: it lands at the next
% scheduling checkpoint (sched_run()'s dispatch, right before a task
% would otherwise run again), as unwind(abort) thrown at the task's next
% goal, so its cleanups run as abort/0's do. The looper below
% yields every iteration, so that checkpoint comes up every single
% tick - deliberately the easiest case, not a worst case.

:- initialization(main).
:- use_module(library(tabling)).

:- dynamic(tick/1).

report(Name, Got, Expect) :-
	(	Got == Expect
	->	format("~w: ok~n", [Name])
	;	format("~w: FAILED got ~q wanted ~q~n", [Name,Got,Expect])
	).

looper(N) :-
	retractall(tick(_)),
	assertz(tick(N)),
	N1 is N + 1,
	task_yield,
	looper(N1).

% Cancel a running task; it must actually stop (tick/1 stops changing)
% rather than merely being asked to.

cancel_stops_task :-
	retractall(tick(_)),
	assertz(tick(0)),
	task_create(looper(0), Qid),
	forall(between(1,20,_), task_yield),
	tick(Before),
	task_cancel(Qid),
	task_wait,
	tick(After),
	forall(between(1,20,_), task_yield),
	tick(Final),
	( After =< Before + 1, Final == After -> R = ok ; R = failed ),
	report(cancel_stops_task, R, ok).

% Cancelling a qid that does not exist (or already finished) is an
% error, not a silent no-op.

cancel_missing_qid_throws :-
	( catch(task_cancel(999999999), error(existence_error(task,_),_), true)
	-> R = threw_existence_error
	;  R = did_not_throw
	),
	report(cancel_missing_qid_throws, R, threw_existence_error).

% task_cancel/1 only takes tasks, not any addressable qid - the
% top-level query's own qid is registered and sendable-to (see
% task_messaging.pl) but is not a task, and cancelling it makes no
% sense.

cancel_non_task_throws :-
	task_self(Me),
	( catch(task_cancel(Me), error(existence_error(task,_),_), true)
	-> R = threw_existence_error
	;  R = did_not_throw
	),
	report(cancel_non_task_throws, R, threw_existence_error).

% A cancelled task's own mailbox is drained on teardown, not leaked -
% send it something it will never get to read, then cancel it.

doomed_waiter :- task_recv(never_arrives, [timeout(30.0)]).

cancel_with_pending_mail :-
	task_create(doomed_waiter, Qid),
	task_send(Qid, unread(1)),
	task_send(Qid, unread(2)),
	task_cancel(Qid),
	task_wait,
	report(cancel_with_pending_mail, done, done).

% A cancel runs the Cleanup of setup_call_cleanup/3, as SWI's thread
% abort and engine_destroy/1 do: while Call sleeps, while it yields in a
% loop, and with Call exited but its choices left.

wait_in(0) :- !.
wait_in(N) :- ( bb_get(in, true) -> true ; task_yield, N1 is N - 1, wait_in(N1) ).

cancel_once_in(G) :-
	bb_put(in, false),
	task_create((task_create(G, Id), wait_in(1000), task_cancel(Id)), _),
	task_wait.

got(K, V) :- ( bb_get(K, V0) -> V = V0 ; V = none ).

sleeper :- setup_call_cleanup(true, (bb_put(in, true), sleep(30)), bb_put(cleaned, sleeping)).
spinner :- setup_call_cleanup(true, (bb_put(in, true), repeat, task_yield, fail), bb_put(cleaned, yielding)).
nondet :- setup_call_cleanup(true, member(_, [1,2,3]), bb_put(cleaned, nondet)), bb_put(in, true), sleep(30).

cancel_runs_cleanup :-
	findall(C, (member(G, [sleeper, spinner, nondet]), bb_put(cleaned, none), cancel_once_in(G), got(cleaned, C)), Cs),
	report(cancel_runs_cleanup, Cs, [sleeping, yielding, nondet]).

% A cancel during Setup lets it finish, then skips Call and runs Cleanup.

in_setup :- setup_call_cleanup((bb_put(in, true), sleep(0.05), bb_put(setup, done)), bb_put(call, ran), bb_put(cleaned, yes)).

cancel_in_setup :-
	forall(member(K, [setup, call, cleaned]), bb_put(K, none)),
	cancel_once_in(in_setup),
	findall(V, (member(K, [setup, call, cleaned]), got(K, V)), Vs),
	report(cancel_in_setup, Vs, [done, none, yes]).

% catch/3 sees the cancel but cannot keep the task going, whether its
% Recovery succeeds or fails.

catch_true :- catch((bb_put(in, true), sleep(30)), E, bb_put(caught, E)), bb_put(after, true).
catch_fail :- ( catch((bb_put(in, true), sleep(30)), _, fail) ; true ), bb_put(after, fail).

cancel_not_swallowed :-
	bb_put(caught, none), bb_put(after, none),
	cancel_once_in(catch_true), got(caught, E), got(after, A1),
	cancel_once_in(catch_fail), got(after, A2),
	report(cancel_not_swallowed, E/A1/A2, error(unwind(abort),task_cancel/1)/none/none).

% A Cleanup that sleeps still finishes, and a second cancel does not cut it short.

slow_cleanup :- setup_call_cleanup(true, (bb_put(in, true), sleep(30)), (sleep(0.02), bb_put(cleaned, slow))).

cancel_twice :-
	bb_put(cleaned, none), bb_put(in, false),
	task_create((task_create(slow_cleanup, Id), wait_in(1000), task_cancel(Id), task_yield, task_yield, task_cancel(Id)), _),
	task_wait,
	got(cleaned, C),
	report(cancel_twice, C, slow).

% A cancel inside a tabled evaluation stops it, and leaves the thread's
% tables to answer the same call in full afterwards.

:- table path/2.
edge(X, Y) :- between(1, 50, X), Y is X + 1.
edge(50, 1).
path(X, Y) :- edge(X, Y).
path(X, Y) :- path(X, Z), sleep(0.0001), edge(Z, Y).

tabled :- setup_call_cleanup(true, (bb_put(in, true), findall(Y, path(1, Y), L), length(L, N), bb_put(n, N)), bb_put(cleaned, tabled)).

cancel_in_tabling :-
	bb_put(cleaned, none), bb_put(n, none),
	cancel_once_in(tabled),
	got(cleaned, C), got(n, N),
	findall(Y, path(1, Y), L), length(L, After),
	report(cancel_in_tabling, C/N/After, tabled/none/51).

main :-
	cancel_stops_task,
	cancel_missing_qid_throws,
	cancel_non_task_throws,
	cancel_with_pending_mail,
	cancel_runs_cleanup,
	cancel_in_setup,
	cancel_not_swallowed,
	cancel_twice,
	cancel_in_tabling.
