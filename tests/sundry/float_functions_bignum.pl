% Float functions and their kin on bigints and rationals. +/1 and numerator/1 shared a bigint with their
% argument, whose cleanup freed it, so +(2^70) gave 0. log/1 dropped a bigint into its type error, log/2
% left a bigint's result unset and returned garbage, exp/1 called a negative bigint undefined, exp/1, log10/1
% and atan2/2 refused a rational or bigint, and a number past a double's range went in as inf, so
% sin(2^1100) was undefined and atan(2^1100) pi/2 where SWI and Scryer raise float_overflow. copysign/2
% left an integer's own sign, ignored -0.0 for one, and refused bigints and rationals.

:- initialization(main).

t(E) :- catch((X is E, R = X), error(Err, _), R = Err), writeq(E), write(' = '), (float(R) -> format("~6f", [R]) ; writeq(R)), nl.

main :-
	t(+(2^70)), t(+(1 rdiv 2)), t(+(2^70) + 1), t(numerator(2^70)), t(numerator(2^70) + 1),
	t(log(2^70)), t(log(2, 2^70)), t(log(2, 1 rdiv 4)), t(log(2^70, 2)), t(log(1 rdiv 2, 2)), t(log(2, -(2^70))),
	t(exp(-(2^70))), t(exp(1 rdiv 2)), t(exp(2^1100 rdiv 3)),
	t(log10(2^70)), t(log10(1 rdiv 100)), t(log10(-(2^70))),
	t(atan2(2^70, 1)), t(atan2(1 rdiv 2, 1 rdiv 2)), t(atan2(1, -(2^70))), t(atan2(2^1100, 1)),
	t(sin(2^1100)), t(atan(2^1100 rdiv 3)), t(erf(2^1100)), t(tanh(-(2^1100))),
	t(copysign(-3, 1)), t(copysign(3, -(0.0))), t(copysign(-(2^63), 1)), t(copysign(2^70, -1)),
	t(copysign(-1 rdiv 2, 1.0)), t(copysign(1, -1 rdiv 2)), t(copysign(2.5, -(2^70))), t(copysign(2^63, -1) + 0).
