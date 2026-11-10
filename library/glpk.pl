:- module(glpk, [
	lp_solve/3,
	lp_solve/4,
	lp_solve_file/3,

	glp_version/1,
	glp_term_out/2,
	glp_create_prob/1,
	glp_delete_prob/1,
	glp_set_obj_dir/2,
	glp_add_rows/3,
	glp_add_cols/3,
	glp_set_row_name/3,
	glp_set_col_name/3,
	glp_set_row_bnds/5,
	glp_set_col_bnds/5,
	glp_set_obj_coef/3,
	glp_load_matrix/5,
	glp_set_col_kind/3,
	glp_get_num_rows/2,
	glp_get_num_cols/2,
	glp_get_col_name/3,
	glp_get_col_kind/3,
	glp_simplex/3,
	glp_intopt/3,
	glp_get_status/2,
	glp_get_obj_val/2,
	glp_get_row_prim/3,
	glp_get_row_dual/3,
	glp_get_col_prim/3,
	glp_get_col_dual/3,
	glp_mip_status/2,
	glp_mip_obj_val/2,
	glp_mip_row_val/3,
	glp_mip_col_val/3,
	glp_read_lp/4,
	glp_write_lp/4,
	glp_read_mps/5,
	glp_write_mps/5
	]).

% Linear and mixed-integer programming with the GNU Linear Programming Kit (GLPK)
%
% UBUNTU: sudo apt install libglpk-dev
% MACOS: brew install glpk
%
% REF: https://www.gnu.org/software/glpk/
%
%	?- lp_solve(max(3*X + 2*Y), [X + Y =< 4, X + 3*Y =< 6, X =< 3, X >= 0, Y >= 0], V).
%	   X = 3.0, Y = 1.0, V = 11.0.
%
% GLPK ends the process on a malformed call, so lp_solve/3,4 check everything before passing it on.

:- use_module(library(lists)).

:- use_foreign_module('libglpk.so', [
	glp_version([], ccstr),
	glp_term_out([sint], sint),
	glp_create_prob([], ptr),
	glp_delete_prob([ptr], void),
	glp_set_obj_dir([ptr,sint], void),
	glp_add_rows([ptr,sint], sint),
	glp_add_cols([ptr,sint], sint),
	glp_set_row_name([ptr,sint,cstr], void),
	glp_set_col_name([ptr,sint,cstr], void),
	glp_set_row_bnds([ptr,sint,sint,double,double], void),
	glp_set_col_bnds([ptr,sint,sint,double,double], void),
	glp_set_obj_coef([ptr,sint,double], void),
	glp_load_matrix([ptr,sint,ptr,ptr,ptr], void),
	glp_set_col_kind([ptr,sint,sint], void),
	glp_get_num_rows([ptr], sint),
	glp_get_num_cols([ptr], sint),
	glp_get_col_name([ptr,sint], ccstr),
	glp_get_col_kind([ptr,sint], sint),
	glp_simplex([ptr,ptr], sint),
	glp_intopt([ptr,ptr], sint),
	glp_get_status([ptr], sint),
	glp_get_obj_val([ptr], double),
	glp_get_row_prim([ptr,sint], double),
	glp_get_row_dual([ptr,sint], double),
	glp_get_col_prim([ptr,sint], double),
	glp_get_col_dual([ptr,sint], double),
	glp_mip_status([ptr], sint),
	glp_mip_obj_val([ptr], double),
	glp_mip_row_val([ptr,sint], double),
	glp_mip_col_val([ptr,sint], double),
	glp_read_lp([ptr,ptr,cstr], sint),
	glp_write_lp([ptr,ptr,cstr], sint),
	glp_read_mps([ptr,sint,ptr,cstr], sint),
	glp_write_mps([ptr,sint,ptr,cstr], sint)
	]).

% lp_solve(+Objective, +Constraints, -Value)
% lp_solve(+Objective, +Constraints, -Value, +Options)
%
% Objective is max(Expr) or min(Expr), and Constraints a list of Expr =< Expr, Expr >= Expr,
% Expr =:= Expr (or Expr = Expr), integer(X) and binary(X), X a variable or list of them. An
% Expr is linear in its variables: numbers, evaluable constants, variables, +, -, scaling by *
% or / and lists, which are summed. On success every variable is bound to its value in an
% optimal solution, an integer for an integer one and a float otherwise, and Value to the
% objective's. Variables are free unless a constraint bounds them. An infeasible problem fails;
% an unbounded one raises error(glpk_error(unbounded), _). Options: write_lp(File) writes the
% problem in CPLEX LP format first, and messages(true) leaves GLPK's terminal output on.

lp_solve(Objective, Constraints, Value) :-
	lp_solve_(Objective, Constraints, Value, [], lp_solve/3).

lp_solve(Objective, Constraints, Value, Options) :-
	lp_solve_(Objective, Constraints, Value, Options, lp_solve/4).

lp_solve_(Objective, Constraints, Value, Options, Ctx) :-
	options_(Options, Ctx, WriteLp, Messages),
	term_variables(Objective-Constraints, Vars),
	copy_term_nat(Vars-Objective-Constraints, Vs-Obj-Cons),
	length(Vars, N),
	numlist_(1, N, Is),
	maplist(lp_var_, Vs, Is),
	objective_(Obj, Vars, Ctx, Dir, ObjTerms, ObjConst),
	constraints_(Cons, Vars, Ctx, Rows, Bounds, Kinds),
	columns_(1, N, Bounds, Kinds, Cols),
	(	N =:= 0
	->	Value is ObjConst + 0.0
	;	with_problem_(Messages, P, (
			build_(P, Dir, N, Cols, ObjTerms, ObjConst, Rows),
			(WriteLp == none -> true ; write_lp_(P, WriteLp, Ctx)),
			(memberchk(col(_, _, _, _, integer), Cols) -> Mip = true ; Mip = false),
			solve_(P, Mip, Ctx, Value0),
			findall(X, (member(col(J, _, _, _, Kind), Cols), col_value_(P, Mip, J, Kind, X)), Xs)
		)),
		Value is Value0 + 0.0,
		Vars = Xs
	).

lp_var_(V, I) :- V = '$lpvar'(I).

numlist_(L, H, []) :- L > H, !.
numlist_(L, H, [L|T]) :- L1 is L + 1, numlist_(L1, H, T).

options_(Options, Ctx, _, _) :-
	var(Options),
	!,
	throw(error(instantiation_error, Ctx)).
options_(Options, Ctx, WriteLp, Messages) :-
	(is_list(Options) -> true ; throw(error(type_error(list, Options), Ctx))),
	options_(Options, Ctx, none, WriteLp, false, Messages).

options_([], _, W, W, M, M).
options_([O|Os], Ctx, W0, W, M0, M) :-
	(	var(O)
	->	throw(error(instantiation_error, Ctx))
	;	O = write_lp(F), atom(F)
	->	options_(Os, Ctx, F, W, M0, M)
	;	O = messages(B), (B == true ; B == false)
	->	options_(Os, Ctx, W0, W, B, M)
	;	throw(error(domain_error(lp_option, O), Ctx))
	).

% A problem of GLPK's, deleted however Goal ends, with its terminal output off unless Messages

with_problem_(Messages, P, Goal) :-
	(Messages == true -> On = 1 ; On = 0),
	setup_call_cleanup(
		(glp_term_out(On, Old), glp_create_prob(P)),
		Goal,
		(glp_delete_prob(P), glp_term_out(Old, _))).

objective_(Obj, _, Ctx, _, _, _) :-
	var(Obj),
	!,
	throw(error(instantiation_error, Ctx)).
objective_(max(E), Vars, Ctx, 2, Terms, K) :- !, linear_(E, Vars, Ctx, Terms, K).
objective_(min(E), Vars, Ctx, 1, Terms, K) :- !, linear_(E, Vars, Ctx, Terms, K).
objective_(Obj, Vars, Ctx, _, _, _) :-
	unlp_(Obj, Vars, Culprit),
	throw(error(domain_error(lp_objective, Culprit), Ctx)).

% Each constraint as a row(Terms, Type, Lb, Ub), a bound J-lb(X), J-ub(X) or J-fx(X) on column J
% when only one variable is in it, or a kind J-integer or J-binary. One with no variables is
% checked here, and fails the solve if it doesn't hold.

constraints_(Cons, Vars, Ctx, _, _, _) :-
	\+ is_list(Cons),
	!,
	(	var(Cons)
	->	throw(error(instantiation_error, Ctx))
	;	unlp_(Cons, Vars, Culprit),
		throw(error(type_error(list, Culprit), Ctx))
	).
constraints_(Cons, Vars, Ctx, Rows, Bounds, Kinds) :-
	constraints_(Cons, Vars, Ctx, Rows, [], Bounds, [], Kinds, []).

constraints_([], _, _, R, R, B, B, K, K).
constraints_([C|Cs], Vars, Ctx, R0, R, B0, B, K0, K) :-
	constraint_(C, Vars, Ctx, R0, R1, B0, B1, K0, K1),
	constraints_(Cs, Vars, Ctx, R1, R, B1, B, K1, K).

constraint_(C, _, Ctx, _, _, _, _, _, _) :-
	var(C),
	!,
	throw(error(instantiation_error, Ctx)).
constraint_(integer(V), Vars, Ctx, R, R, B, B, K0, K) :-
	!,
	kinds_(V, integer, integer(V), Vars, Ctx, K0, K).
constraint_(binary(V), Vars, Ctx, R, R, B, B, K0, K) :-
	!,
	kinds_(V, binary, binary(V), Vars, Ctx, K0, K).
constraint_(C, Vars, Ctx, R0, R, B0, B, K, K) :-
	relation_(C, Op, L, Rt),
	!,
	linear_(L-Rt, Vars, Ctx, Terms, K0),
	Rhs is -K0,
	(	Terms == []
	->	holds_(Op, Rhs),
		R0 = R, B0 = B
	;	Terms = [J-A]
	->	X is Rhs / A,
		bound_(Op, A, J, X, Bound),
		R0 = R, B0 = [Bound|B]
	;	row_(Op, Rhs, Type, Lb, Ub),
		R0 = [row(Terms, Type, Lb, Ub)|R], B0 = B
	).
constraint_(C, Vars, Ctx, _, _, _, _, _, _) :-
	unlp_(C, Vars, Culprit),
	throw(error(domain_error(lp_constraint, Culprit), Ctx)).

relation_(L =< R, le, L, R).
relation_(L >= R, ge, L, R).
relation_(L =:= R, eq, L, R).
relation_(L = R, eq, L, R).

holds_(le, Rhs) :- 0 =< Rhs.
holds_(ge, Rhs) :- 0 >= Rhs.
holds_(eq, Rhs) :- 0 =:= Rhs.

bound_(eq, _, J, X, J-fx(X)).
bound_(le, A, J, X, B) :- (A > 0 -> B = J-ub(X) ; B = J-lb(X)).
bound_(ge, A, J, X, B) :- (A > 0 -> B = J-lb(X) ; B = J-ub(X)).

row_(le, Rhs, 3, 0.0, Rhs).		% GLP_UP
row_(ge, Rhs, 2, Rhs, 0.0).		% GLP_LO
row_(eq, Rhs, 5, Rhs, Rhs).		% GLP_FX

kinds_(V, Kind, C, Vars, Ctx, K0, K) :-
	(	V = '$lpvar'(J)
	->	K0 = [J-Kind|K]
	;	is_list(V), V \== []
	->	kinds_list_(V, Kind, C, Vars, Ctx, K0, K)
	;	unlp_(C, Vars, Culprit),
		throw(error(domain_error(lp_constraint, Culprit), Ctx))
	).

kinds_list_([], _, _, _, _, K, K).
kinds_list_([V|Vs], Kind, C, Vars, Ctx, K0, K) :-
	(	V = '$lpvar'(J)
	->	K0 = [J-Kind|K1],
		kinds_list_(Vs, Kind, C, Vars, Ctx, K1, K)
	;	unlp_(C, Vars, Culprit),
		throw(error(domain_error(lp_constraint, Culprit), Ctx))
	).

% col(J, Type, Lb, Ub, Kind) for columns L..H, from the bounds and kinds gathered for each

columns_(L, H, _, _, []) :- L > H, !.
columns_(L, H, Bounds, Kinds, Cols) :-
	keysort(Bounds, SB),
	keysort(Kinds, SK),
	columns_(L, H, SB, SK, Cols, []).

columns_(J, H, _, _, Cols, Cols) :- J > H, !.
columns_(J, H, B0, K0, [col(J, Type, Lb, Ub, Kind)|Cols], Tail) :-
	take_(B0, J, Bs, B1),
	take_(K0, J, Ks, K1),
	foldl_bounds_(Bs, none, Lb0, none, Ub0),
	(	memberchk(binary, Ks)
	->	Kind = integer,
		max_bound_(Lb0, 0, Lb1),
		min_bound_(Ub0, 1, Ub1)
	;	memberchk(integer, Ks)
	->	Kind = integer, Lb1 = Lb0, Ub1 = Ub0
	;	Kind = continuous, Lb1 = Lb0, Ub1 = Ub0
	),
	(	Kind == integer
	->	int_bound_(ceiling, Lb1, Lb2),
		int_bound_(floor, Ub1, Ub2)
	;	Lb2 = Lb1, Ub2 = Ub1
	),
	col_type_(Lb2, Ub2, Type, Lb, Ub),
	J1 is J + 1,
	columns_(J1, H, B1, K1, Cols, Tail).

take_([K-V|T0], K, [V|Vs], T) :- !, take_(T0, K, Vs, T).
take_(T, _, [], T).

foldl_bounds_([], Lb, Lb, Ub, Ub).
foldl_bounds_([B|Bs], Lb0, Lb, Ub0, Ub) :-
	(	B = lb(X) -> max_bound_(Lb0, X, Lb1), Ub1 = Ub0
	;	B = ub(X) -> min_bound_(Ub0, X, Ub1), Lb1 = Lb0
	;	B = fx(X), max_bound_(Lb0, X, Lb1), min_bound_(Ub0, X, Ub1)
	),
	foldl_bounds_(Bs, Lb1, Lb, Ub1, Ub).

max_bound_(none, X, X) :- !.
max_bound_(B, X, M) :- M is max(B, X).

min_bound_(none, X, X) :- !.
min_bound_(B, X, M) :- M is min(B, X).

% An integer column's bound rounded inward, as GLPK's MIP solver wants, a near-integer one to it

int_bound_(_, none, none) :- !.
int_bound_(ceiling, B, I) :- I is ceiling(B - 1.0e-9 * max(1, abs(B))).
int_bound_(floor, B, I) :- I is floor(B + 1.0e-9 * max(1, abs(B))).

% GLPK's bound type, and the bounds as floats; crossed bounds fail

col_type_(none, none, 1, 0.0, 0.0) :- !.		% GLP_FR
col_type_(Lb, none, 2, L, 0.0) :- !, L is float(Lb).	% GLP_LO
col_type_(none, Ub, 3, 0.0, U) :- !, U is float(Ub).	% GLP_UP
col_type_(Lb, Ub, Type, L, U) :-
	Lb =< Ub,
	L is float(Lb),
	U is float(Ub),
	(Lb < Ub -> Type = 4 ; Type = 5).		% GLP_DB, GLP_FX

build_(P, Dir, N, Cols, ObjTerms, ObjConst, Rows) :-
	glp_set_obj_dir(P, Dir),
	glp_add_cols(P, N, _),
	forall(member(col(J, Type, Lb, Ub, Kind), Cols),
		(	glp_set_col_bnds(P, J, Type, Lb, Ub),
			(Kind == integer -> glp_set_col_kind(P, J, 2) ; true)
		)),
	C0 is float(ObjConst),
	glp_set_obj_coef(P, 0, C0),
	forall(member(J-A, ObjTerms), (A1 is float(A), glp_set_obj_coef(P, J, A1))),
	length(Rows, M),
	(	M > 0
	->	glp_add_rows(P, M, _),
		rows_(Rows, 1, P, IA, JA, AR),
		length(IA, NE),
		load_matrix_(P, NE, IA, JA, AR)
	;	true
	).

rows_([], _, _, [], [], []).
rows_([row(Terms, Type, Lb, Ub)|Rows], I, P, IA, JA, AR) :-
	L is float(Lb),
	U is float(Ub),
	glp_set_row_bnds(P, I, Type, L, U),
	row_entries_(Terms, I, IA, IA1, JA, JA1, AR, AR1),
	I1 is I + 1,
	rows_(Rows, I1, P, IA1, JA1, AR1).

row_entries_([], _, IA, IA, JA, JA, AR, AR).
row_entries_([J-A|Ts], I, [I|IA0], IA, [J|JA0], JA, [A1|AR0], AR) :-
	A1 is float(A),
	row_entries_(Ts, I, IA0, IA, JA0, JA, AR0, AR).

% GLPK's arrays start at 1, so each gets an unused element first

load_matrix_(P, NE, IA, JA, AR) :-
	setup_call_cleanup(
		(	'$array_to_pointer'(sint, [0|IA], PI),
			'$array_to_pointer'(sint, [0|JA], PJ),
			'$array_to_pointer'(double, [0.0|AR], PA)
		),
		glp_load_matrix(P, NE, PI, PJ, PA),
		(	'$free_struct_pointer'(PI),
			'$free_struct_pointer'(PJ),
			'$free_struct_pointer'(PA)
		)).

write_lp_(P, File, Ctx) :-
	glp_write_lp(P, 0, File, Ret),
	(Ret =:= 0 -> true ; throw(error(permission_error(open, source_sink, File), Ctx))).

% The simplex method on the LP, then branch and cut on it for a MIP; an infeasible problem fails

solve_(P, Mip, Ctx, Value) :-
	glp_simplex(P, 0, Ret),
	solver_return_(Ret, Ctx),
	glp_get_status(P, Status),
	lp_status_(Status, Ctx),
	(	Mip == true
	->	glp_intopt(P, 0, Ret2),
		solver_return_(Ret2, Ctx),
		glp_mip_status(P, Status2),
		mip_status_(Status2, Ctx),
		glp_mip_obj_val(P, Value)
	;	glp_get_obj_val(P, Value)
	).

col_value_(P, Mip, J, Kind, X) :-
	(Mip == true -> glp_mip_col_val(P, J, X0) ; glp_get_col_prim(P, J, X0)),
	(Kind == integer -> X is round(X0) ; X is X0 + 0.0).

solver_return_(0, _) :- !.
solver_return_(Ret, _) :- (Ret =:= 10 ; Ret =:= 15), !, fail.	% GLP_ENOPFS, GLP_ENOFEAS
solver_return_(Ret, Ctx) :-
	(solver_error_(Ret, Reason) -> true ; Reason = Ret),
	throw(error(glpk_error(Reason), Ctx)).

solver_error_(1, invalid_basis).
solver_error_(2, singular_matrix).
solver_error_(3, ill_conditioned_matrix).
solver_error_(4, invalid_bounds).
solver_error_(5, solver_failed).
solver_error_(6, objective_lower_limit).
solver_error_(7, objective_upper_limit).
solver_error_(8, iteration_limit).
solver_error_(9, time_limit).
solver_error_(11, unbounded).
solver_error_(12, root_lp_not_optimal).
solver_error_(13, stopped).
solver_error_(14, mip_gap).
solver_error_(16, no_convergence).
solver_error_(17, numerical_instability).
solver_error_(18, invalid_data).
solver_error_(19, out_of_range).

lp_status_(5, _) :- !.							% GLP_OPT
lp_status_(S, _) :- (S =:= 3 ; S =:= 4), !, fail.			% GLP_INFEAS, GLP_NOFEAS
lp_status_(6, Ctx) :- !, throw(error(glpk_error(unbounded), Ctx)).	% GLP_UNBND
lp_status_(_, Ctx) :- throw(error(glpk_error(undefined), Ctx)).

mip_status_(5, _) :- !.
mip_status_(4, _) :- !, fail.
mip_status_(_, Ctx) :- throw(error(glpk_error(undefined), Ctx)).

% Linear form of E: Terms a list of column J-coefficient, one per column, none zero, and a
% constant K

linear_(E, Vars, Ctx, Terms, K) :-
	lin_(E, 1, Vars, Ctx, [], T, 0, K),
	keysort(T, Sorted),
	merge_terms_(Sorted, Terms).

lin_(E, S, _, _, T, T, K0, K) :-
	number(E),
	!,
	K is K0 + S * E.
lin_('$lpvar'(J), S, _, _, T, [J-S|T], K, K) :- !.
lin_(A+B, S, Vars, Ctx, T0, T, K0, K) :- !,
	lin_(A, S, Vars, Ctx, T0, T1, K0, K1),
	lin_(B, S, Vars, Ctx, T1, T, K1, K).
lin_(A-B, S, Vars, Ctx, T0, T, K0, K) :- !,
	lin_(A, S, Vars, Ctx, T0, T1, K0, K1),
	S1 is -S,
	lin_(B, S1, Vars, Ctx, T1, T, K1, K).
lin_(-A, S, Vars, Ctx, T0, T, K0, K) :- !,
	S1 is -S,
	lin_(A, S1, Vars, Ctx, T0, T, K0, K).
lin_(+A, S, Vars, Ctx, T0, T, K0, K) :- !,
	lin_(A, S, Vars, Ctx, T0, T, K0, K).
lin_(A*B, S, Vars, Ctx, T0, T, K0, K) :- !,
	linear_(A, Vars, Ctx, TA, KA),
	(	TA == []
	->	S1 is S * KA,
		lin_(B, S1, Vars, Ctx, T0, T, K0, K)
	;	linear_(B, Vars, Ctx, TB, KB),
		TB == []
	->	S1 is S * KB,
		lin_(A, S1, Vars, Ctx, T0, T, K0, K)
	;	unlp_(A*B, Vars, Culprit),
		throw(error(domain_error(linear_expression, Culprit), Ctx))
	).
lin_(A/B, S, Vars, Ctx, T0, T, K0, K) :- !,
	linear_(B, Vars, Ctx, TB, KB),
	(	TB \== []
	->	unlp_(A/B, Vars, Culprit),
		throw(error(domain_error(linear_expression, Culprit), Ctx))
	;	KB =:= 0
	->	throw(error(evaluation_error(zero_divisor), Ctx))
	;	S1 is S / KB,
		lin_(A, S1, Vars, Ctx, T0, T, K0, K)
	).
lin_([], _, _, _, T, T, K, K) :- !.
lin_([E|Es], S, Vars, Ctx, T0, T, K0, K) :- !,
	lin_(E, S, Vars, Ctx, T0, T1, K0, K1),
	lin_(Es, S, Vars, Ctx, T1, T, K1, K).
lin_(E, S, _, _, T, T, K0, K) :-
	catch(V is E, _, fail),
	!,
	K is K0 + S * V.
lin_(E, _, Vars, Ctx, _, _, _, _) :-
	unlp_(E, Vars, Culprit),
	throw(error(domain_error(linear_expression, Culprit), Ctx)).

merge_terms_([], []).
merge_terms_([J-A|T0], Terms) :-
	merge_same_(T0, J, A, Sum, T1),
	(Sum =:= 0 -> Terms = Terms1 ; Terms = [J-Sum|Terms1]),
	merge_terms_(T1, Terms1).

merge_same_([J-B|T0], J, A, Sum, T) :- !, A1 is A + B, merge_same_(T0, J, A1, Sum, T).
merge_same_(T, _, Sum, Sum, T).

% A term of the copy with its '$lpvar'(J) put back as the caller's variables, for an error

unlp_(T, Vars, U) :-
	(	var(T) -> U = T
	;	T = '$lpvar'(J), integer(J) -> nth1(J, Vars, U)
	;	compound(T) -> T =.. [F|As], maplist(unlp_with_(Vars), As, Us), U =.. [F|Us]
	;	U = T
	).

unlp_with_(Vars, T, U) :- unlp_(T, Vars, U).

% lp_solve_file(+File, -Value, -Solution)
%
% Solves the problem in File, in CPLEX LP format or, for a name ending .mps, free MPS, with
% Solution a list of Name = Value for its columns, as lp_solve/3 would give them.

lp_solve_file(File, Value, Solution) :-
	(	var(File)
	->	throw(error(instantiation_error, lp_solve_file/3))
	;	atom(File)
	->	true
	;	throw(error(type_error(atom, File), lp_solve_file/3))
	),
	(	exists_file(File)
	->	true
	;	throw(error(existence_error(source_sink, File), lp_solve_file/3))
	),
	with_problem_(false, P, (
		read_problem_(P, File, Ret),
		(Ret =:= 0 -> true ; throw(error(syntax_error(glpk_model), lp_solve_file/3))),
		glp_get_num_cols(P, N),
		numlist_(1, N, Js),
		findall(J, (member(J, Js), glp_get_col_kind(P, J, K), K =\= 1), Ints),
		(Ints == [] -> Mip = false ; Mip = true),
		(	N =:= 0
		->	Value0 = 0.0, Solution = []
		;	solve_(P, Mip, lp_solve_file/3, Value0),
			findall(Name = X,
				(	member(J, Js),
					(glp_get_col_name(P, J, Name) -> true ; atom_concat(x, J, Name)),
					(memberchk(J, Ints) -> Kind = integer ; Kind = continuous),
					col_value_(P, Mip, J, Kind, X)
				), Solution)
		)
	)),
	Value is Value0 + 0.0.

read_problem_(P, File, Ret) :-
	(	sub_atom(File, _, 4, 0, Ext), downcase_atom(Ext, '.mps')
	->	glp_read_mps(P, 2, 0, File, Ret)			% GLP_MPS_FILE
	;	glp_read_lp(P, 0, File, Ret)
	).
