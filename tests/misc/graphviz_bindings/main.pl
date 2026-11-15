% library(graphviz): DOT from edge lists and ugraphs with attributes and escaping, SVG and plain text, PNG and PDF
% files, layout positions back by vertex, DOT source as it is, and errors, a failed render leaving the next one
% unharmed. Positions and SVG vary by Graphviz version, so only their shape is checked. Run by
% ../graphviz_bindings.sh, which skips it where Graphviz or the FFI is missing. Temporary files stay in '.'.

:- use_module(library(graphviz)).
:- use_module(library(lists)).
:- initialization(main).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (   var(E) -> format("~w: ~q~n", [Label, Show])
	    ;   E = error(graphviz_error(_), Ctx) -> format("~w: raised ~q~n", [Label, error(graphviz_error('...'), Ctx)])
	    ;   format("~w: raised ~q~n", [Label, E])
	    )
	;   format("~w: failed~n", [Label])
	).

as_atom(S, A) :- atom_string(A, S).

main :-
	t(dot_directed, (graphviz_dot([a-b, edge(b, c, [color=red, label="say \"hi\"\n"])], S1,
		[directed(true), graph([rankdir='LR']), node([shape=box]), nodes([d-[label=html('<b>D</b>')]])]), as_atom(S1, A1)), A1),
	t(dot_ugraph, (graphviz_dot([a-[b], b-[a], c-[]], S2, []), as_atom(S2, A2)), A2),
	t(dot_terms, (graphviz_dot([1-f(x), f(x)-'New York'], S3, [directed(true)]), as_atom(S3, A3)), A3),
	t(svg, (graphviz_render([a-b], svg, S4, []), sub_string(S4, _, _, _, "<svg"), sub_string(S4, _, _, _, ">a</text>")), yes),
	t(plain, (graphviz_render([a-b], plain, S5, []), split_string(S5, "\n", "", Ls), findall(W, (member(L, Ls), split_string(L, " ", "", [W0|_]), atom_string(W, W0)), Ws)), Ws),
	t(layout, (graphviz_layout([a-b, b-c], Ps, [directed(true)]), pairs_keys_values_(Ps, Vs, Pts), maplist(float_point, Pts),
		Ps = [_-point(_, Ya), _-point(_, Yb)|_], Ya > Yb), Vs),
	t(layout_engines, forall(member(E, [neato, fdp, circo, twopi]), graphviz_layout([a-b, b-c, c-a], [_, _, _], [layout(E)])), yes),
	t(layout_dot_source, (graphviz_layout(dot("digraph { \"x y\" -> z }"), Ps2, []), pairs_keys_values_(Ps2, Ks2, _)), Ks2),
	F = 'tmp_graphviz_test.png',
	t(png_file, (graphviz_render_file([a-b, b-c], png, F, []), size_file(F, N), N > 100, open(F, read, In, [type(binary)]), get_byte(In, B), close(In)), B),
	catch(delete_file(F), _, true),
	F2 = 'tmp_graphviz_test.pdf',
	t(pdf_file, (graphviz_render_file([a-b], pdf, F2, []), open(F2, read, In2, [type(binary)]), length(Bs, 5), maplist(get_byte(In2), Bs), close(In2), atom_codes(H6, Bs)), H6),
	catch(delete_file(F2), _, true),
	t(syntax_error, graphviz_render(dot("digraph { a -> }"), svg, _, []), x),
	t(bad_engine, graphviz_render([a-b], svg, _, [layout(nosuch)]), x),
	t(binary_text, graphviz_render([a-b], png, _, []), x),
	t(bad_format, graphviz_render_file([a-b], nosuchformat, 'tmp_graphviz_never.out', []), x),
	t(after_failure, (graphviz_render([a-b], svg, S7, []), sub_string(S7, _, _, _, "<svg")), yes),
	t(bad_edge, graphviz_dot([a-b, c], _, []), x),
	t(bad_attribute, graphviz_dot([a-b], _, [node([shape])]), x),
	t(not_list, graphviz_dot(foo, _, []), x).

float_point(point(X, Y)) :- float(X), float(Y).

pairs_keys_values_([], [], []).
pairs_keys_values_([K-V|Ps], [K|Ks], [V|Vs]) :- pairs_keys_values_(Ps, Ks, Vs).
