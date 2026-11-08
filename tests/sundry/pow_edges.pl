% X ^ Y and X ** Y at the edges: an exponent past 2^32 was truncated (2^(2^32+3) gave 8, 0^(2^40) gave 1),
% a bigint one spun, a power below the smallest smallint came back as garbage, and a bigint with a float
% raised type_error(evaluable, N/0). A result past 2^32 bits is a resource error, as for <<. A bigint holding a
% small value, as >>, gcd and rational arithmetic leave one inside an expression, goes by its value.

:- initialization(main).

t(E) :- catch((X is E, R = X), error(Err, _), R = Err), writeq(E = R), nl.

main :-
	B is 2^70, NB is -B, Big is 2^64, NBig is -Big, Odd is Big+1, NOdd is -Odd,
	t(2^(2^32+3)), t(3^4294967297), t(2^Big), t(B^(2^32)), t(B^Big), t(B^3),
	t(0^(2^40)), t(0^0), t(0^Big), t(1^(2^40)), t((-1)^(2^40+1)), t((-1)^Big), t((-1)^Odd),
	t(2^NBig), t(1^NBig), t((-1)^NOdd), t(0^NBig), t(0^(-1)), t(2^(-1)), t((-1)^(-3)), t(B^(-1)),
	t((-3)^41), t((-2)^63), t((-2)^65), t((-10)^19), t(3^40), t(NB^3),
	t(B^0.5), t(2.0^B), t(0.5^B), t(B**0.5), t(2.0**B), t(2**B), t(B**B), t(0.0^(-1)), t(0.0**(-1)),
	t(NB**2.0), t(2**NB),
	t(3^(B >> 69)), t((4 rdiv 1)^(B >> 69)), t((B >> 69)^(4 rdiv 1)), t((1 rdiv 1)^(-1)), t((0 rdiv 1)^(-1)), t(3^gcd(B, 6)).
