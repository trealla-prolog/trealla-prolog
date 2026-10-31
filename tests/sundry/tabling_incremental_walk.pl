% How much an incremental table recomputes after a database change. The
% check walked its dependencies recursively and gave up 32 deep, so a
% longer chain of tables was recomputed after ANY change, even an assert
% to a predicate that is not incremental, and a lattice where tables
% are reached several ways took exponential time below that depth. A
% deep SCC's tables kept edges to their own SCC, a cycle that ran into
% the same cap. Now the walk visits each table once, with no cap; tables
% shown valid are stamped with the generation, and those found changed
% are marked stale, so the ones above don't walk again. evals counts
% clause bodies run, as SWI does.

:- use_module(library(tabling)).

:- initialization(main).

:- dynamic(base/1).
:- incremental(base/1).
:- dynamic(unrelated/1).
:- dynamic(unrelated_inc/1).
:- incremental(unrelated_inc/1).

base(0).

% A chain of separate tables.

:- table chain/2 as incremental.

chain(0, X) :- !, base(X).
chain(N, X) :- flag(evals, C, C+1), M is N-1, chain(M, Y), X is Y+1.

% Every level reached two ways.

:- table lattice/3 as incremental.

lattice(0, _, X) :- !, base(X).
lattice(N, S, X) :-
	flag(evals, C, C+1),
	M is N-1,
	lattice(M, a, A),
	lattice(M, b, B),
	side(S, S0),
	X is max(A, B) + S0.

side(a, 1).
side(b, 0).

% One deep SCC: ring(1, N) reaches ring(N, N), which calls ring(1, N).

:- table ring/2 as incremental.

ring(A, N) :- flag(evals, C, C+1), ring_step(A, B, N), ring(B, N).
ring(N, N).

ring_step(A, B, N) :- base(_), A < N, B is A + 1.
ring_step(N, 1, N).

t(Name, G) :-
	flag(evals, _, 0),
	( call(G) -> R = G ; R = failed ),
	flag(evals, E, E),
	arg(1, G, A1), functor(G, F, _),
	( arg(3, G, X) -> true ; arg(2, G, X) ),
	( R == failed -> V = failed ; F == ring -> V = A1 ; V = X ),
	write(Name), write(': '), writeq(V), write(' evals='), write(E), nl.

change_base(New) :-
	retract(base(_)),
	assertz(base(New)).

main :-
	t('chain', chain(2000, _)),
	assertz(unrelated(1)),
	t('chain, unrelated assert', chain(2000, _)),
	assertz(unrelated_inc(1)),
	t('chain, unrelated incremental assert', chain(2000, _)),
	change_base(5),
	t('chain, middle first', chain(1000, _)),
	t('chain, then top', chain(2000, _)),
	t('chain, cached', chain(2000, _)),
	t('lattice', lattice(60, a, _)),
	assertz(unrelated(2)),
	t('lattice, unrelated assert', lattice(60, a, _)),
	change_base(6),
	t('lattice, base changed', lattice(60, a, _)),
	t('ring', ring(1, 2000)),
	assertz(unrelated(3)),
	t('ring, unrelated assert', ring(1, 2000)),
	change_base(7),
	t('ring, base changed', ring(1, 2000)).
