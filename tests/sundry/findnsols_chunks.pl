% findnsols/4 renames template and goal, so bindings made inside the goal never showed between chunks (issue #1037).

:- initialization(main).

check(Name, Goal, Expected) :-
	findall(R, call(Goal, R), Got),
	(	Got == Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' got '), writeq(Got), nl
	).

chunks(R) :- findnsols(2, X, member(X, [a,b,c]), L), R = L.
template_unbound(R) :- findnsols(2, X, member(X, [a,b,c]), L), (var(X) -> R = L ; R = bound(X)).
goal_var_unbound(R) :- findnsols(2, X, (member(X, [a,b,c]), Y = X), L), (var(Y) -> R = L ; R = bound(Y)).
outer_binding(R) :- Z = 1, findnsols(2, X-Z, member(X, [a,b]), L), R = L.
qualified(R) :- findnsols(2, X, lists:member(X, [a,b,c]), L), R = L.
count(R) :- N = count(2), findnsols(N, X, member(X, [a,b,c,d]), L), R = L, nb_setarg(1, N, 1).
bound_result(R) :- findnsols(2, X, member(X, [a,b,c]), [a,b]), R = yes.

main :-
	check(chunks, chunks, [[a,b],[c]]),
	check(template_unbound, template_unbound, [[a,b],[c]]),
	check(goal_var_unbound, goal_var_unbound, [[a,b],[c]]),
	check(outer_binding, outer_binding, [[a-1,b-1]]),
	check(qualified, qualified, [[a,b],[c]]),
	check(count, count, [[a,b],[c],[d]]),
	check(bound_result, bound_result, [yes]),
	catch(findnsols(1, _, _, _), E1, true),
	(E1 = error(instantiation_error, _) -> writeln('var_goal ok') ; writeq(E1), nl),
	catch(findnsols(1, _, 3, _), E2, true),
	(E2 = error(type_error(callable, 3), _) -> writeln('callable ok') ; writeq(E2), nl),
	catch(findnsols(-1, _, true, _), E3, true),
	(E3 = error(domain_error(not_less_than_zero, -1), _) -> writeln('negative ok') ; writeq(E3), nl).
