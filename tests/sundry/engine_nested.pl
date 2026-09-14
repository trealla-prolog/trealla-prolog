% Creating an engine inside another engine's goal threw type_error(atom,'$map'(N))
% from '$engine_create'/4.

:- initialization(main).

t(Name) :-
	(	catch(Name, E, (write(Name), write(' THREW '), writeq(E), nl, fail))
	->	true
	;	write(Name), write(' FAILED'), nl
	).

forall_next(E) :-
	(	engine_next(E, X)
	->	engine_yield(X),
		forall_next(E)
	;	true
	).

n1 :- engine_create(Y, (engine_create(Z, member(Z,[1,2]), E2), engine_next(E2, Y)), E1), engine_next(E1, R), engine_destroy(E1), write(n1(R)), nl.
n2 :- engine_create(x, (engine_create(y, true, E2), engine_destroy(E2)), E1), engine_next(E1, R), engine_destroy(E1), write(n2(R)), nl.
n3 :- engine_create(x, engine_create(y, true, _), E1), engine_next(E1, R), engine_destroy(E1), write(n3(R)), nl.
n4 :- engine_create(done, (engine_create(Z, member(Z,[a,b,c]), E2), forall_next(E2)), E1), findall(A, (between(1, 4, _), engine_next(E1, A)), As), engine_destroy(E1), write(n4(As)), nl.
n5 :- engine_create(E2, engine_create(Z, member(Z,[p,q]), E2), E1), engine_next(E1, H), engine_next(H, X1), engine_next(H, X2), engine_destroy(E1), engine_destroy(H), write(n5(X1, X2)), nl.
n6 :- engine_create(R3, (engine_create(R2, (engine_create(R1, R1 = deep, E3), engine_next(E3, R2)), E2), engine_next(E2, R3)), E1), engine_next(E1, R), engine_destroy(E1), write(n6(R)), nl.

main :- t(n1), t(n2), t(n3), t(n4), t(n5), t(n6).
