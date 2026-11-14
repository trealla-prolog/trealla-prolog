% The sre_* builtins read the text "" (the atom []) as "[]", and a UTF-8 byte in a literal never matched as char is signed.

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
	sre_subst_all("é", "éaé", "e", S1), t(utf8_subst_all, S1, "eae").
