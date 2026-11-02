% Issue #989: findall/3 could not represent a cyclic term in its queue -
% the clone has no frame slots to hang a cycle from, so it emitted a
% fresh variable where the back-edge was - and the collected term quietly
% stopped being cyclic. bagof/3, which is built on findall/3, then
% reported answers that looked plausible and were wrong.
%
% That was first refused with type_error(acyclic_term). The queue now
% records where each cycle was cut and the drain rejoins it, so cyclic
% answers come back whole, as in SWI and Scryer.

:- initialization(main).

check(Label, Goal) :-
	(  catch(Goal, E, (format("FAIL ~w: ~q~n", [Label, E]), fail))
	-> writeln(Label)
	;  format("FAIL ~w~n", [Label])
	).

main :-
	% the cycle is created inside the goal
	check(findall_inside, (findall(X1, X1=p(X1), [C1]), C1 = p(D1), D1 == C1)),

	% and when it already existed beforehand
	Y = p(Y),
	check(findall_existing, (findall(V, member(V,[Y]), [C2]), C2 == Y)),

	% a cyclic list spine
	check(findall_list, (findall(X2, X2=[a|X2], [C3]), C3 = [a|D3], D3 == C3)),

	% findnsols/4 collects through the same queue
	check(findnsols, (findnsols(5, X3, X3=p(X3), [C4]), C4 = p(D4), D4 == C4)),

	% the issue's own cases, via bagof/3
	check(bagof_case1, (bagof(X4, Y4^member(Y4-X4,[X4-p(Y4)]), Z4), Z4 = [p(p(p(ZZ4)))], ZZ4 = p(W4), W4 == ZZ4)),
	check(bagof_case2, (bagof(X5, Y5^(Y5-X5=X5-p(Y5)), Z5), Z5 = [p(p(p(ZZ5)))], ZZ5 =.. [p, W5], W5 == ZZ5)),
	check(bagof_case3, (bagof(X6, Y6^(Y6-X6=X6-p(q(Y6))), Z6), Z6 = [p(ZZ6)], ZZ6 =.. [q, W6], W6 = p(V6), V6 == ZZ6)),

	% acyclic collecting is untouched
	findall(N, member(N,[a,b,c]), Ns),
	(  Ns == [a,b,c] -> writeln(acyclic_findall) ; writeln('FAIL acyclic findall') ),
	findall(A-B, (member(A,[1,2]), B=f(A)), Ps),
	(  Ps == [1-f(1), 2-f(2)] -> writeln(acyclic_compound) ; writeln('FAIL acyclic compound') ),
	bagof(C, member(C,[x,y]), Cs),
	(  Cs == [x,y] -> writeln(acyclic_bagof) ; writeln('FAIL acyclic bagof') ),
	findnsols(5, D, member(D,[p,q]), Ds),
	(  Ds == [p,q] -> writeln(acyclic_findnsols) ; writeln('FAIL acyclic findnsols') ).
