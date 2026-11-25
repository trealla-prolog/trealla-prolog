% Scryer's in_character, octet_character, octet_chars and type for must_be/2 and can_be/2. A character
% is one, not '' or end_of_file. For chars a type error outranks an instantiation error. can_be/4 takes
% a partial list as a list.

:- initialization(main).

t(Goal) :-
	copy_term(Goal, G),
	numbervars(G, 0, _),
	( catch(Goal, error(E, _), true) -> (var(E) -> R = true ; R = E) ; R = false ),
	numbervars(R, 0, _),
	format("~q: ~q~n", [G, R]).

main :-
	t(must_be(character, end_of_file)),
	t(must_be(character, '')),
	t(can_be(character, '')),
	t(must_be(in_character, end_of_file)),
	t(must_be(in_character, a)),
	t(must_be(in_character, ab)),
	t(must_be(in_character, _)),
	t(can_be(in_character, 1)),
	t(can_be(in_character, end_of_file)),
	t(must_be(octet_character, a)),
	t(must_be(octet_character, 'ā')),
	t(must_be(octet_character, 1)),
	t(can_be(octet_character, 'ā')),
	t(can_be(octet_character, ab)),
	t(must_be(octet_chars, [a,b])),
	t(must_be(octet_chars, "ab")),
	t(must_be(octet_chars, [a,'ā'])),
	t(must_be(octet_chars, ['ā',1])),
	t(must_be(octet_chars, ['ā',_])),
	t(must_be(octet_chars, foo)),
	t(can_be(octet_chars, [a|_])),
	t(can_be(octet_chars, [a,_,'ā'])),
	t(can_be(octet_chars, ['ā',1])),
	t(must_be(chars, [1,_])),
	t(must_be(chars, [a,_])),
	t(must_be(type, octet_chars)),
	t(must_be(type, list(integer))),
	t(must_be(type, foo)),
	t(must_be(type, _)),
	t(can_be(type, integer)),
	t(can_be(type, list(_))),
	t(can_be(type, list(foo))),
	t(can_be([a|_], list, foo/1, _)),
	t(can_be([a|b], list, foo/1, _)),
	t(can_be("ab", atom, foo/1, _)),
	t(can_be(1.5, not_less_than_zero, foo/1, _)).
