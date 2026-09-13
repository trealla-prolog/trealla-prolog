% shift/1 returns to the nearest reset/3 that still encloses it, ie. whose
% barrier lies ahead. It used to take the nearest reset/3 choice point
% outright and clear its flag: so backtracking into the goal could not
% shift to it again, a shift after the reset/3 had exited (leaving choices)
% or after an inner one went to the wrong reset/3, and the continuation
% stopped at the end of any call/1, once/1, catch/3 or if-then-else the
% shift sat in, losing the goals after it. Results match SWI and Scryer.

:- initialization(main).

k(C) :- ( C = cont(K) -> call(K) ; call(C) ).
try_shift(B) :- catch(shift(B), _, fail).

a_again(L) :- findall(X-B, reset((member(X,[1,2]), shift(X)), B, _), L).
a_again3(L) :- findall(B, reset((member(X,[1,2,3]), shift(X)), B, _), L).
a_some(L) :- findall(X-B, reset((member(X,[1,2,3]), (X == 2 -> true ; shift(X))), B, _), L).
a_cont(L) :- findall(B-R, (reset((member(X,[1,2]), shift(X), R = after(X)), B, C), k(C)), L).

b_after_exit(L) :- findall(R, (reset(member(X,[1,2]), _, _), ( try_shift(y) -> R = wrongly(X) ; R = none(X) )), L).
b_after_shift(L) :- findall(R, (reset((member(X,[1,2]), shift(X)), B, _), ( try_shift(y) -> R = wrongly(B) ; R = ok(B) )), L).
b_outer_exit(L) :- findall(X-B, reset((reset(member(X,[1,2]), _, _), shift(y)), B, _), L).
b_outer_shift(L) :- findall(X-B1-B2, reset((reset((member(X,[1,2]), shift(in(X))), B1, _), shift(out)), B2, _), L).
b_nested(L) :- findall(B1-B2, reset(reset((member(X,[1,2]), shift(X)), B1, _), B2, _), L).

p_call(X) :- call(shift(a)), X = 1.
p_catch(X) :- catch(shift(a), _, true), X = 1.
p_ite(X) :- ( shift(a) -> X = 1 ; X = 2 ).
p_once(X) :- once(shift(a)), X = 1.
p_inner(X) :- call(shift(a)), X = 1.
p_after(X, Y) :- p_inner(X), Y = 2.
p_after_catch(X, Y) :- catch(p_inner(X), _, true), Y = 2.
p_soft_then(X) :- ( true *-> shift(a) ; true ), X = 1.
p_soft_cond(X) :- ( shift(a) *-> X = 1 ; X = 2 ).

w_call(X) :- reset((call(shift(a)), X = 1), _, C), k(C).
w_once(X) :- reset((once(shift(a)), X = 1), _, C), k(C).
w_catch(X) :- reset((catch(shift(a), _, true), X = 1), _, C), k(C).
w_ite(X) :- reset(((true -> shift(a) ; true), X = 1), _, C), k(C).
w_calln(X) :- reset((call(shift, a), X = 1), _, C), k(C).
w_nested_call(X) :- reset(call(call((shift(a), X = 1))), _, C), k(C).
w_soft(X) :- reset(((true *-> shift(a) ; true), X = 1), _, C), k(C).
w_soft_choices(L) :- findall(Y-X, (reset(((member(Y,[1,2]) *-> shift(Y) ; true), X = 1), _, C), k(C)), L).
w_pred_call(X) :- reset(p_call(X), _, C), k(C).
w_pred_catch(X) :- reset(p_catch(X), _, C), k(C).
w_pred_ite(X) :- reset(p_ite(X), _, C), k(C).
w_pred_once(X) :- reset(p_once(X), _, C), k(C).
w_pred_after(X-Y) :- reset(p_after(X, Y), _, C), k(C).
w_pred_after_catch(X-Y) :- reset(p_after_catch(X, Y), _, C), k(C).
w_pred_soft_then(X) :- reset(p_soft_then(X), _, C), k(C).
w_pred_soft_cond(X) :- reset(p_soft_cond(X), _, C), k(C).

main :-
	forall(
		member(T, [a_again, a_again3, a_some, a_cont,
			b_after_exit, b_after_shift, b_outer_exit, b_outer_shift, b_nested,
			w_call, w_once, w_catch, w_ite, w_calln, w_nested_call, w_soft, w_soft_choices,
			w_pred_call, w_pred_catch, w_pred_ite, w_pred_once, w_pred_after, w_pred_after_catch,
			w_pred_soft_then, w_pred_soft_cond]),
		(	G =.. [T, R],
			(	catch(G, E, R = uncaught(E)) -> true ; R = failed ),
			copy_term(R, R1), numbervars(R1, 0, _),
			write(T), write(': '), print(R1), nl
		)
	).
