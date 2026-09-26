% Key chains under threads. Readers walk key chains without the module lock while writers link and
% unlink clauses on the same keys, and turn single-clause keys into chained ones and back (a keyhead
% swapped into the index entry in place). Clauses no writer touches must always be found, by either
% indexed argument and by both, whatever the writers are doing around them.

:- initialization(main).

:- dynamic(kc/2).
:- dynamic(bad/1).

% 600 permanent clauses kc(K, I), I = 1..600, so kc/2 is indexed: 60 keys of 10 clauses on the first
% argument, and one clause per key on the second. Writers use second arguments above 100000.

seed :-
	forall(between(1, 600, I), (K is I mod 60, assertz(kc(K, I)))),
	once(kc(0, _)).

% Churn on the permanent keys, and on keys 1000 up that go from one clause to two and back.

writer(T) :-
	forall(between(1, 6000, I),
		(	K is (I * 7 + T) mod 60,
			V is 100000 * T + I,
			W is V + 50000,
			assertz(kc(K, V)),
			asserta(kc(K, W)),
			S is 1000 + (I mod 50),
			assertz(kc(S, V)),
			assertz(kc(S, W)),
			retract(kc(K, V)),
			retract(kc(K, W)),
			retract(kc(S, V)),
			retract(kc(S, W))
		)).

% Every permanent clause of a key through its first argument; one of them through its second, a key
% with a single clause; and through both, which walks the shorter chain.

reader(T) :-
	forall(between(1, 3000, J),
		(	K is (J * 13 + T) mod 60,
			findall(I, (kc(K, I), I =< 600), Is),
			length(Is, N),
			( N =:= 10 -> true ; assertz(bad(first(K, N))) ),
			I1 is K + 60,
			( kc(K2, I1), K2 == K -> true ; assertz(bad(second(I1))) ),
			( kc(K, I1) -> true ; assertz(bad(both(K, I1))) ),
			S is 1000 + (J mod 50),
			findall(x, kc(S, _), _)
		)).

main :-
	seed,
	findall(Th,
		(	member(G, [writer(1), reader(1), writer(2), reader(2), writer(3), reader(3)]),
			thread_create(G, Th, [])
		), Ths),
	forall(member(Th, Ths), thread_join(Th, _)),
	findall(B, bad(B), Bs),
	(	Bs == []
	->	format("key_chains_threads: ok~n")
	;	length(Bs, NB), Bs = [B1|_], format("key_chains_threads: ~w failures, first ~w~n", [NB, B1])
	).
