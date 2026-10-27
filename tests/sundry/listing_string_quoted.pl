% listing/0,1 quote the elements of a double-quoted list as they would an
% atom, while write/1 still doesn't.

:- initialization(main).

:- dynamic(zz/1).
zz("a b").
zz("aB").
zz([x|"a b"]).
zz("it's").

main :-
	listing(zz/1),
	with_output_to(string(S), listing), (sub_string(S, _, _, _, "zz([a,' ',b]).") -> writeln(listing0_ok) ; writeln(listing0_unquoted)),
	X = "a b", write(X), nl, writeq(X), nl.
