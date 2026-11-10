% library(unicode): normalisation, case folding, mapping options, properties and grapheme clusters over utf8proc.
% Run by ../unicode_bindings.sh, which skips it where libutf8proc or the FFI is missing. Text is shown as code lists.

:- use_module(library(unicode)).
:- initialization(main).

codes(T, Cs) :- atom(T), !, atom_codes(T, Cs).
codes(T, Cs) :- string(T), !, atom_string(A, T), atom_codes(A, Cs).
codes(T, T).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (var(E) -> format("~w: ~q~n", [Label, Show]) ; format("~w: raised ~q~n", [Label, E]))
	;   format("~w: failed~n", [Label])
	).

main :-
	atom_codes(Cafe, [0'c, 0'a, 0'f, 0'e, 0x301]),
	t(nfc, (unicode_nfc(Cafe, A1), codes(A1, C1)), C1),
	t(nfd, (unicode_nfd('caf\xe9\', A2), codes(A2, C2)), C2),
	t(nfkd, (unicode_nfkd('\xfb03\', A3), codes(A3, C3)), C3),
	t(nfkc, (unicode_nfkc('x\x2075\', A4), codes(A4, C4)), C4),
	t(nfkc_casefold, (unicode_nfkc_casefold('Stra\xdf\e', A5), codes(A5, C5)), C5),
	t(casefold, (unicode_casefold('\xc0\\xc9\', A6), codes(A6, C6)), C6),
	t(stripmark, (unicode_map('cr\xe8\me', A7, [decompose, stripmark]), codes(A7, C7)), C7),
	t(lump, (unicode_map('a\x2014\b', A8, [lump]), codes(A8, C8)), C8),
	t(mask, unicode_map(abc, A9, 10), A9),
	t(from_string, unicode_map("abc", A10, [compose]), A10),
	t(from_codes, unicode_map([0'a, 0'b], A11, [compose]), A11),
	t(from_chars, unicode_map([a, b], A12, [compose]), A12),
	t(empty, unicode_map('', A13, [compose]), A13),
	t(bad_option, unicode_map(abc, _, [bogus]), none),
	t(charbound, unicode_map(abc, _, [charbound]), none),
	t(not_list, unicode_map(abc, _, foo), none),
	t(not_text, unicode_map(123, _, []), none),
	t(unbound, unicode_map(_, _, []), none),
	t(version, (unicode_version(V), atom(V)), yes),
	t(valid, findall(C, (member(C, [0'a, 0xD800, 0x378, 0xE000, 0x110000, -1]), unicode_codepoint_valid(C)), Vs), Vs),
	t(properties_A, findall(P, (unicode_property(0'A, P), P \= ambiguous_width(_)), Ps1), Ps1),
	t(properties_cjk, findall(P, (unicode_property(0x4E2D, P), P \= ambiguous_width(_)), Ps2), Ps2),
	t(category_initial, findall(K, (member(K, ['L', 'N', 'Lu', 'Ll']), unicode_property(0'A, category(K))), Ks), Ks),
	t(char_code, unicode_property(a, uppercase(U)), U),
	t(no_mapping, unicode_property(0'A, uppercase(_)), none),
	t(unassigned, unicode_property(0x378, category(_)), none),
	t(enumerate, findall(C, unicode_property(C, category('Zl')), Zl), Zl),
	t(bad_property, unicode_property(0'a, bogus(x)), none),
	t(not_compound, unicode_property(0'a, foo), none),
	t(bad_code, unicode_property(-1, category(_)), none),
	t(two_chars, unicode_property(ab, category(_)), none),
	t(graphemes_combining, (atom_graphemes(Cafe, G1), maplist(codes, G1, GC1)), GC1),
	t(graphemes_flag, (atom_graphemes('a\x1F1F3\\x1F1F1\b', G2), maplist(codes, G2, GC2)), GC2),
	t(graphemes_zwj, (atom_codes(Fam, [0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467]), atom_graphemes(Fam, G3), length(G3, N3)), N3),
	t(graphemes_crlf, (atom_codes(CrLf, [0'a, 13, 10, 0'b]), atom_graphemes(CrLf, G4), maplist(codes, G4, GC4)), GC4),
	t(graphemes_join, atom_graphemes(J, [a, b, c]), J),
	t(graphemes_string, (string_graphemes("ab", G5), maplist(string, G5)), yes),
	t(graphemes_string_join, (string_graphemes(S6, ["a", "b"]), string(S6), codes(S6, C6b)), C6b),
	t(graphemes_empty, atom_graphemes('', G7), G7),
	t(graphemes_check, atom_graphemes(abc, [a, b, c]), yes),
	t(graphemes_mismatch, atom_graphemes(abc, [ab, c]), none),
	t(graphemes_unbound, atom_graphemes(_, _), none).
