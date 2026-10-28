% Clauses for a non-ISO C builtin take it over, as in SWI and Scryer.

:- initialization(main).

main :-
	check(forward_call, (go(E-R), E-R == b-[a,c])),
	check(direct, (split(0, [x,y], A, B), A-B == x-[y])),
	check(call_n, (call(split, 1, [p,q], C, D), C-D == q-[p])),
	check(var_goal, (G = string(S), G, S == hello)),
	check(all_clauses, (findall(S2, string(S2), L), L == [hello,world])),
	check(qualified, (sp:split(Q, _, _, _), Q == other_module)),
	check(not_builtin, \+ predicate_property(split(_,_,_,_), built_in)),
	check(iso_kept, catch((assertz(atom_length(foo, 9)), fail), error(permission_error(modify, static_procedure, _), _), true)),
	check(evaluable_kept, (X is abs(-2), X == 2, abs(z))).

go(E-R) :- split(1, [a,b,c], E, R).

split(0, [X|Xs], X, Xs) :- !.
split(N, [X|Xs], R, [X|Ys]) :- N1 is N-1, split(N1, Xs, R, Ys).

sp:split(other_module, _, _, _).

string(hello).
string(world).

abs(z).

check(Name, Goal) :-
	(catch(Goal, E, (write(Name-E), nl, fail)) -> write(Name-ok) ; write(Name-failed)), nl.
