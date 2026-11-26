% Global variables belong to each thread, engine and task, as SWI gives
% each thread and engine its own; the blackboard is what they share.

:- use_module(library(clpb)).

:- initialization(main).

report(Name, Got, Expect) :-
	(	Got == Expect
	->	format("~w: ok~n", [Name])
	;	format("~w: FAILED got ~q wanted ~q~n", [Name,Got,Expect])
	).

seen(K, V) :- ( nb_current(K, V0) -> V = V0 ; V = none ).

% A task starts with none of its creator's, and what it sets stays its own.

task_body :- seen(k, V), bb_put(task_saw, V), nb_setval(k, task).

task_own :-
	nb_setval(k, main),
	task_create(task_body, _),
	task_wait,
	bb_get(task_saw, V),
	seen(k, After),
	report(task_own, V-After, none-main).

% So does an engine, and its own last from one answer to the next.

engine_own :-
	nb_setval(k, main),
	engine_create(V, (seen(k, V0), nb_setval(k, engine), (V = V0 ; seen(k, V))), E),
	engine_next(E, A),
	engine_next(E, B),
	engine_destroy(E),
	seen(k, After),
	report(engine_own, A-B-After, none-engine-main).

% A thread already had its own.

thread_body :- seen(k, V), bb_put(thread_saw, V), nb_setval(k, thread).

thread_own :-
	nb_setval(k, main),
	(	catch(thread_create(thread_body, T, []), _, fail)
	->	thread_join(T, _),
		bb_get(thread_saw, V),
		seen(k, After),
		R = V-After
	;	R = none-main			% no threads in this build
	),
	report(thread_own, R, none-main).

% Backtracking takes a task's b_setval/2 back off its own variables.

undo_body :- ( b_setval(u, set), fail ; true ), seen(u, V), bb_put(undo_saw, V).

task_undo :-
	task_create(undo_body, _),
	task_wait,
	bb_get(undo_saw, V),
	report(task_undo, V, none).

% A portray hook runs as a subquery, and sees the variables of the task under it.

portray(secret) :- nb_getval(pk, V), write(V).

portray_body :- nb_setval(pk, from_task), with_output_to(atom(A), print(secret)), bb_put(portray_saw, A).

task_portray :-
	nb_setval(pk, from_main),
	task_create(portray_body, _),
	task_wait,
	bb_get(portray_saw, A),
	report(task_portray, A, from_task).

% library(clpb) makes its counters where it first needs them, so it works in each.

clpb_body(Key) :-
	(	sat(X+Y), sat(~X), labeling([X,Y]), sat_count(+[1,_,_,_], N)
	->	bb_put(Key, X-Y-N)
	;	bb_put(Key, failed)
	).

clpb_everywhere :-
	clpb_body(c_main),
	task_create(clpb_body(c_task), _),
	task_wait,
	engine_create(x, clpb_body(c_engine), E),
	engine_next(E, _),
	engine_destroy(E),
	(	catch(thread_create(clpb_body(c_thread), T, []), _, fail)
	->	thread_join(T, _)
	;	bb_put(c_thread, 0-1-8)		% no threads in this build
	),
	findall(V, (member(K, [c_main,c_task,c_engine,c_thread]), bb_get(K, V)), Vs),
	report(clpb_everywhere, Vs, [0-1-8,0-1-8,0-1-8,0-1-8]).

main :-
	task_own,
	engine_own,
	thread_own,
	task_undo,
	task_portray,
	clpb_everywhere.
