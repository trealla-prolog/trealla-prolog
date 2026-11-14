% The sre_*_all loops dropped a match that ended the text, and re_matchp() kept a stale length after a failed try.

:- initialization(main).

tmpfile('tmp.sre_end_of_text.txt').

t(Label, Got, Expected) :-
	(	Got == Expected -> write(ok) ; write(got(Got)) ),
	write(' '), write(Label), nl.

main :-
	sre_match_all("b", "ab", L1), t(match_all_last, L1, ["b"]),
	sre_match_all("\\S", "ab", L2), t(match_all_each, L2, ["a","b"]),
	sre_match_all("\\S", "Needle In A Haystack", L3), last(L3, K), t(match_all_final_k, K, "k"),
	sre_match_all("ab", "ab", L4), t(match_all_whole, L4, ["ab"]),
	sre_match_all("z", "ab", L5), t(match_all_none, L5, []),
	sre_match_all_pos("d.f", "xdafydbfzdcf", P1), t(match_all_pos_offsets, P1, [1-3,5-3,9-3]),
	sre_match_all_pos("b", "ab", P2), t(match_all_pos_last, P2, [1-1]),
	sre_subst_all("b", "ab", "X", S1), t(subst_all_last, S1, "aX"),
	sre_subst_all("d.f", "xdafydbfzdcf", "$", S2), t(subst_all_three, S2, "x$y$z$"),
	sre_subst_all("c", "cca", "#", S3), t(subst_all_at_start, S3, "##a"),
	sre_subst_all("z", "ab", "X", S4), t(subst_all_none, S4, "ab"),
	sre_subst("a", "ab", Pre, Rest), t(subst_empty_prefix, Pre-Rest, []-"b"),
	sre_match("b$", "abb", M1, R1), t(dollar_after_failed_try, M1-R1, "b"-[]),
	sre_match("b$", "abba", M2, R2), t(dollar_no_match, M2-R2, []-[]),
	sre_match("ab*c", "axac", M3, R3), t(star_after_failed_try, M3-R3, "ac"-[]),
	sre_match_all("a*", "bab", Z1), t(empty_match_ends_scan, Z1, []),
	sre_subst_all("a*", "bab", "X", Z2), t(empty_match_ends_subst, Z2, "bab"),
	tmpfile(F),
	setup_call_cleanup(open(F, write, W), write(W, xdafydbf), close(W)),
	sre_match_all_in_file("d.f", F, FL), t(in_file_last, FL, ["daf","dbf"]),
	sre_match_all_pos_in_file("d.f", F, FP), t(pos_in_file_last, FP, [1-3,5-3]),
	sre_subst_all_in_file("d.f", F, "$", FS), t(subst_in_file_last, FS, "x$y$"),
	delete_file(F).
