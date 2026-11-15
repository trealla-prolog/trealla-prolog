:- module(graphviz, [
	graphviz_dot/3,
	graphviz_render/4,
	graphviz_render_file/4,
	graphviz_layout/3,
	graphviz_version/1
	]).

% Graph drawing over Graphviz (https://graphviz.org)
%
% MACOS: brew install graphviz
% UBUNTU: sudo apt install libgraphviz-dev
%
%	?- graphviz_render_file([a-b, b-c, c-a], svg, 'g.svg', [directed(true)]).
%	?- graphviz_render([a-b], svg, SVG, []).
%	?- graphviz_layout([a-b, b-c], Ps, [layout(neato)]).
%	Ps = [a-point(27.0,143.47),b-point(71.02,86.16),c-point(95.13,18.0)].
%
% A graph is an edge list of A-B pairs and edge(A, B, Attributes), a ugraph as library(ugraphs)
% makes, or dot(Text), DOT source as it is. Vertices are any terms, labelled as write/1 would
% write them. Options:
%
%	directed(Bool)		false by default; an undirected ugraph's A-B and B-A are one edge
%	layout(Engine)		dot (the default), neato, fdp, sfdp, circo, twopi, osage or patchwork
%	graph(Attributes), node(Attributes), edge(Attributes)	defaults for all of them
%	nodes(V-Attributes ...)	attributes for each vertex, which also adds the vertex
%
% Attributes are Name=Value, a Value being text, a number or html(Text) for an HTML-like label.
% Errors are error(graphviz_error(Message), _), and Graphviz's warnings are warnings.

:- use_module(library(lists)).

:- use_foreign_module('libgvc.so', [
	gvContext([], ptr),
	gvFreeContext([ptr], sint),
	gvcVersion([ptr], ccstr),
	gvLayout([ptr, ptr, cstr], sint),
	gvFreeLayout([ptr, ptr], sint),
	gvRenderData([ptr, ptr, cstr, -ptr, -ptr], sint),			% size_t as ptr, its width
	gvFreeRenderData([ptr], void),
	gvRenderFilename([ptr, ptr, cstr, cstr], sint),
	agmemread([cstr], ptr),
	agclose([ptr], sint),
	agseterrf([ptr], ptr)
	]).

:- dynamic(error_callback_/1).
:- dynamic(message_/1).

graphviz_version(V) :-
	with_context_(C, gvcVersion(C, V), graphviz_version/1).

% Goal with C, a context of its own: one that rendering failed in keeps the failed job and fails
% every render after, and a new one costs about as much as rendering a small graph

with_context_(C, Goal, Ctx) :-
	gvContext(C),
	(	C =\= 0 -> true ; throw(error(resource_error(memory), Ctx)) ),
	setup_call_cleanup(true, once(Goal), gvFreeContext(C, _)).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% Text, the graph laid out and rendered in a text Format: svg, dot, xdot, json, plain ...; a binary
% one such as png or pdf goes to a file with graphviz_render_file/4

graphviz_render(Graph, Format, Text, Options) :-
	Ctx = graphviz_render/4,
	must_be_(atom, Format, Ctx),
	(	text_format_(Format) -> true
	;	throw(error(domain_error(text_format, Format), Ctx))
	),
	with_layout_(Graph, Options, Ctx, C, G, _, render_data_(C, G, Format, Text, Ctx)).

render_data_(C, G, Format, Text, Ctx) :-
	gvRenderData(C, G, Format, P, Len, Rc),
	(	Rc =:= 0 -> true ; graphviz_error_(Ctx) ),
	setup_call_cleanup(true, '$pointer_to_text'(P, Len, Text), gvFreeRenderData(P)).

text_format_(F) :-
	memberchk(F, [svg, dot, gv, xdot, canon, plain, 'plain-ext', json, json0, dot_json, xdot_json,
		cmapx, imap, ps, eps, fig, pic, tk, vml]).

graphviz_render_file(Graph, Format, File, Options) :-
	Ctx = graphviz_render_file/4,
	must_be_(atom, Format, Ctx),
	(	var(File) -> throw(error(instantiation_error, Ctx))
	;	atom(File) -> F = File
	;	string(File) -> atom_string(F, File)
	;	throw(error(type_error(source_sink, File), Ctx))
	),
	with_layout_(Graph, Options, Ctx, C, G, _, (
		gvRenderFilename(C, G, Format, F, Rc),
		(	Rc =:= 0 -> true ; graphviz_error_(Ctx) )
	)).

% Each vertex's position as V-point(X, Y) in points, the origin at the bottom left

graphviz_layout(Graph, Positions, Options) :-
	Ctx = graphviz_layout/3,
	with_layout_(Graph, Options, Ctx, C, G, Names, render_data_(C, G, plain, Text, Ctx)),
	split_string(Text, "\n", "\r", Lines),
	findall(V-point(X, Y), (member(L, Lines), node_line_(L, Names, V, X, Y)), Positions).

node_line_(Cs, Names, V, X, Y) :-
	tokens_(Cs, [node, Name, XT, YT|_]),
	(	memberchk(Name-V0, Names) -> V = V0 ; V = Name ),
	X is float(XT) * 72,
	Y is float(YT) * 72.

% The words of a line of plain output: quoted names, numbers and other words as atoms

tokens_(Cs0, Ts) :-
	drop_spaces_(Cs0, Cs),
	(	Cs == [] -> Ts = []
	;	Cs = ['"'|Cs1]
	->	quoted_(Cs1, Q, Rest), atom_chars(T, Q), Ts = [T|Ts1], tokens_(Rest, Ts1)
	;	word_(Cs, W, Rest),
		(	catch(number_chars(N, W), _, fail) -> T = N ; atom_chars(T, W) ),
		Ts = [T|Ts1],
		tokens_(Rest, Ts1)
	).

drop_spaces_([' '|Cs0], Cs) :- !, drop_spaces_(Cs0, Cs).
drop_spaces_(Cs, Cs).

quoted_([], [], []).
quoted_(['"'|Cs], [], Cs) :- !.
quoted_(['\\', C|Cs], [C|Q], Rest) :- !, quoted_(Cs, Q, Rest).
quoted_([C|Cs], [C|Q], Rest) :- quoted_(Cs, Q, Rest).

word_([], [], []).
word_([' '|Cs], [], [' '|Cs]) :- !.
word_([C|Cs], [C|W], Rest) :- word_(Cs, W, Rest).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Graphviz calls

% Goal with C, the context, and G, the graph read from DOT and laid out, freed however Goal ends;
% Names maps each node name to its vertex

with_layout_(Graph, Options, Ctx, C, G, Names, Goal) :-
	must_be_(list, Options, Ctx),
	option_(layout(Engine), Options, dot, Ctx),
	must_be_(atom, Engine, Ctx),
	dot_(Graph, Options, Dot, Names, Ctx),
	with_context_(C, quiet_((
		agmemread(Dot, G),
		(	G =\= 0 -> true ; graphviz_error_(Ctx) ),
		setup_call_cleanup(true,
			(	gvLayout(C, G, Engine, Rc),
				(	Rc =:= 0 -> true ; graphviz_error_(Ctx) ),
				setup_call_cleanup(true, once(Goal), gvFreeLayout(C, G, _))
			),
			agclose(G, _))
	)), Ctx).

% Messages go to a callback rather than to stderr, kept for an error, or once Goal succeeds given
% to print_message/2 as a warning

quiet_(Goal) :-
	(	error_callback_(Cb) -> true
	;	catch(foreign_callback(graphviz:message_piece_, [cstr], sint, Cb), _, fail)
	->	assertz(error_callback_(Cb))
	;	Cb = 0
	),
	retractall(message_(_)),
	(	Cb =:= 0 -> call(Goal)
	;	setup_call_cleanup(agseterrf(Cb, Old), Goal, agseterrf(Old, _)),
		warnings_
	).

warnings_ :-
	findall(T, retract(message_(T)), Ts),
	(	Ts == [] -> true
	;	atomic_list_concat(Ts, A0),
		trim_(A0, A1),
		(	atom_concat('Warning: ', A, A1) -> true ; A = A1 ),
		print_message(warning, format("~w", [A]))
	).

message_piece_(Text, 0) :-
	assertz(message_(Text)).

graphviz_error_(Ctx) :-
	findall(T, message_(T), Ts),
	atomic_list_concat(Ts, A0),
	trim_(A0, A1),
	(	A1 == '' -> A = failed ; A = A1 ),
	throw(error(graphviz_error(A), Ctx)).

trim_(A0, A) :-
	atom_chars(A0, Cs0),
	reverse(Cs0, R0),
	drop_ws_(R0, R),
	reverse(R, Cs),
	atom_chars(A, Cs).

drop_ws_([C|Cs], R) :- memberchk(C, [' ', '\n', '\t', '\r']), !, drop_ws_(Cs, R).
drop_ws_(Cs, Cs).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% DOT

% Text, the DOT source for Graph

graphviz_dot(Graph, Text, Options) :-
	Ctx = graphviz_dot/3,
	must_be_(list, Options, Ctx),
	dot_(Graph, Options, Dot, _, Ctx),
	(	string(Dot) -> Text = Dot ; atom_string(Dot, Text) ).

dot_(Graph, _, _, _, Ctx) :- var(Graph), !, throw(error(instantiation_error, Ctx)).
dot_(dot(T), _, T, [], Ctx) :- !, text_(T, _, Ctx).
dot_(Graph, Options, Dot, Names, Ctx) :-
	option_(directed(D), Options, false, Ctx),
	(	(D == true ; D == false) -> true ; throw(error(type_error(boolean, D), Ctx)) ),
	option_(graph(GAs), Options, [], Ctx),
	option_(node(NAs), Options, [], Ctx),
	option_(edge(EAs), Options, [], Ctx),
	option_(nodes(VAs), Options, [], Ctx),
	must_be_(list, VAs, Ctx),
	edges_(Graph, D, Es, Ks, Ctx),
	findall(V, (member(V, Ks) ; member(V-_, VAs) ; member(e(A, B, _), Es), (V = A ; V = B)), Vs0),
	sort(Vs0, Vs),
	length(Vs, NV),
	numlist_(0, NV, Is),
	maplist(node_name_, Is, Ns),
	pairs_(Vs, Ns, VNs),
	pairs_(Ns, Vs, Names),
	(	D == true -> Kind = digraph, Arrow = ' -> ' ; Kind = graph, Arrow = ' -- ' ),
	with_output_to(string(Dot), (
		format("~w {~n", [Kind]),
		defaults_(graph, GAs, Ctx),
		defaults_(node, NAs, Ctx),
		defaults_(edge, EAs, Ctx),
		forall(member(V-N, VNs), node_dot_(V, N, VAs, Ctx)),
		forall(member(e(A, B, As), Es), edge_dot_(A, B, As, VNs, Arrow, Ctx)),
		format("}~n", [])
	)).

node_name_(I, N) :- format(atom(N), "n~d", [I]).

defaults_(_, [], _) :- !.
defaults_(Kind, As, Ctx) :-
	format("  ~w", [Kind]),
	attrs_(As, Ctx),
	format(";~n", []).

node_dot_(V, N, VAs, Ctx) :-
	findall(A, (member(V0-As, VAs), V0 == V, member(A, As)), As1),
	(	member(label=_, As1) -> As2 = As1
	;	format(string(L), "~w", [V]), As2 = [label=L|As1]
	),
	format("  ~w", [N]),
	attrs_(As2, Ctx),
	format(";~n", []).

edge_dot_(A, B, As, VNs, Arrow, Ctx) :-
	memberchk(A-NA, VNs),
	memberchk(B-NB, VNs),
	format("  ~w~w~w", [NA, Arrow, NB]),
	(	As == [] -> true ; attrs_(As, Ctx) ),
	format(";~n", []).

attrs_(As, Ctx) :-
	must_be_(list, As, Ctx),
	format(" [", []),
	attrs_list_(As, Ctx),
	format("]", []).

attrs_list_([], _).
attrs_list_([A|As], Ctx) :-
	(	nonvar(A), A = (N=V), atom(N) -> true ; throw(error(type_error(attribute, A), Ctx)) ),
	value_(V, Text, Ctx),
	format("~w=~s", [N, Text]),
	(	As == [] -> true ; format(", ", []) ),
	attrs_list_(As, Ctx).

% A value as DOT text: quoted with " and \ escaped, or <...> for html(Text)

value_(V, _, Ctx) :- var(V), !, throw(error(instantiation_error, Ctx)).
value_(html(T), Cs, Ctx) :- !,
	text_(T, Cs0, Ctx),
	append(['<'|Cs0], ['>'], Cs).
value_(V, Cs, Ctx) :-
	(	number(V) -> number_chars(V, Cs0)
	;	text_(V, Cs0, Ctx)
	),
	escape_(Cs0, Cs1),
	append(['"'|Cs1], ['"'], Cs).

escape_([], []).
escape_(['"'|Cs], ['\\', '"'|Es]) :- !, escape_(Cs, Es).
escape_(['\\'|Cs], ['\\', '\\'|Es]) :- !, escape_(Cs, Es).
escape_(['\n'|Cs], ['\\', n|Es]) :- !, escape_(Cs, Es).
escape_([C|Cs], [C|Es]) :- escape_(Cs, Es).

% The edges of a ugraph or edge list, as e(A, B, Attributes), and a ugraph's vertices

edges_(G, _, _, _, Ctx) :-
	\+ is_list(G), !,
	throw(error(type_error(list, G), Ctx)).
edges_(G, D, Es, Ks, _) :-
	G = [_|_],
	\+ (member(P, G), \+ (nonvar(P), P = _-Ns, is_list(Ns))),
	!,
	findall(A-B, (member(A-Ns, G), member(B, Ns)), Ps0),
	(	D == true -> Ps = Ps0
	;	maplist(undirected_, Ps0, Ps1),
		sort(Ps1, Ps)
	),
	findall(e(A, B, []), member(A-B, Ps), Es),
	findall(K, member(K-_, G), Ks).
edges_(G, _, Es, [], Ctx) :-
	maplist(edge_(Ctx), G, Es).

edge_(Ctx, E, _) :- var(E), !, throw(error(instantiation_error, Ctx)).
edge_(_, A-B, e(A, B, [])) :- !.
edge_(_, edge(A, B, As), e(A, B, As)) :- is_list(As), !.
edge_(Ctx, E, _) :- throw(error(type_error(edge, E), Ctx)).

undirected_(A-B, E) :- ( A @> B -> E = B-A ; E = A-B ).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

option_(O, Os, _, _) :- memberchk(O, Os), !.
option_(O, _, Default, _) :- arg(1, O, Default).

numlist_(L, H, []) :- L >= H, !.
numlist_(L, H, [L|T]) :- L1 is L + 1, numlist_(L1, H, T).

pairs_([], [], []).
pairs_([K|Ks], [V|Vs], [K-V|Ps]) :- pairs_(Ks, Vs, Ps).

text_(T, _, Ctx) :- var(T), !, throw(error(instantiation_error, Ctx)).
text_([], [], _) :- !.
text_(T, Cs, _) :- string(T), !, Cs = T.
text_(T, Cs, _) :- atom(T), !, atom_chars(T, Cs).
text_(T, _, Ctx) :- throw(error(type_error(text, T), Ctx)).

must_be_(_, X, Ctx) :- var(X), !, throw(error(instantiation_error, Ctx)).
must_be_(atom, X, Ctx) :- !, (atom(X) -> true ; throw(error(type_error(atom, X), Ctx))).
must_be_(list, X, Ctx) :- (is_list(X) -> true ; throw(error(type_error(list, X), Ctx))).
