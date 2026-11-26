% The task_time_slice flag: a task is preempted once its slice is up,
% resumes past the goal it stopped at, and is left alone in sig_atomic/1
% and while its thread's tables are being completed. Tasks share values on
% the blackboard, as their global variables are their own.

:- use_module(library(tabling)).
:- use_module(library(gensym)).

:- initialization(main).

:- dynamic(fact/1).
:- dynamic(sym/1).

report(Name, Got, Expect) :-
	(	Got == Expect
	->	format("~w: ok~n", [Name])
	;	format("~w: FAILED got ~q wanted ~q~n", [Name,Got,Expect])
	).

% Without a slice the cruncher finishes before the setter's turn. The second
% round's one extra goal puts the two-goal loop's builtin on the other parity,
% so a check that skips whatever lands on a user call misses one of them.

crunch(0) :- !.
crunch(N) :- N1 is N - 1, crunch(N1).

cruncher(plain) :- crunch(2000000), crunched.
cruncher(padded) :- atom(x), crunch(2000000), crunched.

crunched :- bb_get(stop, Stop), bb_put(seen, Stop).

setter :- sleep(0.01), bb_put(stop, true).

preempts(Parity) :-
	bb_put(stop, false),
	bb_put(seen, unset),
	task_create(cruncher(Parity), _),
	task_create(setter, _),
	task_wait,
	bb_get(seen, Seen),
	(Seen == true -> R = setter_ran ; R = starved),
	report(preempts(Parity), R, setter_ran).

% The goal a task was preempted after is not run again: each fact lands once.

fill(0) :- !.
fill(N) :- assertz(fact(N)), N1 is N - 1, fill(N1).

no_rerun :-
	retractall(fact(_)),
	task_create(fill(100000), _),
	task_wait,
	findall(x, fact(_), L),
	length(L, C),
	report(no_rerun, C, 100000).

% Two tasks bumping one counter lose nothing when each get-and-set is in
% sig_atomic/1. The countdown puts most goals inside it, wherever a check lands.

countdown(0) :- !.
countdown(N) :- N1 is N - 1, countdown(N1).

bump(0) :- !.
bump(N) :-
	sig_atomic((bb_get(count, C), countdown(20), C1 is C + 1, bb_put(count, C1))),
	N1 is N - 1,
	bump(N1).

atomic_bump :-
	bb_put(count, 0),
	task_create(bump(20000), _),
	task_create(bump(20000), _),
	task_wait,
	bb_get(count, C),
	report(atomic_bump, C, 40000).

% The tasks on a thread share its tables, so one preempted mid-evaluation
% left them half-built for the next: this crashed in the worklist.

n(20000).
edge(X, Y) :- n(N), between(1, 3, K), Y is (X * K + 7) mod N + 1.

:- table path/2.

path(X, Y) :- edge(X, Y).
path(X, Y) :- path(X, Z), edge(Z, Y).

reach(Start, Key) :- findall(Y, path(Start, Y), L), length(L, N), bb_put(Key, N).

tabled :-
	task_create(reach(1, r1), _),
	task_create(reach(2, r2), _),
	task_create(reach(3, r3), _),
	task_wait,
	bb_get(r1, A),
	bb_get(r2, B),
	bb_get(r3, C),
	report(tabled, A-B-C, 15000-10000-15000).

% Library counters read and write in sig_atomic/1: two tasks never get one
% symbol. A long base keeps most goals between gensym/2's read and write.

syms(_, 0) :- !.
syms(Base, N) :- gensym(Base, S), assertz(sym(S)), N1 is N - 1, syms(Base, N1).

gensym_unique :-
	length(Cs, 100),
	maplist(=(s), Cs),
	atom_chars(Base, Cs),
	task_create(syms(Base, 5000), _),
	task_create(syms(Base, 5000), _),
	task_wait,
	findall(S, sym(S), L),
	sort(L, U),
	length(U, C),
	report(gensym_unique, C, 10000).

main :-
	current_prolog_flag(task_time_slice, D),
	report(default_off, D, 0),
	set_prolog_flag(task_time_slice, 1),
	preempts(plain),
	preempts(padded),
	no_rerun,
	atomic_bump,
	tabled,
	gensym_unique.
