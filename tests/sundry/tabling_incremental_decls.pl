% SWI's ways of declaring a dynamic predicate incremental, besides
% incremental/1: ':- dynamic Spec as Opts', where any conjunct may
% carry its own `as`, and dynamic/2 with an option list. They used to
% be unknown directives, so the predicate was not even dynamic, or not
% incremental and the table kept its old answers. One incremental table
% reads each predicate; after an assertz only the incremental ones show
% the new clause. Results match SWI.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

:- dynamic a/1 as incremental.
:- dynamic([b/1], [incremental(true)]).
:- dynamic([c/1], [incremental(false)]).
:- dynamic d/1 as opaque.
:- dynamic e/1 as (incremental, abstract(0)).
:- dynamic f/1, g/1 as incremental.
:- dynamic (h/1, i/1) as incremental.
:- dynamic [j/1, k/1] as incremental.
:- dynamic user:l/1 as incremental.
:- dynamic(m/1), dynamic(n/1 as incremental).

:- table t/2 as incremental.

t(P, X) :- call(P, X).

a(1). b(1). c(1). d(1). e(1). f(1). g(1). h(1). i(1). j(1). k(1). l(1). m(1). n(1).

main :-
	forall(
		member(P, [a,b,c,d,e,f,g,h,i,j,k,l,m,n]),
		(	findall(X, t(P, X), L1),
			G =.. [P, 2], assertz(G),
			findall(X, t(P, X), L2),
			write(P-L1-L2), nl
		)).
