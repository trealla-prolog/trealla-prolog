% A string is a list of characters, not an atom, so must_be/2,4 refuse it for atom and atomic, as atom/1
% and atomic/1 do and as Scryer's must_be/2 does.

:- use_module(library(lists)).
:- initialization(main).

t(G) :-
	( catch(G, error(E, _), true) -> (var(E) -> R = true ; R = E) ; R = false ),
	copy_term(G, G2), numbervars(G2, 0, _),
	format("~q: ~q~n", [G2, R]).

main :-
	t(must_be(atom, "ab")), t(must_be(atom, "a")), t(must_be(atomic, "ab")),
	t(must_be(atom, ab)), t(must_be(atom, [])), t(must_be(atomic, 1)),
	length(Cs, 300), maplist(=(a), Cs), atom_chars(Long, Cs),
	( catch(must_be(atom, Long), _, fail) -> writeln(long_atom: true) ; writeln(long_atom: false) ),
	t(must_be("ab", atom, foo/1, _)), t(must_be("ab", atomic, foo/1, _)), t(must_be(ab, atom, foo/1, _)),
	t(must_be(list(atom), [a, "bc"])).
