% A clause reference names its predicate, so clause/3 and erase/1 find the clause in that
% predicate's chain: names with ':' and digits round-trip, the same name in two modules
% stays apart, and a handle that is only the 16 hex digits still works.

:- initialization(main).

:- dynamic('odd:name 1'/1).
:- dynamic(p/1).

main :-
	check(odd_name, (assertz('odd:name 1'(a), R1), clause('odd:name 1'(X1), true, R1b), X1 == a, R1b == R1, erase(R1), \+ 'odd:name 1'(_))),
	check(two_modules, (assertz(m1:p(1), Ra), assertz(m2:p(1), Rb), Ra \== Rb, erase(Rb), m1:p(1), \+ m2:p(1), erase(Ra), \+ m1:p(1))),
	check(user_module, (assertz(p(9), R2), clause(H2, B2, R2), H2 == p(9), B2 == true, erase(R2), \+ p(9))),
	check(after_abolish, (assertz(q(1), R3), abolish(q/1), \+ erase(R3), \+ clause(_, _, R3))),
	check(hex_only, (assertz(p(7), R4), sub_atom(R4, 0, 16, _, Hex), clause(p(Y4), true, Hex), Y4 == 7, erase(Hex), \+ p(7))),
	check(bad_suffix, (assertz(p(8), R5), sub_atom(R5, 0, 16, _, Hex5), atom_concat(Hex5, ':x', Bad), \+ erase(Bad), p(8), erase(R5))),
	check(record_ref, (recordz(rk, v, R6), recorded(rk, V6, R6), V6 == v, erase(R6), \+ recorded(rk, _))),
	check(junk, (\+ erase(junk), \+ clause(_, _, junk))).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
