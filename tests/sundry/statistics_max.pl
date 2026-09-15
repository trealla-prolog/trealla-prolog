% statistics/2's max_* keys are highwater marks: integers never below what the matching current key reported earlier.

:- initialization(main).

deep(0, F, S) :- !, statistics(frames, F), statistics(slots, S).
deep(N, F, S) :- N1 is N-1, deep(N1, F, S), true.

branch(0, Vs, C, T) :- !, statistics(choices, C), maplist(=(x), Vs), statistics(trails, T).
branch(N, Vs, C, T) :- member(_, [a,b]), N1 is N-1, branch(N1, Vs, C, T).

check(Key, Seen) :-
	statistics(Key, Max),
	(	integer(Max), Max >= Seen -> R = ok ; R = bad(Max, Seen) ),
	write(Key), write(': '), writeq(R), nl.

main :-
	deep(10000, F, S),
	(	F >= 10000 -> write('deep: ok') ; write(deep(F)) ), nl,
	length(Vs, 100),
	branch(50, Vs, C, T),
	numlist(1, 5000, L), statistics(heap, H), length(L, _),
	check(max_frames, F),
	check(max_slots, S),
	check(max_choices, C),
	check(max_trails, T),
	check(max_heap, H).
