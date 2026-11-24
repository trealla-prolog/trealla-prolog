% A meta-call costs the same whatever the size of a term bound to its arguments: call/N
% passes the variable, not a copy, and a heap page trimmed each pass is reused, not zeroed.

:- initialization(main).

noop(_, _).

loop(0, _, _) :- !.
loop(N, G, T) :- call(G, x, T), N1 is N - 1, loop(N1, G, T).

cnt([], N, N).
cnt([_|T], N0, N) :- N1 is N0 + 1, G = cnt, call(G, T, N1, N).

main :-
	numlist(1, 200000, L),
	loop(100000, noop, L),
	G = noop(x, L),
	forall(between(1, 100000, _), call(G)),
	cnt(L, 0, Len),
	writeq(Len), nl.
