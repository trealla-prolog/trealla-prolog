% The recorded database, as in SWI: recorda/2,3, recordz/2,3, recorded/2,3,
% current_key/1, erase/1 and instance/2. A compound key counts by name and arity;
% values are copied with their variable sharing; instance/2 copies too.

:- initialization(main).

main :-
	recordz(k, a), recordz(k, b, Rb), recorda(k, z), recordz(f(1), fx), recordz(7, seven),
	check(order, (findall(V, recorded(k, V), L1), L1 == [z,a,b])),
	check(compound_key, (findall(V, recorded(f(9), V), L2), L2 == [fx])),
	check(by_ref, (recorded(K3, V3, Rb), K3-V3 == k-b)),
	check(all_keys, (findall(K, current_key(K), L4), msort(L4, S4), length(S4, 3), memberchk(k, S4), memberchk(7, S4), member(F4, S4), F4 = f(X4), var(X4))),
	check(enumerate_keys, (findall(K-V, recorded(K, V), L5), length(L5, 5))),
	check(erase, (erase(Rb), findall(V, recorded(k, V), L6), L6 == [z,a])),
	check(erase_again_fails, \+ erase(Rb)),
	check(instance_erased_fails, \+ instance(Rb, _)),
	check(shared_vars, (recordz(s, f(X7, X7, _), R7), instance(R7, f(A7, B7, C7)), A7 == B7, A7 \== C7, var(X7), recorded(s, f(D7, E7, _)), D7 == E7)),
	check(instance_clause, (assertz(rec_cl(1), R8), instance(R8, T8), T8 == (rec_cl(1) :- true))),
	check(instance_copies, (A9 = outer, assertz(rec_cl2(X9, X9), R9), instance(R9, (H9 :- true)), H9 = rec_cl2(P9, Q9), var(P9), P9 == Q9, A9 == outer)),
	throws(var_key, recorda(_, x)),
	throws(float_key, recordz(1.5, x)),
	check(bound_ref, catch((recorda(k, v, R10), recorda(k, v, R10), fail), error(uninstantiation_error(_), _), true)),
	throws(var_instance, instance(_, _)),
	check(erase_all, ((recorded(k, _, R11), erase(R11), fail ; true), \+ recorded(k, _), \+ current_key(k))).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).

throws(Name, Goal) :-
	catch((Goal, format("~w did not throw~n", [Name])), error(E, _), format("~w threw ~q~n", [Name,E])).
