% The one-argument float functions over every kind of number, checked through the shared float_fn_1() helper:
% domain errors, overflow, an atom, -0.0. sqrt of a negative float other than -1.0 let NaN out to is/2, which
% raised undefined there; it now raises in sqrt/1. log10 checks the sign first, as log does, so a tiny positive
% rational is float_overflow, not zero_divisor, and a huge negative one undefined, not float_overflow.

:- initialization(main).

fns([exp, sqrt, log, sin, cos, tan, asin, acos, atan, sinh, cosh, tanh, asinh, acosh, atanh, erf, erfc, log10]).
arg(a). arg(0). arg(1). arg(-1). arg(3). arg(2^1100). arg(-(2^1100)). arg(1 rdiv 3). arg(-1 rdiv 3).
arg(1 rdiv 2^1100). arg(0.0). arg(-(0.0)). arg(0.5). arg(-2.5). arg(1.0).

t(E) :- catch((X is E, (float(X) -> format(atom(R), "~4e", [X]) ; R = X)), error(Err, Ctx), R = error(Err, Ctx)), writeq(E = R), nl.

main :-
	fns(Fs), forall((member(F, Fs), arg(A)), (E =.. [F, A], t(E))),
	forall(member(E, [exp(1.0e300), sqrt(1.0e300), log(1.0e300), sinh(1.0e300), cosh(-1.0e300), tanh(1.0e300), log10(1.0e300)]), t(E)).
