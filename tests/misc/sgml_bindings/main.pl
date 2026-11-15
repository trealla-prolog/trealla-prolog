% library(sgml): XML and HTML from text, files and streams, namespaces, entities and CDATA, whitespace and atom
% options, non-ASCII text, errors by line alone (libxml2's wording varies by version), XPath node sets, numbers,
% booleans, strings and namespaces, and xml_write/3 round trips. Run by ../sgml_bindings.sh, which skips it
% where libxml2 or the FFI is missing. Strings are shown as atoms. Temporary files stay in '.'.

:- use_module(library(sgml)).
:- use_module(library(lists)).
:- initialization(main).

show(S, A) :- string(S), !, atom_string(A, S).
show(T, A) :- compound(T), !, T =.. [F|As], maplist(show, As, Bs), A =.. [F|Bs].
show(T, T).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (   var(E) -> show(Show, S), format("~w: ~q~n", [Label, S])
	    ;   E = error(xml_error(Line, _), Ctx) -> format("~w: raised ~q~n", [Label, error(xml_error(Line, '...'), Ctx)])
	    ;   format("~w: raised ~q~n", [Label, E])
	    )
	;   format("~w: failed~n", [Label])
	).

main :-
	t(elements, load_xml("<a href='x' n='1'>hi <b>there</b><?app go now?></a>", D1, []), D1),
	t(namespaces, load_xml("<feed xmlns='urn:f' xmlns:x='urn:x'><x:t x:k='v'>a</x:t></feed>", D2, []), D2),
	t(space_remove, load_xml("<a>\n  <b>  x y  </b>\n  <c/>\n</a>", D3, [space(remove)]), D3),
	t(space_preserve, load_xml("<a>\n <b/>\n</a>", D4, []), D4),
	t(atoms, (load_xml("<a k='v'>t</a>", [element(a, [k=V], [T])], [cdata(atom), attribute_value(atom)]), atom(V), atom(T)), V/T),
	t(entities_cdata, load_xml("<!DOCTYPE a [<!ENTITY e 'ent'>]><!-- gone --><a>1 &e; 2 &amp; <![CDATA[<raw>]]></a>", D5, []), D5),
	t(non_ascii, load_xml("<a k='é'>ünï ☃</a>", D6, []), D6),
	t(empty_text, load_xml("<a></a>", D7, []), D7),
	t(not_well_formed, load_xml("<a>\n<b></a>", _, []), x),
	t(no_file, load_xml(file('nosuch_sgml_test.xml'), _, []), x),
	t(bad_option, load_xml("<a/>", _, [space(sometimes)]), x),
	F = 'tmp_sgml_test.xml',
	setup_call_cleanup(open(F, write, W), format(W, "<?xml version='1.0'?>~n<r><i>1</i><i>2</i></r>~n", []), close(W)),
	t(file, load_xml(file(F), D8, [space(remove)]), D8),
	t(file_atom, load_xml(F, D9, [space(remove)]), D9),
	t(stream, setup_call_cleanup(open(F, read, R), load_xml(stream(R), D10, [space(remove)]), close(R)), D10),
	t(xpath_file, findall(X, xml_xpath(file(F), '//i/text()', X), Xs), Xs),
	delete_file(F),
	t(html, load_html("<html><body><p class=x>Hello <b>world</b></p></body></html>", D11, [space(remove)]), D11),
	t(html_recover, load_html("<p class=x>a<br>b &amp; c</p>", D12, [space(remove)]), D12),
	t(html_entity, (load_html("<p>a&nbsp;b</p>", D13, []), D13 = [element(html, _, [element(body, _, [element(p, _, [S13])])])], S13 = [_, Nbsp, _], char_code(Nbsp, C13)), C13),
	t(xpath_attrs, findall(H, xml_xpath("<r><a href='1'/><a href='2'/></r>", '//a/@href', H), Hs), Hs),
	t(xpath_elements, findall(E, xml_xpath("<r><a>x</a><b/><a>y</a></r>", '//a', E), Es), Es),
	t(xpath_count, xml_xpath("<r><a/><a/><a/></r>", 'count(//a)', N), N),
	t(xpath_number, xml_xpath("<r/>", '1 div 4', N2), N2),
	t(xpath_boolean, xml_xpath("<r><a/></r>", 'count(//a) > 1', B), B),
	t(xpath_string, xml_xpath("<r><a>x<b>y</b></a></r>", 'string(//a)', S1), S1),
	t(xpath_none, xml_xpath("<r/>", '//a', _), x),
	t(xpath_namespace, findall(E, xml_xpath("<feed xmlns='urn:f'><e>1</e><e>2</e></feed>", '//f:e/text()', E, [namespaces([f='urn:f'])]), Ns), Ns),
	t(xpath_html, findall(T2, xml_xpath("<ul><li>a<li>b</ul>", '//li/text()', T2, [dialect(html)]), T2s), T2s),
	t(xpath_bad, xml_xpath("<r/>", '//a[', _), x),
	t(xpath_prefix, xml_xpath("<r/>", '//q:a', _), x),
	DOM = [element(a, [href="x&\"y<"], ["1 < 2 & 3", element(b, [], []), pi("pi x")]), element(c, [k=1], [element(d, [], ["é"])])],
	t(write, with_output_to(string(W1), (current_output(O1), xml_write(O1, DOM, []))), W1),
	t(write_flat, with_output_to(string(W2), (current_output(O2), xml_write(O2, element(a, [], [element(b, [], [])]), [header(false), layout(false)]))), W2),
	t(round_trip, (with_output_to(string(W3), (current_output(O3), xml_write(O3, element(r, [], DOM), [layout(false)]))), load_xml(W3, [element(r, [], Back)], []), Back == [element(a, [href="x&\"y<"], ["1 < 2 & 3", element(b, [], []), pi("pi x")]), element(c, [k="1"], [element(d, [], ["é"])])]), yes),
	t(write_bad, with_output_to(string(_), (current_output(O4), xml_write(O4, [element(a, [], [f(x)])], []))), x).
