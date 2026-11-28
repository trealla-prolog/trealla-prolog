% A thread whose goal succeeded with choices left dropped them and the
% cleanups they held. As in SWI, those now run as a cut runs them: newest
% first, seeing the goal's bindings, before the at_exit goal, past no
% catch/3 Recovery. One that throws lets the older ones run, and the first
% exception is how the thread ended. The at_exit goal runs on the thread's
% query, and started with frame 0 bound as the goal left it when their
% variable counts matched. The task cases are in tests/sundry/exit_cleanup.pl,
% as WASM has no threads.

:- initialization(main).
:- dynamic(seen/1).

log(X) :- assertz(seen(X)).
seen_all(L) :- findall(X, retract(seen(X)), L).

thread(Name, G) :- thread(Name, G, []).
thread(Name, G, Opts) :-
	thread_create(G, Id, Opts), thread_join(Id, S), seen_all(L),
	writeq(Name-S-L), nl.

two_cleanups :- setup_call_cleanup(true, member(_, [1,2]), log(c1)), setup_call_cleanup(true, member(_, [1,2]), log(c2)).
older_then_throw :- setup_call_cleanup(true, member(_, [1,2]), log(older)), setup_call_cleanup(true, member(_, [1,2]), throw(oops)).
two_throws :- setup_call_cleanup(true, member(_, [1,2]), throw(first)), setup_call_cleanup(true, member(_, [1,2]), throw(second)).
backtracking :- setup_call_cleanup(true, member(_, [1,2]), (member(W, [p,q]), log(W), W == q)).
caught :- catch(setup_call_cleanup(true, member(_, [1,2]), (log(throwing), throw(oops))), B, log(recovered(B))), log(answered).

wait_seen(_, 0) :- !.
wait_seen(X, N) :- ( seen(X) -> true ; sleep(0.01), N1 is N - 1, wait_seen(X, N1) ).

main :-
	thread(thread_bindings, setup_call_cleanup(true, member(V, [1,2]), log(c(V))), [at_exit(log(at_exit))]),
	thread(thread_nested, two_cleanups),
	thread(thread_det, setup_call_cleanup(true, true, log(det))),
	thread(thread_fail, setup_call_cleanup(true, fail, log(failed))),
	thread(thread_exit, setup_call_cleanup(true, (member(_, [1,2]), thread_exit(bye)), log(exited))),
	thread(thread_backtracking, backtracking),
	thread(thread_throw, older_then_throw),
	thread(thread_two_throws, two_throws),
	thread(thread_caught, caught),
	thread(thread_at_exit_fresh, (S = "a string long enough to be shared", member(_, [1,2]), S \== []), [at_exit((thread_self(Me), nonvar(Me), log(self), Y = "another shared string, as long", Y \== [], log(fresh)))]),
	thread_create(setup_call_cleanup(true, member(_, [1,2]), log(detached)), _, [detached(true)]),
	wait_seen(detached, 500),
	seen_all(D), writeq(thread_detached-D), nl.
