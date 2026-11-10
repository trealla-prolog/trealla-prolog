% library(gsl): every export is defined, and the bindings and helpers give the right answers.
% Run by ../gsl_bindings.sh, which skips it where libgsl or the FFI is missing.

:- use_module(library(gsl)).
:- use_module(library(yall)).
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
	decompositions,
	special_functions,
	polynomials,
	sorting,
	fourier,
	histograms,
	complex_eigen,
	splines,
	list_conversion,
	callbacks.

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

special_functions :-
	t(gamma, (gsl_sf_gamma(5.0, G), gsl_sf_lngamma(5.0, LG), gsl_sf_gammainv(5.0, GI),
		gsl_sf_psi(1.0, Psi)), [G,LG,GI,Psi]),
	t(combinatorial, (gsl_sf_fact(5, F), gsl_sf_lnfact(5, LF), gsl_sf_doublefact(5, DF),
		gsl_sf_choose(5, 2, C), gsl_sf_beta(2.0, 3.0, B)), [F,LF,DF,C,B]),
	t(incomplete, (gsl_sf_gamma_inc_P(2.0, 1.0, P), gsl_sf_gamma_inc_Q(2.0, 1.0, Q),
		gsl_sf_beta_inc(2.0, 3.0, 0.5, BI)), [P,Q,BI]),
	t(bessel, (gsl_sf_bessel_J0(1.0, J0), gsl_sf_bessel_J1(1.0, J1), gsl_sf_bessel_Jn(2, 1.0, J2),
		gsl_sf_bessel_Y0(1.0, Y0), gsl_sf_bessel_I0(1.0, I0), gsl_sf_bessel_K0(1.0, K0)),
		[J0,J1,J2,Y0,I0,K0]),
	t(zeta_lambert, (gsl_sf_zeta(2.0, Z), gsl_sf_zetam1(2.0, Zm), gsl_sf_hzeta(2.0, 1.0, Hz),
		gsl_sf_eta(2.0, Eta), gsl_sf_lambert_W0(1.0, W0), gsl_sf_lambert_Wm1(-0.1, Wm1)),
		[Z,Zm,Hz,Eta,W0,Wm1]),
	t(integrals, (gsl_sf_Si(1.0, Si), gsl_sf_Ci(1.0, Ci), gsl_sf_expint_E1(1.0, E1),
		gsl_sf_expint_Ei(1.0, Ei), gsl_sf_dilog(0.5, Di), gsl_sf_clausen(1.0, Cl),
		gsl_sf_debye_1(1.0, De)), [Si,Ci,E1,Ei,Di,Cl,De]),
	t(elementary, (gsl_sf_expm1(0.001, Em), gsl_sf_log_1plusx(0.001, Lp), gsl_sf_sinc(0.5, Sc),
		gsl_sf_hypot(3.0, 4.0, Hy)), [Em,Lp,Sc,Hy]),
	t(legendre_hyperg, (gsl_sf_legendre_P1(0.5, L1), gsl_sf_legendre_P2(0.5, L2),
		gsl_sf_legendre_P3(0.5, L3), gsl_sf_legendre_Pl(3, 0.5, Pl),
		gsl_sf_hyperg_2F1(1.0, 1.0, 2.0, 0.5, Hg)), [L1,L2,L3,Pl,Hg]),

	% The last argument of these four is a precision mode.

	t(airy_elliptic, (gsl_sf_airy_Ai(1.0, 0, Ai), gsl_sf_airy_Bi(1.0, 0, Bi),
		gsl_sf_ellint_Kcomp(0.5, 0, Kc), gsl_sf_ellint_Ecomp(0.5, 0, Ec)), [Ai,Bi,Kc,Ec]),

	% Overflow aborts the process under GSL's own handler, so it is run without one.

	t(overflow_without_handler, (gsl_errors_off(gsl_sf_gamma(200.0, Big)), Big > 1.0e300), too_big).

polynomials :-
	t(poly_eval, (poly_eval([1,2,3], 2.0, Y), poly_eval([1], 7.0, Y2)), [Y,Y2]),
	t(real_roots, poly_roots([-1,0,1], R1), R1),
	t(complex_roots, poly_roots([1,0,1], R2), R2),
	t(cubic_roots, poly_roots([-6,11,-6,1], R3), R3),
	t(solve_quadratic, gsl_poly_solve_quadratic(1.0, 0.0, -4.0, X0, X1, N1), [N1,X0,X1]),
	t(solve_cubic, gsl_poly_solve_cubic(-6.0, 11.0, -6.0, C0, C1, C2, N2), [N2,C0,C1,C2]),
	t(poly_degree, poly_roots([1], _), none),
	t(poly_leading_zero, poly_roots([1,2,0], _), none),
	t(poly_eval_empty, poly_eval([], 1.0, _), none).

sorting :-
	vec_from_list(V, [3,1,4,1,5]),
	t(sort_index, vec_sort_index(V, Idx), Idx),
	t(sort_index_leaves_input, vec_to_list(V, LV), LV),
	gsl_permutation_alloc(5, P),
	t(permute, (gsl_sort_vector_index(P, V, St1), gsl_permute_vector(P, V, St2),
		vec_to_list(V, SV)), [St1,St2]-SV),
	t(permutation, (perm_to_list(P, LP), gsl_permutation_size(P, Size),
		gsl_permutation_valid(P, Valid)), [LP,Size,Valid]),
	gsl_permutation_alloc(5, Inv),
	t(inverse, (gsl_permutation_inverse(Inv, P, St3), perm_to_list(Inv, LI)), St3-LI),
	t(reverse_next, (gsl_permutation_reverse(Inv), perm_to_list(Inv, LR),
		gsl_permutation_next(Inv, St4)), [LR,St4]),
	gsl_permutation_free(Inv),
	gsl_permutation_free(P),
	vec_from_list(W, [5,4,3,2,1]),
	gsl_vector_alloc(2, Small),
	t(smallest, (vec_data_ptr(Small, SP), gsl_sort_vector_smallest(SP, 2, W, St5),
		vec_to_list(Small, LS)), St5-LS).

% The data pointer of a freshly allocated vector, which the raw sort functions want.

vec_data_ptr(V, Ptr) :-
	'$gsl_vector_size'(V, N),
	gsl_vector_subvector(V, 0, N, [_,_,_,Ptr,_,_]).

% A real transform, then back again: the radix2 halfcomplex layout round-trips.

fourier :-
	vec_from_list(V, [1,2,3,4,5,6,7,8]),
	t(fft, vec_fft(V, Pairs), Pairs),
	t(fft_leaves_input, vec_to_list(V, LV), LV),
	t(ifft, (vec_fft(V, P2), vec_ifft(P2, Back), vec_to_list(Back, LB)), LB),
	vec_from_list(Odd, [1,2,3,4,5]),
	t(fft_not_power_of_two, vec_fft(Odd, _), none),
	t(ifft_not_power_of_two, vec_ifft([1.0-0.0,2.0-0.0,3.0-0.0], _), none),
	t(fft_raw, (vec_from_list(R, [1,0,0,0]), vec_data_ptr(R, RP),
		gsl_fft_real_radix2_transform(RP, 1, 4, St), vec_to_list(R, LR)), St-LR).

histograms :-
	hist_new(4, 0.0, 4.0, H),
	hist_fill(H, [0.5, 1.5, 1.6, 2.5, 3.5, 9.9]),
	t(bins, hist_to_list(H, Bins), Bins),
	t(hist_stats, (gsl_histogram_sum(H, Sum), gsl_histogram_mean(H, Mean),
		gsl_histogram_sigma(H, Sigma), gsl_histogram_max_val(H, MaxVal),
		gsl_histogram_max_bin(H, MaxBin), gsl_histogram_bins(H, N)),
		[Sum,Mean,Sigma,MaxVal,MaxBin,N]),
	t(hist_range, (gsl_histogram_min(H, Lo), gsl_histogram_max(H, Hi),
		gsl_histogram_find(H, 2.5, Bin, St1)), [Lo,Hi,Bin,St1]),
	t(hist_arithmetic, (gsl_histogram_clone(H, H2), gsl_histogram_scale(H2, 2.0, St2),
		gsl_histogram_add(H2, H, St3), hist_to_list(H2, B2),
		gsl_histogram_free(H2)), [St2,St3]-B2),
	t(hist_reset, (gsl_histogram_reset(H), gsl_histogram_sum(H, Sum2)), Sum2),
	gsl_histogram_free(H),
	t(hist_bad_bins, hist_new(0, 0.0, 1.0, _), none),
	t(hist_bad_range, hist_new(4, 1.0, 0.0, _), none).

% A rotation has no real eigenvalues, so gsl_eigen_symmv/4 cannot do it at all.

complex_eigen :-
	t(complex_list, (vec_complex_from_list(CV, [1-2, 3-4]), vec_complex_to_list(CV, CL),
		gsl_vector_complex_get(CV, 0, Z), gsl_complex_abs(Z, Abs), gsl_complex_arg(Z, Arg),
		gsl_vector_complex_free(CV)), CL-[Abs,Arg]),
	mat_from_list(Rot, [[0,-1],[1,0]]),
	t(rotation_eigenvalues, mat_eigen_nonsymm(Rot, Vals, _), Vals),

	% The eigenvectors are normalised, so the moduli of their parts are not a phase choice.

	t(eigenvector_moduli, (mat_eigen_nonsymm(Rot, _, Vecs), maplist(moduli, Vecs, Mods)), Mods),
	t(nonsymm_input_intact, mat_to_list(Rot, LRot), LRot),
	mat_from_list(A, [[4,3],[6,3]]),
	t(real_eigenvalues, mat_eigen_nonsymm(A, Vals2, _), Vals2),
	mat_from_list(NS, [[1,2,3],[4,5,6]]),
	t(nonsymm_not_square, mat_eigen_nonsymm(NS, _, _), none).

moduli(Pairs, Mods) :-
	findall(M, (member(Re-Im, Pairs), gsl_complex_abs([gsl_complex,Re,Im], M)), Mods).

% The interpolation types come from global variables, which $dlsym_deref/3 reads.

splines :-
	t(cspline, (spline_new(cspline, [1,2,3,4,5], [1,4,9,16,25], S),
		gsl_spline_name(S, Name), spline_eval(S, 2.5, Y), spline_deriv(S, 2.5, D),
		spline_integ(S, 1.0, 5.0, I), spline_free(S)), [Name,Y,D,I]),
	t(linear, (spline_new(linear, [1,2], [10,20], S2), spline_eval(S2, 1.5, Y2),
		spline_free(S2)), Y2),
	t(every_type, findall(Nm,
		(	spline_type(T), spline_new(T, [1,2,3,4,5,6], [1,4,9,16,25,36], S3),
			gsl_spline_name(S3, Nm), spline_free(S3)
		), Names), Names),
	t(out_of_range, (spline_new(cspline, [1,2,3], [1,4,9], S4), spline_eval(S4, 9.0, _)), none),
	t(too_few_points, spline_new(cspline, [1,2], [1,4], _), none),
	t(unknown_type, spline_new(bogus, [1,2,3], [1,4,9], _), none),
	t(descending_x, spline_new(cspline, [3,2,1], [1,4,9], _), none),
	t(spline_size_mismatch, spline_new(cspline, [1,2,3], [1,4], _), none).

% A list that doesn't match used to walk off the start of the vector and abort GSL.

list_conversion :-
	vec_from_list(V, [1,2,3]),
	t(vec_wrong_length, vec_to_list(V, [1.0,2.0]), none),
	t(vec_wrong_values, vec_to_list(V, [9.0,9.0,9.0]), none),
	t(vec_right, vec_to_list(V, [1.0,2.0,3.0]), yes),
	mat_from_list(M, [[1,2],[3,4]]),
	t(mat_wrong_shape, mat_to_list(M, [[1.0]]), none),
	t(mat_wrong_values, mat_to_list(M, [[9.0,9.0],[9.0,9.0]]), none),
	t(mat_right, mat_to_list(M, [[1.0,2.0],[3.0,4.0]]), yes).

% integrate, deriv, root and minimize run a Prolog closure through a foreign callback.

sq_(X, Y) :- Y is X*X.
gauss_(X, Y) :- Y is exp(-X*X).
cubic_(X, Y) :- Y is X*X*X - 2*X - 5.
bowl_(X, Y) :- Y is (X - 1.5)**2 + 0.25.
inv_sqrt_(X, Y) :- Y is 1/sqrt(X).
not_number_(_, foo).
raises_(_, _) :- throw(raised).

callbacks :-
	t(integrate, integrate(sq_, 0, 3, I1), I1),
	t(integrate_reversed, integrate(sq_, 3, 0, I2), I2),
	t(integrate_singular, integrate(inv_sqrt_, 0, 1, I3), I3),
	t(integrate_infinite, integrate(gauss_, -inf, inf, I4), I4),
	t(integrate_half_infinite, (integrate(gauss_, 0, inf, I5), integrate(gauss_, -inf, 0, I6)), I5-I6),
	t(integrate_lambda, (K = 3, integrate([X,Y]>>(Y is K*sin(X)), 0, pi, I7)), I7),
	t(deriv, deriv(sq_, 3, D1), D1),
	t(root, root(cubic_, 2, 3, R1), R1),
	t(minimize, minimize(bowl_, 1, 0, 4, M1), M1),
	t(no_bracket, root(sq_, 1, 2, _), none),
	t(not_number, integrate(not_number_, 0, 1, _), none),
	t(closure_raises, integrate(raises_, 0, 1, _), none),
	t(raw_qng, (gsl_function_new(sq_, Fn), gsl_integration_qng(Fn, 0.0, 2.0, 1.0e-10, 1.0e-10, Q, _, _, St),
		gsl_function_free(Fn)), Q-St).

