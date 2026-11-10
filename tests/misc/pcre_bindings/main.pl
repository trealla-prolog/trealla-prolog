% library(pcre): matching, submatches and their types, folding, splitting and replacing over PCRE2, with
% character offsets in non-ASCII text. Run by ../pcre_bindings.sh, which skips it where libpcre2-8 or the FFI
% is missing. Strings are shown as atoms.

:- use_module(library(pcre)).
:- initialization(main).

show(S, A) :- string(S), !, atom_string(A, S).
show(T, A) :- compound(T), !, T =.. [F|As], maplist(show, As, Bs), A =.. [F|Bs].
show(T, T).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (var(E) -> show(Show, S), format("~w: ~q~n", [Label, S]) ; format("~w: raised ~q~n", [Label, E]))
	;   format("~w: failed~n", [Label])
	).

count(_, N0, N) :- N is N0 + 1.
whole(Sub, L0, [W|L0]) :- memberchk(0-W, Sub).

main :-
	t(match_caseless, re_match("^needle"/i, "Needle in a haystack"), yes),
	t(no_match, re_match("^needle", "Needle in a haystack"), yes),
	t(named, re_matchsub("(?<y>\\d+)-(?<m>\\d+)", "on 2026-11 we", S1), S1),
	t(numbered, re_matchsub("(\\w+)@(\\w+)", "mail me@here now", S2), S2),
	t(unset_group, re_matchsub("a(x)?b", "ab", S3), S3),
	t(typed_names, re_matchsub("(?<date> (?<year_I>(?:\\d\\d)?\\d\\d) - (?<month_I>\\d\\d) - (?<day_I>\\d\\d) )"/x,
		"2017-04-20", S4, []), S4),
	t(atom_type, re_matchsub("b+"/a, "abbbc", S5), S5),
	t(range_type, re_matchsub("b+"/r, "abbbc", S6), S6),
	t(term_type, re_matchsub("f\\(\\d+\\)"/t, "x f(3) y", S7), S7),
	t(range_non_ascii, re_matchsub("\x00e9\+"/r, "caf\x00e9\\x00e9\!", S8), S8),
	t(start_option, re_matchsub("a"/r, "banana", S9, [start(2)]), S9),
	t(dotall, re_match("a.b"/s, "a\nb"), yes),
	t(multiline, re_matchsub("^b"/m, "a\nb", S10), S10),
	t(codes_and_chars, (re_match("bc", [0'a, 0'b, 0'c]), re_match([b, c], "abc")), yes),
	t(compiled, (re_compile("x+", Re, []), re_match(Re, "axxb")), yes),
	t(foldl_count, re_foldl(count, "a", "aap", 0, N11, []), N11),
	t(foldl_collect, re_foldl(whole, "\\d+", "1 22 333", [], L12, []), L12),
	t(split, re_split("\\s*,\\s*", "a , b,c", L13), L13),
	t(split_atoms, re_split(","/a, "x,y", L14), L14),
	t(split_no_match, re_split(",", "xyz", L15), L15),
	t(split_empty_pattern, re_split("", "abc", L16), L16),
	t(replace_first, re_replace("o", "0", "foo boo", R17), R17),
	t(replace_all, re_replace("o"/g, "0", "foo boo", R18), R18),
	t(replace_refs, re_replace("(\\w+)@(\\w+)"/g, "\\2 at $1", "a@b and c@d", R19), R19),
	t(replace_named, re_replace("(?<date> (?<year_I>(?:\\d\\d)?\\d\\d) - (?<month_I>\\d\\d) - (?<day_I>\\d\\d) )"/x,
		"$month-$day-$year", "on 2017-04-20", R20), R20),
	t(replace_escapes, re_replace("."/g, "$$\\\\", "ab", R21), R21),
	t(replace_braces, re_replace("(a)(b)", "${2}\\{1}x", "abc", R22), R22),
	t(replace_atom, re_replace("b"/ga, "B", "abba", R23), R23),
	t(replace_non_ascii, re_replace("\x00e9\"/g, "e", "\x00e9\clair caf\x00e9\", R24), R24),
	t(replace_unknown_group, re_replace("(a)", "$2", "abc", _), none),
	t(syntax_error, re_match("a(", "a"), none),
	t(unbound, re_match(_, "a"), none),
	t(bad_start, re_match("a", "abc", [start(9)]), none),
	t(flush, (re_flush, re_match("a", "a")), yes).
