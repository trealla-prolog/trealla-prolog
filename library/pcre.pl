:- module(pcre, [
	re_match/2,
	re_match/3,
	re_matchsub/3,
	re_matchsub/4,
	re_foldl/6,
	re_split/3,
	re_split/4,
	re_replace/4,
	re_replace/5,
	re_compile/3,
	re_flush/0
	]).

% Perl-compatible regular expressions over PCRE2 (https://www.pcre.org), with SWI-Prolog's
% library(pcre) interface, but submatches as a list of Key-Value pairs where SWI-Prolog has a dict.
%
% UBUNTU: sudo apt install libpcre2-dev
% MACOS: brew install pcre2
%
%	?- re_matchsub("(?<y>\\d+)-(?<m>\\d+)", "on 2026-11 we", Sub).
%	   Sub = [0-"2026-11",y-"2026",m-"11"].
%
% A Regex is a pattern, as an atom, string or code or character list, a Pattern/Flags with flags
% i (caseless), m (multiline), s (dotall), x (extended), a, r, t (capture types atom, range,
% term), or what re_compile/3 made. Patterns are compiled in UTF mode and kept for reuse until
% re_flush/0. Positions are character offsets.

:- use_module(library(lists)).

:- use_foreign_module('libpcre2-8.so', [
	pcre2_compile_8([cstr,ptr,uint32,-sint,-ptr,ptr], ptr),
	pcre2_code_free_8([ptr], void),
	pcre2_pattern_info_8([ptr,uint32,ptr], sint),
	pcre2_match_data_create_from_pattern_8([ptr,ptr], ptr),
	pcre2_match_data_free_8([ptr], void),
	pcre2_match_8([ptr,cstr,ptr,ptr,uint32,ptr,ptr], sint),
	pcre2_get_ovector_pointer_8([ptr], ptr),
	pcre2_get_error_message_8([sint,ptr,ptr], sint)
	]).

:- dynamic(re_cache_/3).

:- meta_predicate(re_foldl(3, +, +, ?, ?, +)).

re_match(Regex, String) :-
	re_match(Regex, String, []).

re_match(Regex, String, Options) :-
	regex_(Regex, Options, re_match/3, Re),
	subject_(String, re_match/3, Subj),
	match_options_(Options, Subj, re_match/3, Start, Opts),
	with_match_data_(Re, MD, match_(Re, Subj, Start, Opts, MD, _)).

re_matchsub(Regex, String, Sub) :-
	re_matchsub(Regex, String, Sub, []).

re_matchsub(Regex, String, Sub, Options) :-
	regex_(Regex, Options, re_matchsub/4, Re),
	subject_(String, re_matchsub/4, Subj),
	match_options_(Options, Subj, re_matchsub/4, Start, Opts),
	with_match_data_(Re, MD, match_(Re, Subj, Start, Opts, MD, Offs)),
	subs_(Re, Subj, Offs, Sub).

% Goal called on each match's Sub, left to right, as call(Goal, Sub, V0, V1)

re_foldl(Goal, Regex, String, V0, V, Options) :-
	regex_(Regex, Options, re_foldl/6, Re),
	subject_(String, re_foldl/6, Subj),
	match_options_(Options, Subj, re_foldl/6, Start, Opts),
	with_match_data_(Re, MD, all_matches_(Re, Subj, Start, Opts, MD, Matches)),
	foldl_subs_(Matches, Goal, Re, Subj, V0, V).

foldl_subs_([], _, _, _, V, V).
foldl_subs_([Offs|Ms], Goal, Re, Subj, V0, V) :-
	subs_(Re, Subj, Offs, Sub),
	call(Goal, Sub, V0, V1),
	foldl_subs_(Ms, Goal, Re, Subj, V1, V).

% Splits: the text before the first match, that match, the text up to the next, and so on, ending
% with the text after the last; strings, or atoms with flag a or capture_type(atom)

re_split(Pattern, String, Splits) :-
	re_split(Pattern, String, Splits, []).

re_split(Pattern, String, Splits, Options) :-
	output_regex_(Pattern, Options, re_split/4, Re, Type, _),
	subject_(String, re_split/4, Subj),
	match_options_(Options, Subj, re_split/4, Start, Opts),
	with_match_data_(Re, MD, all_matches_(Re, Subj, Start, Opts, MD, Matches)),
	split_(Matches, Subj, Type, 0, Splits).

split_([], Subj, Type, Here, [Last]) :-
	subj_text_(Subj, Text),
	subj_chars_(Subj, N),
	Len is N - Here,
	typed_text_(Type, Text, Here, Len, Last).
split_([[S0,E0|_]|Ms], Subj, Type, Here, [Skipped, Sep|Splits]) :-
	subj_text_(Subj, Text),
	char_at_(Subj, S0, S),
	char_at_(Subj, E0, E),
	SkipLen is S - Here,
	SepLen is E - S,
	typed_text_(Type, Text, Here, SkipLen, Skipped),
	typed_text_(Type, Text, S, SepLen, Sep),
	split_(Ms, Subj, Type, E, Splits).

% The first match of Pattern in String, or every one with flag g, replaced by With, where \N, $N
% and $Name, or \{N}, ${N} and ${Name}, stand for a group's text, and $$ and \\ for $ and \

re_replace(Pattern, With, String, New) :-
	re_replace(Pattern, With, String, New, []).

re_replace(Pattern, With, String, New, Options) :-
	output_regex_(Pattern, Options, re_replace/5, Re, Type, All),
	subject_(String, re_replace/5, Subj),
	text_codes_(With, re_replace/5, WCs),
	replacement_(WCs, Re, Parts),
	match_options_(Options, Subj, re_replace/5, Start, Opts),
	(	All == true
	->	with_match_data_(Re, MD, all_matches_(Re, Subj, Start, Opts, MD, Matches))
	;	with_match_data_(Re, MD, match_(Re, Subj, Start, Opts, MD, Offs))
	->	Matches = [Offs]
	;	Matches = []
	),
	subj_text_(Subj, Text),
	replace_(Matches, Re, Subj, Text, Parts, 0, Pieces),
	atomic_list_concat(Pieces, A),
	(Type == atom -> New = A ; atom_string(A, New)).

replace_([], _, Subj, Text, _, Here, [Last]) :-
	subj_chars_(Subj, N),
	Len is N - Here,
	sub_atom_text_(Text, Here, Len, Last).
replace_([Offs|Ms], Re, Subj, Text, Parts, Here, [Skipped|Pieces]) :-
	Offs = [S0, E0|_],
	char_at_(Subj, S0, S),
	char_at_(Subj, E0, E),
	SkipLen is S - Here,
	sub_atom_text_(Text, Here, SkipLen, Skipped),
	subs_(Re, Subj, Offs, Sub),
	fill_(Parts, Sub, Pieces, Pieces1),
	replace_(Ms, Re, Subj, Text, Parts, E, Pieces1).

fill_([], _, P, P).
fill_([lit(A)|Ps], Sub, [A|P0], P) :- fill_(Ps, Sub, P0, P).
fill_([ref(K)|Ps], Sub, [V|P0], P) :-
	(memberchk(K-V0, Sub) -> true ; V0 = ''),
	(	string(V0) -> atom_string(V, V0)
	;	atomic(V0) -> V = V0
	;	V0 = _-_ -> V = ''
	;	term_to_atom(V0, V)
	),
	fill_(Ps, Sub, P0, P).

sub_atom_text_(Text, B, L, A) :-
	(atom(Text) -> sub_atom(Text, B, L, _, A) ; sub_string(Text, B, L, _, S), atom_string(A, S)).

% The replacement text as lit(Atom) and ref(Key) parts, a name or number that has to be the
% pattern's

replacement_(Cs, Re, Parts) :-
	replacement_(Cs, Re, [], Parts).

replacement_([], _, Lit, Parts) :-
	lit_(Lit, Parts, []).
replacement_([0'$, 0'$|Cs], Re, Lit, Parts) :- !,
	replacement_(Cs, Re, [0'$|Lit], Parts).
replacement_([0'\\, 0'\\|Cs], Re, Lit, Parts) :- !,
	replacement_(Cs, Re, [0'\\|Lit], Parts).
replacement_([C|Cs0], Re, Lit, Parts) :-
	(C == 0'$ ; C == 0'\\),
	reference_(Cs0, Cs, Name),
	!,
	ref_key_(Name, Re, Key),
	lit_(Lit, Parts, [ref(Key)|Parts1]),
	replacement_(Cs, Re, [], Parts1).
replacement_([C|Cs], Re, Lit, Parts) :-
	replacement_(Cs, Re, [C|Lit], Parts).

lit_([], P, P) :- !.
lit_(Rev, [lit(A)|P], P) :- reverse(Rev, Cs), atom_codes(A, Cs).

reference_([0'{|Cs0], Cs, Name) :- !,
	append(Name, [0'}|Cs], Cs0),
	Name \== [],
	\+ memberchk(0'}, Name).
reference_(Cs0, Cs, Name) :-
	word_(Cs0, Name, Cs),
	Name \== [].

word_([C|Cs0], [C|W], Cs) :-
	(C >= 0'a, C =< 0'z ; C >= 0'A, C =< 0'Z ; C >= 0'0, C =< 0'9 ; C == 0'_ ; C > 127),
	!,
	word_(Cs0, W, Cs).
word_(Cs, [], Cs).

ref_key_(Name, re(_, Groups, _), Key) :-
	(	Name = [D|_], D >= 0'0, D =< 0'9, catch(number_codes(N, Name), _, fail), integer(N)
	->	Key = N
	;	atom_codes(K, Name), K = Key
	),
	(	Key == 0
	->	true
	;	member(g(_, Key, _), Groups)
	->	true
	;	throw(error(existence_error(re_capture, Key), re_replace/5))
	).

% A compiled regex as re(Code, Groups, Type): Groups a g(Number, Key, Type) for each group, its
% Key its name or else its number

re_compile(Pattern, Regex, Options) :-
	regex_(Pattern, Options, re_compile/3, Regex).

regex_(Regex, _, Ctx, _) :-
	var(Regex),
	!,
	throw(error(instantiation_error, Ctx)).
regex_(Re, _, _, Re) :-
	Re = re(_, _, _),
	!.
regex_(Pattern/Flags, Options, Ctx, Re) :-
	!,
	(	atom(Flags)
	->	atom_chars(Flags, Fs),
		flags_options_(Fs, FOs, Ctx),
		append(FOs, Options, Options1)
	;	throw(error(type_error(atom, Flags), Ctx))
	),
	regex_(Pattern, Options1, Ctx, Re).
regex_(Pattern, Options, Ctx, re(Code, Groups, Type)) :-
	text_atom_(Pattern, Ctx, P),
	(is_list(Options) -> true ; throw(error(type_error(list, Options), Ctx))),
	compile_mask_(Options, Mask),
	option_(capture_type(Type0), Options, string),
	(memberchk(Type0, [string, atom, range, term]) -> Type = Type0 ; throw(error(domain_error(capture_type, Type0), Ctx))),
	compiled_(P, Mask, Ctx, Code, Groups0),
	maplist(group_type_(Type), Groups0, Groups).

group_type_(Type, g(N, Key0, none), g(N, Key, T)) :-
	!,
	(	atom(Key0), sub_atom(Key0, B, 2, 0, Suffix), sub_atom(Suffix, 0, 1, _, '_'),
		sub_atom(Suffix, 1, 1, _, L), suffix_type_(L, T0)
	->	sub_atom(Key0, 0, B, _, Key), T = T0
	;	Key = Key0, T = Type
	).

suffix_type_('A', atom).
suffix_type_('I', integer).
suffix_type_('F', float).
suffix_type_('N', number).
suffix_type_('T', term).
suffix_type_('S', string).
suffix_type_('R', range).

flags_options_([], [], _).
flags_options_([F|Fs], Os, Ctx) :-
	(	flag_option_(F, O)
	->	Os = [O|Os1]
	;	Os = Os1			% g and the like are for re_replace/5
	),
	flags_options_(Fs, Os1, Ctx).

flag_option_(i, caseless(true)).
flag_option_(m, multiline(true)).
flag_option_(s, dotall(true)).
flag_option_(x, extended(true)).
flag_option_(a, capture_type(atom)).
flag_option_(r, capture_type(range)).
flag_option_(t, capture_type(term)).

% The first Name(Value) in Options, else Default

option_(O, Options, Default) :-
	functor(O, N, 1),
	functor(T, N, 1),
	(	memberchk(T, Options)
	->	O = T
	;	arg(1, O, Default)
	).

bool_option_(Name, Options) :-
	T =.. [Name, V],
	memberchk(T, Options),
	V == true.

compile_mask_(Options, Mask) :-
	findall(Bit, (compile_bit_(Name, Bit), bool_option_(Name, Options)), Bits),
	(option_(greedy(G), Options, true), G == false -> Ungreedy = 0x40000 ; Ungreedy = 0),
	foldl_or_(Bits, 0x80000 \/ Ungreedy, Mask).	% PCRE2_UTF

compile_bit_(caseless, 0x8).
compile_bit_(dollar_endonly, 0x10).
compile_bit_(dotall, 0x20).
compile_bit_(dupnames, 0x40).
compile_bit_(extended, 0x80).
compile_bit_(firstline, 0x100).
compile_bit_(multiline, 0x400).
compile_bit_(no_auto_capture, 0x2000).
compile_bit_(ucp, 0x20000).
compile_bit_(ungreedy, 0x40000).

foldl_or_([], M, M0) :- M0 is M.
foldl_or_([B|Bs], M0, M) :- M1 is M0 \/ B, foldl_or_(Bs, M1, M).

% The code for P under Mask, compiled once and kept, and its groups as g(N, Key, none)

compiled_(P, Mask, _, Code, Groups) :-
	re_cache_(P-Mask, Code, Groups),
	!.
compiled_(P, Mask, Ctx, Code, Groups) :-
	pcre2_compile_8(P, -1, Mask, Err, Off, 0, Code0),		% PCRE2_ZERO_TERMINATED
	(	Code0 =:= 0
	->	error_message_(Err, Msg),
		throw(error(syntax_error(Msg), context(Ctx, Off)))
	;	groups_(Code0, Groups0),
		assertz(re_cache_(P-Mask, Code0, Groups0)),
		Code = Code0,
		Groups = Groups0
	).

re_flush :-
	forall(retract(re_cache_(_, Code, _)), pcre2_code_free_8(Code)).

error_message_(Err, Msg) :-
	length(Zs, 256),
	maplist(=(0), Zs),
	setup_call_cleanup(
		'$array_to_pointer'(uint8, Zs, Buf),
		(	pcre2_get_error_message_8(Err, Buf, 256, _),
			'$pointer_to_array'(uint8, Buf, 256, Bytes)
		),
		'$free_struct_pointer'(Buf)),
	append(Cs, [0|_], Bytes),
	!,
	utf8_codes_(Cs, Codes),
	atom_codes(Msg, Codes).

% Groups 1..CaptureCount, named from the pattern's name table: two bytes of group number, high
% first, then the name, zero-ended, in entries of NameEntrySize bytes

groups_(Code, Groups) :-
	setup_call_cleanup(
		'$array_to_pointer'(ptr, [0], Buf),
		(	info_(Code, Buf, 4, uint32, Count),			% PCRE2_INFO_CAPTURECOUNT
			info_(Code, Buf, 17, uint32, NameCount),		% PCRE2_INFO_NAMECOUNT
			info_(Code, Buf, 18, uint32, EntrySize),		% PCRE2_INFO_NAMEENTRYSIZE
			info_(Code, Buf, 19, ptr, Table),			% PCRE2_INFO_NAMETABLE
			(	NameCount > 0
			->	Len is NameCount * EntrySize,
				'$pointer_to_array'(uint8, Table, Len, Bytes),
				names_(NameCount, EntrySize, Bytes, Names)
			;	Names = []
			)
		),
		'$free_struct_pointer'(Buf)),
	numlist_(1, Count, Ns),
	findall(g(N, Key, none), (member(N, Ns), (memberchk(N-Key, Names) -> true ; Key = N)), Groups).

info_(Code, Buf, What, Type, V) :-
	pcre2_pattern_info_8(Code, What, Buf, _),
	'$pointer_to_array'(Type, Buf, 1, [V]).

names_(0, _, _, []) :- !.
names_(K, Size, Bytes, [N-Name|Names]) :-
	length(Entry, Size),
	append(Entry, Rest, Bytes),
	Entry = [Hi, Lo|NameBytes],
	N is Hi * 256 + Lo,
	(append(Cs, [0|_], NameBytes) -> true ; Cs = NameBytes),
	utf8_codes_(Cs, Codes),
	atom_codes(Name, Codes),
	K1 is K - 1,
	names_(K1, Size, Rest, Names).

numlist_(L, H, []) :- L > H, !.
numlist_(L, H, [L|T]) :- L1 is L + 1, numlist_(L1, H, T).

utf8_codes_([], []).
utf8_codes_([B|Bs], [C|Cs]) :-
	(	B < 0x80 -> C = B, Bs1 = Bs
	;	B < 0xE0 -> Bs = [B1|Bs1], C is (B /\ 0x1F) << 6 \/ (B1 /\ 0x3F)
	;	B < 0xF0 -> Bs = [B1, B2|Bs1], C is (B /\ 0x0F) << 12 \/ (B1 /\ 0x3F) << 6 \/ (B2 /\ 0x3F)
	;	Bs = [B1, B2, B3|Bs1], C is (B /\ 0x07) << 18 \/ (B1 /\ 0x3F) << 12 \/ (B2 /\ 0x3F) << 6 \/ (B3 /\ 0x3F)
	),
	utf8_codes_(Bs1, Cs).

% re_split/4 and re_replace/5 take their output type from flag a or s, or capture_type(atom), and
% re_replace/5 its g flag for every match, and leave other flags to the pattern

output_regex_(Pattern/Flags, Options, Ctx, Re, Type, All) :-
	!,
	(atom(Flags) -> true ; throw(error(type_error(atom, Flags), Ctx))),
	atom_chars(Flags, Fs),
	(memberchk(g, Fs) -> All = true ; All = false),
	(	member(F, Fs), (F == a ; F == s)
	->	(F == a -> Type = atom ; Type = string)
	;	option_(capture_type(T), Options, string), (T == atom -> Type = atom ; Type = string)
	),
	findall(F, (member(F, Fs), \+ memberchk(F, [g, a, s, r, t])), Fs1),
	atom_chars(Flags1, Fs1),
	regex_(Pattern/Flags1, Options, Ctx, Re).
output_regex_(Pattern, Options, Ctx, Re, Type, false) :-
	option_(capture_type(T), Options, string),
	(T == atom -> Type = atom ; Type = string),
	regex_(Pattern, Options, Ctx, Re).

% subj(Text, Bytes, Chars, Map): the subject as an atom or string, its length in bytes and in
% characters, and ascii or m(ByteToChar, CharToByte) for the offsets of a non-ASCII one

subject_(String, Ctx, subj(Text, Bytes, Chars, Map)) :-
	(	String == [] -> Text = ''
	;	atom(String) -> Text = String
	;	string(String) -> Text = String
	;	text_codes_(String, Ctx, Cs0), string_codes(Text, Cs0)
	),
	(atom(Text) -> atom_codes(Text, Cs) ; string_codes(Text, Cs)),
	length(Cs, Chars),
	(	ascii_(Cs)
	->	Bytes = Chars, Map = ascii
	;	offsets_(Cs, 0, 0, BL, CL, Bytes),
		B2C =.. [b|BL],
		C2B =.. [c|CL],
		Map = m(B2C, C2B)
	).

ascii_([]).
ascii_([C|Cs]) :- C < 0x80, ascii_(Cs).

offsets_([], I, B, [I], [B], B).
offsets_([C|Cs], I, B, BL, [B|CL], Bytes) :-
	utf8_len_(C, L),
	Pad is L - 1,
	length(Fill, Pad), maplist(=(-1), Fill),
	append([I|Fill], BL1, BL),
	I1 is I + 1,
	B1 is B + L,
	offsets_(Cs, I1, B1, BL1, CL, Bytes).

utf8_len_(C, L) :- (C < 0x80 -> L = 1 ; C < 0x800 -> L = 2 ; C < 0x10000 -> L = 3 ; L = 4).

subj_text_(subj(Text, _, _, _), Text).
subj_chars_(subj(_, _, Chars, _), Chars).

char_at_(subj(_, _, _, ascii), B, B) :- !.
char_at_(subj(_, _, _, m(B2C, _)), B, C) :- B1 is B + 1, arg(B1, B2C, C).

byte_at_(subj(_, _, _, ascii), C, C) :- !.
byte_at_(subj(_, _, _, m(_, C2B)), C, B) :- C1 is C + 1, arg(C1, C2B, B).

text_atom_(T, Ctx, _) :- var(T), !, throw(error(instantiation_error, Ctx)).
text_atom_([], _, '') :- !.
text_atom_(T, _, T) :- atom(T), !.
text_atom_(T, _, A) :- string(T), !, atom_string(A, T).
text_atom_(T, Ctx, A) :- text_codes_(T, Ctx, Cs), atom_codes(A, Cs).

text_codes_(T, Ctx, _) :- var(T), !, throw(error(instantiation_error, Ctx)).
text_codes_([], _, []) :- !.
text_codes_(T, _, Cs) :- atom(T), !, atom_codes(T, Cs).
text_codes_(T, _, Cs) :- string(T), !, string_codes(T, Cs).
text_codes_([C|Cs], Ctx, Codes) :-
	is_list(Cs),
	!,
	(	integer(C)
	->	Codes = [C|Cs]
	;	catch(atom_chars(A, [C|Cs]), _, throw(error(type_error(text, [C|Cs]), Ctx))),
		atom_codes(A, Codes)
	).
text_codes_(T, Ctx, _) :- throw(error(type_error(text, T), Ctx)).

% The match-time options: start(From), a character offset, and anchored, bol, eol, empty and
% empty_atstart

match_options_(Options, Subj, Ctx, Start, Opts) :-
	(is_list(Options) -> true ; throw(error(type_error(list, Options), Ctx))),
	option_(start(From), Options, 0),
	subj_chars_(Subj, N),
	(	integer(From), From >= 0, From =< N
	->	byte_at_(Subj, From, Start)
	;	throw(error(domain_error(re_start, From), Ctx))
	),
	findall(Bit, (match_bit_(O, Bit), memberchk(O, Options)), Bits),
	foldl_or_(Bits, 0, Opts).

match_bit_(anchored(true), 0x80000000).
match_bit_(bol(false), 0x1).
match_bit_(eol(false), 0x2).
match_bit_(empty(false), 0x4).
match_bit_(empty_atstart(false), 0x8).

with_match_data_(re(Code, _, _), MD, Goal) :-
	setup_call_cleanup(
		pcre2_match_data_create_from_pattern_8(Code, 0, MD),
		Goal,
		pcre2_match_data_free_8(MD)).

% Offs, the byte offsets of group 0, 1, ... in pairs, a group that took no part as -1, for the
% match at or after byte Start

match_(re(Code, _, _), subj(Text, Bytes, _, _), Start, Opts, MD, Offs) :-
	pcre2_match_8(Code, Text, Bytes, Start, Opts, MD, 0, Rc),
	(	Rc > 0
	->	pcre2_get_ovector_pointer_8(MD, OV),
		N is 2 * Rc,
		'$pointer_to_array'(ptr, OV, N, Offs0),
		maplist(unset_(Bytes), Offs0, Offs)
	;	Rc =:= -1							% PCRE2_ERROR_NOMATCH
	->	fail
	;	error_message_(Rc, Msg),
		throw(error(pcre_error(Rc, Msg), _))
	).

unset_(Bytes, O0, O) :- ((O0 < 0 ; O0 > Bytes) -> O = -1 ; O = O0).

% Every match from Start on, as Perl finds them: after an empty one, a non-empty one at the same
% place, else the next match a character on

all_matches_(Re, Subj, Start, Opts, MD, Matches) :-
	(	match_(Re, Subj, Start, Opts, MD, Offs)
	->	Matches = [Offs|Ms],
		Offs = [S, E|_],
		(	E > S
		->	all_matches_(Re, Subj, E, Opts, MD, Ms)
		;	after_empty_(Re, Subj, E, Opts, MD, Ms)
		)
	;	Matches = []
	).

after_empty_(_, subj(_, Bytes, _, _), E, _, _, []) :-
	E >= Bytes,
	!.
after_empty_(Re, Subj, E, Opts, MD, Ms) :-
	Opts1 is Opts \/ 0x80000000 \/ 0x8,				% PCRE2_ANCHORED, PCRE2_NOTEMPTY_ATSTART
	(	match_(Re, Subj, E, Opts1, MD, Offs)
	->	Ms = [Offs|Ms1],
		Offs = [_, E1|_],
		all_matches_(Re, Subj, E1, Opts, MD, Ms1)
	;	char_at_(Subj, E, C),
		C1 is C + 1,
		byte_at_(Subj, C1, Next),
		all_matches_(Re, Subj, Next, Opts, MD, Ms)
	).

% Sub: 0-Whole and Key-Value for each group that took part, its value as its type has it

subs_(re(_, Groups, Type), Subj, [S0, E0|Offs], [0-V0|Subs]) :-
	value_(Type, Subj, S0, E0, V0),
	group_subs_(Groups, Offs, Subj, Subs).

group_subs_([], _, _, []).
group_subs_([g(_, Key, Type)|Gs], Offs, Subj, Subs) :-
	(	Offs = [S, E|Offs1]
	->	(	S >= 0
		->	value_(Type, Subj, S, E, V),
			Subs = [Key-V|Subs1]
		;	Subs = Subs1
		),
		group_subs_(Gs, Offs1, Subj, Subs1)
	;	Subs = []
	).

value_(Type, Subj, S0, E0, V) :-
	char_at_(Subj, S0, S),
	char_at_(Subj, E0, E),
	Len is E - S,
	(	Type == range
	->	V = S-Len
	;	subj_text_(Subj, Text),
		typed_text_(Type, Text, S, Len, V)
	).

typed_text_(Type, Text, S, Len, V) :-
	(atom(Text) -> sub_atom(Text, S, Len, _, A), atom_string(A, Str) ; sub_string(Text, S, Len, _, Str)),
	typed_(Type, Str, V).

typed_(string, S, S).
typed_(atom, S, A) :- atom_string(A, S).
typed_(integer, S, V) :- string_number_(S, V), integer(V).
typed_(float, S, V) :- string_number_(S, N), V is float(N).
typed_(number, S, V) :- string_number_(S, V).
typed_(term, S, V) :- atom_string(A, S), term_to_atom(V, A).

string_number_(S, N) :- string_codes(S, Cs), catch(number_codes(N, Cs), _, fail).
