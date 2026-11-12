% The Trealla mark: a lowercase t in three worlds at once. The copies are hinged at
% the top of the stem, so they share a past and fan apart toward the tail, and where
% they overlap their light adds, to white where all three agree. Drawn with library(cairo).
%
%   cd site/logo && tpl -q logo.pl -g main
%
% Writes the site's favicon and hero image, and a 1280x640 card for link previews.
% Overlaps are clipped fills, not a blend operator, so the SVGs stay plain vector.
% The card's text is drawn as outlines from whatever serif cairo finds, so it
% looks best on a Mac (Iowan Old Style).

:- use_module(library(cairo)).
:- use_module(library(lists)).

colour(ground,   0x1d, 0x1a, 0x16).	% warm stone, the site's dark background
colour(glow,     0x2c, 0x3b, 0x2c).
colour(plaster,  0xe9, 0xe3, 0xd6).
colour(stone,    0xa8, 0xa0, 0x92).
colour(blue,     0x4f, 0x86, 0xe0).
colour(sky,      0x3f, 0xae, 0x5f).	% the site's green
colour(ochre,    0xe0, 0xa2, 0x4a).

rgb(Name, R, G, B) :- colour(Name, R0, G0, B0), R is R0/255, G is G0/255, B is B0/255.

ink(Cr, Name) :- rgb(Name, R, G, B), cairo_set_source_rgb(Cr, R, G, B).

% cairo takes doubles; the drawing is on an integer grid.
move(Cr, X, Y) :- X1 is float(X), Y1 is float(Y), cairo_move_to(Cr, X1, Y1).
line(Cr, X, Y) :- X1 is float(X), Y1 is float(Y), cairo_line_to(Cr, X1, Y1).
arc(Cr, X, Y, R, A0, A1) :-
	X1 is float(X), Y1 is float(Y), R1 is float(R), B0 is float(A0), B1 is float(A1),
	cairo_arc(Cr, X1, Y1, R1, B0, B1).
translate(Cr, X, Y) :- X1 is float(X), Y1 is float(Y), cairo_translate(Cr, X1, Y1).
scale(Cr, S) :- S1 is float(S), cairo_scale(Cr, S1, S1).

poly(Cr, [X0-Y0|Ps]) :-
	cairo_new_path(Cr),
	move(Cr, X0, Y0),
	forall(member(X-Y, Ps), line(Cr, X, Y)),
	cairo_close_path(Cr).

% A lowercase t on a 512 grid, as X-Y points (y down).
t_outline(Ps) :-
	L = 196, R = 286,				% stem
	Ct = 150, Cb = 222, Xl = 128, Xr = 372,		% crossbar
	Cy = 330, Ri = 34, Ro = 124, Cx is R + Ri,	% bowl
	arc_pts(Cx, Cy, Ri, 180, 90, 12, Inner),
	arc_pts(Cx, Cy, Ro, 90, 180, 24, Outer),
	Top = [L-100, R-56, R-Ct, Xr-Ct, Xr-Cb, R-Cb],
	Tail = [382-360, 400-446],
	Left = [L-Cb, Xl-Cb, Xl-Ct, L-Ct],
	append([Top, Inner, Tail, Outer, Left], Ps).

arc_pts(Cx, Cy, R, A0, A1, N, Ps) :-
	numlist(0, N, Is),
	maplist(arc_pt(Cx, Cy, R, A0, A1, N), Is, Ps).

arc_pt(Cx, Cy, R, A0, A1, N, I, X-Y) :-
	A is (A0 + (A1-A0)*I/N) * pi / 180,
	X is Cx + R*cos(A), Y is Cy + R*sin(A).

% The three worlds: each copy swings Swing degrees about a horizontal hinge through the top of the stem.
world(blue,  -18).
world(sky,     0).
world(ochre,  18).

copies(Copies) :-
	t_outline(Ps),
	findall(Qs-C, (world(C, Swing), maplist(hinged(Swing), Ps, Qs)), Copies).

hinged(Swing, X-Y, P) :-
	A is Swing*pi/180, Dy is Y-70,
	Y1 is 70 + Dy*cos(A), Z is Dy*sin(A),
	view(X, Y1, Z, P).

% Seen from a little to the right and above, in perspective.
view(X, Y, Z, Px-Py) :-
	A is 34*pi/180, B is -10*pi/180, F = 1100,
	X0 is X-256, Y0 is Y-256,
	X1 is X0*cos(A) + Z*sin(A), Z1 is Z*cos(A) - X0*sin(A),
	Y2 is Y0*cos(B) + Z1*sin(B), Z2 is Z1*cos(B) - Y0*sin(B),
	K is F/(F+Z2), Px is X1*K, Py is Y2*K.

% Light adds: overlapping copies take the clamped sum of their colours.
fill_mix(Cr, Cs) :-
	findall(R-G-B, (member(C, Cs), rgb(C, R, G, B)), L),
	foldl(add3, L, 0-0-0, R1-G1-B1),
	R2 is min(1.0, R1), G2 is min(1.0, G1), B2 is min(1.0, B1),
	cairo_set_source_rgb(Cr, R2, G2, B2).

add3(R-G-B, R0-G0-B0, R1-G1-B1) :- R1 is R0+R, G1 is G0+G, B1 is B0+B.

% Each copy, then each pair clipped to its overlap, then all three.
superpose(Cr, Copies) :-
	forall(member(P-C, Copies), (fill_mix(Cr, [C]), poly(Cr, P), cairo_fill(Cr))),
	forall(two(Copies, P-C, Q-D), (
		cairo_save(Cr), poly(Cr, P), cairo_clip(Cr),
		fill_mix(Cr, [C, D]), poly(Cr, Q), cairo_fill(Cr),
		cairo_restore(Cr))),
	Copies = [P1-C1, P2-C2, P3-C3],
	cairo_save(Cr), poly(Cr, P1), cairo_clip(Cr), poly(Cr, P2), cairo_clip(Cr),
	fill_mix(Cr, [C1, C2, C3]), poly(Cr, P3), cairo_fill(Cr),
	cairo_restore(Cr).

two(L, A, B) :- append(_, [A|T], L), member(B, T).

% The copies scaled to fit Size, centred on (X,Y).
worlds(Cr, X, Y, Size) :-
	copies(Copies),
	findall(U, (member(P-_, Copies), member(U-_, P)), Us),
	findall(V, (member(P-_, Copies), member(_-V, P)), Vs),
	min_list(Us, U0), max_list(Us, U1), min_list(Vs, V0), max_list(Vs, V1),
	S is Size / max(U1-U0, V1-V0),
	Tx is X - S*(U0+U1)/2, Ty is Y - S*(V0+V1)/2,
	cairo_save(Cr), translate(Cr, Tx, Ty), scale(Cr, S),
	superpose(Cr, Copies),
	cairo_restore(Cr).

mark(Cr) :-
	rounded_rect(Cr, 0, 0, 512, 512, 112),
	glow(Cr, 96, 64, 560),
	worlds(Cr, 256, 256, 360).

% The mark beside the name, for link previews.
card(Cr) :-
	cairo_rectangle(Cr, 0.0, 0.0, 1280.0, 640.0),
	glow(Cr, 140, 80, 1200),
	worlds(Cr, 330, 320, 460),
	text(Cr, 'Trealla', 616, 300, 168, plaster),
	text(Cr, 'Prolog', 622, 392, 84, sky),
	text(Cr, 'Compact, efficient ISO Prolog in plain C', 624, 478, 32, stone).

text(Cr, Text, X, Y, Size, Colour) :-
	cairo_select_font_face(Cr, 'Iowan Old Style', 0, 0),
	Sz is float(Size),
	cairo_set_font_size(Cr, Sz),
	ink(Cr, Colour),
	move(Cr, X, Y),
	cairo_show_text(Cr, Text).

% Fills the current path with stone, lit faintly green from (X,Y).
glow(Cr, X, Y, R) :-
	X1 is float(X), Y1 is float(Y), R1 is float(R),
	cairo_pattern_create_radial(X1, Y1, 0.0, X1, Y1, R1, P),
	stop(P, 0.0, glow),
	stop(P, 1.0, ground),
	cairo_set_source(Cr, P),
	cairo_fill(Cr),
	cairo_pattern_destroy(P).

stop(P, Off, Name) :-
	rgb(Name, R, G, B),
	cairo_pattern_add_color_stop_rgb(P, Off, R, G, B).

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
