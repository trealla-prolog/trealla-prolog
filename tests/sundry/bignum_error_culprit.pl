% An error naming a bigint or rational fresh from arithmetic shared it into the error term, and unwinding to
% the catcher unshared it back to refcount 0 and freed it while the evaluator still held it, read and freed
% again on its way out. Only ASan shows it; a plain build carries on over the freed block.

:- initialization(main).

t(E) :- catch((X is E, R = X), error(Err, _), R = Err), writeq(E = R), nl.

main :-
	t((1 rdiv 2) // 1), t(msb(1 rdiv 2)), t(gcd(1 rdiv 2, 2)), t((1 rdiv 2) /\ 1), t(1 << (1 rdiv 2)),
	t((2^70) >> (1 rdiv 2)), t(msb(-(2^70))), t(msb(-(2^70) rdiv 3)),
	forall(between(1, 1000, _), catch(_ is msb(1 rdiv 2), _, true)), write(done), nl.
