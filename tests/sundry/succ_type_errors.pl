% can_be(not_less_than_zero, X) let any number through, so succ(X, 1.5) and succ(1.5, X) fell through to an
% instantiation error where SWI and GNU raise type_error(integer, 1.5).

:- initialization(main).

t(Name, G) :- catch((G -> R = true ; R = false), error(E, _), R = E), writeq(Name = R), nl.

main :-
	Q is 1 rdiv 2, B is 2^70,
	t(succ_var_float, succ(_, 1.5)), t(succ_float_var, succ(1.5, _)), t(succ_var_rational, succ(_, Q)),
	t(succ_var_atom, succ(_, a)), t(succ_var_negative, succ(_, -1)), t(succ_var_zero, succ(_, 0)),
	t(succ_3_4, succ(3, 4)), t(succ_down, (succ(X, 4), X == 3)), t(succ_up, (succ(4, Y), Y == 5)),
	t(succ_bigint, (succ(B, S), S =:= 2^70+1)),
	t(can_be_float, can_be(not_less_than_zero, 1.5)), t(can_be_rational, can_be(not_less_than_zero, Q)),
	t(can_be_negative, can_be(not_less_than_zero, -1)), t(can_be_int, can_be(not_less_than_zero, 3)),
	t(can_be_var, can_be(not_less_than_zero, _)).
