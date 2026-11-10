% A copy of a list whose tails were bound one at a time, as maplist/3 builds one, kept each inner '.' cell's size
% from the source, 3, rather than the rest of the copy. Printing follows tails so looked right, but anything that
% skips a subterm by its size cut the copy short: =.. gave [.,a,[b,[]|dummy]], and so did an asserted copy.

:- initialization(main).
:- dynamic(stored/1).

t(Label, Goal) :-
	(   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

main :-
	t(univ, (maplist(=, [a,b,c], L1), copy_term(L1, C1), C1 =.. U1, U1 == ['.', a, [b,c]])),
	t(stepwise, (L2 = [a|T1], T1 = [b|T2], T2 = [c,d|T3], T3 = [e], copy_term(L2, C2), C2 =.. U2, U2 == ['.', a, [b,c,d,e]])),
	t(asserted, (maplist(=, [a,b,c], L3), copy_term(L3, C3), assertz(stored(C3)), stored(S3), S3 =.. U3, U3 == ['.', a, [b,c]])),
	t(findall, (findall(L4, maplist(=, [a,b,c], L4), [C4]), C4 =.. U4, U4 == ['.', a, [b,c]])),
	t(nested, (findall(f(L5), maplist(=, [a,b,c], L5), [f(C5)]), C5 = [_|T5], T5 =.. U5, U5 == ['.', b, [c]])),
	t(arg, (maplist(=, [x,y,z], L6), copy_term(g(L6, end), C6), arg(2, C6, A6), A6 == end)),
	t(partial, (L7 = [a|T7], T7 = [b|_], copy_term(L7, C7), C7 = [a,b|V7], var(V7), C7 =.. [_, _, U7], U7 = [b|W7], var(W7))),
	t(sorted, (maplist(=, [c,a,b], L8), copy_term(L8, C8), msort([C8, [a]], S8), S8 == [[a],[c,a,b]])).
