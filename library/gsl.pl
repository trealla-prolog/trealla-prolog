:- module(gsl, [

	gsl_set_error_handler_off/1,

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

	gsl_eigen_symmv_calloc/2,
	gsl_eigen_symmv_alloc/2,
	gsl_eigen_symmv/4,
	gsl_eigen_symmv_free/1,
	gsl_eigen_symmv_sort/3,

	gsl_linalg_LU_decomp/4,
	gsl_linalg_LU_solve/5,
	gsl_linalg_LU_det/3,

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
	mat_eigen/3
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
	gsl_matrix_swap_cols([ptr,ulong,ulong], sint),
	gsl_matrix_swap_rowcol([ptr,ulong,ulong], sint),
	gsl_matrix_swap_add([ptr,ptr], sint),
	gsl_matrix_swap_sub([ptr,ptr], sint),
	gsl_matrix_swap_mul_elements([ptr,ptr], sint),
	gsl_matrix_swap_div_elements([ptr,ptr], sint),
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

	gsl_eigen_symmv_calloc([ulong], ptr),
	gsl_eigen_symmv_alloc([ulong], ptr),
	gsl_eigen_symmv([ptr,ptr,ptr,ptr], void),
	gsl_eigen_symmv_free([ptr], void),
	gsl_eigen_symmv_sort([ptr,ptr,sint], void),

	gsl_linalg_LU_decomp([ptr,ptr,-sint], sint),
	gsl_linalg_LU_solve([ptr,ptr,ptr,ptr], sint),
	gsl_linalg_LU_det([ptr,sint], double)
	]).

gslConst(gslGSL_EIGEN_SORT_VAL_ASC, 0).
gslConst(gslGSL_EIGEN_SORT_VAL_DESC, 1).
gslConst(gslGSL_EIGEN_SORT_ABS_ASC, 2).
gslConst(gslGSL_EIGEN_SORT_ABS_DESC, 3).

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

check_error_(Goal, Check, Action) :-
	Goal, (Check; (Action, Goal =.. [Pred|_], throw(domain_error(Pred, Check)))),
	!.

% Calculate LU(P) determinant of square matrix

mat_lup_det(M0, Det0) :-
	'$gsl_matrix_size'(M0, Rows, Cols),
	(Rows =:= Cols -> true; throw(error(domain_error(matrix_not_square, (Rows * Cols)), mat_lup_det/2))),
	Size is Rows,
	(Size > 0 -> true; throw(error(domain_error(matrix_empty, Size), mat_lup_det/2))),
	gsl_matrix_alloc(Size, Size, M),
	gsl_matrix_memcpy(M, M0, _),
	gsl_permutation_alloc(Size, P),
	check_error_(
		gsl_linalg_LU_decomp(M, P, Signum, Status1),
		Status1 =:= 0,
		(gsl_permutation_free(P), gsl_matrix_free(M))
		),
	gsl_vector_alloc(Size, B),
	( cfor(0, Size-1, I),
		V is float(I+1),
		gsl_vector_set(B, I, V),
		fail; true
	),
	gsl_vector_alloc(Size, X),
	check_error_(
		gsl_linalg_LU_solve(M, P, B, X, Status2),
		Status2 =< 1,
		(gsl_vector_free(X), gsl_vector_free(B), gsl_permutation_free(P))
		),
	gsl_vector_free(X),
	gsl_vector_free(B),
	gsl_permutation_free(P),
	gsl_linalg_LU_det(M, Signum, Det),
	gsl_matrix_free(M),
	Det0 is Det.			% checks for NAN

% Calculate the eigenvalues/eigenvectors of a square matrix...

mat_eigen(M, Vals, Vecs) :-
	'$gsl_matrix_size'(M, Rows, Cols),
	(Rows =:= Cols -> true; throw(error(domain_error(matrix_not_square, (Rows * Cols)), mat_eigen/3))),
	Size is Rows,
	gsl_vector_calloc(Size, Eval),
	gsl_matrix_calloc(Size, Size, Evec),
	gsl_eigen_symmv_alloc(Size, W),
	gsl_eigen_symmv(M, Eval, Evec, W),
	gsl_eigen_symmv_free(W),
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
