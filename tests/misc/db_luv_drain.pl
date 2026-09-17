% The logical update view has to hold while drains run.
%
% leave_predicate() takes retracted clauses out of the chain while other
% readers are still inside, but only once every reader that entered before
% a retraction has gone: one that entered before may still see the clause.
%
% A reader enters q/1 and pauses at its tenth solution. A churner then
% retracts the second half of the clauses - ahead of the reader - and
% enters and leaves q/1 itself thousands of times, so drains start and
% try to complete while the reader is still inside. The reader resumes and
% must still collect every clause it entered with. Against a build whose
% drains ignore the readers that predate them it collects only half.

:- initialization(main).

:- dynamic(q/1).
:- dynamic(paused/0).
:- dynamic(resume/0).
:- dynamic(result/1).

n(2000).

setup :- n(N), forall(between(1, N, I), assertz(q(I))).

wait_for(G) :- ( call(G) -> true ; sleep(0.001), wait_for(G) ).

reader :-
	findall(X, (q(X), ( X =:= 10 -> assertz(paused), wait_for(resume) ; true )), L),
	n(N), numlist(1, N, Expected),
	(	L == Expected
	->	assertz(result(ok))
	;	length(L, Len), assertz(result(bad(Len)))
	).

churner :-
	wait_for(paused),
	n(N), Half is N // 2 + 1,
	forall(between(Half, N, I), retract(q(I))),
	forall(between(1, 20000, _), ( q(_) -> true ; true )),
	assertz(resume).

main :-
	setup,
	thread_create(reader, R, []),
	thread_create(churner, C, []),
	thread_join(R, _),
	thread_join(C, _),
	(	result(ok) -> format("db_luv_drain: ok~n")
	;	result(X) -> format("db_luv_drain: ~w~n", [X])
	;	format("db_luv_drain: no result~n")
	).
