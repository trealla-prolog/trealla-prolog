% x << n and x >> n as SWI and Scryer have them: a count of 64 or more,
% a negative count (the other way), a bigint count, and a negative bigint
% shifted right, which rounds down as a small one does. A bigint count holding a small value, as
% >> and rational arithmetic leave one inside an expression, shifts by that value, not as if huge.

:- initialization(main).

t(E) :- catch((X is E, R = X), error(Err, _), R = Err), writeq(E = R), nl.

main :-
	t(42 >> 64), t(-42 >> 64), t(-1 >> 100), t(8 >> -1), t(8 << -1),
	t(1 << 64), t(-1 << 63), t(-3 << 2), t(123456789 << 40), t(-7 >> 1),
	B is 2^70, NB is -B, NB1 is NB-1, NB5 is NB-5,
	t(1 >> B), t(-5 >> B), t(0 << B), t(1 << B), t(8 >> -B),
	t(B >> 80), t(NB >> 100), t(NB1 >> 1), t(NB5 >> 2), t(NB >> 3), t(B >> -3),
	t(3 << (B >> 69)), t(-9 >> (4 rdiv 2)), t(3 << -(B >> 69)).
