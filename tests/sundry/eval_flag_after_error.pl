% A non-evaluable in is/2, as integer(2), threw before call_builtin put the eval flag back, so later goals ran
% as if inside arithmetic: float(3) and rational(1.5) succeeded as type tests, and float(2^1100) overflowed.

:- initialization(main).

t(Name, G) :- (catch(G, E, (writeq(Name = E), nl, fail)) -> writeq(Name = true) ; writeq(Name = false)), nl.

main :-
	catch(_ is integer(2), E1, true), writeq(E1), nl,
	t(float_3, float(3)), t(rational_float, rational(1.5)), t(integer_float, integer(1.5)),
	B is 2^1100, t(float_big, float(B)),
	E =.. [integer, 2], catch(_ is E, E2, true), writeq(E2), nl,
	t(float_3_again, float(3)), t(rational_3, rational(3)), t(float_1_5, float(1.5)).
