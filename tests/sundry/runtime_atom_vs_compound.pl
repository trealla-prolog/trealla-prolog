% An atom built at runtime (atom_chars, atom_codes, name, char_code, sub_atom,
% atom_concat) no longer unifies with a compound of the same name, in = or in head
% matching, and still unifies with the atom itself.

:- initialization(main).

k(foo(_), c1).
k(foo, a).
k(foo(_,_), c2).

e(N/A, h(N,A)) :- !.
e(X, atom(X)).

main :-
	atom_chars(F, [f,o,o]),
	atom_codes(S, [0'/]),
	name(N, "foo"),
	char_code(C, 0'f),
	sub_atom(xfooy, 1, 3, _, B),
	atom_concat(fo, o, P),
	check(chars_vs_1, \+ F = foo(_)),
	check(chars_vs_2, \+ F = foo(_,_)),
	check(compound_vs_chars, \+ foo(_) = F),
	check(codes_vs_slash, \+ S = _/_),
	check(name_vs_1, \+ N = foo(_)),
	check(char_code_vs_1, \+ C = f(_)),
	check(sub_atom_vs_1, \+ B = foo(_)),
	check(concat_vs_1, \+ P = foo(_)),
	check(occurs_check, \+ unify_with_occurs_check(F, foo(_))),
	check(still_atom, (F = foo, S = '/', N = foo, C = f, B = foo, P = foo)),
	check(head_match, (findall(R, k(F, R), L1), L1 == [a])),
	check(head_cut, (e(S, R2), R2 == atom('/'))).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
