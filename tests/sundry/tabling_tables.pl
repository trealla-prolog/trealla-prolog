% library(tables), as SWI's: XSB's predicates for inspecting and
% abolishing tables. get_calls/3 and get_call/3 find tables, returning
% ret/N of the call's variables, moded arguments last; get_returns/2 and
% its _and_tvs/3 and _and_dls/3 forms read answers, get_residual/2 the
% delay lists of undefined ones, and the abolish predicates drop tables
% by call, predicate or module. Results match SWI but for two lines:
% abolish_table_subgoals/1 drops a moded table, which SWI cannot find,
% and the culprit in abolish_table_pred(3)'s error is 3, not user:3.

:- initialization(main).

:- use_module(library(tabling)).
:- use_module(library(tables)).
:- use_module(library(lists)).

:- table p/1, q/1, w/1, r/0, s/1.
p(X) :- between(1, 3, X).
q(X) :- member(X, [a, b]).
w(X) :- tnot(w(X)), X = a.
r :- w(a).
s(X) :- member(X, [1, 2]), tnot(w(a)).
:- table sp(_, _, min).
sp(a, b, 3). sp(a, b, 1). sp(a, c, 2).

show(Tag, T) :- \+ \+ (numbervars(T, 0, _), print(Tag-T), nl).
ntables(N) :- findall(V, current_table(V, _), Vs), length(Vs, N).
try(Tag, G) :- catch((G -> show(Tag, yes) ; show(Tag, no)), error(E, _), show(Tag, E)).

main :-
	findall(X, p(X), _), findall(X, q(X), _), q(a), findall(X-C, sp(a, X, C), _), (w(a) -> true ; true), (r -> true ; true), findall(X, s(X), _),
	findall(G-R, (get_calls(q(G), _, R)), L1), msort(L1, S1), show(get_calls_q, S1),
	findall(V-R, (V = sp(_, _, _), get_calls(V, _, R)), L2), show(get_calls_sp, L2),
	(get_call(p(_), T1, R1) -> show(get_call_p, R1) ; show(get_call_p, none)),
	(get_call(p(1), _, _) -> show(get_call_p1, found) ; show(get_call_p1, none)),
	findall(R, get_returns(T1, R), L3), msort(L3, S3), show(get_returns_p, S3),
	get_call(sp(a, _, _), T2, R2), findall(R2, get_returns(T2, R2), L4), msort(L4, S4), show(get_returns_sp, S4),
	get_call(w(a), T3, R3), findall(R3-TV, get_returns_and_tvs(T3, R3, TV), L5), show(tvs_w, L5),
	findall(R3-DL, get_returns_and_dls(T3, R3, DL), L6), show(dls_w, L6),
	findall(DL, get_residual(w(a), DL), L7), show(residual_w, L7),
	findall(DL, get_residual(r, DL), L8), show(residual_r, L8),
	findall(X-DL, get_residual(s(X), DL), L9), msort(L9, S9), show(residual_s, S9),
	findall(DL, get_residual(p(2), DL), L10), show(residual_p2, L10),
	findall(A, get_returns_for_call(q(_), A), L11), msort(L11, S11), show(for_call_q, S11),
	tfindall(X, p(X), L12), show(tfindall, L12),
	try(t_not, 't not'(p(9))),
	ntables(N0), abolish_table_call(q(a)), ntables(N1), show(abolish_call, N0-N1),
	(get_call(q(_), _, _) -> show(q_var_left, yes) ; show(q_var_left, no)),
	abolish_table_pred(q/1), ntables(N2), show(abolish_pred, N2),
	abolish_table_subgoals(sp(a, _, _)), ntables(N3), show(abolish_subgoals, N3),
	try(bad_option, abolish_table_subgoals(p(_), [foo])),
	try(bad_pred, abolish_table_pred(3)),
	try(not_tabled, abolish_table_subgoals(append(_, _, _))),
	abolish_module_tables(user), ntables(N4), show(abolish_module, N4).
