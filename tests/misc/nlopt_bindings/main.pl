% library(nlopt): derivative-free, gradient (given and by differences) and global algorithms, bounds, inequality
% and equality constraints, a local optimizer under auglag, maximizing, the stop status, a closure raising, and
% errors. Coordinates are shown to 4 places and values only as near enough, as the last digits vary by platform.
% Run by ../nlopt_bindings.sh, which skips it where libnlopt or the FFI is missing.

:- use_module(library(nlopt)).
:- use_module(library(lists)).
:- initialization(main).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (var(E) -> format("~w: ~q~n", [Label, Show]) ; format("~w: raised ~q~n", [Label, E]))
	;   format("~w: failed~n", [Label])
	).

r(Xs, Ys) :- maplist(r1, Xs, Ys).
r1(X, Y) :- Y0 is round(X * 10000) / 10000, (Y0 =:= 0 -> Y = 0.0 ; Y = Y0).

near(F, T) :- abs(F - T) < 1.0e-6.

quad([X,Y], F) :- F is (X-1)**2 + (Y+2)**2.
quad_grad([X,Y], [GX,GY]) :- GX is 2*(X-1), GY is 2*(Y+2).
rosen([X,Y], F) :- F is (1-X)**2 + 100*(Y-X*X)**2.
neg_area([W,H], F) :- F is -(W*H).
perim([W,H], C) :- C is 2*W + 2*H - 20.
circle([X,Y], C) :- C is X*X + Y*Y - 1.
lin([X,Y], F) :- F is X + Y.
area([W,H], F) :- F is W*H.
bad(_, _) :- throw(oops).

main :-
	t(cobyla, (nlopt_minimize(quad, [0, 0], X1, F1, [status(S1)]), r(X1, R1), near(F1, 0)), R1/S1),
	t(gradient_given, (nlopt_minimize(quad, [0, 0], X2, F2, [algorithm(ld_lbfgs), gradient(quad_grad)]), r(X2, R2), near(F2, 0)), R2),
	t(gradient_numeric, (nlopt_minimize(rosen, [-1.2, 1], X3, F3, [algorithm(ld_lbfgs)]), r(X3, R3), near(F3, 0)), R3),
	t(bobyqa, (nlopt_minimize(rosen, [-1.2, 1], X4, _, [algorithm(ln_bobyqa)]), r(X4, R4)), R4),
	t(inequality, (nlopt_minimize(neg_area, [1, 1], X5, F5, [lower(0), inequality(perim)]), r(X5, R5), near(F5, -25)), R5),
	t(equality, (nlopt_minimize(lin, [0.5, 0.5], X6, _, [equality(circle)]), r(X6, R6)), R6),
	t(slsqp, (nlopt_minimize(lin, [-0.5, -0.4], X7, _, [equality(circle), algorithm(ld_slsqp)]), r(X7, R7)), R7),
	t(global, (nlopt_minimize(quad, [0, 0], X8, _, [algorithm(gn_direct_l), lower([-5,-5]), upper([5,5]), maxeval(3000)]), r(X8, R8)), R8),
	t(maximize, (nlopt_maximize(area, [1, 1], X9, F9, [upper(3), lower(0)]), r(X9, R9), near(F9, 9)), R9),
	t(auglag, (nlopt_minimize(quad, [0, 0], X10, _, [algorithm(auglag), local(ln_cobyla), inequality(perim)]), r(X10, R10)), R10),
	t(maxeval, nlopt_minimize(rosen, [-1.2, 1], _, _, [maxeval(5), status(S11)]), S11),
	t(seeded, (nlopt_minimize(quad, [0, 0], X12, _, [algorithm(gn_crs2_lm), lower(-5), upper(5), seed(42), maxeval(4000)]), r(X12, [A12, B12]), abs(A12-1) < 0.01, abs(B12+2) < 0.01), yes),
	t(closure_raises, nlopt_minimize(bad, [0, 0], _, _, []), x),
	t(unknown_algorithm, nlopt_minimize(quad, [0, 0], _, _, [algorithm(nosuch)]), x),
	t(global_unbounded, (catch(nlopt_minimize(quad, [0, 0], _, _, [algorithm(gn_direct_l)]), error(nlopt_error(C, _), Ctx), true)), C/Ctx),
	t(empty_start, nlopt_minimize(quad, [], _, _, []), x),
	t(bad_bounds, nlopt_minimize(quad, [0, 0], _, _, [lower([1])]), x),
	t(bad_start, nlopt_minimize(quad, [a, 0], _, _, []), x).
