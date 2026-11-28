% A task whose goal succeeded with choices left dropped them and the
% cleanups they held. As for a thread in SWI, those now run as a cut runs
% them: newest first, seeing the goal's bindings. One that throws lets the
% older ones run, and the first exception is how the task ended. The
% thread cases are in tests/misc/exit_cleanup_threads.pl, as WASM has no
% threads.

:- initialization(main).
:- dynamic(seen/1).

log(X) :- assertz(seen(X)).
seen_all(L) :- findall(X, retract(seen(X)), L).

task(Name, G) :-
	task_create(G, _), task_wait, seen_all(L),
	writeq(Name-L), nl.

two_cleanups :- setup_call_cleanup(true, member(_, [1,2]), log(c1)), setup_call_cleanup(true, member(_, [1,2]), log(c2)).
two_throws :- setup_call_cleanup(true, member(_, [1,2]), throw(first)), setup_call_cleanup(true, member(_, [1,2]), throw(second)).
backtracking :- setup_call_cleanup(true, member(_, [1,2]), (member(W, [p,q]), log(W), W == q)).

wait_in(0) :- !.
wait_in(N) :- ( bb_get(in, true) -> true ; task_yield, N1 is N - 1, wait_in(N1) ).

cancelled :-
	bb_put(in, false),
	task_create((task_create((setup_call_cleanup(true, member(_, [1,2]), log(older)), setup_call_cleanup(true, member(_, [1,2]), (bb_put(in, true), sleep(30), log('NOT_HERE')))), Id), wait_in(1000), task_cancel(Id)), _),
	task_wait, seen_all(L),
	writeq(task_cancelled-L), nl.

main :-
	task(task_bindings, setup_call_cleanup(true, member(V, [1,2]), log(c(V)))),
	task(task_nested, two_cleanups),
	task(task_yields, setup_call_cleanup(true, member(_, [1,2]), (log(before), task_yield, log(after)))),
	task(task_backtracking, backtracking),
	task(task_two_throws, (log(ran), two_throws)),
	task(task_subtask, setup_call_cleanup(true, member(_, [1,2]), (task_create(log(sub), _), task_wait, log(cleanup)))),
	cancelled.
