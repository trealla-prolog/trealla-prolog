% Issue #1159: current_predicate(N/N) answered with the arity.
%
% search_functor/4 unified the name and the arity with cells it had
% dereferenced before either binding, so with a shared variable the
% arity unify bound the same slot again and overwrote the name. No
% predicate indicator can have Name == Arity, so N/N has no solutions.
%
% https://github.com/trealla-prolog/trealla/issues/1159

:- initialization(main).

a.
b(_).
c(_,_).

main :-
	check(shared_var_fails, \+ current_predicate(_N/_N)),
	check(name_stays_an_atom, forall(current_predicate(N/_), atom(N))),
	check(arity_stays_an_integer, forall(current_predicate(_/A), integer(A))),
	check(enumerates_name, (findall(N, current_predicate(N/1), L1), msort(L1,S1), memberchk(b, S1))),
	check(enumerates_arity, (findall(A, current_predicate(c/A), L2), L2 == [2])),
	check(enumerates_both, (findall(A, (current_predicate(N3/A), N3 == a), L3), L3 == [0])),
	check(no_bindings_leak, (findall(N4, current_predicate(N4/2), L4), msort(L4,S4), memberchk(c, S4))).

check(Name, Goal) :-
	(   catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
