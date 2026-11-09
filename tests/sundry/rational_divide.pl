% / takes a rational as a float, as int/int gives one, as Scryer and SWI's iso mode do. Only rational/smallint
% worked: an integer, bigint, float or rational divided by a rational, and a rational divided by a bigint, float
% or rational, raised type_error(evaluable, N/0). float_integer_part/1 and float_fractional_part/1 likewise,
% where they now give a float, as Scryer does.

:- initialization(main).

t(E) :- catch((X is E, R = X), error(Err, _), R = Err), writeq(E), write(' = '), (float(R) -> format("~6f", [R]) ; writeq(R)), nl.

main :-
	t((1 rdiv 3) / 2), t(2 / (1 rdiv 3)), t((1 rdiv 3) / (1 rdiv 2)), t((1 rdiv 3) / (2 rdiv 3)),
	t(2^70 * ((1 rdiv 3) / 2^70)), t(2^70 / (1 rdiv 3)), t((2^70 rdiv 3) / (1 rdiv 3)), t((-1 rdiv 3) / (1 rdiv 2)),
	t((1 rdiv 3) / 0.5), t(0.5 / (1 rdiv 3)), t((1 rdiv 3) / 0), t((1 rdiv 3) / 0.0), t(0 / (1 rdiv 3)),
	t(0 / (1 rdiv 2^1100)), t(1 / (1 rdiv 2^1100)), t((2^1100 rdiv 3) / 2), t(2 / (2^1100 rdiv 3)),
	t((1 rdiv 3) / a), t(a / (1 rdiv 3)),
	t(float_integer_part(7 rdiv 2)), t(float_integer_part(-7 rdiv 2)), t(float_integer_part(2^1100 rdiv 3)),
	t(float_fractional_part(7 rdiv 2)), t(float_fractional_part(-7 rdiv 2)), t(float_fractional_part(2^1100 rdiv 3)),
	t(float_integer_part(7)), t(float_fractional_part(2^70)).
