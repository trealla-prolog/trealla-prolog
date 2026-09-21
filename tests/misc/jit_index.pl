% A predicate's index is built on the first lookup that wants one, not on the assert
% that takes it past the threshold. That build happens with readers already walking
% the clause chain, so it must publish nothing until the index is complete: a reader
% that sees a half-built index misses clauses that are there.
%
% Four readers query q/2 while a fifth thread grows it past the threshold. Each
% reader's lookup is on a key that is present once, and every clause must be found.
% The same must hold with --nojitindex, which builds on assert as before.

:- initialization(main).

:- dynamic(q/2).

n(3000).

grower :- n(N), forall(between(1, N, I), assertz(q(I, I))).

reader :- n(N), forall(between(1, N, _), ( q(7, X) -> check(X) ; true )).

check(7) :- !.
check(X) :- assertz(bad(X)).

main :-
	findall(T, (between(1, 4, _), thread_create(reader, T, [])), Rs),
	thread_create(grower, G, []),
	forall(member(T, [G|Rs]), thread_join(T, _)),
	findall(x, q(_, _), L), length(L, Cnt), n(N),
	(	catch(bad(X), _, fail) -> format("jit_index: wrong solution ~w~n", [X])
	;	Cnt =\= N -> format("jit_index: lost clauses, ~w of ~w~n", [Cnt, N])
	;	format("jit_index: ok~n")
	).
