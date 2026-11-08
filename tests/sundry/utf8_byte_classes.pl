% Trimming and the regex classes \s, \w and \d passed a plain char to isspace() and friends, undefined for a
% UTF-8 byte (negative there) and on NetBSD a read before the table. A cast alone would not do: in a UTF-8 locale
% macOS takes byte A0 as a no-break space and C3 as a letter, which would trim the tail off à (C3 A0).

:- initialization(main).

t(Label, G) :- (catch(G, E, (print_message(error, E), fail)) -> true ; write(failed)), write(' '), write(Label), nl.

main :-
	t(trim_keeps_a_grave, (parse_csv_line("xà ,à", L1, [trim(true)]), L1 = [F1, F2], length(F1, 2), length(F2, 1), write(L1))),
	t(word_skips_utf8, (sre_match("\\w+", "àé1", M1, _), write(M1))),
	t(space_skips_utf8, (sre_match("\\s", "àà b", M2, R2), length(M2, 1), write(M2-R2))),
	t(digit_skips_utf8, (sre_match("\\d", "à²3", M3, _), write(M3))).
