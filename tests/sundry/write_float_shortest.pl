% A float is written with the fewest significant digits that read back as the same double: 1.0e-20 was
% written as 9.999999999999999e-21, 9.0e-5 as 9.000000000000001e-05. Values needing 16 or 17 still get them.

:- initialization(main).

main :-
	forall(member(X, [1.0e-20, 0.00009, 8.0e-15, 0.1, 0.3, 123.456, 1.0e-10, 1.5e-7, 1.0e15, 123456789012345.0]), (write(X), nl)),
	X1 is 0.1 + 0.2, write(X1), nl,
	X2 is 1 / 3.0, write(X2), nl,
	X3 is 2 / 3.0, write(X3), nl,
	X4 is 5.0e-324, write(X4), nl,
	X5 is 1.7976931348623157e308, write(X5), nl,
	forall(member(Y, [1.0e-20, 0.00009, 8.0e-15, X1, X2, X3, X4, X5]),
		(	format(atom(A), "~w", [Y]), atom_number(A, Z),
			( Y == Z -> true ; format("~w does not read back~n", [A]) )
		)).
