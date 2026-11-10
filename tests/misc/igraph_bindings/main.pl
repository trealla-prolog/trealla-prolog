% library(igraph): building from edge lists and ugraphs, centrality, components, communities, paths and
% distances weighted and not, cliques, colouring, isomorphism, DAGs, bridges, igraph's own errors raised
% rather than aborting, and argument errors. Results cross-checked against networkx. Run by
% ../igraph_bindings.sh, which skips it where igraph or the FFI is missing. Floats to four places.

:- use_module(library(igraph)).
:- use_module(library(lists)).
:- initialization(main).

r(X, Y) :- float(X), !, Y is round(X * 10000) / 10000.
r(T, Y) :- compound(T), !, T =.. [F|As], maplist(r, As, Bs), Y =.. [F|Bs].
r(T, T).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (var(E) -> r(Show, S), format("~w: ~q~n", [Label, S]) ; format("~w: raised ~q~n", [Label, E]))
	;   format("~w: failed~n", [Label])
	).

tri([a-b, b-c, c-a, c-d]).
two([a-b, b-c, c-a, d-e, e-f, f-d, c-d]).

main :-
	igraph_seed(42),
	tri(E), two(E2),
	t(vertices_edges, (igraph_new(E, G), igraph_vertices(G, Vs), igraph_edges(G, Es), igraph_free(G)), Vs/Es),
	t(ugraph, igraph_degree([a-[b,c], b-[a], c-[a], d-[]], D1), D1),
	t(isolated, (igraph_new([], G2, [vertices([x,y])]), igraph_degree(G2, D2), igraph_free(G2)), D2),
	t(empty, igraph_cliques([], C0), C0),
	t(degree, igraph_degree(E, D), D),
	t(betweenness, igraph_betweenness(E, B), B),
	t(closeness, igraph_closeness(E, C), C),
	t(closeness_sink, (igraph_new([a-b], G3, [directed(true)]), igraph_closeness(G3, C3), igraph_free(G3)), C3),
	t(pagerank, igraph_pagerank(E, P), P),
	t(eigenvector, igraph_eigenvector_centrality(E, X), X),
	t(hubs_authorities, (igraph_new([a-b, a-c, b-c], G4, [directed(true)]), igraph_hubs_authorities(G4, H, A), igraph_free(G4)), H/A),
	t(strength, (igraph_new(E, G5, [weights([1, 2, 3, 4.5])]), igraph_strength(G5, S5), igraph_free(G5)), S5),
	t(components, igraph_components([a-b, c-d, e-c], Cs), Cs),
	t(connected, igraph_is_connected(E), yes),
	t(not_connected, igraph_is_connected([a-b, c-d]), yes),
	t(louvain, igraph_communities(E2, louvain, L), L),
	t(leiden, igraph_communities(E2, leiden, L2), L2),
	t(label_propagation, igraph_communities(E2, label_propagation, L3), L3),
	t(modularity, igraph_modularity(E2, [[a,b,c],[d,e,f]], Q), Q),
	t(path, igraph_shortest_path(E, a, d, Pa), Pa),
	t(distance, igraph_distance(E, a, d, Di), Di),
	t(unreachable, igraph_shortest_path([a-b, c-d], a, d, _), x),
	t(weighted, (igraph_new([a-b, b-c, a-c], G6, [weights([1, 1, 5])]),
		igraph_shortest_path(G6, a, c, P6), igraph_distance(G6, a, c, D6), igraph_mst(G6, M6),
		igraph_maxflow(G6, a, c, F6), igraph_diameter(G6, Dm6), igraph_free(G6)), P6/D6/M6/F6/Dm6),
	t(measures, (igraph_diameter(E, Dm), igraph_density(E, De), igraph_transitivity(E, Tr), igraph_girth(E, Gi)), Dm/De/Tr/Gi),
	t(forest_girth, igraph_girth([a-b, b-c], _), x),
	t(cliques, igraph_cliques([a-b, b-c, c-a, c-d, d-e, e-c, f-g], Cl), Cl),
	t(largest_cliques, igraph_largest_cliques(E, Lc), Lc),
	t(clique_number, igraph_clique_number(E, Cn), Cn),
	t(coloring, (igraph_coloring(E, Co), \+ (member(U-V, E), memberchk(U-K, Co), memberchk(V-K, Co))), proper),
	t(isomorphic, igraph_isomorphic([a-b, b-c], [x-y, x-z]), yes),
	t(not_isomorphic, igraph_isomorphic([a-b, b-c], [x-y, z-w]), yes),
	t(subisomorphic, igraph_subisomorphic(E, [x-y, y-z, z-x]), yes),
	t(automorphisms, igraph_automorphisms([a-b, b-c, c-a], Au), Au),
	t(dag, (igraph_new([a-b, b-c, a-c], G7, [directed(true)]), igraph_is_dag(G7), igraph_topological_sort(G7, Ts), igraph_free(G7)), Ts),
	t(cycle, (igraph_new([a-b, b-c, c-a], G8, [directed(true)]), \+ igraph_topological_sort(G8, _),
		igraph_strong_components(G8, Sc), igraph_free(G8)), Sc),
	t(bridges, (igraph_bridges(E, Br), igraph_articulation_points(E, Ap)), Br/Ap),
	t(igraph_error, igraph_pagerank([a-b], 2.0, _), x),
	t(negative_weight, (igraph_new([a-b, b-c], G9, [weights([1, -1])]),
		catch(igraph_shortest_path(G9, a, c, _), Ex, (igraph_free(G9), throw(Ex)))), x),
	t(after_error, igraph_degree([a-b], D10), D10),
	t(no_vertex, igraph_shortest_path(E, a, nosuch, _), x),
	t(weights_count, igraph_new(E, _, [weights([1])]), x),
	t(weights_ugraph, igraph_new([a-[b]], _, [weights([1])]), x),
	t(not_list, igraph_new(foo, _), x),
	t(not_pair, igraph_degree([a-b, c], _), x),
	t(method, igraph_communities(E, nosuch, _), x),
	t(partition, igraph_modularity(E, [[a,b]], _), x),
	t(same_vertex, igraph_maxflow(E, a, a, _), x),
	t(not_graph, igraph_free(foo), x).
