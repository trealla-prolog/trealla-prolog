% A rational turned into a float divided its halves as doubles, so with both past a double's range it was inf/inf:
% float/1 raised evaluation_error(undefined) for (2^1100+1) rdiv 2^1100, which is 1.0. And float/1, sqrt/1, log/1
% and tan/1 overflowing on a rational wrote the double into accum while it was still tagged rational, so the error
% read the double as a pointer and the process died.

:- initialization(main).

t(E) :- catch((X is E, R = X), error(Err, _), R = Err), writeq(R), nl.

main :-
	B is 2^1100, B1 is B+1, NB1 is -B1, D is (2^1022)*3, H is 2^2200,
	t(float(B1 rdiv B)), t(float(NB1 rdiv B)), t(float(1 rdiv B)), t(float(1 rdiv D)), t(float(B rdiv 3)),
	t(sqrt(B1 rdiv B)), t(sqrt(H rdiv 3)), t(log(H rdiv 3)), t(log(B1 rdiv B)), t(float(2 rdiv 3)).
