% A rational went to a double as its two halves each made a double and then divided, rounding twice, so
% float(-17872664523988541 rdiv 194) came out one step off and float(rationalize(F)) missed F for about 3% of
% floats. It is now rounded once, ties to even, as SWI does; past the range float_overflow, below it 0.0.

:- initialization(main).

t(E) :- catch((X is E, R = X), error(Err, _), R = Err), writeq(E = R), nl.

rt(F) :- A is float(rational(F)), B is float(rationalize(F)), ((A == F, B == F) -> R = ok ; R = bad(A, B)), writeq(F = R), nl.

main :-
	t(float(-17872664523988541 rdiv 194)), t(float(-13724206191762853 rdiv 55142)), t(float(1 rdiv 3)),
	t(float((2^1100+1) rdiv 2^1099)), t(float(((10^400)+1) rdiv (10^399))), t((1 rdiv 3) + 0.5),
	t(float(1 rdiv 2^1074)), t(float(1 rdiv 2^1075)), t(float(3 rdiv 2^1076)), t(float(1 rdiv 2^1076)),
	t(float(2^1100 rdiv 3)), t(float(-(2^1100) rdiv 3)),
	forall(member(F, [5.5780685715890197e-250, -4.4546369624763854e-249, 1.5197153644568326e-276,
		2.420663898854312e-247, 0.1, 1.0e-300, 5.0e-324, 1.7976931348623157e308]), rt(F)).
