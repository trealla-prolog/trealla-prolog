% A predicate assertz/1 creates is dynamic before another thread can find it. It was
% published first and marked dynamic after, so clause/2 in a thread in between raised
% permission_error(access,private_procedure).

:- initialization(main).

new(M, Id, X) :-
	\+ clause(M:p(X, Id), true),
	assertz(M:p(X, Id)).

work(M, N) :-
	forall(between(1, 5000, I), (X is I mod N, ignore(new(M, N, X)))).

round(R, Ss) :-
	format(atom(M), "dynamic_create_race_~d", [R]),
	assertz(M:dummy),
	findall(T, (member(N, [7,100,1000,5000]), thread_create(work(M, N), T, [])), Ts),
	findall(S, (member(T, Ts), thread_join(T, S)), Ss).

main :-
	findall(Ss, (between(1, 5, R), round(R, Ss)), All),
	(	forall(member(Ss, All), Ss == [true,true,true,true])
	->	writeln(ok)
	;	writeq(All), nl
	).
