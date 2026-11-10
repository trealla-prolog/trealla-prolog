:- module(unicode, [
	unicode_property/2,
	unicode_map/3,
	unicode_nfd/2,
	unicode_nfc/2,
	unicode_nfkd/2,
	unicode_nfkc/2,
	unicode_nfkc_casefold/2,
	unicode_casefold/2,
	unicode_version/1,
	unicode_codepoint_valid/1,
	atom_graphemes/2,
	string_graphemes/2
	]).

% Unicode normalisation, case folding, character properties and grapheme clusters, over utf8proc
% (https://github.com/JuliaStrings/utf8proc), with SWI-Prolog's library(unicode) interface.
%
% UBUNTU: sudo apt install libutf8proc-dev
% MACOS: brew install utf8proc
%
% unicode_property/2 knows category, width, ambiguous_width, uppercase, lowercase and titlecase:
% utf8proc keeps the rest in C bitfields, which the FFI can't read.

:- use_module(library(lists)).

:- use_foreign_module('libutf8proc.so', [
	utf8proc_version([], ccstr),
	utf8proc_unicode_version([], ccstr),
	utf8proc_map([cstr,ptr,-cstr,sint], ptr),
	utf8proc_codepoint_valid([sint32], bool),
	utf8proc_category([sint32], sint),
	utf8proc_category_string([sint32], ccstr),
	utf8proc_charwidth([sint32], sint),
	utf8proc_charwidth_ambiguous([sint32], bool),
	utf8proc_tolower([sint32], sint32),
	utf8proc_toupper([sint32], sint32),
	utf8proc_totitle([sint32], sint32),
	utf8proc_grapheme_break_stateful([sint32,sint32,ptr], bool)
	]).

:- foreign_struct(unicode_break_state_, [sint32]).

unicode_nfd(In, Out) :- unicode_map(In, Out, [stable,decompose]).
unicode_nfc(In, Out) :- unicode_map(In, Out, [stable,compose]).
unicode_nfkd(In, Out) :- unicode_map(In, Out, [stable,decompose,compat]).
unicode_nfkc(In, Out) :- unicode_map(In, Out, [stable,compose,compat]).
unicode_nfkc_casefold(In, Out) :- unicode_map(In, Out, [stable,compose,compat,casefold]).
unicode_casefold(In, Out) :- unicode_map(In, Out, [stable,casefold]).

% Out is the atom utf8proc_map() makes of the text In, as Options say: a list of stable, compat,
% compose, decompose, ignore, rejectna, nlf2ls, nlf2ps, nlf2lf, stripcc, casefold, lump and
% stripmark, or the integer mask they make. Not charbound, whose 0xFF marks are no UTF-8: see
% atom_graphemes/2.

unicode_map(In, Out, Options) :-
	map_mask_(Options, Mask),
	text_atom_(In, A, unicode_map/3),
	(	A == ''
	->	Out = ''
	;	Mask1 is Mask \/ 1,			% UTF8PROC_NULLTERM
		utf8proc_map(A, 0, Out0, Mask1, Len),
		(	Len >= 0
		->	Out = Out0
		;	map_error_(Len, In, Options)
		)
	).

map_mask_(Options, _) :-
	var(Options),
	!,
	throw(error(instantiation_error, unicode_map/3)).
map_mask_(Mask, Mask) :-
	integer(Mask),
	!,
	(	Mask /\ 2048 =:= 0
	->	true
	;	throw(error(domain_error(unicode_mapping, charbound), unicode_map/3))
	).
map_mask_(Options, Mask) :-
	(is_list(Options) -> true ; throw(error(type_error(list, Options), unicode_map/3))),
	map_mask_(Options, 0, Mask).

map_mask_([], Mask, Mask).
map_mask_([O|Os], Mask0, Mask) :-
	(	var(O)
	->	throw(error(instantiation_error, unicode_map/3))
	;	map_option_(O, Bit)
	->	Mask1 is Mask0 \/ Bit,
		map_mask_(Os, Mask1, Mask)
	;	throw(error(domain_error(unicode_mapping, O), unicode_map/3))
	).

map_option_(stable, 2).
map_option_(compat, 4).
map_option_(compose, 8).
map_option_(decompose, 16).
map_option_(ignore, 32).
map_option_(rejectna, 64).
map_option_(nlf2ls, 128).
map_option_(nlf2ps, 256).
map_option_(nlf2lf, 384).
map_option_(stripcc, 512).
map_option_(casefold, 1024).
map_option_(lump, 4096).
map_option_(stripmark, 8192).

map_error_(-1, _, _) :- !, throw(error(resource_error(memory), unicode_map/3)).
map_error_(-2, _, _) :- !, throw(error(resource_error(string_length), unicode_map/3)).
map_error_(-5, _, Options) :- !, throw(error(domain_error(unicode_map_options, Options), unicode_map/3)).
map_error_(_, In, _) :- throw(error(domain_error(unicode_string, In), unicode_map/3)).

% The atom for text given as an atom, a string, or a list of codes or characters

text_atom_(T, _, Ctx) :- var(T), !, throw(error(instantiation_error, Ctx)).
text_atom_([], '', _) :- !.
text_atom_(T, T, _) :- atom(T), !.
text_atom_(T, A, _) :- string(T), !, atom_string(A, T).
text_atom_([C|Cs], A, Ctx) :-
	is_list(Cs),
	!,
	(	integer(C)
	->	catch(atom_codes(A, [C|Cs]), _, throw(error(type_error(text, [C|Cs]), Ctx)))
	;	catch(atom_chars(A, [C|Cs]), _, throw(error(type_error(text, [C|Cs]), Ctx)))
	).
text_atom_(T, _, Ctx) :- throw(error(type_error(text, T), Ctx)).

unicode_version(V) :-
	utf8proc_unicode_version(V).

% A code point with a character assigned to it: not a surrogate, not unassigned

unicode_codepoint_valid(C) :-
	integer(C),
	C >= 0,
	C =< 0x10FFFF,
	utf8proc_codepoint_valid(C, 1),
	utf8proc_category(C, Cat),
	Cat =\= 0.				% UTF8PROC_CATEGORY_CN

% Code, an integer code point or a one-character atom, has Property: category(Cat), width(W),
% ambiguous_width(Bool), uppercase(Code), lowercase(Code) or titlecase(Code). A category may be
% asked for by its initial alone, as category('L'). An unassigned code point has none.

unicode_property(Code, Property) :-
	nonvar(Code),
	nonvar(Property),
	!,
	code_(Code, C),
	property_check_(Property),
	property_(C, Property).
unicode_property(Code, Property) :-
	nonvar(Code),
	!,
	code_(Code, C),
	property_name_(Property),
	property_(C, Property).
unicode_property(Code, Property) :-
	(nonvar(Property) -> property_check_(Property) ; true),
	between(0, 0x10FFFF, Code),
	property_name_(Property),
	property_(Code, Property).

code_(Code, C) :-
	(	integer(Code)
	->	C = Code
	;	atom(Code), atom_length(Code, 1)
	->	char_code(Code, C)
	;	throw(error(type_error(code, Code), unicode_property/2))
	),
	(	C >= 0, C =< 0x10FFFF
	->	true
	;	throw(error(domain_error(code, Code), unicode_property/2))
	).

property_check_(P) :-
	(	compound(P), functor(P, N, 1), functor(T, N, 1), property_name_(T)
	->	true
	;	compound(P)
	->	throw(error(domain_error(unicode_property, P), unicode_property/2))
	;	throw(error(type_error(compound, P), unicode_property/2))
	).

property_name_(category(_)).
property_name_(width(_)).
property_name_(ambiguous_width(_)).
property_name_(uppercase(_)).
property_name_(lowercase(_)).
property_name_(titlecase(_)).

property_(C, P) :-
	utf8proc_category(C, Cat),
	Cat =\= 0,
	property_(P, C, Cat).

property_(category(Name), C, _) :-
	utf8proc_category_string(C, S),
	(	atom(Name), atom_length(Name, 1)
	->	sub_atom(S, 0, 1, _, Name)
	;	Name = S
	).
property_(width(W), C, _) :-
	utf8proc_charwidth(C, W).
property_(ambiguous_width(B), C, _) :-
	catch(utf8proc_charwidth_ambiguous(C, A), error(existence_error(_, _), _), fail),
	(A =:= 0 -> B = false ; B = true).
property_(uppercase(U), C, _) :-
	utf8proc_toupper(C, U), U =\= C.
property_(lowercase(L), C, _) :-
	utf8proc_tolower(C, L), L =\= C.
property_(titlecase(T), C, _) :-
	utf8proc_totitle(C, T), T =\= C.

% The grapheme clusters, user-perceived characters as UAX #29 has them, of Atom as atoms, or
% Atom made from them

atom_graphemes(Atom, Graphemes) :-
	graphemes_(Atom, Graphemes, atom, atom_graphemes/2).

string_graphemes(String, Graphemes) :-
	graphemes_(String, Graphemes, string, string_graphemes/2).

graphemes_(Text, Gs, Type, Ctx) :-
	(	nonvar(Text)
	->	text_atom_(Text, A, Ctx),
		atom_codes(A, Cs),
		clusters_(Cs, Groups),
		maplist(group_text_(Type), Groups, Gs0),
		Gs = Gs0
	;	var(Gs)
	->	throw(error(instantiation_error, Ctx))
	;	maplist(text_atom_any_(Ctx), Gs, As),
		atomic_list_concat(As, A),
		(Type == atom -> Text = A ; atom_string(A, Text))
	).

text_atom_any_(Ctx, G, A) :- text_atom_(G, A, Ctx).

group_text_(atom, Cs, A) :- atom_codes(A, Cs).
group_text_(string, Cs, S) :- atom_codes(A, Cs), atom_string(A, S).

% Code lists split where utf8proc says a grapheme break falls, its state carried in a C int

clusters_([], []) :- !.
clusters_([C|Cs], Groups) :-
	setup_call_cleanup(
		'$struct_to_pointer'([unicode_break_state_, 0], State),
		clusters_(Cs, C, [C|T], T, State, Groups),
		'$free_struct_pointer'(State)).

clusters_([], _, G, [], _, [G]).
clusters_([C|Cs], Prev, G, T, State, Groups) :-
	utf8proc_grapheme_break_stateful(Prev, C, State, Break),
	(	Break =:= 0
	->	T = [C|T1],
		clusters_(Cs, C, G, T1, State, Groups)
	;	T = [],
		Groups = [G|Groups1],
		clusters_(Cs, C, [C|T1], T1, State, Groups1)
	).
