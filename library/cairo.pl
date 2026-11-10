:- module(cairo, [
	cairo_png/4,
	cairo_svg/4,
	cairo_pdf/4,
	text_extents/3,
	font_extents/2,
	cairo_enum/2,

	cairo_version_string/1,
	cairo_image_surface_create/4,
	cairo_image_surface_create_from_png/2,
	cairo_image_surface_get_width/2,
	cairo_image_surface_get_height/2,
	cairo_svg_surface_create/4,
	cairo_pdf_surface_create/4,
	cairo_surface_write_to_png/3,
	cairo_surface_flush/1,
	cairo_surface_finish/1,
	cairo_surface_destroy/1,
	cairo_surface_status/2,
	cairo_create/2,
	cairo_destroy/1,
	cairo_status/2,
	cairo_status_to_string/2,
	cairo_save/1,
	cairo_restore/1,
	cairo_show_page/1,
	cairo_set_source_rgb/4,
	cairo_set_source_rgba/5,
	cairo_set_source/2,
	cairo_set_source_surface/4,
	cairo_set_line_width/2,
	cairo_set_line_cap/2,
	cairo_set_line_join/2,
	cairo_set_dash/4,
	cairo_set_operator/2,
	cairo_set_fill_rule/2,
	cairo_translate/3,
	cairo_scale/3,
	cairo_rotate/2,
	cairo_identity_matrix/1,
	cairo_new_path/1,
	cairo_new_sub_path/1,
	cairo_close_path/1,
	cairo_move_to/3,
	cairo_line_to/3,
	cairo_curve_to/7,
	cairo_rel_move_to/3,
	cairo_rel_line_to/3,
	cairo_rel_curve_to/7,
	cairo_arc/6,
	cairo_arc_negative/6,
	cairo_rectangle/5,
	cairo_paint/1,
	cairo_paint_with_alpha/2,
	cairo_stroke/1,
	cairo_stroke_preserve/1,
	cairo_fill/1,
	cairo_fill_preserve/1,
	cairo_clip/1,
	cairo_clip_preserve/1,
	cairo_reset_clip/1,
	cairo_select_font_face/4,
	cairo_set_font_size/2,
	cairo_show_text/2,
	cairo_text_path/2,
	cairo_pattern_create_linear/5,
	cairo_pattern_create_radial/7,
	cairo_pattern_add_color_stop_rgb/5,
	cairo_pattern_add_color_stop_rgba/6,
	cairo_pattern_destroy/1
	]).

% 2D vector graphics to PNG, SVG and PDF with cairo (https://www.cairographics.org)
%
% UBUNTU: sudo apt install libcairo2-dev
% MACOS: brew install cairo
%
%	?- A is 2*pi, cairo_png('circle.png', 100, 100, [Cr]>>(
%		cairo_set_source_rgb(Cr, 0.2, 0.4, 0.8),
%		cairo_arc(Cr, 50.0, 50.0, 40.0, 0.0, A),
%		cairo_fill(Cr))).
%
% The cairo_* predicates are the C functions, taking floats where C takes a double and the
% enumerations' integers, which cairo_enum/2 names.

:- use_module(library(lists)).

:- use_foreign_module('libcairo.so', [
	cairo_version_string([], ccstr),
	cairo_image_surface_create([sint,sint,sint], ptr),
	cairo_image_surface_create_from_png([cstr], ptr),
	cairo_image_surface_get_width([ptr], sint),
	cairo_image_surface_get_height([ptr], sint),
	cairo_svg_surface_create([cstr,double,double], ptr),
	cairo_pdf_surface_create([cstr,double,double], ptr),
	cairo_surface_write_to_png([ptr,cstr], sint),
	cairo_surface_flush([ptr], void),
	cairo_surface_finish([ptr], void),
	cairo_surface_destroy([ptr], void),
	cairo_surface_status([ptr], sint),
	cairo_create([ptr], ptr),
	cairo_destroy([ptr], void),
	cairo_status([ptr], sint),
	cairo_status_to_string([sint], ccstr),
	cairo_save([ptr], void),
	cairo_restore([ptr], void),
	cairo_show_page([ptr], void),
	cairo_set_source_rgb([ptr,double,double,double], void),
	cairo_set_source_rgba([ptr,double,double,double,double], void),
	cairo_set_source([ptr,ptr], void),
	cairo_set_source_surface([ptr,ptr,double,double], void),
	cairo_set_line_width([ptr,double], void),
	cairo_set_line_cap([ptr,sint], void),
	cairo_set_line_join([ptr,sint], void),
	cairo_set_dash([ptr,ptr,sint,double], void),
	cairo_set_operator([ptr,sint], void),
	cairo_set_fill_rule([ptr,sint], void),
	cairo_translate([ptr,double,double], void),
	cairo_scale([ptr,double,double], void),
	cairo_rotate([ptr,double], void),
	cairo_identity_matrix([ptr], void),
	cairo_new_path([ptr], void),
	cairo_new_sub_path([ptr], void),
	cairo_close_path([ptr], void),
	cairo_move_to([ptr,double,double], void),
	cairo_line_to([ptr,double,double], void),
	cairo_curve_to([ptr,double,double,double,double,double,double], void),
	cairo_rel_move_to([ptr,double,double], void),
	cairo_rel_line_to([ptr,double,double], void),
	cairo_rel_curve_to([ptr,double,double,double,double,double,double], void),
	cairo_arc([ptr,double,double,double,double,double], void),
	cairo_arc_negative([ptr,double,double,double,double,double], void),
	cairo_rectangle([ptr,double,double,double,double], void),
	cairo_paint([ptr], void),
	cairo_paint_with_alpha([ptr,double], void),
	cairo_stroke([ptr], void),
	cairo_stroke_preserve([ptr], void),
	cairo_fill([ptr], void),
	cairo_fill_preserve([ptr], void),
	cairo_clip([ptr], void),
	cairo_clip_preserve([ptr], void),
	cairo_reset_clip([ptr], void),
	cairo_select_font_face([ptr,cstr,sint,sint], void),
	cairo_set_font_size([ptr,double], void),
	cairo_show_text([ptr,cstr], void),
	cairo_text_path([ptr,cstr], void),
	cairo_text_extents([ptr,cstr,ptr], void),
	cairo_font_extents([ptr,ptr], void),
	cairo_pattern_create_linear([double,double,double,double], ptr),
	cairo_pattern_create_radial([double,double,double,double,double,double], ptr),
	cairo_pattern_add_color_stop_rgb([ptr,double,double,double,double], void),
	cairo_pattern_add_color_stop_rgba([ptr,double,double,double,double,double], void),
	cairo_pattern_destroy([ptr], void)
	]).

:- meta_predicate(cairo_png(+, +, +, 1)).
:- meta_predicate(cairo_svg(+, +, +, 1)).
:- meta_predicate(cairo_pdf(+, +, +, 1)).

% call(Goal, Cr) drawing on a Width by Height image, written to File as PNG once it succeeds

cairo_png(File, Width, Height, Goal) :-
	must_be_(atom, File, cairo_png/4),
	must_be_(integer, Width, cairo_png/4),
	must_be_(integer, Height, cairo_png/4),
	cairo_image_surface_create(0, Width, Height, S),		% CAIRO_FORMAT_ARGB32
	with_surface_(S, Goal, cairo_png/4, write_png_(S, File)).

write_png_(S, File) :-
	cairo_surface_write_to_png(S, File, St),
	status_(St, cairo_png/4).

% call(Goal, Cr) drawing on a Width by Height page in points, written to File as SVG or PDF

cairo_svg(File, Width, Height, Goal) :-
	vector_(cairo_svg_surface_create, File, Width, Height, Goal, cairo_svg/4).

cairo_pdf(File, Width, Height, Goal) :-
	vector_(cairo_pdf_surface_create, File, Width, Height, Goal, cairo_pdf/4).

vector_(Create, File, Width, Height, Goal, Ctx) :-
	must_be_(atom, File, Ctx),
	must_be_(number, Width, Ctx),
	must_be_(number, Height, Ctx),
	W is float(Width),
	H is float(Height),
	call(Create, File, W, H, S),
	with_surface_(S, Goal, Ctx, cairo_surface_finish(S)).

% A context on S for Goal, then Done once it succeeds, cairo's status checked after each; both
% destroyed however it ends

with_surface_(S, Goal, Ctx, Done) :-
	setup_call_cleanup(
		true,
		(	cairo_surface_status(S, St0),
			status_(St0, Ctx),
			setup_call_cleanup(
				cairo_create(S, Cr),
				(	call(Goal, Cr),
					cairo_status(Cr, St1),
					status_(St1, Ctx)
				),
				cairo_destroy(Cr)),
			call(Done),
			cairo_surface_status(S, St2),
			status_(St2, Ctx)
		),
		cairo_surface_destroy(S)).

status_(0, _) :- !.
status_(St, Ctx) :-
	cairo_status_to_string(St, Msg),
	throw(error(cairo_error(St, Msg), Ctx)).

must_be_(_, X, Ctx) :- var(X), !, throw(error(instantiation_error, Ctx)).
must_be_(atom, X, Ctx) :- !, (atom(X) -> true ; throw(error(type_error(atom, X), Ctx))).
must_be_(integer, X, Ctx) :- !, (integer(X) -> true ; throw(error(type_error(integer, X), Ctx))).
must_be_(number, X, Ctx) :- (number(X) -> true ; throw(error(type_error(number, X), Ctx))).

% extents(XBearing, YBearing, Width, Height, XAdvance, YAdvance) of Text in the current font

text_extents(Cr, Text, extents(XB, YB, W, H, XA, YA)) :-
	with_doubles_(6, P, cairo_text_extents(Cr, Text, P), [XB, YB, W, H, XA, YA]).

% extents(Ascent, Descent, Height, MaxXAdvance, MaxYAdvance) of the current font

font_extents(Cr, extents(A, D, H, MX, MY)) :-
	with_doubles_(5, P, cairo_font_extents(Cr, P), [A, D, H, MX, MY]).

with_doubles_(N, P, Goal, Values) :-
	length(Zs, N),
	maplist(=(0.0), Zs),
	setup_call_cleanup(
		'$array_to_pointer'(double, Zs, P),
		(	call(Goal),
			'$pointer_to_array'(double, P, N, Values)
		),
		'$free_struct_pointer'(P)).

% The enumerations cairo's functions take, by name

cairo_enum(format_argb32, 0).
cairo_enum(format_rgb24, 1).
cairo_enum(format_a8, 2).
cairo_enum(format_a1, 3).
cairo_enum(line_cap_butt, 0).
cairo_enum(line_cap_round, 1).
cairo_enum(line_cap_square, 2).
cairo_enum(line_join_miter, 0).
cairo_enum(line_join_round, 1).
cairo_enum(line_join_bevel, 2).
cairo_enum(fill_rule_winding, 0).
cairo_enum(fill_rule_even_odd, 1).
cairo_enum(font_slant_normal, 0).
cairo_enum(font_slant_italic, 1).
cairo_enum(font_slant_oblique, 2).
cairo_enum(font_weight_normal, 0).
cairo_enum(font_weight_bold, 1).
cairo_enum(operator_clear, 0).
cairo_enum(operator_source, 1).
cairo_enum(operator_over, 2).
cairo_enum(operator_in, 3).
cairo_enum(operator_out, 4).
cairo_enum(operator_atop, 5).
cairo_enum(operator_dest, 6).
cairo_enum(operator_dest_over, 7).
cairo_enum(operator_add, 12).
cairo_enum(operator_multiply, 14).
cairo_enum(operator_screen, 15).
