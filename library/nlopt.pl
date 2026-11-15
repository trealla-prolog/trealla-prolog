:- module(nlopt, [
	nlopt_minimize/4,
	nlopt_minimize/5,
	nlopt_maximize/4,
	nlopt_maximize/5,
	nlopt_version/1
	]).

% Nonlinear optimization over NLopt (https://nlopt.readthedocs.io)
%
% MACOS: brew install nlopt
% UBUNTU: sudo apt install libnlopt-dev
%
%	?- nlopt_minimize([[X,Y],F]>>(F is (X-1)**2 + (Y+2)**2), [0.0, 0.0], Xs, V, []).
%	Xs = [0.99999..., -1.99999...], V = 4.8e-17.
%
% F is called as call(F, Xs, Y) for Y = f(Xs), Xs a list of floats as long as X0, the starting
% point. Options:
%
%	algorithm(A)		an NLopt algorithm by name in lower case, as ln_cobyla (the default),
%						ln_bobyqa, ln_neldermead, ln_sbplx, ld_lbfgs, ld_mma, ld_slsqp, gn_direct_l,
%						gn_crs2_lm, gn_isres, gn_esch or auglag; ld_ ones use a gradient, gn_
%						ones are global and need both bounds
%	lower(L), upper(U)	bounds, a list or one number for every coordinate
%	inequality(C)		a constraint C(Xs) =< 0, called as F is; any number of them
%	equality(C)			a constraint C(Xs) = 0
%	gradient(G)			call(G, Xs, Gs) for the gradient of F, by central differences if
%						not given; constraints' gradients are always by differences
%	local(A)			the local algorithm under auglag or g_mlsl
%	xtol_rel(T), ftol_rel(T), stopval(V), maxeval(N), maxtime(Seconds)
%						when to stop: xtol_rel(1.0e-8) and maxeval(100000) by default
%	constraint_tol(T)	how far a constraint may miss, 1.0e-8 by default
%	seed(N)				the random seed, for the stochastic algorithms
%	status(-S)			why it stopped: success, stopval_reached, ftol_reached,
%						xtol_reached, maxeval_reached or maxtime_reached
%
% An error or failure in a closure is raised once NLopt returns. NLopt's own failures are
% error(nlopt_error(Code, Message), _).

:- use_module(library(lists)).

:- use_foreign_module('libnlopt.so', [
	nlopt_version([-sint, -sint, -sint], void),
	nlopt_algorithm_from_string([cstr], sint),
	nlopt_srand([ulong], void),
	nlopt_create([sint, uint], ptr),
	nlopt_destroy([ptr], void),
	nlopt_optimize([ptr, ptr, -double], sint),
	nlopt_set_min_objective([ptr, ptr, ptr], sint),
	nlopt_set_max_objective([ptr, ptr, ptr], sint),
	nlopt_get_errmsg([ptr], ccstr),
	nlopt_set_lower_bounds([ptr, ptr], sint),
	nlopt_set_upper_bounds([ptr, ptr], sint),
	nlopt_add_inequality_constraint([ptr, ptr, ptr, double], sint),
	nlopt_add_equality_constraint([ptr, ptr, ptr, double], sint),
	nlopt_set_stopval([ptr, double], sint),
	nlopt_set_ftol_rel([ptr, double], sint),
	nlopt_set_xtol_rel([ptr, double], sint),
	nlopt_set_maxeval([ptr, sint], sint),
	nlopt_set_maxtime([ptr, double], sint),
	nlopt_set_local_optimizer([ptr, ptr], sint)
	]).

:- meta_predicate(nlopt_minimize(2, +, -, -)).
:- meta_predicate(nlopt_minimize(2, +, -, -, +)).
:- meta_predicate(nlopt_maximize(2, +, -, -)).
:- meta_predicate(nlopt_maximize(2, +, -, -, +)).

nlopt_version(Major-Minor-Bugfix) :-
	nlopt_version(Major, Minor, Bugfix).

nlopt_minimize(F, X0, X, V) :-
	optimize_(min, F, X0, X, V, [], nlopt_minimize/4).

nlopt_minimize(F, X0, X, V, Options) :-
	optimize_(min, F, X0, X, V, Options, nlopt_minimize/5).

nlopt_maximize(F, X0, X, V) :-
	optimize_(max, F, X0, X, V, [], nlopt_maximize/4).

nlopt_maximize(F, X0, X, V, Options) :-
	optimize_(max, F, X0, X, V, Options, nlopt_maximize/5).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

optimize_(Sense, F, X0, X, V, Options, Ctx) :-
	must_be_(list, X0, Ctx),
	must_be_(list, Options, Ctx),
	(	X0 == [] -> throw(error(domain_error(non_empty_list, X0), Ctx)) ; true ),
	maplist(to_float_(Ctx), X0, Fs0),
	length(Fs0, N),
	(	F = M:_ -> true ; M = user ),
	option_(algorithm(A), Options, ln_cobyla),
	algorithm_(A, Alg, Ctx),
	(	memberchk(gradient(G0), Options) -> qualify_(M, G0, G) ; G = numeric ),
	nlopt_create(Alg, N, Opt),
	(	Opt =:= 0 -> throw(error(domain_error(nlopt_algorithm, A), Ctx)) ; true ),
	setup_call_cleanup(true,
		run_(Opt, Sense, F, G, M, Fs0, N, Options, X, V, Ctx),
		nlopt_destroy(Opt)).

run_(Opt, Sense, F, G, M, Fs0, N, Options, X, V, Ctx) :-
	setup_call_cleanup(
		callbacks_(F, G, M, Options, Cbs, Cons),
		(	objective_(Sense, Opt, Cbs, Ctx),
			option_(constraint_tol(Tol0), Options, 1.0e-8),
			to_float_(Ctx, Tol0, Tol),
			constraints_(Cons, Opt, Tol, Ctx),
			bounds_(Options, Opt, N, Ctx),
			stopping_(Options, Opt, Ctx),
			local_(Options, Opt, N, Ctx),
			(	memberchk(seed(S), Options) -> must_be_(integer, S, Ctx), nlopt_srand(S) ; true ),
			setup_call_cleanup(
				'$array_to_pointer'(double, Fs0, XP),
				(	nlopt_optimize(Opt, XP, V, Rc),
					'$pointer_to_array'(double, XP, N, X)
				),
				'$free_struct_pointer'(XP)),
			result_(Rc, Opt, Status, Ctx),
			(	memberchk(status(St), Options) -> St = Status ; true )
		),
		(	foreign_callback_free(Cbs),
			forall(member(_-Cb, Cons), foreign_callback_free(Cb))
		)).

% A callback each for the objective and every constraint, as nlopt_func

callbacks_(F, G, M, Options, Cb, Cons) :-
	foreign_callback(nlopt:objective_(F, G), [uint, ptr, ptr, ptr], double, Cb),
	findall(K-C, (member(O, Options), constraint_option_(O, K, C0), qualify_(M, C0, C)), KCs),
	findall(K-Cb1, (member(K-C, KCs), foreign_callback(nlopt:objective_(C, numeric), [uint, ptr, ptr, ptr], double, Cb1)), Cons).

constraint_option_(inequality(C), inequality, C).
constraint_option_(equality(C), equality, C).

qualify_(_, C, C) :- nonvar(C), C = _:_, !.
qualify_(M, C, M:C).

% NLopt's nlopt_func: f(n, x, gradient, data), the gradient, when not NULL, filled in place

objective_(F, G, N, XP, GP, _, Y) :-
	'$pointer_to_array'(double, XP, N, Xs),
	value_(F, Xs, Y),
	(	GP =:= 0 -> true
	;	gradient_(G, F, Xs, Y, Gs),
		'$array_into_pointer'(double, Gs, GP)
	).

value_(F, Xs, Y) :-
	call(F, Xs, Y0),
	Y is float(Y0).

gradient_(numeric, F, Xs, _, Gs) :- !,
	length(Xs, N),
	N1 is N - 1,
	findall(D, (between(0, N1, I), central_(F, Xs, I, D)), Gs).
gradient_(G, _, Xs, _, Gs) :-
	call(G, Xs, Gs0),
	maplist(to_float_(gradient), Gs0, Gs).

central_(F, Xs, I, D) :-
	nth0(I, Xs, Xi),
	H is 1.0e-6 * max(1.0, abs(Xi)),
	Up is Xi + H,
	Down is Xi - H,
	replace_(Xs, I, Up, XsUp),
	replace_(Xs, I, Down, XsDown),
	value_(F, XsUp, YUp),
	value_(F, XsDown, YDown),
	D is (YUp - YDown) / (2 * H).

replace_([_|Xs], 0, Y, [Y|Xs]) :- !.
replace_([X|Xs], I, Y, [X|Ys]) :- I1 is I - 1, replace_(Xs, I1, Y, Ys).

objective_(min, Opt, Cb, Ctx) :- nlopt_set_min_objective(Opt, Cb, 0, Rc), rc_(Rc, Opt, Ctx).
objective_(max, Opt, Cb, Ctx) :- nlopt_set_max_objective(Opt, Cb, 0, Rc), rc_(Rc, Opt, Ctx).

constraints_([], _, _, _).
constraints_([K-Cb|Cs], Opt, Tol, Ctx) :-
	(	K == inequality -> nlopt_add_inequality_constraint(Opt, Cb, 0, Tol, Rc)
	;	nlopt_add_equality_constraint(Opt, Cb, 0, Tol, Rc)
	),
	rc_(Rc, Opt, Ctx),
	constraints_(Cs, Opt, Tol, Ctx).

bounds_(Options, Opt, N, Ctx) :-
	(	memberchk(lower(L), Options) -> bound_(L, N, Opt, nlopt_set_lower_bounds, Ctx) ; true ),
	(	memberchk(upper(U), Options) -> bound_(U, N, Opt, nlopt_set_upper_bounds, Ctx) ; true ).

bound_(B, N, Opt, Set, Ctx) :-
	(	number(B) -> length(Bs0, N), maplist(=(B), Bs0)
	;	is_list(B), length(B, N) -> Bs0 = B
	;	throw(error(domain_error(bounds_for_each_coordinate, B), Ctx))
	),
	maplist(to_float_(Ctx), Bs0, Bs),
	setup_call_cleanup('$array_to_pointer'(double, Bs, P), call(Set, Opt, P, Rc), '$free_struct_pointer'(P)),
	rc_(Rc, Opt, Ctx).

stopping_(Options, Opt, Ctx) :-
	option_(xtol_rel(XT), Options, 1.0e-8),
	set_float_(nlopt_set_xtol_rel, Opt, XT, Ctx),
	(	memberchk(ftol_rel(FT), Options) -> set_float_(nlopt_set_ftol_rel, Opt, FT, Ctx) ; true ),
	(	memberchk(stopval(SV), Options) -> set_float_(nlopt_set_stopval, Opt, SV, Ctx) ; true ),
	(	memberchk(maxtime(MT), Options) -> set_float_(nlopt_set_maxtime, Opt, MT, Ctx) ; true ),
	option_(maxeval(ME), Options, 100000),
	must_be_(integer, ME, Ctx),
	nlopt_set_maxeval(Opt, ME, Rc),
	rc_(Rc, Opt, Ctx).

set_float_(Set, Opt, V0, Ctx) :-
	to_float_(Ctx, V0, V),
	call(Set, Opt, V, Rc),
	rc_(Rc, Opt, Ctx).

% A local optimizer, copied by NLopt, so this one goes at once

local_(Options, Opt, N, Ctx) :-
	(	memberchk(local(A), Options)
	->	algorithm_(A, Alg, Ctx),
		nlopt_create(Alg, N, Local),
		(	Local =:= 0 -> throw(error(domain_error(nlopt_algorithm, A), Ctx)) ; true ),
		setup_call_cleanup(true,
			(	option_(xtol_rel(XT), Options, 1.0e-8),
				set_float_(nlopt_set_xtol_rel, Local, XT, Ctx),
				nlopt_set_local_optimizer(Opt, Local, Rc),
				rc_(Rc, Opt, Ctx)
			),
			nlopt_destroy(Local))
	;	true
	).

algorithm_(A, _, Ctx) :- var(A), !, throw(error(instantiation_error, Ctx)).
algorithm_(A, Alg, Ctx) :-
	(	atom(A) -> true ; throw(error(type_error(atom, A), Ctx)) ),
	upcase_atom(A, U),
	nlopt_algorithm_from_string(U, Alg),
	(	Alg >= 0 -> true ; throw(error(domain_error(nlopt_algorithm, A), Ctx)) ).

result_(Rc, Opt, Status, Ctx) :-
	(	status_(Rc, Status) -> true
	;	rc_(Rc, Opt, Ctx)
	).

status_(1, success).
status_(2, stopval_reached).
status_(3, ftol_reached).
status_(4, xtol_reached).
status_(5, maxeval_reached).
status_(6, maxtime_reached).

rc_(Rc, _, _) :- Rc > 0, !.
rc_(Rc, Opt, Ctx) :-
	(	nlopt_get_errmsg(Opt, Msg) -> true
	;	code_message_(Rc, Msg) -> true
	;	Msg = failure
	),
	throw(error(nlopt_error(Rc, Msg), Ctx)).

code_message_(-1, failure).
code_message_(-2, invalid_args).
code_message_(-3, out_of_memory).
code_message_(-4, roundoff_limited).
code_message_(-5, forced_stop).

option_(O, Os, _) :- memberchk(O, Os), !.
option_(O, _, Default) :- arg(1, O, Default).

to_float_(Ctx, X, _) :- var(X), !, throw(error(instantiation_error, Ctx)).
to_float_(_, X, F) :- number(X), !, F is float(X).
to_float_(Ctx, X, _) :- throw(error(type_error(number, X), Ctx)).

must_be_(_, X, Ctx) :- var(X), !, throw(error(instantiation_error, Ctx)).
must_be_(integer, X, Ctx) :- !, (integer(X) -> true ; throw(error(type_error(integer, X), Ctx))).
must_be_(list, X, Ctx) :- (is_list(X) -> true ; throw(error(type_error(list, X), Ctx))).
