% seeing/telling after see/tell no longer crash and told goes back to user_output;
% getlines/1 works and empty(true) stops at the first empty line; tab/2 refuses an
% input stream; numlist/3 checks a given list; hex/octal digits in either case are
% read whole; hex_bytes/2 compares in either case and still fills a list of
% unbound chars, as uuid_string/2 needs; ~0c prints nothing; format/3 on
% an unknown alias is an existence error.

:- initialization(main).

main :-
	File = 'tmp_edinburgh_fixes.txt',
	open(File, write, W), write(W, 'one\ntwo\r\n\nafter\nlast'), close(W),
	check(seeing_after_see, (see(File), seeing(S1), seen, seeing(S2), \+ atom(S1), S2 == user)),
	check(seeing_user, seeing(user)),
	check(telling_after_tell, (tell('tmp_edinburgh_out.txt'), telling(T1), told, telling(T2), \+ atom(T1), T2 == user)),
	check(told_restores_output, (tell('tmp_edinburgh_out.txt'), told, current_output(O), stream_property(O, alias(user_output)))),
	check(seeing_alias, (open(File, read, S3, [alias(edin)]), set_input(S3), seeing(A3), seen, A3 == edin)),
	check(getlines_1, (open(File, read, S4), set_input(S4), getlines(L4), close(S4), length(L4, 5))),
	check(getlines_empty, (open(File, read, S5), getlines(S5, L5, [empty(true)]), close(S5), L5 = [X5, Y5], atom_chars(A5, X5), atom_chars(B5, Y5), A5 == one, B5 == two)),
	throws(tab_input, tab(user_input, 2)),
	throws(tab_unknown, tab(nosuch, 2)),
	check(tab_zero, (with_output_to(chars(C6), (tab(0), tab(-1), tab(2))), C6 == [' ',' '])),
	check(numlist_given, numlist(1, 3, [1,2,3])),
	check(numlist_wrong, \+ numlist(1, 3, [1,2])),
	check(hex_upper, (hex_chars(N7, 'FF'), N7 == 255)),
	check(hex_lower, (hex_chars(N8, "ff"), N8 == 255)),
	check(hex_bigint, (hex_chars(18446744073709551616, H9), atom_chars(A9, H9), A9 == '10000000000000000')),
	throws(hex_partial, hex_chars(_, "1g")),
	throws(hex_empty, hex_chars(_, "")),
	throws(octal_bad, octal_chars(_, "9")),
	check(hex_bytes_upper, (atom_chars('01FF', L10), hex_bytes(L10, [1,255]))),
	check(hex_bytes_lower, (atom_chars('01ff', L11), hex_bytes(L11, [1,255]))),
	check(hex_bytes_mismatch, (atom_chars(x1, L12), catch(hex_bytes(L12, [1]), error(domain_error(hex_encoding, _), _), true))),
	check(hex_bytes_fill, (length(L14, 4), hex_bytes(L14, [1,255]), atom_chars(A14, L14), A14 == '01ff')),
	check(hex_bytes_upper_var, (hex_bytes(H15, [1,255]), atom_chars(A15, H15), A15 == '01FF')),
	check(uuid_string, (use_module(library(uuid)), uuidv4_string(U16), length(U16, 36))),
	throws(hex_bytes_odd, hex_bytes("1ff", _)),
	throws(hex_bytes_bad, hex_bytes("zz", _)),
	check(format_zero_c, (format(atom(A13), "[~0c~*c]", [0'x, 0, 0'y]), A13 == '[]')),
	throws(format_unknown, format(nosuch, "x", [])),
	delete_file(File), delete_file('tmp_edinburgh_out.txt').

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).

throws(Name, Goal) :-
	catch((Goal, format("~w did not throw~n", [Name])), error(E, _), format("~w threw ~q~n", [Name,E])).
