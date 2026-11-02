% A tail call passing a reference-counted value, such as a long atom, a string or a bigint, reused its frame but left
% a trail entry a call behind, with or without an older choicepoint.

:- initialization(main).

walk(_, 0) :- !.
walk(X, N) :- M is N-1, walk(X, M).

sum(0, A, A) :- !.
sum(N, A0, A) :- A1 is A0+N, M is N-1, sum(M, A1, A).

trails(Name, Goal) :-
	call(Goal),
	statistics(max_trails, T),
	write(Name), write(': '),
	(	T < 1000 -> write(ok) ; write(trails(T)) ), nl.

main :-
	atom_concat(abcdefghijklmnopqrstuvwxyz, '0123456789', A),
	atom_string(A, S),
	B is 2^200,
	trails(atom, walk(A, 100000)),
	trails(string, walk(S, 100000)),
	trails(bigint, (sum(100000, B, X1), X1 =:= B + 5000050000)),
	trails(atom_choice, (walk(A, 100000) ; true)),
	trails(bigint_choice, (member(_, [1,2]), sum(100000, B, X2), X2 =:= B + 5000050000)),
	findall(X, (member(_, [1,2,3]), sum(1000, B, X)), Xs),
	(	Xs = [X3,X3,X3], X3 =:= B + 500500 -> write(intact) ; write(Xs) ), nl,
	findall(A2, (member(_, [1,2,3]), walk(A, 1000), A2 = A), As),
	(	As = [A,A,A] -> write(intact) ; write(As) ), nl.
