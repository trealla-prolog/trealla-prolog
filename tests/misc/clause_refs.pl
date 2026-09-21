% Clause references round-trip through assertz/2, clause/3, instance/2 and erase/1.
% They are opaque handles: a program only ever hands one back, so this checks the
% round-trips rather than the printed form, which is an implementation detail.

:- initialization(main).

:- dynamic(f/1).

main :-
	assertz(f(1), R1),
	assertz(f(2), R2),
	asserta(f(0), R0),
	(	R0 == R1 ; R1 == R2 ; R0 == R2
	->	format("clause_refs: refs not distinct~n")
	;	instance(R2, I), I == f(2),
		clause(f(X), true, R1), X == 1,
		erase(R1),
		findall(Y, f(Y), L), L == [0,2],
		clause(f(_), true, R2),
		\+ clause(f(_), true, R1),
		format("clause_refs: ok~n")
	).
