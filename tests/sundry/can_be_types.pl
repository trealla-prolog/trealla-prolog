% can_be/2 as Scryer has it: a variable or partial list can still become the type, list(Type) is must_be/2's
% only, a string is a list and not an atom, and chars checks each bound element. var raises
% uninstantiation_error, as must_be/2 does.

:- initialization(main).

t(Goal) :-
	copy_term(Goal, G),
	numbervars(G, 0, _),
	( catch(Goal, error(E, _), true) -> (var(E) -> R = true ; R = E) ; R = false ),
	numbervars(R, 0, _),
	format("~q: ~q~n", [G, R]).

main :-
	t(can_be(list(integer), [a,b])),
	t(can_be(list(_), [a])),
	t(can_be("atom", a)),
	t(can_be(list, [a|_])),
	t(can_be(list, [_|_])),
	t(can_be(list, "ab")),
	t(can_be(list, [a|b])),
	t(can_be(atom, "ab")),
	t(can_be(atomic, "ab")),
	t(can_be(atom, a)),
	t(can_be(chars, "ab")),
	t(can_be(chars, [a|"bc"])),
	t(can_be(chars, [a,_|_])),
	t(can_be(chars, [a,1])),
	t(can_be(chars, a)),
	t(can_be(chars, [1|2])),
	t(can_be(var, _)),
	t(can_be(var, a)),
	t(can_be(ground, f(_))),
	t(can_be(nonvar, a)),
	X = f(X),
	catch((can_be(acyclic, X), R = true), error(E, _), (functor(E, N, A), R = N/A)),
	format("can_be(acyclic,cyclic): ~q~n", [R]),
	t(must_be("atom", a)).
