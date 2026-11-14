% The sre_* builtins read "" (the atom []) as "[]", a UTF-8 literal never matched, and . or a class split a character.

:- initialization(main).

t(Label, Got, Expected) :-
	(	Got == Expected -> write(ok) ; write(got(Got)) ),
	write(' '), write(Label), nl.

main :-
	sre_match(".", "", M1, R1), t(match_empty, M1-R1, []-[]),
	sre_compile(".", Re), sre_matchp(Re, "", M2, R2), t(matchp_empty, M2-R2, []-[]),
	sre_subst(".", "", P1, R3), t(subst_empty, P1-R3, []-[]),
	sre_substp(Re, "", P2, R4), t(substp_empty, P2-R4, []-[]),
	sre_match("x", '', M3, R5), t(match_empty_atom, M3-R5, []-[]),
	sre_subst("x", '', P3, R6), t(subst_empty_atom, P3-R6, []-[]),
	sre_match_all(".", "", L1), t(match_all_empty, L1, []),
	sre_match("é", "aéb", M4, R7), t(utf8_literal, M4-R7, "é"-"b"),
	sre_match("ü\\s", "aü b", M5, R8), t(utf8_literal_then_class, M5-R8, "ü "-"b"),
	sre_match_all("é", "éaé", L2), t(utf8_match_all, L2, ["é","é"]),
	sre_match_all_pos("é", "éaé", L3), t(utf8_char_offsets, L3, [0-1,2-1]),
	sre_subst_all("é", "éaé", "e", S1), t(utf8_subst_all, S1, "eae"),
	sre_match_all(".", "éaü", L4), t(utf8_dot, L4, ["é","a","ü"]),
	sre_match_all_pos(".", "éa", L5), t(utf8_dot_offsets, L5, [0-1,1-1]),
	sre_match_all("\\S", "é a", L6), t(utf8_not_space, L6, ["é","a"]),
	sre_match_all("[^a]", "éaü", L7), t(utf8_negated_class, L7, ["é","ü"]),
	sre_match("\\W+", "aé€b", M6, R9), t(utf8_not_word, M6-R9, "é€"-"b"),
	sre_match("aé+", "xaééb", M7, R10), t(utf8_literal_plus, M7-R10, "aéé"-"b"),
	sre_match("[à-ü]+", "xaéüÿ", M8, R11), t(utf8_range, M8-R11, "éü"-"ÿ"),
	sre_match("[é€]+", "aé€€é", M9, R12), t(utf8_class_members, M9-R12, "é€€é"-[]),
	sre_match(".+b", "€€€b", M10, R13), t(utf8_backtrack, M10-R13, "€€€b"-[]),
	sre_match_all_pos("😀", "a😀b😀", L8), t(utf8_four_byte, L8, [1-1,3-1]),
	sre_subst_all(".", "é€", "x", S2), t(utf8_subst_dot, S2, "xx").
