% A rational rdiv an integer kept the numerator left by the last rational worked out, so with 7 rdiv 11
% done in between, (2 rdiv 3) rdiv 5 gave 7r15, and against a bigint it took 1, halving (2 rdiv 3) rdiv 2^70.
% rdiv with a bigint also leaked the reused numerator or denominator on every call.

:- initialization(main).

t(Y, E) :- _ is 7 rdiv 11, catch((X is Y rdiv E, R = X), error(Err, _), R = Err), writeq(Y rdiv E = R), nl.

main :-
	A is 2 rdiv 3, B is -2 rdiv 3, C is 2^70 rdiv 3,
	t(A, 5), t(A, 2^70), t(B, -5), t(C, 2^71), t(A, 4 rdiv 9), t(A, 0),
	t(2^70, 5 rdiv 7), t(5, A), t(2^70, -(2^71)), t(6, 2^70), t(2^70, 2).
