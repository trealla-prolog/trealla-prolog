% The binary operators past their fast paths take both numbers at the wider of integer, rational and float. Cases
% that the old per-pair branches got differently: a huge bigint times 0.0 let NaN out to is/2; a huge bigint or
% rational over 0 raised float_overflow before zero_divisor; a huge number against a float in a comparison raised
% float_overflow on one side and not the other, and in max/2 and min/2 for a bigint; atan2(0.0, -0.0) read the
% second float's bits as an integer and gave pi.

:- initialization(main).

t(E) :- catch((X is E, R = X), error(Err, _), R = Err), writeq(E = R), nl.
c(G) :- catch((G -> R = true ; R = false), error(Err, _), R = Err), writeq(G = R), nl.

main :-
	t(2^1100 * 0.0), t(-(2^1100) * -(0.0)), t(2^1100 / 0), t((2^1100 rdiv 3) / 0), t((2^1100 rdiv 3) / 0.0),
	c(1.0 < 2^1100), c(2^1100 > 1.0), c(-(2^1100) < -1.0e300), c(0.0 =:= -(2^1100)), c((2^1100 rdiv 3) > 1.0e300),
	c(1.0e300 >= 2^1100 rdiv 3), t(max(2^1100, 1.0) - 2^1100), t(min(2^1100, 3.7)), t(max(-(2^1100), 0.5)),
	c(min(-(2^1100) rdiv 3, -1.0e300) < -1.0e300),
	t(atan2(0.0, -(0.0))), t(atan2(-(0.0), -(0.0))), t(atan2(0, 0)), t(atan2(1, -(0.0))),
	t(2^70 + 1.5), t(1 rdiv 3 + 2^70), t(2^70 // 3), t(7 mod (2^70 >> 69)), t(gcd(-(2^62)*2, 0)), t(max(1 rdiv 3, 0.5)),
	t(min(0.0, -(0.0))), t(max(2, 2.0)), t(xor(-(2^70), 2^64)), t(2^64 rem -7), t(-(2^70) div 3).
