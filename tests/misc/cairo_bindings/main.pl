% library(cairo): drawing to PNG, SVG and PDF, reading a PNG back, text and font extents, and the errors the
% helpers raise, cairo's messages left out as they vary by version. Run by ../cairo_bindings.sh, which skips it where libcairo or the FFI is missing.

:- use_module(library(cairo)).
:- initialization(main).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (	var(E) -> format("~w: ~q~n", [Label, Show])
		;	E = error(cairo_error(Code, _), Ctx) -> format("~w: raised ~q~n", [Label, error(cairo_error(Code), Ctx)])
		;	format("~w: raised ~q~n", [Label, E])
		)
	;   format("~w: failed~n", [Label])
	).

draw(Cr) :-
	A is 2*pi,
	cairo_set_source_rgb(Cr, 1.0, 1.0, 1.0),
	cairo_paint(Cr),
	cairo_pattern_create_linear(0.0, 0.0, 200.0, 0.0, G),
	cairo_pattern_add_color_stop_rgb(G, 0.0, 0.9, 0.2, 0.2),
	cairo_pattern_add_color_stop_rgb(G, 1.0, 0.2, 0.2, 0.9),
	cairo_set_source(Cr, G),
	cairo_rectangle(Cr, 10.0, 10.0, 180.0, 40.0),
	cairo_fill(Cr),
	cairo_pattern_destroy(G),
	cairo_set_source_rgba(Cr, 0.1, 0.6, 0.1, 0.8),
	cairo_arc(Cr, 100.0, 110.0, 40.0, 0.0, A),
	cairo_fill(Cr),
	cairo_set_source_rgb(Cr, 0.0, 0.0, 0.0),
	cairo_set_line_width(Cr, 3.0),
	cairo_move_to(Cr, 10.0, 190.0),
	cairo_curve_to(Cr, 60.0, 150.0, 140.0, 230.0, 190.0, 190.0),
	cairo_stroke(Cr),
	cairo_set_font_size(Cr, 20.0),
	cairo_move_to(Cr, 60.0, 37.0),
	cairo_show_text(Cr, 'Trealla').

sized(Cr) :-
	cairo_set_font_size(Cr, 20.0),
	text_extents(Cr, 'MMM', extents(_, YB, W, H, XA, _)),
	font_extents(Cr, extents(As, De, _, _, _)),
	W > 0, H > 0, YB < 0, XA >= W - 1, As > 0, De > 0.

cleanup(F) :- (exists_file(F) -> delete_file(F) ; true).

main :-
	F = 'cairo_bindings_test',
	atom_concat(F, '.png', Png), atom_concat(F, '.svg', Svg), atom_concat(F, '.pdf', Pdf),
	t(png, (cairo_png(Png, 200, 200, draw), size_file(Png, S1), S1 > 1000), yes),
	t(png_read_back, (cairo_image_surface_create_from_png(Png, S), cairo_image_surface_get_width(S, W),
		cairo_image_surface_get_height(S, H), cairo_surface_destroy(S)), W/H),
	t(svg, (cairo_svg(Svg, 200, 200, draw), open(Svg, read, In), read_line_to_string(In, L), close(In),
		sub_string(L, 0, 5, _, Head), atom_string(HeadA, Head)), HeadA),
	t(pdf, (cairo_pdf(Pdf, 200, 200, draw), size_file(Pdf, S3), S3 > 500), yes),
	t(extents, cairo_png(Png, 50, 50, sized), yes),
	t(goal_fails, cairo_png(Png, 10, 10, fail_), none),
	t(goal_raises, cairo_png(Png, 10, 10, raise_), none),
	t(unwritable, cairo_png('no_such_dir/x.png', 10, 10, true_), none),
	t(bad_size, cairo_png(Png, -5, 10, true_), none),
	t(integer_for_double, cairo_png(Png, 10, 10, int_move_), none),
	t(drawing_error, cairo_png(Png, 10, 10, cairo_restore), none),
	t(unbound_file, cairo_png(_, 10, 10, true_), none),
	t(missing_png, (cairo_image_surface_create_from_png('no_such_image.png', S4), cairo_surface_status(S4, St),
		cairo_surface_destroy(S4)), St),
	t(enum, findall(N-V, (cairo_enum(N, V), sub_atom(N, 0, _, _, line_cap)), Es), Es),
	maplist(cleanup, [Png, Svg, Pdf]).

true_(_).
fail_(_) :- fail.
raise_(_) :- throw(oops).
int_move_(Cr) :- cairo_move_to(Cr, 1, 2).
