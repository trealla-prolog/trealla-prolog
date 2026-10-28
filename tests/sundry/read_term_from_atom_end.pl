% read_term_from_atom/3, read_term_from_chars/3 and so term_to_atom/2 and
% atom_to_term/3 read text ending in a symbol char, a symbol atom ending in '.',
% 0'. or a % comment, as SWI does; text already ending in '.' reads as before.

:- initialization(main).

main :-
	check(slash, (term_to_atom(T1, '/'), T1 == (/))),
	check(minus, (term_to_atom(T2, '-'), T2 == (-))),
	check(neck, (term_to_atom(T3, ':-'), T3 == (:-))),
	check(backslash, (term_to_atom(T4, '\\'), T4 == (\))),
	check(univ, (term_to_atom(T5, '=..'), T5 == (=..))),
	check(char_dot, (term_to_atom(T6, '0\'.'), T6 == 0'.)),
	check(comment, (term_to_atom(T7, 'x % c'), T7 == x)),
	check(atom_to_term, (atom_to_term('-', T8, B8), T8 == (-), B8 == [])),
	check(from_chars, (read_term_from_chars([/], T9, []), T9 == (/))),
	check(from_atom, (read_term_from_atom('+', T10, []), T10 == (+))),
	check(ended, (term_to_atom(T11, 'f(x).'), T11 == f(x))),
	check(ended_space, (term_to_atom(T12, 'a. '), T12 == a)),
	check(first_term, (term_to_atom(T13, 'f(x). g(y)'), T13 == f(x))),
	check(spaced_op, (term_to_atom(T14, '- .'), T14 == (-))).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
