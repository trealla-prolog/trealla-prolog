:- module(gsl, [

	gsl_set_error_handler_off/1,
	gsl_set_error_handler/2,

	gsl_vector_alloc/2,
	gsl_vector_calloc/2,
	gsl_vector_free/1,
	gsl_vector_ptr/3,
	gsl_vector_const_ptr/3,
	gsl_vector_memcpy/3,
	gsl_vector_swap/3,
	gsl_vector_set/3,
	gsl_vector_get/3,
	gsl_vector_set_all/2,
	gsl_vector_set_zero/1,
	gsl_vector_set_basis/3,
	gsl_vector_fwrite/3,
	gsl_vector_fread/3,
	gsl_vector_add/3,
	gsl_vector_sub/3,
	gsl_vector_mul/3,
	gsl_vector_div/3,
	gsl_vector_scale/3,
	gsl_vector_add_constant/3,
	gsl_vector_sum/2,
	gsl_vector_max/2,
	gsl_vector_min/2,
	gsl_vector_minmax/3,
	gsl_vector_max_index/2,
	gsl_vector_min_index/2,
	gsl_vector_minmax_index/3,
	gsl_vector_isnull/2,
	gsl_vector_ispos/2,
	gsl_vector_isneg/2,
	gsl_vector_isnonneg/2,

	gsl_vector_subvector/4,

	gsl_matrix_alloc/3,
	gsl_matrix_calloc/3,
	gsl_matrix_free/1,
	gsl_matrix_ptr/4,
	gsl_matrix_const_ptr/4,
	gsl_matrix_memcpy/3,
	gsl_matrix_swap/3,
	gsl_matrix_set/4,
	gsl_matrix_get/4,
	gsl_matrix_set_all/2,
	gsl_matrix_set_zero/1,
	gsl_matrix_set_identity/1,
	gsl_matrix_minmax/3,
	gsl_matrix_max/2,
	gsl_matrix_min/2,
	gsl_matrix_transpose_memcpy/3,
	gsl_matrix_transpose/2,
	gsl_matrix_fwrite/3,
	gsl_matrix_fread/3,
	gsl_matrix_get_row/4,
	gsl_matrix_get_col/4,
	gsl_matrix_set_row/4,
	gsl_matrix_set_col/4,
	gsl_matrix_swap_rows/4,
	gsl_matrix_swap_columns/4,
	gsl_matrix_swap_rowcol/4,
	gsl_matrix_add/3,
	gsl_matrix_sub/3,
	gsl_matrix_mul_elements/3,
	gsl_matrix_div_elements/3,
	gsl_matrix_scale/3,
	gsl_matrix_scale_columns/3,
	gsl_matrix_scale_rows/3,
	gsl_matrix_add_constant/3,

	gsl_matrix_submatrix/6,
	gsl_matrix_row/3,
	gsl_matrix_column/3,
	gsl_matrix_subrow/5,
	gsl_matrix_subcolumn/5,
	gsl_matrix_diagonal/2,
	gsl_matrix_subdiagonal/3,
	gsl_matrix_superdiagonal/3,

	gsl_permutation_calloc/2,
	gsl_permutation_alloc/2,
	gsl_permutation_init/1,
	gsl_permutation_free/1,

	gsl_eigen_symmv_alloc/2,
	gsl_eigen_symmv/4,
	gsl_eigen_symmv_free/1,
	gsl_eigen_symmv_sort/3,

	gsl_linalg_LU_decomp/4,
	gsl_linalg_LU_solve/5,
	gsl_linalg_LU_det/3,
	gsl_linalg_LU_invert/4,

	gsl_linalg_cholesky_decomp1/2,
	gsl_linalg_cholesky_solve/4,
	gsl_linalg_cholesky_svx/3,
	gsl_linalg_cholesky_invert/2,

	gsl_linalg_QR_decomp/3,
	gsl_linalg_QR_solve/5,
	gsl_linalg_QR_lssolve/6,
	gsl_linalg_QR_unpack/5,

	gsl_linalg_SV_decomp/5,
	gsl_linalg_SV_solve/6,

	gsl_blas_ddot/4,
	gsl_blas_dnrm2/2,
	gsl_blas_dasum/2,
	gsl_blas_daxpy/4,
	gsl_blas_dscal/2,
	gsl_blas_dgemv/7,
	gsl_blas_dgemm/8,

	gsl_stats_mean/4,
	gsl_stats_variance/4,
	gsl_stats_variance_m/5,
	gsl_stats_sd/4,
	gsl_stats_sd_m/5,
	gsl_stats_absdev/4,
	gsl_stats_skew/4,
	gsl_stats_kurtosis/4,
	gsl_stats_max/4,
	gsl_stats_min/4,
	gsl_stats_median/4,
	gsl_stats_median_from_sorted_data/4,
	gsl_stats_quantile_from_sorted_data/5,
	gsl_stats_covariance/6,
	gsl_stats_correlation/6,

	gsl_sort_vector/1,

	gsl_fit_linear/12,
	gsl_fit_wlinear/14,
	gsl_fit_linear_est/9,
	gsl_fit_mul/9,
	gsl_fit_mul_est/6,

	gsl_multifit_linear_alloc/3,
	gsl_multifit_linear_free/1,
	gsl_multifit_linear/7,
	gsl_multifit_wlinear/8,
	gsl_multifit_linear_est/6,

	gsl_cdf_ugaussian_P/2,
	gsl_cdf_ugaussian_Q/2,
	gsl_cdf_ugaussian_Pinv/2,
	gsl_cdf_ugaussian_Qinv/2,
	gsl_cdf_gaussian_P/3,
	gsl_cdf_gaussian_Q/3,
	gsl_cdf_gaussian_Pinv/3,
	gsl_cdf_gaussian_Qinv/3,
	gsl_cdf_exponential_P/3,
	gsl_cdf_exponential_Q/3,
	gsl_cdf_exponential_Pinv/3,
	gsl_cdf_exponential_Qinv/3,
	gsl_cdf_chisq_P/3,
	gsl_cdf_chisq_Q/3,
	gsl_cdf_chisq_Pinv/3,
	gsl_cdf_chisq_Qinv/3,
	gsl_cdf_tdist_P/3,
	gsl_cdf_tdist_Q/3,
	gsl_cdf_tdist_Pinv/3,
	gsl_cdf_tdist_Qinv/3,
	gsl_cdf_fdist_P/4,
	gsl_cdf_fdist_Q/4,
	gsl_cdf_fdist_Pinv/4,
	gsl_cdf_fdist_Qinv/4,
	gsl_cdf_gamma_P/4,
	gsl_cdf_gamma_Q/4,
	gsl_cdf_gamma_Pinv/4,
	gsl_cdf_gamma_Qinv/4,
	gsl_cdf_beta_P/4,
	gsl_cdf_beta_Q/4,
	gsl_cdf_beta_Pinv/4,
	gsl_cdf_beta_Qinv/4,
	gsl_cdf_poisson_P/3,
	gsl_cdf_poisson_Q/3,
	gsl_cdf_binomial_P/4,
	gsl_cdf_binomial_Q/4,

	gsl_rng_env_setup/1,
	gsl_rng_alloc/2,
	gsl_rng_clone/2,
	gsl_rng_free/1,
	gsl_rng_set/2,
	gsl_rng_get/2,
	gsl_rng_max/2,
	gsl_rng_min/2,
	gsl_rng_name/2,
	gsl_rng_uniform/2,
	gsl_rng_uniform_pos/2,
	gsl_rng_uniform_int/3,

	gsl_ran_ugaussian/2,
	gsl_ran_ugaussian_pdf/2,
	gsl_ran_gaussian/3,
	gsl_ran_gaussian_pdf/3,
	gsl_ran_exponential/3,
	gsl_ran_exponential_pdf/3,
	gsl_ran_flat/4,
	gsl_ran_flat_pdf/4,
	gsl_ran_gamma/4,
	gsl_ran_gamma_pdf/4,
	gsl_ran_beta/4,
	gsl_ran_beta_pdf/4,
	gsl_ran_chisq/3,
	gsl_ran_chisq_pdf/3,
	gsl_ran_tdist/3,
	gsl_ran_tdist_pdf/3,
	gsl_ran_poisson/3,
	gsl_ran_poisson_pdf/3,
	gsl_ran_binomial/4,
	gsl_ran_binomial_pdf/4,
	gsl_ran_bernoulli/3,
	gsl_ran_bernoulli_pdf/3,

	vec_from_list/2,
	vec_to_list/2,
	vec_read/3,
	vec_write/2,
	vec_random/2,

	mat_from_list/2,
	mat_to_list/2,
	mat_read/4,
	mat_write/2,
	mat_lup_det/2,
	mat_random/3,
	mat_eigen/3,
	mat_mul/3,
	mat_solve/3,
	mat_inverse/2,
	mat_cholesky_solve/3,
	mat_lssolve/3,
	mat_svd/4,
	mat_fit_linear/5,

	vec_mean/2,
	vec_variance/2,
	vec_sd/2,
	vec_absdev/2,
	vec_skew/2,
	vec_kurtosis/2,
	vec_median/2,
	vec_quantile/3,
	vec_covariance/3,
	vec_correlation/3,
	vec_fit_linear/5,

	rng_new/2,
	rng_free/1
	]).

% GNU Scientific Library (GSL)
%
% UBUNTU: sudo apt install libgsl-dev
%
% REF: https://www.gnu.org/software/gsl/doc/html/index.html
%
% Contributions to add new definitions are most welcome!

:- use_module(library(lists)).

:- foreign_struct(gsl_vector_view, [ulong,ulong,ptr,ptr,sint]).
:- foreign_struct(gsl_matrix_view, [ulong,ulong,ulong,ptr,ptr,sint]).

:- use_foreign_module('libgslcblas.so', []).

:- use_foreign_module('libgsl.so', [
	gsl_set_error_handler_off([], ptr),
	gsl_set_error_handler([ptr], ptr),

	gsl_vector_alloc([ulong], ptr),
	gsl_vector_calloc([ulong], ptr),
	gsl_vector_free([ptr], void),
	gsl_vector_memcpy([ptr,ptr], sint),
	gsl_vector_swap([ptr,ptr], sint),
	gsl_vector_ptr([ptr,ulong], ptr),
	gsl_vector_const_ptr([ptr,ulong], ptr),
	gsl_vector_set([ptr,ulong,double], void),
	gsl_vector_get([ptr,ulong], double),
	gsl_vector_set_all([ptr,double], void),
	gsl_vector_set_zero([ptr], void),
	gsl_vector_set_basis([ptr,ulong], sint),
	gsl_vector_fwrite([ptr,ptr], sint),
	gsl_vector_fread([ptr,ptr], sint),
	gsl_vector_add([ptr,ptr], sint),
	gsl_vector_sub([ptr,ptr], sint),
	gsl_vector_mul([ptr,ptr], sint),
	gsl_vector_div([ptr,ptr], sint),
	gsl_vector_scale([ptr,double], sint),
	gsl_vector_add_constant([ptr,double], sint),
	gsl_vector_sum([ptr], double),
	gsl_vector_max([ptr], double),
	gsl_vector_min([ptr], double),
	gsl_vector_minmax([ptr,-double,-double], void),
	gsl_vector_max_index([ptr], ulong),
	gsl_vector_min_index([ptr], ulong),
	gsl_vector_minmax_index([ptr,-ulong,-ulong], void),
	gsl_vector_isnull([ptr], sint),
	gsl_vector_ispos([ptr], sint),
	gsl_vector_isneg([ptr], sint),
	gsl_vector_isnonneg([ptr], sint),

	gsl_vector_subvector([ptr,ulong,ulong], gsl_vector_view),

	gsl_matrix_alloc([ulong,ulong], ptr),
	gsl_matrix_calloc([ulong,ulong], ptr),
	gsl_matrix_free([ptr], void),
	gsl_matrix_memcpy([ptr,ptr], sint),
	gsl_matrix_swap([ptr,ptr], sint),
	gsl_matrix_ptr([ptr,ulong,ulong], ptr),
	gsl_matrix_const_ptr([ptr,ulong,ulong], ptr),
	gsl_matrix_set([ptr,ulong,ulong,double], void),
	gsl_matrix_get([ptr,ulong,ulong], double),
	gsl_matrix_set_all([ptr,double], void),
	gsl_matrix_set_zero([ptr], void),
	gsl_matrix_set_identity([ptr], void),
	gsl_matrix_minmax([ptr,-double,-double], void),
	gsl_matrix_max([ptr], double),
	gsl_matrix_min([ptr], double),
	gsl_matrix_transpose_memcpy([ptr,ptr], sint),
	gsl_matrix_transpose([ptr], sint),
	gsl_matrix_fwrite([ptr,ptr], sint),
	gsl_matrix_fread([ptr,ptr], sint),
	gsl_matrix_get_row([ptr,ptr,ulong], sint),
	gsl_matrix_get_col([ptr,ptr,ulong], sint),
	gsl_matrix_set_row([ptr,ulong,ptr], sint),
	gsl_matrix_set_col([ptr,ulong,ptr], sint),
	gsl_matrix_swap_rows([ptr,ulong,ulong], sint),
	gsl_matrix_swap_columns([ptr,ulong,ulong], sint),
	gsl_matrix_swap_rowcol([ptr,ulong,ulong], sint),
	gsl_matrix_add([ptr,ptr], sint),
	gsl_matrix_sub([ptr,ptr], sint),
	gsl_matrix_mul_elements([ptr,ptr], sint),
	gsl_matrix_div_elements([ptr,ptr], sint),
	gsl_matrix_scale([ptr,double], sint),
	gsl_matrix_scale_columns([ptr,ptr], sint),
	gsl_matrix_scale_rows([ptr,ptr], sint),
	gsl_matrix_add_constant([ptr,double], sint),

	gsl_matrix_submatrix([ptr,ulong,ulong,ulong,ulong], gsl_matrix_view),
	gsl_matrix_row([ptr,ulong], gsl_vector_view),
	gsl_matrix_column([ptr,ulong], gsl_vector_view),
	gsl_matrix_subrow([ptr,ulong,ulong,ulong], gsl_vector_view),
	gsl_matrix_subcolumn([ptr,ulong,ulong,ulong], gsl_vector_view),
	gsl_matrix_diagonal([ptr], gsl_vector_view),
	gsl_matrix_subdiagonal([ptr,ulong], gsl_vector_view),
	gsl_matrix_superdiagonal([ptr,ulong], gsl_vector_view),

	gsl_permutation_calloc([ulong], ptr),
	gsl_permutation_alloc([ulong], ptr),
	gsl_permutation_init([ptr], void),
	gsl_permutation_free([ptr], void),

	gsl_eigen_symmv_alloc([ulong], ptr),
	gsl_eigen_symmv([ptr,ptr,ptr,ptr], void),
	gsl_eigen_symmv_free([ptr], void),
	gsl_eigen_symmv_sort([ptr,ptr,sint], void),

	gsl_linalg_LU_decomp([ptr,ptr,-sint], sint),
	gsl_linalg_LU_solve([ptr,ptr,ptr,ptr], sint),
	gsl_linalg_LU_det([ptr,sint], double),
	gsl_linalg_LU_invert([ptr,ptr,ptr], sint),

	% Cholesky, QR and SVD all overwrite the matrix they are given; the helpers below work on a copy.

	gsl_linalg_cholesky_decomp1([ptr], sint),
	gsl_linalg_cholesky_solve([ptr,ptr,ptr], sint),
	gsl_linalg_cholesky_svx([ptr,ptr], sint),
	gsl_linalg_cholesky_invert([ptr], sint),

	gsl_linalg_QR_decomp([ptr,ptr], sint),
	gsl_linalg_QR_solve([ptr,ptr,ptr,ptr], sint),
	gsl_linalg_QR_lssolve([ptr,ptr,ptr,ptr,ptr], sint),
	gsl_linalg_QR_unpack([ptr,ptr,ptr,ptr], sint),

	gsl_linalg_SV_decomp([ptr,ptr,ptr,ptr], sint),
	gsl_linalg_SV_solve([ptr,ptr,ptr,ptr,ptr], sint),

	% The leading sint of dgemv/dgemm is a CBLAS transpose flag: 111 none, 112 transpose.

	gsl_blas_ddot([ptr,ptr,-double], sint),
	gsl_blas_dnrm2([ptr], double),
	gsl_blas_dasum([ptr], double),
	gsl_blas_daxpy([double,ptr,ptr], sint),
	gsl_blas_dscal([double,ptr], void),
	gsl_blas_dgemv([sint,double,ptr,ptr,double,ptr], sint),
	gsl_blas_dgemm([sint,sint,double,ptr,ptr,double,ptr], sint),

	% The statistics take a raw array of doubles, its stride and its length, which vec_data_/4 gets from a vector.

	gsl_stats_mean([ptr,ulong,ulong], double),
	gsl_stats_variance([ptr,ulong,ulong], double),
	gsl_stats_variance_m([ptr,ulong,ulong,double], double),
	gsl_stats_sd([ptr,ulong,ulong], double),
	gsl_stats_sd_m([ptr,ulong,ulong,double], double),
	gsl_stats_absdev([ptr,ulong,ulong], double),
	gsl_stats_skew([ptr,ulong,ulong], double),
	gsl_stats_kurtosis([ptr,ulong,ulong], double),
	gsl_stats_max([ptr,ulong,ulong], double),
	gsl_stats_min([ptr,ulong,ulong], double),
	gsl_stats_median([ptr,ulong,ulong], double),
	gsl_stats_median_from_sorted_data([ptr,ulong,ulong], double),
	gsl_stats_quantile_from_sorted_data([ptr,ulong,ulong,double], double),
	gsl_stats_covariance([ptr,ulong,ptr,ulong,ulong], double),
	gsl_stats_correlation([ptr,ulong,ptr,ulong,ulong], double),

	gsl_sort_vector([ptr], void),

	gsl_fit_linear([ptr,ulong,ptr,ulong,ulong,-double,-double,-double,-double,-double,-double], sint),
	gsl_fit_wlinear([ptr,ulong,ptr,ulong,ptr,ulong,ulong,-double,-double,-double,-double,-double,-double], sint),
	gsl_fit_linear_est([double,double,double,double,double,double,-double,-double], sint),
	gsl_fit_mul([ptr,ulong,ptr,ulong,ulong,-double,-double,-double], sint),
	gsl_fit_mul_est([double,double,double,-double,-double], sint),

	gsl_multifit_linear_alloc([ulong,ulong], ptr),
	gsl_multifit_linear_free([ptr], void),
	gsl_multifit_linear([ptr,ptr,ptr,ptr,-double,ptr], sint),
	gsl_multifit_wlinear([ptr,ptr,ptr,ptr,ptr,-double,ptr], sint),
	gsl_multifit_linear_est([ptr,ptr,ptr,-double,-double], sint),

	% Cumulative distributions and their inverses. A double argument must be a float, so write 1.0 and not 1.

	gsl_cdf_ugaussian_P([double], double),
	gsl_cdf_ugaussian_Q([double], double),
	gsl_cdf_ugaussian_Pinv([double], double),
	gsl_cdf_ugaussian_Qinv([double], double),
	gsl_cdf_gaussian_P([double,double], double),
	gsl_cdf_gaussian_Q([double,double], double),
	gsl_cdf_gaussian_Pinv([double,double], double),
	gsl_cdf_gaussian_Qinv([double,double], double),
	gsl_cdf_exponential_P([double,double], double),
	gsl_cdf_exponential_Q([double,double], double),
	gsl_cdf_exponential_Pinv([double,double], double),
	gsl_cdf_exponential_Qinv([double,double], double),
	gsl_cdf_chisq_P([double,double], double),
	gsl_cdf_chisq_Q([double,double], double),
	gsl_cdf_chisq_Pinv([double,double], double),
	gsl_cdf_chisq_Qinv([double,double], double),
	gsl_cdf_tdist_P([double,double], double),
	gsl_cdf_tdist_Q([double,double], double),
	gsl_cdf_tdist_Pinv([double,double], double),
	gsl_cdf_tdist_Qinv([double,double], double),
	gsl_cdf_fdist_P([double,double,double], double),
	gsl_cdf_fdist_Q([double,double,double], double),
	gsl_cdf_fdist_Pinv([double,double,double], double),
	gsl_cdf_fdist_Qinv([double,double,double], double),
	gsl_cdf_gamma_P([double,double,double], double),
	gsl_cdf_gamma_Q([double,double,double], double),
	gsl_cdf_gamma_Pinv([double,double,double], double),
	gsl_cdf_gamma_Qinv([double,double,double], double),
	gsl_cdf_beta_P([double,double,double], double),
	gsl_cdf_beta_Q([double,double,double], double),
	gsl_cdf_beta_Pinv([double,double,double], double),
	gsl_cdf_beta_Qinv([double,double,double], double),
	gsl_cdf_poisson_P([uint,double], double),
	gsl_cdf_poisson_Q([uint,double], double),
	gsl_cdf_binomial_P([uint,double,uint], double),
	gsl_cdf_binomial_Q([uint,double,uint], double),

	% A seeded generator, for a repeatable stream that random/1 can't give. rng_new/2 allocates one.

	gsl_rng_env_setup([], ptr),
	gsl_rng_alloc([ptr], ptr),
	gsl_rng_clone([ptr], ptr),
	gsl_rng_free([ptr], void),
	gsl_rng_set([ptr,ulong], void),
	gsl_rng_get([ptr], ulong),
	gsl_rng_max([ptr], ulong),
	gsl_rng_min([ptr], ulong),
	gsl_rng_name([ptr], ccstr),
	gsl_rng_uniform([ptr], double),
	gsl_rng_uniform_pos([ptr], double),
	gsl_rng_uniform_int([ptr,ulong], ulong),

	gsl_ran_ugaussian([ptr], double),
	gsl_ran_ugaussian_pdf([double], double),
	gsl_ran_gaussian([ptr,double], double),
	gsl_ran_gaussian_pdf([double,double], double),
	gsl_ran_exponential([ptr,double], double),
	gsl_ran_exponential_pdf([double,double], double),
	gsl_ran_flat([ptr,double,double], double),
	gsl_ran_flat_pdf([double,double,double], double),
	gsl_ran_gamma([ptr,double,double], double),
	gsl_ran_gamma_pdf([double,double,double], double),
	gsl_ran_beta([ptr,double,double], double),
	gsl_ran_beta_pdf([double,double,double], double),
	gsl_ran_chisq([ptr,double], double),
	gsl_ran_chisq_pdf([double,double], double),
	gsl_ran_tdist([ptr,double], double),
	gsl_ran_tdist_pdf([double,double], double),
	gsl_ran_poisson([ptr,double], uint),
	gsl_ran_poisson_pdf([uint,double], double),
	gsl_ran_binomial([ptr,double,uint], uint),
	gsl_ran_binomial_pdf([uint,double,uint], double),
	gsl_ran_bernoulli([ptr,double], uint),
	gsl_ran_bernoulli_pdf([uint,double], double)
	]).

gslConst(gslGSL_EIGEN_SORT_VAL_ASC, 0).
gslConst(gslGSL_EIGEN_SORT_VAL_DESC, 1).
gslConst(gslGSL_EIGEN_SORT_ABS_ASC, 2).
gslConst(gslGSL_EIGEN_SORT_ABS_DESC, 3).
gslConst(gslCblasNoTrans, 111).
gslConst(gslCblasTrans, 112).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% Create vector of random values

vec_random(V, Cols) :-
	gsl_vector_alloc(Cols, V),
	( cfor(0, Cols-1, J),
		random(Val),
		gsl_vector_set(V, J, Val),
		fail; true
	).

% Create matrix of random values

mat_random(M, Rows, Cols) :-
	gsl_matrix_alloc(Rows, Cols, M),
	( cfor(0, Rows-1, I),
		( cfor(0, Cols-1, J),
			random(Val),
			gsl_matrix_set(M, I, J, Val),
			fail; true
		),
		fail; true
	).

% Size of a square matrix, else a domain error from Ctx

square_size_(M, Size, Ctx) :-
	'$gsl_matrix_size'(M, Rows, Cols),
	(Rows =:= Cols -> true; throw(error(domain_error(matrix_not_square, (Rows * Cols)), Ctx))),
	(Rows > 0 -> true; throw(error(domain_error(matrix_empty, Rows), Ctx))),
	Size = Rows.

% LU-decompose a copy of M; Singular is true on an exactly zero pivot, which is also what makes GSL's LU_solve abort

lu_(M, Size, LU, P, Signum, Singular) :-
	gsl_matrix_alloc(Size, Size, LU),
	gsl_matrix_memcpy(LU, M, _),
	gsl_permutation_alloc(Size, P),
	gsl_linalg_LU_decomp(LU, P, Signum, _),
	Max is Size - 1,
	(between(0, Max, I), gsl_matrix_get(LU, I, I, D), D =:= 0.0 -> Singular = true ; Singular = false).

lu_nonsingular_(M, Size, LU, P, Ctx) :-
	lu_(M, Size, LU, P, _, Singular),
	(	Singular == true
	->	gsl_permutation_free(P),
		gsl_matrix_free(LU),
		throw(error(domain_error(matrix_singular, Size), Ctx))
	;	true
	).

% Calculate LU(P) determinant of square matrix

mat_lup_det(M0, Det0) :-
	square_size_(M0, Size, mat_lup_det/2),
	lu_(M0, Size, M, P, Signum, Singular),
	(Singular == true -> Det = 0.0 ; gsl_linalg_LU_det(M, Signum, Det)),
	gsl_permutation_free(P),
	gsl_matrix_free(M),
	Det0 is Det.			% checks for NAN

% Solve A x = B for a new vector X

mat_solve(A, B, X) :-
	square_size_(A, Size, mat_solve/3),
	'$gsl_vector_size'(B, BSize),
	(BSize =:= Size -> true; throw(error(domain_error(vector_size_mismatch, (Size, BSize)), mat_solve/3))),
	lu_nonsingular_(A, Size, LU, P, mat_solve/3),
	gsl_vector_alloc(Size, X),
	gsl_linalg_LU_solve(LU, P, B, X, _),
	gsl_permutation_free(P),
	gsl_matrix_free(LU).

% Inverse of a square matrix, as a new matrix

mat_inverse(A, Inv) :-
	square_size_(A, Size, mat_inverse/2),
	lu_nonsingular_(A, Size, LU, P, mat_inverse/2),
	gsl_matrix_alloc(Size, Size, Inv),
	gsl_linalg_LU_invert(LU, P, Inv, _),
	gsl_permutation_free(P),
	gsl_matrix_free(LU).

% Matrix product A B, as a new matrix

mat_mul(A, B, C) :-
	'$gsl_matrix_size'(A, R1, C1),
	'$gsl_matrix_size'(B, R2, C2),
	(C1 =:= R2 -> true; throw(error(domain_error(matrix_size_mismatch, (R1 * C1, R2 * C2)), mat_mul/3))),
	gsl_matrix_alloc(R1, C2, C),
	gslConst(gslCblasNoTrans, NoTrans),
	gsl_blas_dgemm(NoTrans, NoTrans, 1.0, A, B, 0.0, C, _).

% Calculate the eigenvalues/eigenvectors of a symmetric matrix, leaving M intact

mat_eigen(M0, Vals, Vecs) :-
	square_size_(M0, Size, mat_eigen/3),
	Max is Size - 1,
	(	between(0, Max, I), I1 is I + 1, between(I1, Max, J),
		gsl_matrix_get(M0, I, J, X), gsl_matrix_get(M0, J, I, Y), X =\= Y
	->	throw(error(domain_error(matrix_not_symmetric, (Size * Size)), mat_eigen/3))
	;	true
	),
	gsl_matrix_alloc(Size, Size, M),
	gsl_matrix_memcpy(M, M0, _),
	gsl_vector_calloc(Size, Eval),
	gsl_matrix_calloc(Size, Size, Evec),
	gsl_eigen_symmv_alloc(Size, W),
	gsl_eigen_symmv(M, Eval, Evec, W),
	gsl_eigen_symmv_free(W),
	gsl_matrix_free(M),
	gslConst(gslGSL_EIGEN_SORT_ABS_ASC,Val),
	gsl_eigen_symmv_sort(Eval, Evec, Val),
	Size1 is Size - 1,

	findall(
		Eval_i,
		(between(0, Size1, I),
			gsl_vector_get(Eval, I, Eval_i)
		),
		Vals
	),

	findall(
		L,
		(between(0, Size1, I),
			gsl_matrix_column(Evec, I, Evec_i),
			'$struct_to_pointer'(Evec_i, V),
			vec_to_list(V, L),
			'$free_struct_pointer'(V)
		),
		Vecs
	),

	gsl_vector_free(Eval),
	gsl_matrix_free(Evec).

% Data pointer, stride and length of a vector, which is what the statistics and fitting
% functions take. A row, column or subvector view has a stride of its own, reported here.

vec_data_(V, Ptr, Stride, Size) :-
	'$gsl_vector_size'(V, Size),
	gsl_vector_subvector(V, 0, Size, [_,_,Stride,Ptr,_,_]).

% Two vectors of the same length, else a domain error from Ctx

vec_pair_(V, W, P1, S1, P2, S2, Size, Ctx) :-
	vec_data_(V, P1, S1, Size),
	vec_data_(W, P2, S2, Size2),
	(Size =:= Size2 -> true; throw(error(domain_error(vector_size_mismatch, (Size, Size2)), Ctx))),
	(Size > 0 -> true; throw(error(domain_error(vector_empty, Size), Ctx))).

% A sorted copy of V, for the median and the quantiles

vec_sorted_(V, Copy, Ptr, Stride, Size) :-
	'$gsl_vector_size'(V, Size),
	gsl_vector_alloc(Size, Copy),
	gsl_vector_memcpy(Copy, V, _),
	gsl_sort_vector(Copy),
	vec_data_(Copy, Ptr, Stride, Size).

% Call a binding with GSL's error handler off, so a data-dependent failure comes
% back as a status instead of aborting the process

errors_off_(Goal, Status) :-
	setup_call_cleanup(
		gsl_set_error_handler_off(Old),
		call(Goal, Status),
		gsl_set_error_handler(Old, _)
	).

% Statistics of a vector or of a row, column or subvector view

vec_mean(V, X) :- vec_data_(V, P, S, N), gsl_stats_mean(P, S, N, X).
vec_variance(V, X) :- vec_data_(V, P, S, N), gsl_stats_variance(P, S, N, X).
vec_sd(V, X) :- vec_data_(V, P, S, N), gsl_stats_sd(P, S, N, X).
vec_absdev(V, X) :- vec_data_(V, P, S, N), gsl_stats_absdev(P, S, N, X).
vec_skew(V, X) :- vec_data_(V, P, S, N), gsl_stats_skew(P, S, N, X).
vec_kurtosis(V, X) :- vec_data_(V, P, S, N), gsl_stats_kurtosis(P, S, N, X).

vec_median(V, X) :-
	vec_sorted_(V, Copy, P, S, N),
	gsl_stats_median_from_sorted_data(P, S, N, X),
	gsl_vector_free(Copy).

% Eg. vec_quantile(V, 0.75, Q)

vec_quantile(V, F, X) :-
	F2 is float(F),
	(F2 >= 0.0, F2 =< 1.0 -> true; throw(error(domain_error(probability, F), vec_quantile/3))),
	vec_sorted_(V, Copy, P, S, N),
	gsl_stats_quantile_from_sorted_data(P, S, N, F2, X),
	gsl_vector_free(Copy).

vec_covariance(V, W, X) :-
	vec_pair_(V, W, P1, S1, P2, S2, N, vec_covariance/3),
	gsl_stats_covariance(P1, S1, P2, S2, N, X).

vec_correlation(V, W, X) :-
	vec_pair_(V, W, P1, S1, P2, S2, N, vec_correlation/3),
	gsl_stats_correlation(P1, S1, P2, S2, N, X).

% Least-squares fit of Y = C0 + C1*X over two vectors of the same length

vec_fit_linear(X, Y, C0, C1, Sumsq) :-
	vec_pair_(X, Y, XP, XS, YP, YS, N, vec_fit_linear/5),
	gsl_fit_linear(XP, XS, YP, YS, N, C0, C1, _, _, _, Sumsq, _).

% Least-squares fit of Y over the columns of design matrix X, which for a
% polynomial fit holds 1, x, x^2 ... C and Cov are new, Chisq the residual

mat_fit_linear(X, Y, C, Cov, Chisq) :-
	'$gsl_matrix_size'(X, Rows, Cols),
	'$gsl_vector_size'(Y, YSize),
	(Rows =:= YSize -> true; throw(error(domain_error(vector_size_mismatch, (Rows, YSize)), mat_fit_linear/5))),
	(Rows >= Cols -> true; throw(error(domain_error(matrix_underdetermined, (Rows * Cols)), mat_fit_linear/5))),
	gsl_vector_alloc(Cols, C),
	gsl_matrix_alloc(Cols, Cols, Cov),
	gsl_multifit_linear_alloc(Rows, Cols, W),
	gsl_multifit_linear(X, Y, C, Cov, Chisq, W, _),
	gsl_multifit_linear_free(W).

% Solve A x = B for a symmetric positive definite A, leaving A intact

mat_cholesky_solve(A, B, X) :-
	square_size_(A, Size, mat_cholesky_solve/3),
	'$gsl_vector_size'(B, BSize),
	(BSize =:= Size -> true; throw(error(domain_error(vector_size_mismatch, (Size, BSize)), mat_cholesky_solve/3))),
	gsl_matrix_alloc(Size, Size, L),
	gsl_matrix_memcpy(L, A, _),
	errors_off_(gsl_linalg_cholesky_decomp1(L), St),
	(	St =:= 0
	->	gsl_vector_alloc(Size, X),
		gsl_linalg_cholesky_solve(L, B, X, _),
		gsl_matrix_free(L)
	;	gsl_matrix_free(L),
		throw(error(domain_error(matrix_not_positive_definite, Size), mat_cholesky_solve/3))
	).

% Least-squares solution of an overdetermined A x = B, by QR, as a new vector

mat_lssolve(A, B, X) :-
	'$gsl_matrix_size'(A, Rows, Cols),
	'$gsl_vector_size'(B, BSize),
	(Rows =:= BSize -> true; throw(error(domain_error(vector_size_mismatch, (Rows, BSize)), mat_lssolve/3))),
	(Rows >= Cols -> true; throw(error(domain_error(matrix_underdetermined, (Rows * Cols)), mat_lssolve/3))),
	gsl_matrix_alloc(Rows, Cols, QR),
	gsl_matrix_memcpy(QR, A, _),
	gsl_vector_alloc(Cols, Tau),
	gsl_linalg_QR_decomp(QR, Tau, _),
	gsl_vector_alloc(Cols, X),
	gsl_vector_alloc(Rows, Residual),
	gsl_linalg_QR_lssolve(QR, Tau, B, X, Residual, _),
	gsl_vector_free(Residual),
	gsl_vector_free(Tau),
	gsl_matrix_free(QR).

% Singular value decomposition A = U S V^T, with U, S and V new and A intact

mat_svd(A, U, S, V) :-
	'$gsl_matrix_size'(A, Rows, Cols),
	(Rows >= Cols -> true; throw(error(domain_error(matrix_underdetermined, (Rows * Cols)), mat_svd/4))),
	gsl_matrix_alloc(Rows, Cols, U),
	gsl_matrix_memcpy(U, A, _),
	gsl_matrix_alloc(Cols, Cols, V),
	gsl_vector_alloc(Cols, S),
	gsl_vector_alloc(Cols, Work),
	gsl_linalg_SV_decomp(U, V, S, Work, _),
	gsl_vector_free(Work).

% A generator seeded for a repeatable stream. The type comes from GSL_RNG_TYPE
% in the environment, else it is mt19937

rng_new(Seed, R) :-
	(integer(Seed) -> true; throw(error(type_error(integer, Seed), rng_new/2))),
	(Seed >= 0 -> true; throw(error(domain_error(not_less_than_zero, Seed), rng_new/2))),
	gsl_rng_env_setup(T),
	gsl_rng_alloc(T, R),
	gsl_rng_set(R, Seed).

rng_free(R) :-
	gsl_rng_free(R).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% Eg. vec_from_list(V, [1,2,3])

vec_from_list(V, L) :-
	length(L, Size),
	Size > 0,
	gsl_vector_alloc(Size, V),
	vec_from_list_(V, 0, L).

vec_from_list_(_, _, []) :- !.
vec_from_list_(V, I, [H|T]) :-
	H2 is float(H),
	gsl_vector_set(V, I, H2),
	I2 is I + 1,
	vec_from_list_(V, I2, T).

vec_to_list(V, L) :-
	'$gsl_vector_size'(V, Size),
	vec_to_list_(V, Size, [], L).

vec_to_list_(_, 0, L, L) :- !.
vec_to_list_(V, Col, L0, L) :-
	NewCol is Col - 1,
	gsl_vector_get(V, NewCol, Val),
	vec_to_list_(V, NewCol, [Val|L0], L).

vec_write(V, S) :-
	'$gsl_vector_write'(V, S).

vec_read(V, S, Size1) :-
	'$gsl_vector_alloc'(S, Size1),
	gsl_vector_alloc(Size1, V),
	'$gsl_vector_read'(V, S).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% Eg. mat_from_list(M, [[1,2,3],[4,5,6],[7,8,9]])

mat_from_list(M, L) :-
	length(L, Rows),
	Rows > 0,
	L = [H|_],
	length(H, Cols),
	Cols > 0,
	gsl_matrix_calloc(Rows, Cols, M),
	new_row_(M, 0, L).

new_row_(_, _, []) :- !.
new_row_(M, Row, [H|T]) :-
	new_col_(M, Row, 0, H),
	Row2 is Row + 1,
	new_row_(M, Row2, T).

new_col_(_, _, _, []).
new_col_(M, Row, Col, [H|T]) :-
	H2 is float(H),
	gsl_matrix_set(M, Row, Col, H2),
	Col2 is Col + 1,
	new_col_(M, Row, Col2, T).

mat_to_list(M, L) :-
	'$gsl_matrix_size'(M, Size1, Size2),
	mat_to_list_row_(M, Size1, Size2, [], L).

mat_to_list_row_(_, 0, _, L, L) :- !.
mat_to_list_row_(M, Row, Size2, L0, L) :-
	NewRow is Row - 1,
	mat_to_list_col_(M, NewRow, Size2, [], NewL),
	mat_to_list_row_(M, NewRow, Size2, [NewL|L0], L).

mat_to_list_col_(_, _, 0, L, L) :- !.
mat_to_list_col_(M, Row, Col, L0, L) :-
	NewCol is Col - 1,
	gsl_matrix_get(M, Row, NewCol, Val),
	mat_to_list_col_(M, Row, NewCol, [Val|L0], L).

mat_write(M, S) :-
	'$gsl_matrix_write'(M, S).

mat_read(M, S, Size1, Size2) :-
	'$gsl_matrix_alloc'(S, Size1, Size2),
	gsl_matrix_alloc(Size1, Size2, M),
	'$gsl_matrix_read'(M, S).
