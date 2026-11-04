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
	eigen,
	stats,
	fitting,
	distributions,
	generators,
	decompositions.

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

% The statistics take a raw array, a stride and a length, which the helpers read off a vector.

stats :-
	vec_from_list(V, [2,4,4,4,5,5,7,9]),
	t(mean_sd_variance, (vec_mean(V, M), vec_sd(V, S), vec_variance(V, Var)), [M,S,Var]),
	t(median_quantile, (vec_median(V, Md), vec_quantile(V, 0.25, Q)), [Md,Q]),
	t(absdev_skew_kurtosis, (vec_absdev(V, A), vec_skew(V, Sk), vec_kurtosis(V, K)), [A,Sk,K]),
	t(stats_input_intact, vec_to_list(V, LV), LV),
	vec_from_list(W, [1,2,3,4,5,6,7,8]),
	t(covariance_correlation, (vec_covariance(V, W, C), vec_correlation(V, W, R)), [C,R]),
	vec_from_list(W3, [1,2,3]),
	t(stats_size_mismatch, vec_covariance(V, W3, _), none),
	t(quantile_out_of_range, vec_quantile(V, 1.5, _), none),

	% A matrix column is a view with a stride of its own, so its mean is not the first column's.

	mat_from_list(M3, [[1,10],[2,20],[3,30]]),
	gsl_matrix_column(M3, 1, View),
	'$struct_to_pointer'(View, CV),
	t(column_stats, (vec_mean(CV, CM), vec_median(CV, CMd), vec_sd(CV, CS)), [CM,CMd,CS]),
	'$free_struct_pointer'(CV),
	t(stats_raw, (gsl_vector_subvector(W, 0, 8, [_,_,St,Pt,_,_]),
		gsl_stats_max(Pt, St, 8, Mx), gsl_stats_min(Pt, St, 8, Mn),
		gsl_stats_mean(Pt, St, 8, Mean), gsl_stats_variance_m(Pt, St, 8, Mean, Vm),
		gsl_stats_sd_m(Pt, St, 8, Mean, Sm), gsl_stats_median(Pt, St, 8, Md2)),
		[Mx,Mn,Vm,Sm,Md2]).

fitting :-
	vec_from_list(X, [1,2,3,4,5]),
	vec_from_list(Y, [2.1,3.9,6.2,8.0,9.9]),
	t(fit_linear, vec_fit_linear(X, Y, C0, C1, Sumsq), [C0,C1,Sumsq]),
	t(fit_linear_raw, (gsl_vector_subvector(X, 0, 5, [_,_,XS,XP,_,_]),
		gsl_vector_subvector(Y, 0, 5, [_,_,YS,YP,_,_]),
		gsl_fit_linear(XP, XS, YP, YS, 5, D0, D1, V00, V01, V11, Sq, St1),
		gsl_fit_linear_est(6.0, D0, D1, V00, V01, V11, Est, Err, St2)),
		[St1,St2,Sq,Est,Err]),
	vec_from_list(Short, [1,2,3]),
	t(fit_size_mismatch, vec_fit_linear(X, Short, _, _, _), none),

	% Design matrix of 1, x, x^2: a quadratic through five points.

	mat_from_list(Dm, [[1,1,1],[1,2,4],[1,3,9],[1,4,16],[1,5,25]]),
	vec_from_list(Y2, [2.2,4.8,9.1,16.2,24.8]),
	t(multifit, (mat_fit_linear(Dm, Y2, C, _, Chisq), vec_to_list(C, LC)), LC-Chisq),
	t(multifit_est, (mat_fit_linear(Dm, Y2, C2, Cov, _),
		vec_from_list(At, [1,6,36]),
		gsl_multifit_linear_est(At, C2, Cov, Ey, Eerr, St3)), [St3,Ey,Eerr]),
	vec_from_list(Y3, [1,2,3]),
	t(multifit_size_mismatch, mat_fit_linear(Dm, Y3, _, _, _), none),
	mat_from_list(Wide, [[1,2,3],[4,5,6]]),
	vec_from_list(Y4, [1,2]),
	t(multifit_underdetermined, mat_fit_linear(Wide, Y4, _, _, _), none).

distributions :-
	t(ugaussian, (gsl_cdf_ugaussian_P(1.96, P1), gsl_cdf_ugaussian_Q(1.96, P2),
		gsl_cdf_ugaussian_Pinv(0.975, P3), gsl_cdf_ugaussian_Qinv(0.025, P4)), [P1,P2,P3,P4]),
	t(chisq_t_f, (gsl_cdf_chisq_Q(3.841, 1.0, P5), gsl_cdf_tdist_Q(2.0, 10.0, P6),
		gsl_cdf_fdist_Q(4.0, 2.0, 10.0, P7)), [P5,P6,P7]),
	t(gamma_beta_exponential, (gsl_cdf_gamma_P(2.0, 2.0, 1.0, P8), gsl_cdf_beta_Pinv(0.5, 2.0, 2.0, P9),
		gsl_cdf_exponential_P(1.0, 2.0, P10)), [P8,P9,P10]),
	t(discrete_cdf, (gsl_cdf_poisson_P(2, 3.0, P11), gsl_cdf_binomial_P(3, 0.5, 10, P12)), [P11,P12]),
	t(densities, (gsl_ran_ugaussian_pdf(0.0, D1), gsl_ran_gaussian_pdf(1.0, 2.0, D2),
		gsl_ran_poisson_pdf(2, 3.0, D3), gsl_ran_binomial_pdf(5, 0.5, 10, D4),
		gsl_ran_bernoulli_pdf(1, 0.25, D5)), [D1,D2,D3,D4,D5]).

% mt19937 is exactly specified, so a seeded uniform stream is the same everywhere. The
% variates are only checked for range and repeatability, which no GSL version changes.

generators :-
	rng_new(42, R),
	t(rng_name, gsl_rng_name(R, N), N),
	t(uniform, (gsl_rng_uniform(R, U1), gsl_rng_uniform(R, U2), gsl_rng_uniform_int(R, 100, I)), [U1,U2,I]),
	rng_new(42, R2),
	t(repeatable, (gsl_rng_uniform(R2, V1), gsl_rng_uniform(R2, V2), V1 =:= U1, V2 =:= U2), same),
	t(bounds, (gsl_rng_min(R, Lo), gsl_rng_max(R, Hi), Lo < Hi), [Lo,Hi]),
	t(variates_in_range, (gsl_ran_gaussian(R, 1.0, _), gsl_ran_exponential(R, 2.0, E), E > 0.0,
		gsl_ran_flat(R, 1.0, 2.0, F), F >= 1.0, F =< 2.0,
		gsl_ran_gamma(R, 2.0, 1.0, G), G > 0.0,
		gsl_ran_beta(R, 2.0, 2.0, B), B >= 0.0, B =< 1.0,
		gsl_ran_chisq(R, 3.0, Ch), Ch > 0.0,
		gsl_ran_tdist(R, 5.0, _),
		gsl_ran_poisson(R, 3.0, Po), Po >= 0,
		gsl_ran_binomial(R, 0.5, 10, Bi), Bi >= 0, Bi =< 10,
		gsl_ran_bernoulli(R, 0.5, Be), (Be == 0 ; Be == 1)), ok),
	rng_free(R),
	rng_free(R2),
	t(bad_seed, rng_new(-1, _), none),
	t(seed_not_integer, rng_new(foo, _), none).

% Cholesky, QR and SVD all overwrite their input, so each helper works on a copy.

decompositions :-
	mat_from_list(Pd, [[4,2],[2,3]]),
	vec_from_list(B, [10,10]),
	t(cholesky_solve, (mat_cholesky_solve(Pd, B, X), vec_to_list(X, LX)), LX),
	t(cholesky_input_intact, mat_to_list(Pd, LPd), LPd),
	mat_from_list(Npd, [[1,2],[2,1]]),
	t(cholesky_not_positive_definite, mat_cholesky_solve(Npd, B, _), none),
	vec_from_list(B3, [1,2,3]),
	t(cholesky_size_mismatch, mat_cholesky_solve(Pd, B3, _), none),

	% An overdetermined system: least squares by QR agrees with the design-matrix fit.

	mat_from_list(Od, [[1,1],[1,2],[1,3]]),
	vec_from_list(Ob, [1,2,3.1]),
	t(lssolve, (mat_lssolve(Od, Ob, Xl), vec_to_list(Xl, LXl)), LXl),
	t(lssolve_agrees, (mat_fit_linear(Od, Ob, C, _, _), vec_to_list(C, LC)), LC),
	t(lssolve_input_intact, mat_to_list(Od, LOd), LOd),
	mat_from_list(Wide, [[1,2,3],[4,5,6]]),
	vec_from_list(B2, [1,2]),
	t(lssolve_underdetermined, mat_lssolve(Wide, B2, _), none),
	t(svd, (mat_svd(Od, _, S, _), vec_to_list(S, LS)), LS),

	% U diag(S) V^T is the matrix we started from.

	t(svd_reconstructs, (mat_svd(Od, U, S2, V), vec_to_list(S2, [S1,S22]),
		mat_from_list(Dg, [[S1,0],[0,S22]]), gsl_matrix_transpose(V, _),
		mat_mul(U, Dg, UD), mat_mul(UD, V, A), mat_to_list(A, LA)), LA),
	t(svd_underdetermined, mat_svd(Wide, _, _, _), none).
