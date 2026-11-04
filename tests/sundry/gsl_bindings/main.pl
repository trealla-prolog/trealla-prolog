% library(gsl): every export is defined, and the bindings and helpers give the right answers.
% Run by ../gsl_bindings.sh, which skips it where libgsl or the FFI is missing.

:- use_module(library(gsl)).
:- initialization(main).

% Floats to 6 places, so last-bit differences between BLAS builds don't show.

rnd(X, R) :- float(X), !, R is round(X * 1000000) / 1000000.0.
rnd(X, R) :- compound(X), !, X =.. [F|As], maplist(rnd, As, Rs), R =.. [F|Rs].
rnd(X, X).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (   var(E)
	    ->  rnd(Show, R), format("~w: ~q~n", [Label, R])
	    ;   format("~w: raised ~q~n", [Label, E])
	    )
	;   format("~w: failed~n", [Label])
	).

main :-
	exports_defined,
	matrix_elementwise,
	vectors,
	blas,
	linear_systems,
	eigen.

% Bindings under names GSL lacks used to leave exports undefined.

exports_defined :-
	open('library/gsl.pl', read, S),
	read(S, (:- module(gsl, Exports))),
	close(S),
	findall(PI, (member(PI, Exports), \+ current_predicate(gsl:PI)), Missing),
	format("undefined exports: ~q~n", [Missing]).

matrix_elementwise :-
	forall(
		member(Op, [gsl_matrix_add, gsl_matrix_sub, gsl_matrix_mul_elements, gsl_matrix_div_elements]),
		(	mat_from_list(A, [[1,2],[3,4]]),
			mat_from_list(B, [[5,6],[8,8]]),
			G =.. [Op, A, B, St],
			t(Op, (call(G), mat_to_list(A, L)), St-L)
		)),
	mat_from_list(C, [[1,2],[3,4]]),
	t(gsl_matrix_swap_columns, (gsl_matrix_swap_columns(C, 0, 1, St), mat_to_list(C, L)), St-L).

vectors :-
	vec_from_list(V, [3,-1,4,1]),
	t(max_min, (gsl_vector_max(V, Mx), gsl_vector_min(V, Mn), gsl_vector_minmax(V, Lo, Hi)), [Mx,Mn,Lo,Hi]),
	t(indices, (gsl_vector_max_index(V, I1), gsl_vector_min_index(V, I2), gsl_vector_minmax_index(V, I3, I4)), [I1,I2,I3,I4]),
	forall(
		member(L, [[0,0],[1,2],[-1,-2],[0,2]]),
		(	vec_from_list(W, L),
			t(signs(L), (gsl_vector_isnull(W, A), gsl_vector_ispos(W, B), gsl_vector_isneg(W, C), gsl_vector_isnonneg(W, D)), [A,B,C,D])
		)).

blas :-
	vec_from_list(X, [3,-1,4,1]),
	vec_from_list(Y, [1,2,3,4]),
	t(ddot, gsl_blas_ddot(X, Y, Dot, St1), Dot-St1),
	t(dnrm2_dasum, (gsl_blas_dnrm2(Y, N), gsl_blas_dasum(X, S)), N-S),
	t(daxpy_dscal, (gsl_blas_daxpy(2.0, Y, X, St2), gsl_blas_dscal(0.5, X), vec_to_list(X, LX)), St2-LX),
	mat_from_list(M, [[1,2],[3,4],[5,6]]),
	vec_from_list(Ones2, [1,1]),
	gsl_vector_alloc(3, Out3),
	t(dgemv, (gsl_blas_dgemv(111, 1.0, M, Ones2, 0.0, Out3, St3), vec_to_list(Out3, L3)), St3-L3),
	vec_from_list(Ones3, [1,1,1]),
	gsl_vector_alloc(2, Out2),
	t(dgemv_transposed, (gsl_blas_dgemv(112, 1.0, M, Ones3, 0.0, Out2, St4), vec_to_list(Out2, L2)), St4-L2),
	mat_from_list(K, [[1,0,2],[0,1,3]]),
	t(mat_mul, (mat_mul(M, K, P), mat_to_list(P, LP)), LP),
	t(mat_mul_mismatch, mat_mul(M, M, _), none).

linear_systems :-
	mat_from_list(A, [[4,3],[6,3]]),
	vec_from_list(B, [10,12]),
	t(mat_solve, (mat_solve(A, B, X), vec_to_list(X, LX)), LX),
	t(mat_inverse, (mat_inverse(A, I), mat_to_list(I, LI)), LI),
	t(inverse_times_a, (mat_inverse(A, I2), mat_mul(I2, A, P), mat_to_list(P, LP)), LP),
	t(mat_lup_det, mat_lup_det(A, D), D),
	t(input_intact, mat_to_list(A, LA), LA),
	mat_from_list(Sg, [[1,2],[2,4]]),
	t(singular_det, mat_lup_det(Sg, D2), D2),
	t(singular_solve, mat_solve(Sg, B, _), none),
	t(singular_inverse, mat_inverse(Sg, _), none),
	vec_from_list(B3, [1,2,3]),
	t(solve_size_mismatch, mat_solve(A, B3, _), none),
	mat_from_list(NS, [[1,2,3],[4,5,6]]),
	t(det_not_square, mat_lup_det(NS, _), none).

% gsl_eigen_symmv overwrites the lower triangle, so mat_eigen/3 works on a copy.

eigen :-
	mat_from_list(E2, [[2,1],[1,2]]),
	t(mat_eigen, mat_eigen(E2, V2, _), V2),
	mat_from_list(E4, [[4,1,2,0],[1,3,0,1],[2,0,5,1],[0,1,1,2]]),
	t(eigen_input_intact, (mat_eigen(E4, V4, _), mat_to_list(E4, L4)), V4-L4),
	mat_from_list(A, [[4,3],[6,3]]),
	t(eigen_not_symmetric, mat_eigen(A, _, _), none).
