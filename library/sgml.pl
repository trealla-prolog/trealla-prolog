:- module(sgml, [
	load_xml/3,
	load_html/3,
	xml_xpath/3,
	xml_xpath/4,
	xml_write/3
	]).

% XML and HTML over libxml2 (https://gitlab.gnome.org/GNOME/libxml2)
%
% MACOS: part of the system
% UBUNTU: sudo apt install libxml2-dev
%
%	?- load_xml("<a href='x'>hi <b>there</b></a>", DOM, []).
%	DOM = [element(a,[href="x"],["hi ",element(b,[],["there"])])].
%
%	?- xml_xpath(file('feed.xml'), '//item/title/text()', T).
%
% A document is a list of element(Name, Attributes, Content), Attributes being Name=Value and
% Content elements, text and pi(Text), as SWI-Prolog's library(sgml) gives. Names are atoms,
% Prefix:Local as one atom as written, with xmlns declarations among the attributes. Text and
% attribute values are strings, or atoms with cdata(atom) and attribute_value(atom). Comments
% and the doctype are left out.
%
% A Source is file(File), stream(Stream), string(Text), an atom naming a file, or a string or
% chars of the document itself. External entities and DTDs are never fetched.
%
% Options of load_xml/3 and load_html/3: space(preserve), the default, or space(remove), which
% trims text and drops what is left empty; cdata(string|atom); attribute_value(string|atom).
% Errors are error(xml_error(Line, Message), _). HTML is parsed as browsers do, recovering
% from what isn't well formed.

:- use_module(library(lists)).

:- use_foreign_module('libxml2.so', [
	xmlInitParser([], void),
	xmlReadMemory([ptr, sint, ptr, cstr, sint], ptr(xml_doc)),
	xmlReadFile([cstr, ptr, sint], ptr(xml_doc)),
	htmlReadMemory([ptr, sint, ptr, cstr, sint], ptr(xml_doc)),
	htmlReadFile([cstr, ptr, sint], ptr(xml_doc)),
	xmlFreeDoc([free(xml_doc)], void),
	xmlGetLastError([], ptr),
	xmlResetLastError([], void),
	xmlSetStructuredErrorFunc([ptr, ptr], void),
	xmlXPathNewContext([ptr(xml_doc)], ptr(xpath_context)),
	xmlXPathFreeContext([free(xpath_context)], void),
	xmlXPathRegisterNs([ptr(xpath_context), cstr, cstr], sint),
	xmlXPathEvalExpression([cstr, ptr(xpath_context)], ptr),
	xmlXPathFreeObject([ptr], void)
	]).

% libxml2's public structs, as far as they're read: a node's first fields are common to every
% kind, xmlNode then goes on with ns, content, properties and nsDef

:- foreign_struct(sgml_head, [ptr, sint, ptr, ptr, ptr, ptr, ptr]).
:- foreign_struct(sgml_node, [ptr, sint, ccstr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ccstr, ptr, ptr]).
:- foreign_struct(sgml_attr, [ptr, sint, ccstr, ptr, ptr, ptr, ptr, ptr, ptr, ptr]).
:- foreign_struct(sgml_ns, [ptr, sint, ccstr, ccstr]).
:- foreign_struct(sgml_error, [sint, sint, ccstr, sint, ccstr, sint]).
:- foreign_struct(sgml_xpath_object, [sint, ptr, sint, double, ccstr]).
:- foreign_struct(sgml_node_set, [sint, sint, ptr]).

:- initialization(catch(xmlInitParser, _, true)).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

load_xml(Source, DOM, Options) :-
	load_(xml, Source, DOM, Options, load_xml/3).

load_html(Source, DOM, Options) :-
	load_(html, Source, DOM, Options, load_html/3).

load_(Dialect, Source, DOM, Options, Ctx) :-
	options_(Options, Os, Ctx),
	with_doc_(Dialect, Source, Doc, document_(Doc, Os, DOM), Ctx).

% Goal with Doc, the document Source parses to, freed however Goal ends

with_doc_(Dialect, Source, Doc, Goal, Ctx) :-
	parse_(Dialect, Source, Doc, Ctx),
	setup_call_cleanup(true, once(Goal), xmlFreeDoc(Doc)).

% NONET, NOWARNING and NOERROR: libxml2 neither fetches nor prints; HTML also RECOVER

parse_flags_(xml, 2144).
parse_flags_(html, 2145).

parse_(Dialect, Source, Doc, Ctx) :-
	source_(Source, Src, Ctx),
	parse_flags_(Dialect, Flags),
	xmlResetLastError,
	parse_src_(Src, Dialect, Flags, Doc),
	(	nonvar(Doc) -> true
	;	last_error_(Ctx)
	).

parse_src_(file(F), xml, Flags, Doc) :- xmlReadFile(F, 0, Flags, Doc).
parse_src_(file(F), html, Flags, Doc) :- htmlReadFile(F, 0, Flags, Doc).
parse_src_(text(T), Dialect, Flags, Doc) :-
	setup_call_cleanup(
		'$text_to_pointer'(T, P, Len),
		(	Dialect == xml -> xmlReadMemory(P, Len, 0, 'UTF-8', Flags, Doc)
		;	htmlReadMemory(P, Len, 0, 'UTF-8', Flags, Doc)
		),
		'$free_struct_pointer'(P)).

source_(S, _, Ctx) :- var(S), !, throw(error(instantiation_error, Ctx)).
source_(file(F), file(A), Ctx) :- !, file_(F, A, Ctx).
source_(stream(S), text(T), _) :- !, stream_text_(S, T).
source_(string(T), text(T), Ctx) :- !, text_(T, Ctx).
source_(F, file(A), Ctx) :- atom(F), F \== [], !, file_(F, A, Ctx).
source_(T, text(T), Ctx) :- text_(T, Ctx).

file_(F, A, Ctx) :-
	(	var(F) -> throw(error(instantiation_error, Ctx))
	;	atom(F) -> A = F
	;	'$string'(F) -> atom_string(A, F)
	;	throw(error(type_error(source_sink, file(F)), Ctx))
	),
	(	exists_file(A) -> true
	;	throw(error(existence_error(source_sink, A), Ctx))
	).

text_(T, Ctx) :-
	(	var(T) -> throw(error(instantiation_error, Ctx))
	;	(atom(T) ; '$string'(T)) -> true
	;	throw(error(type_error(text, T), Ctx))
	).

% The rest of Stream as one string, packed, as '$text_to_pointer'/3 wants

stream_text_(S, T) :-
	stream_chars_(S, Cs),
	format(string(T), "~s", [Cs]).

stream_chars_(S, Cs) :-
	read_line_to_string(S, L),
	(	L == end_of_file -> Cs = []
	;	append(L, ['\n'|Cs1], Cs),
		stream_chars_(S, Cs1)
	).

last_error_(Ctx) :-
	xmlGetLastError(E),
	(	E =\= 0,
		'$pointer_to_struct'(sgml_error, E, [_, _, _, Msg0, _, _, Line]),
		Msg0 \== 0
	->	trim_(Msg0, Msg)
	;	Line = 0, Msg = 'not well formed'
	),
	throw(error(xml_error(Line, Msg), Ctx)).

trim_(A0, A) :-
	atom_chars(A0, Cs0),
	reverse(Cs0, R0),
	drop_ws_(R0, R),
	reverse(R, Cs),
	atom_chars(A, Cs).

drop_ws_([C|Cs], R) :- memberchk(C, [' ', '\n', '\t', '\r']), !, drop_ws_(Cs, R).
drop_ws_(Cs, Cs).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Options

options_(Options, os(Space, CData, AttrValue), Ctx) :-
	must_be_(list, Options, Ctx),
	option_(space(Space), Options, preserve, [preserve, remove], Ctx),
	option_(cdata(CData), Options, string, [string, atom], Ctx),
	option_(attribute_value(AttrValue), Options, string, [string, atom], Ctx).

option_(O, Options, Default, Allowed, Ctx) :-
	(	memberchk(O, Options) -> arg(1, O, V),
		(	var(V) -> throw(error(instantiation_error, Ctx))
		;	memberchk(V, Allowed) -> true
		;	throw(error(domain_error(sgml_option, O), Ctx))
		)
	;	arg(1, O, Default)
	).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% The tree

% A document's top level: its elements and processing instructions, as text, comments and the
% doctype are left out

document_(Doc, Os, DOM) :-
	'$pointer_to_struct'(sgml_head, Doc, [_, _, _, _, First|_]),
	top_(First, Os, DOM).

top_(0, _, []) :- !.
top_(P, Os, Items) :-
	'$pointer_to_struct'(sgml_head, P, [_, _, Type, _, _, _, _, Next]),
	(	(Type =:= 1 ; Type =:= 7)
	->	node_(P, Os, Item),
		Items = [Item|Items1]
	;	Items = Items1
	),
	top_(Next, Os, Items1).

% One element or processing instruction as a term

node_(P, Os, Item) :-
	'$pointer_to_struct'(sgml_node, P, [_, _, Type, Name, Children, _, _, _, _, _, Ns, Content, Props, NsDef]),
	(	Type =:= 1
	->	qname_(Name, Ns, QName),
		ns_defs_(NsDef, Os, As0),
		attrs_(Props, Os, As1),
		append(As0, As1, As),
		children_(Children, Os, Cs),
		Item = element(QName, As, Cs)
	;	pi_text_(Name, Content, Text),
		cdata_(Os, Text, T),
		Item = pi(T)
	).

pi_text_(Target, 0, Target) :- !.
pi_text_(Target, Data, Text) :- atomic_list_concat([Target, ' ', Data], Text).

qname_(Name, 0, Name) :- !.
qname_(Name, Ns, QName) :-
	'$pointer_to_struct'(sgml_ns, Ns, [_, _, _, _, Prefix]),
	(	Prefix == 0 -> QName = Name
	;	atomic_list_concat([Prefix, ':', Name], QName)
	).

ns_defs_(0, _, []) :- !.
ns_defs_(P, Os, [Name=V|As]) :-
	'$pointer_to_struct'(sgml_ns, P, [_, Next, _, Href, Prefix]),
	(	Prefix == 0 -> Name = xmlns
	;	atomic_list_concat([xmlns, ':', Prefix], Name)
	),
	(	Href == 0 -> Href1 = '' ; Href1 = Href ),
	attr_value_(Os, Href1, V),
	ns_defs_(Next, Os, As).

attrs_(0, _, []) :- !.
attrs_(P, Os, [QName=V|As]) :-
	'$pointer_to_struct'(sgml_attr, P, [_, _, _, Name, Children, _, _, Next, _, _, Ns]),
	qname_(Name, Ns, QName),
	texts_(Children, Ts),
	join_(Ts, A),
	attr_value_(Os, A, V),
	attrs_(Next, Os, As).

% Text pieces as one, made an atom only when there are several, as a value is mostly one piece

join_([], '') :- !.
join_([A], A) :- !.
join_(As, A) :- atomic_list_concat(As, A).

% An attribute's value, from its text and entity reference children

texts_(0, []) :- !.
texts_(P, Ts) :-
	'$pointer_to_struct'(sgml_node, P, [_, _, _, _, _, _, _, Next, _, _, _, Content|_]),
	(	Content == 0 -> Ts = Ts1 ; Ts = [Content|Ts1] ),
	texts_(Next, Ts1).

% An element's content: adjacent text, split by entity references, as one, then space(remove)

children_(P, Os, Cs) :-
	raw_children_(P, Os, Rs),
	merge_text_(Rs, Ms),
	finish_(Ms, Os, Cs).

raw_children_(0, _, []) :- !.
raw_children_(P, Os, Rs) :-
	'$pointer_to_struct'(sgml_head, P, [_, _, Type, _, _, _, _, Next]),
	(	(Type =:= 1 ; Type =:= 7)
	->	node_(P, Os, Item),
		Rs = [Item|Rs1]
	;	(Type =:= 3 ; Type =:= 4 ; Type =:= 5)
	->	'$pointer_to_struct'(sgml_node, P, [_, _, _, _, _, _, _, _, _, _, _, Content|_]),
		(	Content == 0 -> Rs = Rs1 ; Rs = [t(Content)|Rs1] )
	;	Rs = Rs1
	),
	raw_children_(Next, Os, Rs1).

merge_text_([], []).
merge_text_([t(A)|Rs], Ms) :- !,
	take_text_(Rs, As, Rest),
	join_([A|As], T),
	Ms = [t(T)|Ms1],
	merge_text_(Rest, Ms1).
merge_text_([R|Rs], [R|Ms]) :-
	merge_text_(Rs, Ms).

take_text_([t(A)|Rs], [A|As], Rest) :- !, take_text_(Rs, As, Rest).
take_text_(Rs, [], Rs).

finish_([], _, []).
finish_([t(A)|Ms], Os, Cs) :- !,
	(	Os = os(remove, _, _)
	->	atom_string(A, S0),
		strip_(S0, S),
		(	S == [] -> Cs = Cs1 ; text_as_(Os, S, T), Cs = [T|Cs1] )
	;	cdata_(Os, A, T),
		Cs = [T|Cs1]
	),
	finish_(Ms, Os, Cs1).
finish_([M|Ms], Os, [M|Cs]) :-
	finish_(Ms, Os, Cs).

% White space off both ends of a string, kept a packed string

strip_(S0, S) :-
	(	S0 = [F|_], \+ ws_(F), last(S0, L), \+ ws_(L)
	->	S = S0
	;	drop_ws_(S0, S1),
		reverse(S1, R1),
		drop_ws_(R1, R),
		reverse(R, Cs),
		(	Cs == [] -> S = [] ; format(string(S), "~s", [Cs]) )
	).

ws_(C) :- memberchk(C, [' ', '\n', '\t', '\r']).

% Text from libxml2, an atom already, as a string unless atoms were asked for

cdata_(os(_, string, _), A, S) :- !, atom_string(A, S).
cdata_(_, A, A).

text_as_(os(_, string, _), S, S) :- !.
text_as_(_, S, A) :- atom_string(A, S).

attr_value_(os(_, _, string), A, S) :- !, atom_string(A, S).
attr_value_(_, A, A).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% XPath

% Each Item XPath selects in Source, on backtracking: an element as a term, an attribute as its
% value, text and comments as text, or the number, string or true/false an expression gives.
% Options are those of load_xml/3, dialect(xml|html), xml by default, and namespaces(Prefix=URI,
% ...) for the prefixes XPath uses, which a document with a default namespace needs.

xml_xpath(Source, XPath, Item) :-
	xml_xpath(Source, XPath, Item, []).

xml_xpath(Source, XPath, Item, Options) :-
	Ctx = xml_xpath/4,
	options_(Options, Os, Ctx),
	option_(dialect(Dialect), Options, xml, [xml, html], Ctx),
	(	memberchk(namespaces(NSs), Options) -> must_be_(list, NSs, Ctx) ; NSs = [] ),
	text_(XPath, Ctx),
	(	'$string'(XPath) -> atom_string(X, XPath) ; X = XPath ),
	with_doc_(Dialect, Source, Doc, xpath_(Doc, X, NSs, Os, Items, Ctx), Ctx),
	member(Item, Items).

xpath_(Doc, XPath, NSs, Os, Items, Ctx) :-
	xmlXPathNewContext(Doc, XC),
	(	var(XC) -> throw(error(resource_error(memory), Ctx)) ; true ),
	setup_call_cleanup(true,
		(	maplist(register_ns_(XC, Ctx), NSs),
			xmlResetLastError,
			quiet_(xmlXPathEvalExpression(XPath, XC, Obj)),
			(	Obj =:= 0 -> last_error_(Ctx) ; true ),
			setup_call_cleanup(true, result_(Obj, Os, Items), xmlXPathFreeObject(Obj))
		),
		xmlXPathFreeContext(XC)).

register_ns_(XC, Ctx, NS) :-
	(	nonvar(NS), NS = (Prefix=URI), atom(Prefix), (atom(URI) ; '$string'(URI))
	->	xmlXPathRegisterNs(XC, Prefix, URI, _)
	;	throw(error(type_error(prefix_uri, NS), Ctx))
	).

% XPath errors go to libxml2's structured error handler, which prints them unless one is set

:- dynamic(error_callback_/1).

quiet_(Goal) :-
	(	error_callback_(Cb) -> true
	;	catch(foreign_callback(sgml:ignore_error_, [ptr, ptr], void, Cb), _, fail)
	->	assertz(error_callback_(Cb))
	;	Cb = 0
	),
	setup_call_cleanup(xmlSetStructuredErrorFunc(0, Cb), Goal, xmlSetStructuredErrorFunc(0, 0)).

ignore_error_(_, _).

result_(Obj, Os, Items) :-
	'$pointer_to_struct'(sgml_xpath_object, Obj, [_, Type, NodeSet, Bool, Num, Str]),
	result_(Type, NodeSet, Bool, Num, Str, Os, Items).

result_(1, 0, _, _, _, _, []) :- !.
result_(1, NodeSet, _, _, _, Os, Items) :- !,
	'$pointer_to_struct'(sgml_node_set, NodeSet, [_, N, _, Tab]),
	'$pointer_to_array'(ptr, Tab, N, Ps),
	maplist(item_(Os), Ps, Items).
result_(2, _, B, _, _, _, [V]) :- !, ( B =:= 0 -> V = false ; V = true ).
result_(3, _, _, X, _, _, [V]) :- !, number_(X, V).
result_(4, _, _, _, S, _, [V]) :- !, ( S == 0 -> V = "" ; atom_string(S, V) ).
result_(_, _, _, _, _, _, []).

% XPath numbers are doubles: a whole one comes back as an integer

number_(X, V) :-
	(	X =\= X -> V = nan
	;	X > 1.0e308 -> V = inf
	;	X < -1.0e308 -> V = -inf
	;	X =:= truncate(X), abs(X) < 9.0e15 -> V is truncate(X)
	;	V = X
	).

item_(Os, P, Item) :-
	'$pointer_to_struct'(sgml_head, P, [_, _, Type|_]),
	item_(Type, P, Os, Item).

item_(1, P, Os, Item) :- !, node_(P, Os, Item).
item_(7, P, Os, Item) :- !, node_(P, Os, Item).
item_(2, P, Os, V) :- !,
	'$pointer_to_struct'(sgml_attr, P, [_, _, _, _, Children|_]),
	texts_(Children, Ts),
	join_(Ts, A),
	attr_value_(Os, A, V).
item_(Type, P, Os, Item) :- (Type =:= 9 ; Type =:= 13), !, document_(P, Os, Item).
item_(18, P, Os, V) :- !,
	'$pointer_to_struct'(sgml_ns, P, [_, _, _, Href, _]),
	(	Href == 0 -> A = '' ; A = Href ),
	attr_value_(Os, A, V).
item_(_, P, Os, Item) :-
	'$pointer_to_struct'(sgml_node, P, [_, _, _, _, _, _, _, _, _, _, _, Content|_]),
	(	Content == 0 -> A = '' ; A = Content ),
	cdata_(Os, A, Item).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Writing

% DOM, a document or one element, as XML on Stream. Options: header(Bool), the <?xml ...?> line,
% and layout(Bool), indenting elements whose content is only elements; both true by default.

xml_write(Stream, DOM, Options) :-
	Ctx = xml_write/3,
	must_be_(list, Options, Ctx),
	bool_option_(header(Header), Options, Ctx),
	bool_option_(layout(Layout), Options, Ctx),
	(	is_list(DOM) -> Items = DOM ; Items = [DOM] ),
	(	Header == true -> format(Stream, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>~n", []) ; true ),
	write_top_(Items, Stream, Layout, Ctx).

bool_option_(O, Options, Ctx) :-
	option_(O, Options, true, [true, false], Ctx).

write_top_([], _, _, _).
write_top_([I|Is], S, Layout, Ctx) :-
	write_item_(I, S, 0, Layout, Ctx),
	(	Layout == true -> nl(S) ; true ),
	write_top_(Is, S, Layout, Ctx).

write_item_(I, _, _, _, Ctx) :- var(I), !, throw(error(instantiation_error, Ctx)).
write_item_(element(N, As, Cs), S, D, Layout, Ctx) :- !,
	(	atom(N) -> true ; throw(error(type_error(atom, N), Ctx)) ),
	must_be_(list, As, Ctx),
	must_be_(list, Cs, Ctx),
	format(S, "<~w", [N]),
	maplist(write_attr_(S, Ctx), As),
	(	Cs == []
	->	format(S, "/>", [])
	;	format(S, ">", []),
		(	Layout == true, \+ (member(C, Cs), \+ C = element(_, _, _))
		->	D1 is D + 1,
			forall(member(C, Cs), (nl(S), indent_(S, D1), write_item_(C, S, D1, Layout, Ctx))),
			nl(S),
			indent_(S, D)
		;	forall(member(C, Cs), write_item_(C, S, D, false, Ctx))
		),
		format(S, "</~w>", [N])
	).
write_item_(pi(T), S, _, _, Ctx) :- !,
	text_chars_(T, Cs, Ctx),
	format(S, "<?~s?>", [Cs]).
write_item_(T, S, _, _, Ctx) :-
	(	(atom(T) ; '$string'(T)) -> true ; throw(error(type_error(xml_content, T), Ctx)) ),
	text_chars_(T, Cs, Ctx),
	escape_(Cs, text, Es),
	format(S, "~s", [Es]).

write_attr_(S, Ctx, A) :-
	(	nonvar(A), A = (N=V), atom(N) -> true ; throw(error(type_error(attribute, A), Ctx)) ),
	text_chars_(V, Cs, Ctx),
	escape_(Cs, attr, Es),
	format(S, " ~w=\"~s\"", [N, Es]).

indent_(S, D) :- N is D * 2, forall(between(1, N, _), put_char(S, ' ')).

text_chars_(V, _, Ctx) :- var(V), !, throw(error(instantiation_error, Ctx)).
text_chars_([], [], _) :- !.
text_chars_(V, Cs, _) :- '$string'(V), !, Cs = V.
text_chars_(V, Cs, _) :- atom(V), !, atom_chars(V, Cs).
text_chars_(V, Cs, _) :- number(V), !, number_chars(V, Cs).
text_chars_(V, _, Ctx) :- throw(error(type_error(text, V), Ctx)).

escape_([], _, []).
escape_([C|Cs], Where, Es) :-
	(	escaped_(C, Where, E) -> atom_chars(E, ECs), append(ECs, Es1, Es)
	;	Es = [C|Es1]
	),
	escape_(Cs, Where, Es1).

escaped_('&', _, '&amp;').
escaped_('<', _, '&lt;').
escaped_('>', _, '&gt;').
escaped_('"', attr, '&quot;').

must_be_(_, X, Ctx) :- var(X), !, throw(error(instantiation_error, Ctx)).
must_be_(list, X, Ctx) :- (is_list(X) -> true ; throw(error(type_error(list, X), Ctx))).
