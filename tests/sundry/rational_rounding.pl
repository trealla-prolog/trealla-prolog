% truncate/1, round/1, ceiling/1, floor/1, max/2 and min/2 raised type_error(evaluable, 7r2/0) or
% type_error(integer, 1r3) on a rational. They now work exactly, as in SWI and Scryer, except that round/1
% breaks a tie upwards as it does for a float, where those two round -5r2 to -3. round/1, ceiling/1, floor/1
% and float_*_part/1 also gave a bigint type_error(evaluable, N/0) where a smallint gets type_error(float, N).

:- initialization(main).

t(E) :- catch((X is E, R = X), error(Err, _), R = Err), writeq(E = R), nl.

main :-
	t(truncate(7 rdiv 2)), t(truncate(-7 rdiv 2)), t(floor(7 rdiv 2)), t(floor(-7 rdiv 2)), t(ceiling(7 rdiv 2)), t(ceiling(-7 rdiv 2)),
	t(round(7 rdiv 2)), t(round(-5 rdiv 2)), t(round(-1 rdiv 2)), t(round(5 rdiv 3)), t(round(-5 rdiv 3)), t(round(-1 rdiv 3)),
	t(floor(-1 rdiv 3)), t(ceiling(-1 rdiv 3)), t(truncate(-1 rdiv 3)),
	t(ceiling(-(2^200+1) rdiv 2)), t(round((2^200+1) rdiv 2)), t(floor((-(2^64)-1) rdiv 2)), t(floor(7 rdiv 2) + 1), t(2^floor(9 rdiv 2)),
	t(max(1 rdiv 3, 1 rdiv 2)), t(min(1 rdiv 3, 1 rdiv 2)), t(min(-1 rdiv 3, 0)), t(max(7 rdiv 2, 3)), t(max(1 rdiv 3, 1)),
	t(max(1 rdiv 3, 0.5)), t(min(1 rdiv 3, 0.5)), t(max(1 rdiv 2, 0.5)), t(min(0.5, 1 rdiv 2)), t(max(2^70 rdiv 3, 2^68)),
	t(min(2^70 rdiv 3, 2^69)), t(max(2^1100 rdiv 3, 0.5)), t(min(2^1100 rdiv 3, 0.5)), t(max(1 rdiv 3, 1 rdiv 3) + 0),
	t(round(2^70)), t(floor(2^70)), t(ceiling(2^70)), t(float_integer_part(2^70)), t(float_fractional_part(2^70)).
