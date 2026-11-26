:- module(tasks, [
	task_actor_spawn/2,
	task_actor_spawn/3,
	task_actor_self/1,
	task_actor_send/2,
	task_actor_recv/1,
	task_actor_recv/2,
	task_actor_link/1,
	task_actor_unlink/1,
	task_supervisor_start/2,
	task_supervisor_start/3,
	task_supervisor_stop/1
	]).

% Actors on tasks rather than threads - same shape as library(actors/threads),
% different backend, so skynet-sized actor counts stay off the OS
% thread ceiling. Pid is a qid (task_self/1, task_create/2), not a
% thread id: the two libraries' Pids are not interchangeable, and
% neither is a drop-in replacement for the other - see
% docs/DESIGN-GUSTTO.md phase 5 for why both are kept.
%
% No handshake needed for spawn+link atomicity, unlike the thread
% version: tasks on one OS thread run cooperatively, so a freshly
% task_create/2'd child provably has not run a single instruction by
% the time task_create/2 returns - nothing yields between the spawn and
% the link below. A short-lived thread can die before its creator
% installs a link; a short-lived task cannot outrun code that has not
% yielded yet. With task_time_slice set a task can be preempted between
% any two goals, so the spawn and the link, and each pair of link
% updates, go inside sig_atomic/1, which a slice does not interrupt.
%
% task_recv/1 does not block (it never did, even before GUSTTO) and still
% does not - task_actor_recv/1,2 use task_recv/2 instead, which parks the
% task (do_yield(), the same mechanism thread_get_message/3's blocking
% form uses) rather than spinning. No CPU spent polling: a task
% waiting on a message costs nothing until task_send/2 wakes it, or its
% timeout elapses.

:- use_module(library(lists)).

:- dynamic('$task_actor_link'/2).		% when A dies, tell B
:- dynamic('$task_actor_reason'/2).

task_actor_self(Pid) :- task_self(Pid).

task_actor_send(Pid, Msg) :- task_send(Pid, Msg).

task_actor_recv(Msg) :- task_recv(Msg, []).

task_actor_recv(Msg, Opts) :- task_recv(Msg, Opts).

task_actor_spawn(Goal, Pid) :- task_actor_spawn(Goal, Pid, []).

task_actor_spawn(Goal, Pid, Opts) :-
	sig_atomic((
		task_create('$task_actor_body'(Goal), Pid),
		( memberchk(link(true), Opts) -> task_actor_link(Pid) ; true )
	)).

:- meta_predicate(task_actor_spawn(0,-)).
:- meta_predicate(task_actor_spawn(0,-,+)).

'$task_actor_body'(Goal) :-
	task_self(Me),
	( catch(Goal, E, true) -> ( var(E) -> R = true ; R = exception(E) ) ; R = false ),
	sig_atomic((
		forall(retract('$task_actor_link'(Me, Other)), task_actor_send(Other, exit(Me, R))),
		retractall('$task_actor_link'(_, Me))
	)).

% Links are bidirectional: either death notifies the other.

task_actor_link(Pid) :-
	task_self(Me),
	sig_atomic((assertz('$task_actor_link'(Me, Pid)), assertz('$task_actor_link'(Pid, Me)))).

task_actor_unlink(Pid) :-
	task_self(Me),
	sig_atomic((retractall('$task_actor_link'(Me, Pid)), retractall('$task_actor_link'(Pid, Me)))).

:- help(task_actor_spawn(+callable,-integer), [iso(false)]).
:- help(task_actor_spawn(+callable,-integer,+list), [iso(false)]).
:- help(task_actor_self(-integer), [iso(false)]).
:- help(task_actor_send(+integer,+term), [iso(false)]).
:- help(task_actor_recv(?term), [iso(false)]).
:- help(task_actor_recv(?term,+list), [iso(false)]).
:- help(task_actor_link(+integer), [iso(false)]).
:- help(task_actor_unlink(+integer), [iso(false)]).

% A minimal supervisor, straight port of library(actors/threads)'s:
% one_for_one, link to each child, restart the one that dies, with a
% restart budget - max_restarts(N) within period(Seconds), default 5
% in 5 - so a child that dies immediately on start spins forever
% instead of taking the supervisor down with it. Exhausting it stops
% the supervisor. What made this port possible is task_cancel/1: the
% thread version leans on thread_cancel/1 to kill the children when
% stopping or when the budget is blown, and until task_cancel/1
% existed there was no equivalent for a task.
%
% One thing the port does not get for free: a thread-based supervisor
% runs in the background just by existing - it is a real, preemptively
% scheduled thread. A task-based one only makes progress while its
% owning thread drives the scheduler (task_wait/0, or a caller that keeps
% yielding it turns), so task_supervisor_start/2,3 called directly from
% a thread that then goes on to do other things leaves it starved -
% spawned, registered, never actually run. Host it on a thread of its
% own instead:
%
%   sup_host(Children, Opts, ReadyQid) :-
%       task_supervisor_start(Children, Sup, Opts),
%       task_send(ReadyQid, sup(Sup)),
%       task_wait.
%
%   task_self(Me), thread_create(sup_host(Children, Opts, Me), _, []),
%   task_recv(sup(Sup), [timeout(5.0)])
%
% which gets the supervisor and everything under it its own thread to
% run on indefinitely, exactly as a thread-based supervisor already
% has, while the caller stays free.

task_supervisor_start(Children, Sup) :- task_supervisor_start(Children, Sup, []).

task_supervisor_start(Children, Sup, Opts) :-
	( memberchk(max_restarts(M), Opts) -> true ; M = 5 ),
	( memberchk(period(P), Opts) -> true ; P = 5 ),
	task_actor_spawn('$task_sup_init'(Children, M, P), Sup).

task_supervisor_stop(Sup) :- task_actor_send(Sup, '$sup_stop').

'$task_sup_init'(Children, M, P) :-
	findall(Pid-G, (member(G, Children), task_actor_spawn(G, Pid, [link(true)])), Running),
	'$task_sup_loop'(Running, M, P, []).

'$task_sup_loop'(Running, M, P, Hist) :-
	task_actor_recv(Msg),
	'$task_sup_msg'(Msg, Running, M, P, Hist).

'$task_sup_msg'('$sup_stop', Running, _, _, _) :-
	!,
	forall(member(Pid-_, Running), catch(task_cancel(Pid), _, true)).

'$task_sup_msg'(exit(Pid, _), Running, M, P, Hist) :-
	selectchk(Pid-Goal, Running, Rest),
	!,
	get_time(Now),
	Cutoff is Now - P,
	include('$task_sup_recent'(Cutoff), Hist, Recent),
	length(Recent, N),
	(	N >= M
	->	forall(member(Other-_, Rest), catch(task_cancel(Other), _, true))
	;	task_actor_spawn(Goal, New, [link(true)]),
		'$task_sup_loop'([New-Goal|Rest], M, P, [Now|Recent])
	).

'$task_sup_msg'(_, Running, M, P, Hist) :-
	'$task_sup_loop'(Running, M, P, Hist).

'$task_sup_recent'(Cutoff, T) :- T >= Cutoff.

:- help(task_supervisor_start(+list,-integer), [iso(false)]).
:- help(task_supervisor_start(+list,-integer,+list), [iso(false)]).
:- help(task_supervisor_stop(+integer), [iso(false)]).
