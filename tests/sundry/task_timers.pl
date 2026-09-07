% A sleeping task must yield, not spin.
%
% tests/sundry/tasks_scheduler.pl already shows that sleep/1 inside a task
% runs at all, but it cannot show *how*: its nappers are spawned in the same
% order as their delays, so a task that simply busy-waited would print the
% same thing. These delays descend instead, so the two behaviours differ.
%
% Yielding puts each task on the scheduler's timer heap and wakes them in
% delay order: 3 2 1. Spinning would run them to completion in spawn order,
% giving 1 2 3.
%
% This is the property that was missing from freestanding builds entirely -
% there sleep/1 did not exist, so nothing could reach the timer heap and the
% scheduler could only round-robin. The gaps are 100ms, which is the same
% order tasks_scheduler.pl already relies on.

:- initialization(main).

napper(N, S) :-
	sleep(S),
	write(N),
	write(' ').

main :-
	call_task(napper(1, 0.3)),
	call_task(napper(2, 0.2)),
	call_task(napper(3, 0.1)),
	wait,
	nl.
