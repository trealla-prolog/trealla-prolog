% An arithmetic error unwound to a catch/3 that undid the binding of its bigint or rational argument, freeing
% it while the function still held a copy, which its cleanup then read (a double free if that read 0). Only an
% ASan build sees it fail; this checks the errors come out right while the frees are held back.

:- initialization(main).

t(Name, G) :- catch((G, R = no_error), error(E, _), R = E), writeq(Name = R), nl.

main :-
	t(float_overflow, (Q is 2^1100 rdiv 3, _ is float(Q))),
	t(float_overflow_bigint, (B is 2^1100, _ is float(B))),
	t(sin_overflow, (B is 2^1100, _ is sin(B))),
	t(zero_divisor, (B is 2^70, _ is B // 0)),
	t(nested_zero_divisor, (B is 2^70, _ is 1 + (B // 0))),
	t(not_evaluable, (B is 2^70, _ is B + a)),
	t(rational_not_integer, (Q is 1 rdiv 3, _ is Q // 2)),
	t(aliased, (B is 2^70, C = B, _ is C // 0)),
	t(cleanup_in_unwind, setup_call_cleanup(true, (B is 2^70, _ is B // 0), (C is 2^80, _ is C + 1))),
	t(nested_catch, (B is 2^70, catch((C is 2^71, _ is C // 0), _, true), _ is B // 0)),
	(between(1, 1000, _), catch((B is 2^70, Q is B rdiv 3, _ is Q // 0), _, true), fail ; true),
	writeq(loop = done), nl.
