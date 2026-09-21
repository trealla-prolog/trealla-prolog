% A predicate's index is built on the first lookup that wants one, not on the assert
% that takes it past the threshold. That build happens with readers already walking
% the clause chain, so it must publish nothing until the index is complete: a reader
% that sees a half-built index misses clauses that are there.
%
% Four readers query q/2 while a fifth thread grows it past the threshold. Each
% reader's lookup is on a key that is present once, and every clause must be found.
% The same must hold with --nojitindex, which builds on assert as before.
%
% The composite index is built from the read path too, once enough lookups have both key
% arguments bound, so r/3 repeats the exercise for that one.

:- initialization(main).

:- dynamic(q/2).
:- dynamic(r/3).

n(3000).

grower :- n(N), forall(between(1, N, I), assertz(q(I, I))).

reader :- n(N), forall(between(1, N, _), ( q(7, X) -> check(X) ; true )).

% Both key arguments bound: the shape the composite index answers.
grower2 :- n(N), forall(between(1, N, I), (J is I mod 64, assertz(r(J, I, I)))).

reader2 :- n(N), forall(between(1, N, _), ( r(7, 71, Y) -> check2(Y) ; true )).

check(7) :- !.
check(X) :- assertz(bad(X)).

check2(71) :- !.
check2(X) :- assertz(bad(X)).

main :-
	findall(T, (between(1, 4, _), thread_create(reader, T, [])), Rs),
	thread_create(grower, G, []),
	forall(member(T, [G|Rs]), thread_join(T, _)),
	findall(x, q(_, _), L), length(L, Cnt), n(N),
	findall(T2, (between(1, 4, _), thread_create(reader2, T2, [])), Rs2),
	thread_create(grower2, G2, []),
	forall(member(T2, [G2|Rs2]), thread_join(T2, _)),
	findall(x, r(_, _, _), L2), length(L2, Cnt2),
	(	catch(bad(X), _, fail) -> format("jit_index: wrong solution ~w~n", [X])
	;	Cnt =\= N -> format("jit_index: lost clauses, ~w of ~w~n", [Cnt, N])
	;	Cnt2 =\= N -> format("jit_index: lost composite clauses, ~w of ~w~n", [Cnt2, N])
	;	format("jit_index: ok~n")
	).
