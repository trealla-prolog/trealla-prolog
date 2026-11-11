% The Trealla mark: a search tree, two branches tried and dropped, one path
% proved down to a solution. Drawn with library(cairo).
%
%   cd site/logo && tpl -q logo.pl -g main
%
% Writes the site's favicon and hero image, and a 1280x640 card for link previews.
% The card's text is drawn as outlines from whatever serif cairo finds, so it
% looks best on a Mac (Iowan Old Style).

:- use_module(library(cairo)).
:- use_module(library(lists)).

colour(ground,   0x1d, 0x1a, 0x16).	% warm stone, the site's dark background
colour(plaster,  0xe9, 0xe3, 0xd6).	% the bust in the old logo
colour(sky,      0x3f, 0xae, 0x5f).	% its green sky
colour(ochre,    0xe0, 0xa2, 0x4a).
colour(glow,     0x2c, 0x3b, 0x2c).
colour(stone,    0xa8, 0xa0, 0x92).

ink(Cr, Name) :-
	colour(Name, R, G, B),
	R1 is R/255, G1 is G/255, B1 is B/255,
	cairo_set_source_rgb(Cr, R1, G1, B1).

% cairo takes doubles; the drawing is on an integer grid.
move(Cr, X, Y) :- X1 is float(X), Y1 is float(Y), cairo_move_to(Cr, X1, Y1).
line(Cr, X, Y) :- X1 is float(X), Y1 is float(Y), cairo_line_to(Cr, X1, Y1).
arc(Cr, X, Y, R, A0, A1) :-
	X1 is float(X), Y1 is float(Y), R1 is float(R), B0 is float(A0), B1 is float(A1),
	cairo_arc(Cr, X1, Y1, R1, B0, B1).
width(Cr, W) :- W1 is float(W), cairo_set_line_width(Cr, W1).

% The tree on a 512 grid: nodes, then edges between them.
node(root, 292, 122).
node(mid,  200, 248).
node(no1,  384, 248).
node(no2,  128, 374).
node(goal, 272, 374).

edge(root, mid,  proved).
edge(root, no1,  dropped).
edge(mid,  no2,  dropped).
edge(mid,  goal, proved).

mark(Cr) :-
	rounded_rect(Cr, 0, 0, 512, 512, 112),
	glow(Cr, 96, 64, 560),
	tree(Cr).

tree(Cr) :-
	forall(edge(A, B, How), draw_edge(Cr, A, B, How)),
	forall(node(N, X, Y), draw_node(Cr, N, X, Y)).

% The mark's tree beside the name, for link previews.
card(Cr) :-
	cairo_rectangle(Cr, 0.0, 0.0, 1280.0, 640.0),
	glow(Cr, 140, 80, 1200),
	cairo_save(Cr),
	cairo_translate(Cr, 36.0, 30.0),
	cairo_scale(Cr, 1.15, 1.15),
	tree(Cr),
	cairo_restore(Cr),
	text(Cr, 'Trealla', 616, 300, 168, normal, plaster),
	text(Cr, 'Prolog', 622, 392, 84, normal, sky),
	text(Cr, 'Compact, efficient ISO Prolog in plain C', 624, 478, 32, normal, stone).

text(Cr, Text, X, Y, Size, Slant, Colour) :-
	( Slant == italic -> S = 1 ; S = 0 ),
	cairo_select_font_face(Cr, 'Iowan Old Style', S, 0),
	Sz is float(Size),
	cairo_set_font_size(Cr, Sz),
	ink(Cr, Colour),
	move(Cr, X, Y),
	cairo_show_text(Cr, Text).

% Fills the current path with stone, lit faintly green from (X,Y) like the old logo's sky.
glow(Cr, X, Y, R) :-
	X1 is float(X), Y1 is float(Y), R1 is float(R),
	cairo_pattern_create_radial(X1, Y1, 0.0, X1, Y1, R1, P),
	stop(P, 0.0, glow),
	stop(P, 1.0, ground),
	cairo_set_source(Cr, P),
	cairo_fill(Cr),
	cairo_pattern_destroy(P).

stop(P, Off, Name) :-
	colour(Name, R, G, B),
	R1 is R/255, G1 is G/255, B1 is B/255,
	cairo_pattern_add_color_stop_rgb(P, Off, R1, G1, B1).

% An edge into a hollow node stops at its rim, so the hole stays empty.
draw_edge(Cr, A, B, How) :-
	node(A, X0, Y0), node(B, X1, Y1),
	( rim(B, R) -> true ; R = 0 ),
	D is sqrt((X1-X0)^2 + (Y1-Y0)^2),
	X2 is X1 - (X1-X0)*R/D, Y2 is Y1 - (Y1-Y0)*R/D,
	( How == proved -> ink(Cr, sky) ; ink(Cr, plaster) ),
	width(Cr, 30),
	cairo_set_line_cap(Cr, 0),				% butt, to meet the rim cleanly
	move(Cr, X0, Y0),
	line(Cr, X2, Y2),
	cairo_stroke(Cr).

rim(no1, 40).
rim(no2, 40).
rim(goal, 50).

draw_node(Cr, goal, X, Y) :- !,		% the solution: a target, hit
	ring(Cr, X, Y, 50, 32, sky),
	disc(Cr, X, Y, 18, plaster).
draw_node(Cr, N, X, Y) :-			% tried and dropped: hollow
	memberchk(N, [no1, no2]), !,
	ring(Cr, X, Y, 40, 23, ochre).
draw_node(Cr, root, X, Y) :- !,
	disc(Cr, X, Y, 44, plaster).
draw_node(Cr, _, X, Y) :-
	disc(Cr, X, Y, 36, plaster).

disc(Cr, X, Y, R, Colour) :-
	ink(Cr, Colour),
	A is 2*pi,
	cairo_new_sub_path(Cr),
	arc(Cr, X, Y, R, 0, A),
	cairo_fill(Cr).

ring(Cr, X, Y, Outer, Inner, Colour) :-
	ink(Cr, Colour),
	R is (Outer+Inner)/2, W is Outer-Inner, A is 2*pi,
	width(Cr, W),
	cairo_new_sub_path(Cr),
	arc(Cr, X, Y, R, 0, A),
	cairo_stroke(Cr).

rounded_rect(Cr, X, Y, W, H, R) :-
	P is pi, P2 is pi/2, P3 is 3*pi/2,
	X1 is X+W-R, Y1 is Y+R, X2 is X+R, Y2 is Y+H-R,
	cairo_new_sub_path(Cr),
	arc(Cr, X1, Y1, R, P3, 0),
	arc(Cr, X1, Y2, R, 0, P2),
	arc(Cr, X2, Y2, R, P2, P),
	arc(Cr, X2, Y1, R, P, P3),
	cairo_close_path(Cr).

main :-
	cairo_svg('../public/favicon.svg', 512, 512, mark),
	cairo_svg('../src/assets/trealla-mark.svg', 512, 512, mark),
	cairo_png('../public/social.png', 1280, 640, card),
	halt.
