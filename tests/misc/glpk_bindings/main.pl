% library(glpk): lp_solve/3,4 on LPs and MIPs, its bounds, errors and options, and lp_solve_file/3.
% Run by ../glpk_bindings.sh, which skips it where libglpk or the FFI is missing.

:- use_module(library(glpk)).
:- use_module(library(lists)).
:- use_module(library(yall)).
:- initialization(main).

% Floats to 6 places, so last-bit differences between GLPK builds don't show.

rnd(X, R) :- float(X), !, R is round(X * 1000000) / 1000000.0.
rnd(X, R) :- compound(X), !, X =.. [F|As], maplist(rnd, As, Rs), R =.. [F|Rs].
rnd(X, X).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (   var(E)
	    ->  rnd(Show, R), format("~w: ~q~n", [Label, R])
	    ;   numbervars(E, 0, _), format("~w: raised ~q~n", [Label, E])
	    )
	;   format("~w: failed~n", [Label])
	).

% The 0/1 assignment of N jobs to N workers at least total cost, (I*7 + J*13) mod 17 + 1.

assignment(N, Cost) :-
	length(Rows, N),
	maplist([R]>>length(R, N), Rows),
	findall(C, (between(1, N, I), between(1, N, J), C is (I*7 + J*13) mod 17 + 1), Costs),
	append(Rows, Xs),
	maplist([C,X,C*X]>>true, Costs, Xs, Obj),
	maplist([R,R =:= 1]>>true, Rows, RowCons),
	columns_(Rows, Cols),
	maplist([C,C =:= 1]>>true, Cols, ColCons),
	maplist([X,X >= 0]>>true, Xs, Pos),
	append([RowCons, ColCons, Pos], Cons),
	lp_solve(min(Obj), Cons, Cost).

columns_([[]|_], []) :- !.
columns_(Rs, [Col|Cols]) :- maplist([[H|T],H,T]>>true, Rs, Col, Rest), columns_(Rest, Cols).

main :-
	t(lp, lp_solve(max(3*X1 + 2*Y1), [X1 + Y1 =< 4, X1 + 3*Y1 =< 6, X1 =< 3, X1 >= 0, Y1 >= 0], V1), [X1, Y1, V1]),
	t(minimize, lp_solve(min(2*A + 3*B), [A + B >= 10, A >= 2, B >= 3], V2), [A, B, V2]),
	t(equalities, lp_solve(max(X3 + Y3), [X3 + 2*Y3 =:= 8, X3 - Y3 = 2], V3), [X3, Y3, V3]),
	t(mip, lp_solve(max(5*P + 4*Q + 3*R), [2*P + 3*Q + R =< 5, 4*P + Q + 2*R =< 11, 3*P + 4*Q + 2*R =< 8,
		integer([P, Q, R]), P >= 0, Q >= 0, R >= 0], V4), [P, Q, R, V4]),
	t(knapsack, (length(Ks, 5), maplist([W,K,W*K]>>true, [12,2,1,1,4], Ks, Wt), maplist([C,K,C*K]>>true, [4,2,1,2,10], Ks, Val),
		lp_solve(max(Val), [Wt =< 15, binary(Ks)], V5)), [Ks, V5]),
	t(assignment, assignment(30, V6), V6),
	t(bounds_only, lp_solve(max(X7), [X7 =< 3], V7), [X7, V7]),
	t(integer_bound_rounded, lp_solve(max(X8), [X8 =< 3.5, X8 >= 0.1*30, integer(X8)], V8), [X8, V8]),
	t(negative_coefficient, lp_solve(min(X9), [-2*X9 =< 6], V9), [X9, V9]),
	t(expressions, lp_solve(max(2*(X10 + Y10)/4 + pi*0), [(X10 + Y10)*3 =< 6, [X10, Y10] >= 1, X10 >= 0, Y10 >= 0], V10), V10),
	t(bound_variable, (X11 = 2, lp_solve(max(X11 + Y11), [X11 + Y11 =< 5], V11)), [Y11, V11]),
	t(no_variables, lp_solve(max(2 + 3), [1 =< 2], V12), V12),
	t(infeasible_bounds, lp_solve(max(X13), [X13 >= 5, X13 =< 3], _), none),
	t(infeasible_rows, lp_solve(max(X14 + Y14), [X14 + Y14 >= 5, X14 + Y14 =< 3], _), none),
	t(infeasible_integer, lp_solve(max(X15), [X15 >= 0.2, X15 =< 0.8, integer(X15)], _), none),
	t(infeasible_constant, lp_solve(max(1), [3 =< 2], _), none),
	t(unbounded, lp_solve(max(X16 + Y16), [X16 - Y16 =< 1, X16 >= 0, Y16 >= 0], _), none),
	t(nonlinear, lp_solve(max(X17*Y17), [X17 =< 1], _), none),
	t(strict, lp_solve(max(X18), [X18 < 1], _), none),
	t(bad_objective, lp_solve(foo(X19), [X19 =< 1], _), none),
	t(not_a_variable, lp_solve(max(x), [], _), none),
	t(not_a_list, lp_solve(max(X20), foo, _), none),
	t(bad_kind, lp_solve(max(X21), [integer(3), X21 =< 1], _), none),
	t(zero_divisor, lp_solve(max(X22/0), [X22 =< 1], _), none),
	t(bad_option, lp_solve(max(X23), [X23 =< 1], _, [bogus]), none),
	t(write_and_read, (lp_solve(max(X24 + 2*Y24), [X24 + Y24 =< 2, X24 >= 0, Y24 >= 0, integer(Y24)], V24,
		[write_lp('glpk_bindings_test.lp')]), lp_solve_file('glpk_bindings_test.lp', F24, S24),
		delete_file('glpk_bindings_test.lp')), [V24, F24, S24]),
	t(missing_file, lp_solve_file('no_such_model.lp', _, _), none).
