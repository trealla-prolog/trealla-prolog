% atom_string/2, upcase_atom/2 and downcase_atom/2 as in SWI: any text in either
% direction, a bound second argument compared as text, and SWI's errors.

:- initialization(main).

main :-
	check(as_atom, (atom_string(abc, S1), string(S1), S1 == "abc")),
	check(as_number, (atom_string(42, S2), S2 == "42")),
	check(as_float, (atom_string(1.5, S3), S3 == "1.5")),
	check(from_string, (atom_string(A4, "xyz"), A4 == xyz)),
	check(from_atom, (atom_string(A5, xyz), A5 == xyz)),
	check(from_number, (atom_string(A6, 42), A6 == '42')),
	check(from_codes, (atom_string(A7, [0'a, 0'b]), A7 == ab)),
	check(from_chars, (atom_string(A8, [a, b]), A8 == ab)),
	check(compare_same, atom_string(abc, "abc")),
	check(compare_differ, \+ atom_string(abc, "abd")),
	check(empty, (atom_string('', S9), atom_string(A9, S9), A9 == '')),
	throws(both_unbound, atom_string(_, _)),
	throws(compound, atom_string(f(x), _)),
	check(upcase, (upcase_atom(abc, U1), U1 == 'ABC')),
	check(upcase_string, (upcase_atom("abc", U2), U2 == 'ABC')),
	check(upcase_number, (upcase_atom(42, U3), U3 == '42')),
	check(upcase_compare, (upcase_atom(abc, 'ABC'), upcase_atom(abc, "ABC"))),
	check(upcase_atom_out, (upcase_atom("x", U4), atom(U4))),
	check(downcase, (downcase_atom('AbC', D1), D1 == abc)),
	throws(upcase_unbound, upcase_atom(_, _)),
	throws(upcase_codes, upcase_atom([0'a], _)),
	throws(downcase_compound, downcase_atom(f(x), _)).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).

throws(Name, Goal) :-
	catch((Goal, format("~w did not throw~n", [Name])), error(E, _), format("~w threw ~q~n", [Name,E])).
