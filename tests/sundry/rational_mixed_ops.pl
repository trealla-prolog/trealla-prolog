% +, - and * with a rational and a float read the float's bits as an integer, so (1 rdiv 2) + 0.5 gave
% 9205357638345293825r2, and with an integer on the left and a rational on the right - was reversed,
% so 1 - (1 rdiv 2) gave -1r2. A rational divided by 0 overflowed to inf and the error read the double
% as the rational's pointer, so the process died.

:- initialization(main).

t(E) :- catch((X is E, R = X), error(Err, _), R = Err), writeq(E = R), nl.

main :-
	B is 2^70, Y is 1 rdiv 3,
	t((1 rdiv 2) + 0.5), t(0.5 + (1 rdiv 2)), t((1 rdiv 4) - 1.0), t(1.0 - (1 rdiv 4)), t((1 rdiv 2) * 0.5), t(0.5 * (1 rdiv 2)),
	t(1 - (1 rdiv 2)), t(3 - (1 rdiv 2)), t(B - (1 rdiv 2)), t(2^70 - (1 rdiv 2)), t((1 rdiv 2) - 3), t((1 rdiv 2) - B),
	t((2^1100 rdiv 3) + 0.5), t(0.0 * (2^1100 rdiv 3)), t(1.0e308 * (7 rdiv 2)),
	t((1 rdiv 3) / 0), t(Y / 0), t((1 rdiv 4) / 2), t((1 rdiv 4) / (-2)).
