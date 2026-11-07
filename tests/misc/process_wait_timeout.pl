% process_wait/3's timeout(T) never used its value: any number meant one
% non-blocking wait that failed if the child was still running, and the value
% itself was read without dereferencing. As SWI does on Unix it now takes
% infinite or a number, of which only zero does not wait, answering timeout for
% a child still running; any other number waits as infinite does.

:- initialization(main).

main :-
	process_create('/bin/sleep', ['5'], [process(P1)]),
	process_wait(P1, S1, [timeout(0)]), write(zero(S1)), nl,
	process_wait(P1, S2, [timeout(0.0)]), write(zero_float(S2)), nl,
	T = 0, process_wait(P1, S3, [timeout(T)]), write(zero_var(S3)), nl,
	process_kill(P1), process_wait(P1, killed(_)), write(killed), nl,
	process_create('/usr/bin/true', [], [process(P2)]),
	poll_until_done(P2, S4), write(poll_done(S4)), nl,
	process_create('/bin/sleep', ['0'], [process(P3)]),
	process_wait(P3, S5, [timeout(5)]), write(number_waits(S5)), nl,
	process_create('/usr/bin/true', [], [process(P4)]),
	process_wait(P4, S6, [timeout(infinite)]), write(infinite(S6)), nl,
	process_create('/usr/bin/true', [], [process(P5)]),
	catch(process_wait(P5, _, [timeout(foo)]), error(E1, _), (write(E1), nl)),
	catch(process_wait(P5, _, [timeout(_)]), error(E2, _), (write(E2), nl)),
	process_wait(P5, _).

poll_until_done(P, S) :-
	process_wait(P, S0, [timeout(0)]),
	(	S0 == timeout
	->	sleep(0.05), poll_until_done(P, S)
	;	S = S0
	).
