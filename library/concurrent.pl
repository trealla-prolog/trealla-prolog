:- module(concurrent, [
	future/3,
	future_all/2,
	future_any/2,
	future_cancel/1,
	future_done/1,
	await/2
	]).

:- use_module(library(apply)).
:- dynamic('$concurrent_count'/1).
:- dynamic('$future'/1).
:- dynamic('$future_result'/2).

'$concurrent_count'(0).

% Results used to come back over send/1 into the parent's queue and be
% read with recv/1. Both went in GUSTTO phase 1, along with await/0, so
% a finished task records its result in the database instead - which it
% shares with its parent, being a subquery of it.
%
% await/0 was the one that mattered: it let the parent run tasks until
% *one* reported, which is what any/2 needs to resolve on the first
% result rather than the last. task_end_wait/0 does that job - a task calling
% it releases the parent from task_wait/0 with the rest still queued - so
% each task calls it as it finishes, either way, and the waiters below
% loop over task_wait/0 until they see what they are after.

% sig_atomic/1 keeps a task_time_slice slice from splitting a read and its write.

future(Template, Goal, F) :-
	sig_atomic((retract('$concurrent_count'(N)), N1 is N + 1, assertz('$concurrent_count'(N1)))),
	F = '$future'(N),
	assertz(F),
	Task0 = ((Goal -> (sig_atomic((retract(F), assertz('$future_result'(F,Template)))), task_end_wait) ; (retract(F), task_end_wait, fail))),
	copy_term(Task0, Task),
	task_call(callgoal_, Task, F).

:- meta_predicate(future(-,0,?)).
:- help(future(+term,+callable,?list), [iso(false)]).

future_all(Fs, all(Fs)).
future_any(Fs, any(Fs)).

:- help(future_all(+list,-term), [iso(false)]).
:- help(future_any(+list,-term), [iso(false)]).

% Each waiter runs task_wait/0 until what it wants shows up, or until the
% futures it waits on are all finished - a future that failed never
% records a result, and the old repeat/0 form hung on exactly that.
%
% A cancelled future still hangs a waiter, because '$cancel_future'/1
% reaps the task before its body can retract '$future'(N), so it never
% reads as done. That was true of the message-queue version too.

await(all(Fs), Templates) :-
	!,
	findall(F-V, (member(F,Fs), await_one_(F,V)), Msgs),
	msort(Msgs, Msgs1),
	strip_prefix_(Msgs1, [], Templates).

await(any(Fs), Template) :-
	!,
	await_any_(Fs, Template).

await(F, Template) :-
	await_one_(F, Template).

await_one_(F, Template) :-
	(	retract('$future_result'(F,Template))
	->	true
	;	future_done(F)
	->	fail
	;	task_wait,
		await_one_(F, Template)
	).

await_any_(Fs, Template) :-
	(	member(F, Fs),
		retract('$future_result'(F,Template))
	->	true
	;	forall(member(F,Fs), future_done(F))
	->	fail
	;	task_wait,
		await_any_(Fs, Template)
	).

future_cancel(all(Fs)) :-
	Fs = [F|Rest],
	future_cancel(F),
	future_cancel(Rest).

future_cancel(any(Fs)) :-
	Fs = [F|Rest],
	future_cancel(F) -> true
	; future_cancel(any(Rest)).

future_cancel([]).
future_cancel('$future'(N)) :-
	'$cancel_future'(N).

future_done(all(Fs)) :-
	Fs = [F|Rest],
	future_done(F),
	future_done(Rest).

future_done(any(Fs)) :-
	Fs = [F|Rest],
	future_done(F) -> true
	; future_done(any(Rest)).

future_done([]).
future_done(F) :-
	\+ clause(F, _).

:- help(await(+term,?term), [iso(false)]).

strip_prefix_([], L0, L) :- reverse(L0, L).
strip_prefix_([_-V|Rest], Init, L) :-
	strip_prefix_(Rest, [V|Init], L).

% The goal goes to the task as a term. It used to be written out as an
% atom and read back here, because task_call/N passed variable cells by
% reference and a bound variable inside a compound reached the task
% unbound - '$future'(N) among them, which is this predicate's second
% argument. task_call/N clones and rebases now, so the round trip is
% gone, and with it the requirement that a goal survive being written
% and re-read.

callgoal_(T, '$future'(N)) :-
	'$set_future'(N),
	T.
