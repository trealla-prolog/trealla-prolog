% A tail call passing a reference-counted value, such as a code string or a bigint, never reused its frame while an older choicepoint existed.

:- initialization(main).

long_atom('abcdefghijklmnopqrstuvwxyz0123456789').

loop_codes(0, Cs) :- !, long_atom(A), atom_codes(A2, Cs), A2 == A.
loop_codes(N, _) :- long_atom(A), atom_codes(A, Cs), M is N-1, loop_codes(M, Cs).

loop_big(0, X) :- !, X =:= 2^200 + 1.
loop_big(N, _) :- X is 2^200 + N, M is N-1, loop_big(M, X).

frames(Name) :-
	statistics(max_frames, F),
	write(Name), write(': '),
	(	F < 1000 -> write(ok) ; write(frames(F)) ), nl.

main :-
	(	member(_, [1,2]), loop_codes(100000, []), fail ; true ), frames(codes),
	(	member(_, [1,2]), loop_big(100000, 0), fail ; true ), frames(big),
	findall(ok, (member(_, [1,2,3]), loop_codes(1000, [])), L1), length(L1, N1),
	write(codes_intact(N1)), nl,
	findall(ok, (member(_, [1,2,3]), loop_big(1000, 0)), L2), length(L2, N2),
	write(big_intact(N2)), nl.
