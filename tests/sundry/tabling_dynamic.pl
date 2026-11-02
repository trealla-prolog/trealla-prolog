% A tabled predicate may be dynamic too, as in SWI, declared either side
% of its table. Its clauses are its worker's, as the (:- table)
% expansion renames them while loading, and asserta, assertz, retract,
% retractall and clause/2 on it now reach them there. Declaring it
% dynamic after its table used to stop the load, and declaring it before
% left asserted clauses outside the table. As in SWI, a table that is
% not incremental keeps its answers after a change, and an incremental
% one is recomputed. Results match SWI.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

% table first, then dynamic
:- table a/1.
:- dynamic a/1.
a(1).

% dynamic first, then table
:- dynamic b/1.
:- table b/1.
b(1).

% incremental, both orders
:- table c/1 as incremental.
:- dynamic([c/1], [incremental(true)]).
c(1).
:- dynamic d/1 as incremental.
:- table d/1 as incremental.
d(1).

% a tabled, incremental, left-recursive closure that is dynamic itself
:- dynamic([e/2], [incremental(true)]).
:- table r/2 as incremental.
:- dynamic([r/2], [incremental(true)]).
r(X, Y) :- e(X, Y).
r(X, Y) :- r(X, Z), e(Z, Y).
e(1, 2). e(2, 3).

all(G, L) :- findall(X, call(G, X), L0), msort(L0, L).
show(T, V) :- write(T: V), nl.

main :-
	assertz(a(2)), all(a, A1), show(a_before, A1),
	assertz(a(3)), all(a, A2), show(a_after_stale, A2),
	assertz(b(2)), all(b, B1), show(b_before, B1),
	all(c, C0), assertz(c(2)), asserta(c(0)), all(c, C1), show(c_inc, C0-C1),
	retract(c(1)), all(c, C2), show(c_retract, C2),
	assertz((c(X) :- X = 9)), all(c, C3), show(c_rule, C3),
	findall(H-B, clause(c(H), B), Cl), numbervars(Cl, 0, _), show(c_clauses, Cl),
	retract((c(Y) :- Y = 9)), all(c, C4), show(c_retract_rule, C4),
	retractall(c(_)), all(c, C5), show(c_retractall, C5),
	all(d, D0), assertz(user:d(2)), all(d, D1), show(d_inc_qualified, D0-D1),
	findall(Q, r(1, Q), R0), msort(R0, Rs0), assertz(e(3, 4)), findall(Q, r(1, Q), R1), msort(R1, Rs1),
	assertz(r(1, 9)), findall(Q, r(1, Q), R2), msort(R2, Rs2), show(r, [Rs0, Rs1, Rs2]),
	findall(P, (member(P, [dynamic, tabled, incremental]), predicate_property(d(_), P)), Ps), show(d_props, Ps),
	catch(assertz((a(foo) :- true)), E, true), (var(E) -> show(a_assert_rule, ok) ; show(a_assert_rule, E)).
