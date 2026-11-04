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

	gsl_sf_gamma/2,
	gsl_sf_lngamma/2,
	gsl_sf_gammainv/2,
	gsl_sf_fact/2,
	gsl_sf_lnfact/2,
	gsl_sf_doublefact/2,
	gsl_sf_choose/3,
	gsl_sf_lnchoose/3,
	gsl_sf_beta/3,
	gsl_sf_lnbeta/3,
	gsl_sf_psi/2,
	gsl_sf_psi_1/2,
	gsl_sf_gamma_inc/3,
	gsl_sf_gamma_inc_P/3,
	gsl_sf_gamma_inc_Q/3,
	gsl_sf_beta_inc/4,
	gsl_sf_bessel_J0/2,
	gsl_sf_bessel_J1/2,
	gsl_sf_bessel_Jn/3,
	gsl_sf_bessel_Y0/2,
	gsl_sf_bessel_Y1/2,
	gsl_sf_bessel_Yn/3,
	gsl_sf_bessel_I0/2,
	gsl_sf_bessel_I1/2,
	gsl_sf_bessel_In/3,
	gsl_sf_bessel_K0/2,
	gsl_sf_bessel_K1/2,
	gsl_sf_bessel_Kn/3,
	gsl_sf_zeta/2,
	gsl_sf_zetam1/2,
	gsl_sf_hzeta/3,
	gsl_sf_eta/2,
	gsl_sf_lambert_W0/2,
	gsl_sf_lambert_Wm1/2,
	gsl_sf_dilog/2,
	gsl_sf_clausen/2,
	gsl_sf_Si/2,
	gsl_sf_Ci/2,
	gsl_sf_expint_E1/2,
	gsl_sf_expint_Ei/2,
	gsl_sf_expm1/2,
	gsl_sf_log_1plusx/2,
	gsl_sf_sinc/2,
	gsl_sf_debye_1/2,
	gsl_sf_hypot/3,
	gsl_sf_legendre_P1/2,
	gsl_sf_legendre_P2/2,
	gsl_sf_legendre_P3/2,
	gsl_sf_legendre_Pl/3,
	gsl_sf_hyperg_2F1/5,
	gsl_sf_airy_Ai/3,
	gsl_sf_airy_Bi/3,
	gsl_sf_ellint_Kcomp/3,
	gsl_sf_ellint_Ecomp/3,

	gsl_poly_eval/4,
	gsl_poly_dd_init/5,
	gsl_poly_dd_eval/5,
	gsl_poly_solve_quadratic/6,
	gsl_poly_solve_cubic/7,
	gsl_poly_complex_workspace_alloc/2,
	gsl_poly_complex_workspace_free/1,
	gsl_poly_complex_solve/5,

	gsl_sort_vector_index/3,
	gsl_sort_vector_smallest/4,
	gsl_sort_vector_largest/4,
	gsl_permute_vector/3,
	gsl_permute_vector_inverse/3,
	gsl_permutation_get/3,
	gsl_permutation_size/2,
	gsl_permutation_swap/4,
	gsl_permutation_reverse/1,
	gsl_permutation_inverse/3,
	gsl_permutation_valid/2,
	gsl_permutation_next/2,
	gsl_permutation_prev/2,
	gsl_permutation_memcpy/3,
	gsl_permutation_mul/4,

	gsl_fft_real_radix2_transform/4,
	gsl_fft_halfcomplex_radix2_inverse/4,
	gsl_fft_halfcomplex_radix2_backward/4,
	gsl_fft_complex_radix2_forward/4,
	gsl_fft_complex_radix2_inverse/4,
	gsl_fft_complex_radix2_backward/4,

	gsl_histogram_alloc/2,
	gsl_histogram_calloc/2,
	gsl_histogram_free/1,
	gsl_histogram_clone/2,
	gsl_histogram_memcpy/3,
	gsl_histogram_set_ranges_uniform/4,
	gsl_histogram_set_ranges/4,
	gsl_histogram_increment/3,
	gsl_histogram_accumulate/4,
	gsl_histogram_get/3,
	gsl_histogram_get_range/5,
	gsl_histogram_max/2,
	gsl_histogram_min/2,
	gsl_histogram_bins/2,
	gsl_histogram_reset/1,
	gsl_histogram_find/4,
	gsl_histogram_max_val/2,
	gsl_histogram_max_bin/2,
	gsl_histogram_min_val/2,
	gsl_histogram_min_bin/2,
	gsl_histogram_mean/2,
	gsl_histogram_sigma/2,
	gsl_histogram_sum/2,
	gsl_histogram_equal_bins_p/3,
	gsl_histogram_scale/3,
	gsl_histogram_shift/3,
	gsl_histogram_add/3,
	gsl_histogram_sub/3,
	gsl_histogram_mul/3,
	gsl_histogram_div/3,

	gsl_complex_abs/2,
	gsl_complex_arg/2,

	gsl_vector_complex_alloc/2,
	gsl_vector_complex_calloc/2,
	gsl_vector_complex_free/1,
	gsl_vector_complex_get/3,
	gsl_vector_complex_set/3,
	gsl_vector_complex_set_zero/1,

	gsl_matrix_complex_alloc/3,
	gsl_matrix_complex_calloc/3,
	gsl_matrix_complex_free/1,
	gsl_matrix_complex_get/4,
	gsl_matrix_complex_set/4,
	gsl_matrix_complex_column/3,
	gsl_matrix_complex_row/3,

	gsl_eigen_nonsymmv_alloc/2,
	gsl_eigen_nonsymmv/5,
	gsl_eigen_nonsymmv_free/1,
	gsl_eigen_nonsymmv_sort/4,
	gsl_eigen_nonsymmv_params/2,

	gsl_interp_alloc/3,
	gsl_interp_init/5,
	gsl_interp_eval/6,
	gsl_interp_eval_deriv/6,
	gsl_interp_eval_deriv2/6,
	gsl_interp_eval_integ/7,
	gsl_interp_free/1,
	gsl_interp_name/2,
	gsl_interp_min_size/2,
	gsl_interp_type_min_size/2,
	gsl_interp_bsearch/5,
	gsl_interp_accel_alloc/1,
	gsl_interp_accel_free/1,
	gsl_interp_accel_reset/2,

	gsl_spline_alloc/3,
	gsl_spline_init/5,
	gsl_spline_eval/4,
	gsl_spline_eval_deriv/4,
	gsl_spline_eval_deriv2/4,
	gsl_spline_eval_integ/5,
	gsl_spline_free/1,
	gsl_spline_name/2,
	gsl_spline_min_size/2,

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
	rng_free/1,

	vec_complex_from_list/2,
	vec_complex_to_list/2,
	vec_sort_index/2,
	vec_fft/2,
	vec_ifft/2,
	mat_eigen_nonsymm/3,

	perm_to_list/2,

	poly_eval/3,
	poly_roots/2,

	hist_new/4,
	hist_fill/2,
	hist_to_list/2,

	spline_type/1,
	spline_new/4,
	spline_eval/3,
	spline_deriv/3,
	spline_integ/4,
	spline_free/1,

	gsl_errors_off/1
	]).

% GNU Scientific Library (GSL)
%
% UBUNTU: sudo apt install libgsl-dev
%
% REF: https://www.gnu.org/software/gsl/doc/html/index.html
%
% Contributions to add new definitions are most welcome!

:- use_module(library(lists)).

:- meta_predicate(gsl_errors_off(0)).

:- foreign_struct(gsl_vector_view, [ulong,ulong,ptr,ptr,sint]).
:- foreign_struct(gsl_matrix_view, [ulong,ulong,ulong,ptr,ptr,sint]).
:- foreign_struct(gsl_vector_complex_view, [ulong,ulong,ptr,ptr,sint]).
:- foreign_struct(gsl_complex, [double,double]).

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
	gsl_ran_bernoulli_pdf([uint,double], double),

	% Special functions that is/2 lacks. A domain or overflow error in one of these calls
	% GSL's error handler, which aborts: gsl_errors_off/1 turns that into a NaN.

	gsl_sf_gamma([double], double),
	gsl_sf_lngamma([double], double),
	gsl_sf_gammainv([double], double),
	gsl_sf_fact([uint], double),
	gsl_sf_lnfact([uint], double),
	gsl_sf_doublefact([uint], double),
	gsl_sf_choose([uint,uint], double),
	gsl_sf_lnchoose([uint,uint], double),
	gsl_sf_beta([double,double], double),
	gsl_sf_lnbeta([double,double], double),
	gsl_sf_psi([double], double),
	gsl_sf_psi_1([double], double),
	gsl_sf_gamma_inc([double,double], double),
	gsl_sf_gamma_inc_P([double,double], double),
	gsl_sf_gamma_inc_Q([double,double], double),
	gsl_sf_beta_inc([double,double,double], double),
	gsl_sf_bessel_J0([double], double),
	gsl_sf_bessel_J1([double], double),
	gsl_sf_bessel_Jn([sint,double], double),
	gsl_sf_bessel_Y0([double], double),
	gsl_sf_bessel_Y1([double], double),
	gsl_sf_bessel_Yn([sint,double], double),
	gsl_sf_bessel_I0([double], double),
	gsl_sf_bessel_I1([double], double),
	gsl_sf_bessel_In([sint,double], double),
	gsl_sf_bessel_K0([double], double),
	gsl_sf_bessel_K1([double], double),
	gsl_sf_bessel_Kn([sint,double], double),
	gsl_sf_zeta([double], double),
	gsl_sf_zetam1([double], double),
	gsl_sf_hzeta([double,double], double),
	gsl_sf_eta([double], double),
	gsl_sf_lambert_W0([double], double),
	gsl_sf_lambert_Wm1([double], double),
	gsl_sf_dilog([double], double),
	gsl_sf_clausen([double], double),
	gsl_sf_Si([double], double),
	gsl_sf_Ci([double], double),
	gsl_sf_expint_E1([double], double),
	gsl_sf_expint_Ei([double], double),
	gsl_sf_expm1([double], double),
	gsl_sf_log_1plusx([double], double),
	gsl_sf_sinc([double], double),
	gsl_sf_debye_1([double], double),
	gsl_sf_hypot([double,double], double),
	gsl_sf_legendre_P1([double], double),
	gsl_sf_legendre_P2([double], double),
	gsl_sf_legendre_P3([double], double),
	gsl_sf_legendre_Pl([sint,double], double),
	gsl_sf_hyperg_2F1([double,double,double,double], double),

	% The trailing uint of these four is a precision mode: 0 double, 1 single, 2 approximate.

	gsl_sf_airy_Ai([double,uint], double),
	gsl_sf_airy_Bi([double,uint], double),
	gsl_sf_ellint_Kcomp([double,uint], double),
	gsl_sf_ellint_Ecomp([double,uint], double),

	% Polynomials. gsl_poly_complex_solve/5 writes 2n doubles of root, so poly_roots/2 gives it a vector.

	gsl_poly_eval([ptr,sint,double], double),
	gsl_poly_dd_init([ptr,ptr,ptr,ulong], sint),
	gsl_poly_dd_eval([ptr,ptr,ulong,double], double),
	gsl_poly_solve_quadratic([double,double,double,-double,-double], sint),
	gsl_poly_solve_cubic([double,double,double,-double,-double,-double], sint),
	gsl_poly_complex_workspace_alloc([ulong], ptr),
	gsl_poly_complex_workspace_free([ptr], void),
	gsl_poly_complex_solve([ptr,ulong,ptr,ptr], sint),

	gsl_sort_vector_index([ptr,ptr], sint),
	gsl_sort_vector_smallest([ptr,ulong,ptr], sint),
	gsl_sort_vector_largest([ptr,ulong,ptr], sint),
	gsl_permute_vector([ptr,ptr], sint),
	gsl_permute_vector_inverse([ptr,ptr], sint),
	gsl_permutation_get([ptr,ulong], ulong),
	gsl_permutation_size([ptr], ulong),
	gsl_permutation_swap([ptr,ulong,ulong], sint),
	gsl_permutation_reverse([ptr], void),
	gsl_permutation_inverse([ptr,ptr], sint),
	gsl_permutation_valid([ptr], sint),
	gsl_permutation_next([ptr], sint),
	gsl_permutation_prev([ptr], sint),
	gsl_permutation_memcpy([ptr,ptr], sint),
	gsl_permutation_mul([ptr,ptr,ptr], sint),

	% The FFTs work in place on a raw array of doubles, and the radix2 ones need a power-of-two length.

	gsl_fft_real_radix2_transform([ptr,ulong,ulong], sint),
	gsl_fft_halfcomplex_radix2_inverse([ptr,ulong,ulong], sint),
	gsl_fft_halfcomplex_radix2_backward([ptr,ulong,ulong], sint),
	gsl_fft_complex_radix2_forward([ptr,ulong,ulong], sint),
	gsl_fft_complex_radix2_inverse([ptr,ulong,ulong], sint),
	gsl_fft_complex_radix2_backward([ptr,ulong,ulong], sint),

	gsl_histogram_alloc([ulong], ptr),
	gsl_histogram_calloc([ulong], ptr),
	gsl_histogram_free([ptr], void),
	gsl_histogram_clone([ptr], ptr),
	gsl_histogram_memcpy([ptr,ptr], sint),
	gsl_histogram_set_ranges_uniform([ptr,double,double], sint),
	gsl_histogram_set_ranges([ptr,ptr,ulong], sint),
	gsl_histogram_increment([ptr,double], sint),
	gsl_histogram_accumulate([ptr,double,double], sint),
	gsl_histogram_get([ptr,ulong], double),
	gsl_histogram_get_range([ptr,ulong,-double,-double], sint),
	gsl_histogram_max([ptr], double),
	gsl_histogram_min([ptr], double),
	gsl_histogram_bins([ptr], ulong),
	gsl_histogram_reset([ptr], void),
	gsl_histogram_find([ptr,double,-ulong], sint),
	gsl_histogram_max_val([ptr], double),
	gsl_histogram_max_bin([ptr], ulong),
	gsl_histogram_min_val([ptr], double),
	gsl_histogram_min_bin([ptr], ulong),
	gsl_histogram_mean([ptr], double),
	gsl_histogram_sigma([ptr], double),
	gsl_histogram_sum([ptr], double),
	gsl_histogram_equal_bins_p([ptr,ptr], sint),
	gsl_histogram_scale([ptr,double], sint),
	gsl_histogram_shift([ptr,double], sint),
	gsl_histogram_add([ptr,ptr], sint),
	gsl_histogram_sub([ptr,ptr], sint),
	gsl_histogram_mul([ptr,ptr], sint),
	gsl_histogram_div([ptr,ptr], sint),

	% A complex number passes and comes back as [gsl_complex,Real,Imaginary].

	gsl_complex_abs([gsl_complex], double),
	gsl_complex_arg([gsl_complex], double),

	gsl_vector_complex_alloc([ulong], ptr),
	gsl_vector_complex_calloc([ulong], ptr),
	gsl_vector_complex_free([ptr], void),
	gsl_vector_complex_get([ptr,ulong], gsl_complex),
	gsl_vector_complex_set([ptr,ulong,gsl_complex], void),
	gsl_vector_complex_set_zero([ptr], void),

	gsl_matrix_complex_alloc([ulong,ulong], ptr),
	gsl_matrix_complex_calloc([ulong,ulong], ptr),
	gsl_matrix_complex_free([ptr], void),
	gsl_matrix_complex_get([ptr,ulong,ulong], gsl_complex),
	gsl_matrix_complex_set([ptr,ulong,ulong,gsl_complex], void),
	gsl_matrix_complex_column([ptr,ulong], gsl_vector_complex_view),
	gsl_matrix_complex_row([ptr,ulong], gsl_vector_complex_view),

	% Eigenvalues of a matrix that is not symmetric, which gsl_eigen_symmv/4 can't do

	gsl_eigen_nonsymmv_alloc([ulong], ptr),
	gsl_eigen_nonsymmv([ptr,ptr,ptr,ptr], sint),
	gsl_eigen_nonsymmv_free([ptr], void),
	gsl_eigen_nonsymmv_sort([ptr,ptr,sint], sint),
	gsl_eigen_nonsymmv_params([sint,ptr], void),

	% Interpolation. The type comes from a global, which spline_new/4 reads; an accelerator is optional, so 0 will do.

	gsl_interp_alloc([ptr,ulong], ptr),
	gsl_interp_init([ptr,ptr,ptr,ulong], sint),
	gsl_interp_eval([ptr,ptr,ptr,double,ptr], double),
	gsl_interp_eval_deriv([ptr,ptr,ptr,double,ptr], double),
	gsl_interp_eval_deriv2([ptr,ptr,ptr,double,ptr], double),
	gsl_interp_eval_integ([ptr,ptr,ptr,double,double,ptr], double),
	gsl_interp_free([ptr], void),
	gsl_interp_name([ptr], ccstr),
	gsl_interp_min_size([ptr], uint),
	gsl_interp_type_min_size([ptr], uint),
	gsl_interp_bsearch([ptr,double,ulong,ulong], ulong),
	gsl_interp_accel_alloc([], ptr),
	gsl_interp_accel_free([ptr], void),
	gsl_interp_accel_reset([ptr], sint),

	gsl_spline_alloc([ptr,ulong], ptr),
	gsl_spline_init([ptr,ptr,ptr,ulong], sint),
	gsl_spline_eval([ptr,double,ptr], double),
	gsl_spline_eval_deriv([ptr,double,ptr], double),
	gsl_spline_eval_deriv2([ptr,double,ptr], double),
	gsl_spline_eval_integ([ptr,double,double,ptr], double),
	gsl_spline_free([ptr], void),
	gsl_spline_name([ptr], ccstr),
	gsl_spline_min_size([ptr], uint)
	]).

gslConst(gslGSL_EIGEN_SORT_VAL_ASC, 0).
gslConst(gslGSL_EIGEN_SORT_VAL_DESC, 1).
gslConst(gslGSL_EIGEN_SORT_ABS_ASC, 2).
gslConst(gslGSL_EIGEN_SORT_ABS_DESC, 3).
gslConst(gslCblasNoTrans, 111).
gslConst(gslCblasTrans, 112).
gslConst(gslGSL_PREC_DOUBLE, 0).
gslConst(gslGSL_PREC_SINGLE, 1).
gslConst(gslGSL_PREC_APPROX, 2).

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
	gsl_errors_off(call(Goal, Status)).

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

% A complex vector as a list of Real-Imaginary pairs, and back

vec_complex_to_list(V, L) :-
	'$gsl_vector_size'(V, Size),
	Max is Size - 1,
	findall(Re-Im, (between(0, Max, I), gsl_vector_complex_get(V, I, [_,Re,Im])), L).

vec_complex_from_list(V, L) :-
	length(L, Size),
	Size > 0,
	gsl_vector_complex_alloc(Size, V),
	vec_complex_from_list_(V, 0, L).

vec_complex_from_list_(_, _, []) :- !.
vec_complex_from_list_(V, I, [Re0-Im0|T]) :-
	Re is float(Re0),
	Im is float(Im0),
	gsl_vector_complex_set(V, I, [gsl_complex,Re,Im]),
	I2 is I + 1,
	vec_complex_from_list_(V, I2, T).

% Eigenvalues and eigenvectors of a matrix that needn't be symmetric, as Real-Imaginary
% pairs ordered by increasing magnitude, leaving M intact

mat_eigen_nonsymm(M0, Vals, Vecs) :-
	square_size_(M0, Size, mat_eigen_nonsymm/3),
	gsl_matrix_alloc(Size, Size, M),
	gsl_matrix_memcpy(M, M0, _),
	gsl_vector_complex_alloc(Size, Eval),
	gsl_matrix_complex_alloc(Size, Size, Evec),
	gsl_eigen_nonsymmv_alloc(Size, W),
	errors_off_(gsl_eigen_nonsymmv(M, Eval, Evec, W), St),
	gsl_eigen_nonsymmv_free(W),
	gsl_matrix_free(M),
	(	St =:= 0
	->	true
	;	gsl_vector_complex_free(Eval),
		gsl_matrix_complex_free(Evec),
		throw(error(domain_error(matrix_no_convergence, Size), mat_eigen_nonsymm/3))
	),
	gslConst(gslGSL_EIGEN_SORT_ABS_ASC, Sort),
	gsl_eigen_nonsymmv_sort(Eval, Evec, Sort, _),
	vec_complex_to_list(Eval, Vals),
	Max is Size - 1,
	findall(Col,
		(	between(0, Max, J),
			findall(Re-Im, (between(0, Max, I), gsl_matrix_complex_get(Evec, I, J, [_,Re,Im])), Col)
		),
		Vecs),
	gsl_vector_complex_free(Eval),
	gsl_matrix_complex_free(Evec).

% The permutation that would sort V, as a list of indices into it

vec_sort_index(V, Indices) :-
	'$gsl_vector_size'(V, Size),
	gsl_permutation_alloc(Size, P),
	gsl_sort_vector_index(P, V, _),
	perm_to_list(P, Indices),
	gsl_permutation_free(P).

perm_to_list(P, L) :-
	gsl_permutation_size(P, Size),
	Max is Size - 1,
	findall(I, (between(0, Max, J), gsl_permutation_get(P, J, I)), L).

power_of_two_(N, Ctx) :-
	(N > 0, N /\ (N - 1) =:= 0 -> true; throw(error(domain_error(power_of_two, N), Ctx))).

% The spectrum of a real vector, as one Real-Imaginary pair per sample, with V intact.
% Its length has to be a power of two, which is what the radix2 transform takes.

vec_fft(V, Pairs) :-
	'$gsl_vector_size'(V, N),
	power_of_two_(N, vec_fft/2),
	gsl_vector_alloc(N, C),
	gsl_vector_memcpy(C, V, _),
	vec_data_(C, P, S, N),
	gsl_fft_real_radix2_transform(P, S, N, _),
	Max is N - 1,
	findall(Pair, (between(0, Max, K), halfcomplex_pair_(C, N, K, Pair)), Pairs),
	gsl_vector_free(C).

% In the radix2 halfcomplex layout the real parts run up from 0 and the imaginary
% parts back down from the end, and the upper half is the conjugate of the lower

halfcomplex_pair_(C, N, K, Re-Im) :-
	Half is N // 2,
	(	K =:= 0
	->	gsl_vector_get(C, 0, Re),
		Im = 0.0
	;	K =:= Half
	->	gsl_vector_get(C, Half, Re),
		Im = 0.0
	;	K < Half
	->	gsl_vector_get(C, K, Re),
		NK is N - K,
		gsl_vector_get(C, NK, Im)
	;	NK is N - K,
		gsl_vector_get(C, NK, Re),
		gsl_vector_get(C, K, Im0),
		Im is -Im0
	).

% The real vector whose spectrum is Pairs, as vec_fft/2 gives them

vec_ifft(Pairs, V) :-
	length(Pairs, N),
	power_of_two_(N, vec_ifft/2),
	gsl_vector_calloc(N, V),
	Half is N // 2,
	pack_halfcomplex_(Pairs, 0, Half, N, V),
	vec_data_(V, P, S, N),
	gsl_fft_halfcomplex_radix2_inverse(P, S, N, _).

pack_halfcomplex_([], _, _, _, _).
pack_halfcomplex_([Re0-Im0|T], K, Half, N, V) :-
	(	(K =:= 0 ; K =:= Half)
	->	Re is float(Re0),
		gsl_vector_set(V, K, Re)
	;	K < Half
	->	Re is float(Re0),
		Im is float(Im0),
		gsl_vector_set(V, K, Re),
		NK is N - K,
		gsl_vector_set(V, NK, Im)
	;	true			% the upper half repeats what the lower one said
	),
	K2 is K + 1,
	pack_halfcomplex_(T, K2, Half, N, V).

% Eg. poly_eval([1,2,3], 2.0, Y) for 1 + 2x + 3x^2, lowest degree first

poly_eval(Coeffs, X, Y) :-
	length(Coeffs, Len),
	(Len > 0 -> true; throw(error(domain_error(nonempty_list, Coeffs), poly_eval/3))),
	X2 is float(X),
	vec_from_list(C, Coeffs),
	vec_data_(C, P, _, _),
	gsl_poly_eval(P, Len, X2, Y),
	gsl_vector_free(C).

% The roots of a polynomial, as Real-Imaginary pairs

poly_roots(Coeffs, Roots) :-
	length(Coeffs, N),
	(N >= 2 -> true; throw(error(domain_error(polynomial_degree, N), poly_roots/2))),
	last(Coeffs, Lead),
	(Lead =\= 0 -> true; throw(error(domain_error(nonzero_leading_coefficient, Lead), poly_roots/2))),
	Doubles is 2 * (N - 1),
	vec_from_list(C, Coeffs),
	vec_data_(C, P, _, _),
	gsl_vector_calloc(Doubles, Z),
	vec_data_(Z, ZP, _, _),
	gsl_poly_complex_workspace_alloc(N, W),
	errors_off_(gsl_poly_complex_solve(P, N, W, ZP), St),
	gsl_poly_complex_workspace_free(W),
	gsl_vector_free(C),
	vec_to_list(Z, Flat),
	gsl_vector_free(Z),
	(	St =:= 0
	->	flat_pairs_(Flat, Roots)
	;	throw(error(domain_error(polynomial_no_convergence, N), poly_roots/2))
	).

flat_pairs_([], []).
flat_pairs_([Re,Im|T], [Re-Im|T2]) :-
	flat_pairs_(T, T2).

% A histogram of Bins equal bins spanning Min to Max

hist_new(Bins, Min, Max, H) :-
	(integer(Bins), Bins > 0 -> true; throw(error(domain_error(positive_integer, Bins), hist_new/4))),
	Min2 is float(Min),
	Max2 is float(Max),
	(Min2 < Max2 -> true; throw(error(domain_error(increasing_range, (Min, Max)), hist_new/4))),
	gsl_histogram_alloc(Bins, H),
	gsl_histogram_set_ranges_uniform(H, Min2, Max2, _).

% Count each value of a list, ignoring those outside the histogram's range

hist_fill(_, []) :- !.
hist_fill(H, [X|T]) :-
	X2 is float(X),
	gsl_histogram_increment(H, X2, _),
	hist_fill(H, T).

% The bins of a histogram, as bin(Lower, Upper, Count) terms

hist_to_list(H, Bins) :-
	gsl_histogram_bins(H, N),
	Max is N - 1,
	findall(bin(Lo, Hi, Count),
		(	between(0, Max, I),
			gsl_histogram_get_range(H, I, Lo, Hi, _),
			gsl_histogram_get(H, I, Count)
		),
		Bins).

% The interpolation types, for spline_new/4

spline_type(linear).
spline_type(polynomial).
spline_type(cspline).
spline_type(cspline_periodic).
spline_type(akima).
spline_type(akima_periodic).
spline_type(steffen).

% GSL hands out its interpolation types through global variables, so they take a
% dlopen of our own. The handle is cached, and dlopen hands back the same one anyway.

:- dynamic(gsl_lib_handle_/1).

gsl_lib_(H) :-
	gsl_lib_handle_(H),
	!.
gsl_lib_(H) :-
	'$dlopen'('libgsl.so', 0, H),
	assertz(gsl_lib_handle_(H)).

spline_type_ptr_(Type, T) :-
	(spline_type(Type) -> true; throw(error(domain_error(spline_type, Type), spline_new/4))),
	gsl_lib_(H),
	atom_concat(gsl_interp_, Type, Symbol),
	('$dlsym_deref'(H, Symbol, T) -> true; throw(error(existence_error(spline_type, Type), spline_new/4))).

% A spline of Type through the points Xs, Ys, which must ascend in Xs. It keeps a copy
% of the points, so nothing has to be held on to but the spline itself.

spline_new(Type, Xs, Ys, S) :-
	spline_type_ptr_(Type, T),
	length(Xs, N),
	length(Ys, NY),
	(N =:= NY -> true; throw(error(domain_error(vector_size_mismatch, (N, NY)), spline_new/4))),
	gsl_interp_type_min_size(T, Min),
	(N >= Min -> true; throw(error(domain_error(too_few_points(Min), N), spline_new/4))),
	(ascending_(Xs) -> true; throw(error(domain_error(ascending_x, Xs), spline_new/4))),
	vec_from_list(VX, Xs),
	vec_from_list(VY, Ys),
	vec_data_(VX, XP, _, _),
	vec_data_(VY, YP, _, _),
	gsl_spline_alloc(T, N, S),
	gsl_spline_init(S, XP, YP, N, _),
	gsl_vector_free(VX),
	gsl_vector_free(VY).

ascending_([]).
ascending_([_]) :- !.
ascending_([X,Y|T]) :-
	X < Y,
	ascending_([Y|T]).

% Eg. spline_eval(S, 2.5, Y), for an X within the points the spline was built from

spline_eval(S, X, Y) :-
	X2 is float(X),
	gsl_errors_off(gsl_spline_eval(S, X2, 0, Y0)),
	in_range_(Y0, X, spline_eval/3, Y).

spline_deriv(S, X, Y) :-
	X2 is float(X),
	gsl_errors_off(gsl_spline_eval_deriv(S, X2, 0, Y0)),
	in_range_(Y0, X, spline_deriv/3, Y).

spline_integ(S, A, B, Y) :-
	A2 is float(A),
	B2 is float(B),
	gsl_errors_off(gsl_spline_eval_integ(S, A2, B2, 0, Y0)),
	in_range_(Y0, (A, B), spline_integ/4, Y).

spline_free(S) :-
	gsl_spline_free(S).

% With the error handler off, an x outside the spline's range gives a NaN back

in_range_(Y0, X, Ctx, Y) :-
	(	catch(Y0 =:= Y0, _, fail)
	->	Y = Y0
	;	throw(error(domain_error(spline_range, X), Ctx))
	).

% Run Goal with GSL's error handler off, so that a domain or overflow error gives a NaN
% or a status back instead of aborting the process

gsl_errors_off(Goal) :-
	setup_call_cleanup(
		gsl_set_error_handler_off(Old),
		Goal,
		gsl_set_error_handler(Old, _)
	).

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
	Col > 0,
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
	Row > 0,
	NewRow is Row - 1,
	mat_to_list_col_(M, NewRow, Size2, [], NewL),
	mat_to_list_row_(M, NewRow, Size2, [NewL|L0], L).

mat_to_list_col_(_, _, 0, L, L) :- !.
mat_to_list_col_(M, Row, Col, L0, L) :-
	Col > 0,
	NewCol is Col - 1,
	gsl_matrix_get(M, Row, NewCol, Val),
	mat_to_list_col_(M, Row, NewCol, [Val|L0], L).

mat_write(M, S) :-
	'$gsl_matrix_write'(M, S).

mat_read(M, S, Size1, Size2) :-
	'$gsl_matrix_alloc'(S, Size1, Size2),
	gsl_matrix_alloc(Size1, Size2, M),
	'$gsl_matrix_read'(M, S).
