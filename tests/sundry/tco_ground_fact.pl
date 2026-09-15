% A callee binding the caller's variable to a ground compound of its own clause pinned the calling frame, so the tail call never reused it.

:- initialization(main).

mk_f(f(a)).

loop(0) :- !.
loop(N) :- mk_f(_), N1 is N-1, loop(N1).

main :-
	loop(100000),
	statistics(max_frames, F),
	(	F < 1000 -> write(ok) ; write(frames(F)) ), nl.
