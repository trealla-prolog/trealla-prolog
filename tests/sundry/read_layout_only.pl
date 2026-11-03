% Text holding only layout (whitespace and complete comments) reads as
% end_of_file from read_term_from_atom/3 and read_term_from_chars/3, as a
% stream at its end does and as SWI and GNU do; an incomplete term is still
% a syntax error. read_term_from_chars_/3 also used to crash when the read
% left nothing behind or threw into a catch/3.

:- initialization(main).

main :-
	check(space, (read_term_from_chars(" ", T1, []), T1 == end_of_file)),
	check(newline, (read_term_from_atom('\n', T2, []), T2 == end_of_file)),
	check(line_comment, (read_term_from_chars("% c", T3, []), T3 == end_of_file)),
	check(line_comment_nl, (read_term_from_atom('% c\n', T4, []), T4 == end_of_file)),
	check(block_comment, (read_term_from_atom('/* x */\n', T5, []), T5 == end_of_file)),
	check(mixed, (read_term_from_atom(' /* a */ % b\n ', T6, []), T6 == end_of_file)),
	check(chars_list, (read_from_chars([' ', '%', c], T7), T7 == end_of_file)),
	check(term_after_comment, (read_term_from_atom(' % c\n a', T8, []), T8 == a)),
	check(open_block_comment, catch(read_term_from_atom('/* x', _, []), error(syntax_error(_), _), true)),
	check(open_quote, catch(read_term_from_chars("'abc", _, []), error(syntax_error(_), _), true)),
	check(rest_space, (read_term_from_chars_(T9, " ", R9), T9 == end_of_file, R9 == [])),
	check(rest_comment, (read_term_from_chars_(T10, [' ', '%'], R10), T10 == end_of_file, R10 == [])),
	check(rest_term, (read_term_from_chars_(T11, "f(X). b", R11), T11 = f(_), R11 == "b")),
	check(rest_no_end, catch(read_term_from_chars_(_, "a", _), error(syntax_error(_), _), true)),
	check(rest_no_end_list, catch(read_term_from_chars_(_, [a], _), error(syntax_error(_), _), true)).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
