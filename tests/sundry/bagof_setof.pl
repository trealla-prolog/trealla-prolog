% bagof/3 and setof/3 with and without free variables, and a loop of them that must keep only a few frames per call.

:- initialization(main).

p(1, a).
p(2, b).
p(3, a).
p(2, a).

q(1, x, u).
q(2, y, v).
q(1, y, v).

r(X, f(Y, W)) :- q(X, Y, W).

% Each call still keeps a frame or two while its result escapes it; the library helpers kept 26 an iteration.
loop(0) :- !.
loop(N) :- setof(X, Y^p(X, Y), _), bagof(X, p(X, a), _), M is N-1, loop(M).

show(Name, Goal, Result) :-
	write(Name), write(': '),
	(	catch(Goal, error(E, _), (write(E), nl, fail))
	->	write(Result), nl
	;	true
	).

main :-
	show(setof_plain, setof(X1, member(X1, [c,a,b,a]), L1), L1),
	show(bagof_plain, bagof(X2, member(X2, [c,a,b,a]), L2), L2),
	( bagof(_, fail, _) -> write(bagof_empty(yes)) ; write(bagof_empty(no)) ), nl,
	( setof(_, fail, _) -> write(setof_empty(yes)) ; write(setof_empty(no)) ), nl,
	findall(Y3-L3, bagof(X3, p(X3, Y3), L3), G3), write(bagof_groups(G3)), nl,
	findall(Y4-L4, setof(X4, p(X4, Y4), L4), G4), write(setof_groups(G4)), nl,
	show(caret, setof(X5, Y5^p(X5, Y5), L5), L5),
	show(caret2, setof(X6, Y6^Z6^q(X6, Y6, Z6), L6), L6),
	show(caret_compound, setof(X7, f(Y7, W7)^r(X7, f(Y7, W7)), L7), L7),
	findall(Z8-L8, setof(X8, Y8^q(X8, Y8, Z8), L8), G8), write(caret_groups(G8)), nl,
	show(template_pair, bagof(X9-Y9, p(X9, Y9), L9), L9),
	bagof(X10-_, p(X10, a), L10), length(L10, N10), write(template_fresh(N10)), nl,
	Y11 = Z11, show(aliased_caret, setof(X11, Z11^p(X11, Y11), L11), L11),
	G12 = (Y12^p(X12, Y12)), show(bound_goal, setof(X12, G12, L12), L12),
	show(partial_list, bagof(X13, member(X13, [a,b]), [H13|T13]), H13-T13),
	show(var_goal, bagof(_, _, _), none),
	show(not_list, bagof(X14, member(X14, [a]), foo), none),
	loop(10000),
	statistics(max_frames, F),
	(	F < 50000 -> write(frames(ok)) ; write(frames(F)) ), nl.
