% rational/1 and rationalize/1 as functions, as SWI has them: rational(F) is a float's exact value,
% rationalize(F) the first continued-fraction convergent that rounds back to F. Both were type_error(evaluable).
% rational/1 as a goal stays a type test.

:- initialization(main).

t(E) :- catch((X is E, R = X), error(Err, _), R = Err), writeq(E = R), nl.

main :-
	t(rational(0.1)), t(rational(0.5)), t(rational(-0.75)), t(rational(2.0)), t(rational(1.0e20)), t(rational(0.0)),
	t(rational(3)), t(rational(1 rdiv 3)), t(rational(2^70)), t(rational(2.0) + 1), t(rational(0.1) + 1),
	t(rationalize(0.1)), t(rationalize(-0.1)), t(rationalize(0.2)), t(rationalize(1.1)), t(rationalize(2.675)),
	t(rationalize(123.456)), t(rationalize(-7.25)), t(rationalize(1.0e-5)), t(rationalize(0.333333333333333)),
	t(rationalize(3.14159)), t(rationalize(1.0e20)), t(rationalize(-(0.0))), t(rationalize(3)), t(rationalize(1 rdiv 3)),
	t(rationalize(5.0e-324)), t(rationalize(1.7976931348623157e308) - rational(1.7976931348623157e308)),
	t(rationalize(a)), t(rational(foo)),
	catch((_ is rational(_)), error(E1, _), true), writeq(var = E1), nl,
	(rational(0.1) -> writeq(goal_float(true)) ; writeq(goal_float(false))), nl,
	(rational(3) -> writeq(goal_int(true)) ; writeq(goal_int(false))), nl,
	format("~d~n", [rational(2.0)]).
