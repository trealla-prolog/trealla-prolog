:- module(igraph, [
	igraph_new/2,
	igraph_new/3,
	igraph_free/1,
	igraph_vertices/2,
	igraph_edges/2,
	igraph_degree/2,
	igraph_strength/2,
	igraph_betweenness/2,
	igraph_closeness/2,
	igraph_pagerank/2,
	igraph_pagerank/3,
	igraph_eigenvector_centrality/2,
	igraph_hubs_authorities/3,
	igraph_components/2,
	igraph_strong_components/2,
	igraph_is_connected/1,
	igraph_communities/3,
	igraph_modularity/3,
	igraph_shortest_path/4,
	igraph_distance/4,
	igraph_diameter/2,
	igraph_density/2,
	igraph_transitivity/2,
	igraph_girth/2,
	igraph_mst/2,
	igraph_maxflow/4,
	igraph_cliques/2,
	igraph_largest_cliques/2,
	igraph_clique_number/2,
	igraph_coloring/2,
	igraph_isomorphic/2,
	igraph_subisomorphic/2,
	igraph_automorphisms/2,
	igraph_is_dag/1,
	igraph_topological_sort/2,
	igraph_bridges/2,
	igraph_articulation_points/2,
	igraph_seed/1,
	igraph_version/1
	]).

% Graph algorithms over igraph 1.0 (https://igraph.org/c/)
%
% UBUNTU: build igraph 1.0 from source; the distributions' libigraph-dev is 0.10, whose API differs
% MACOS: brew install igraph
%
%	?- igraph_new([a-b, b-c, c-a, c-d], G), igraph_betweenness(G, B), igraph_free(G).
%	B = [a-0.0,b-0.0,c-2.0,d-0.0].
%
% A graph is an edge list of A-B pairs or a ugraph, a list of Vertex-Neighbours as library(ugraphs)
% makes, its vertices any terms. igraph_new/2,3 builds one as a handle for igraph_free/1; every
% other predicate also takes the list itself, built undirected and unweighted for that call.
% Options of igraph_new/3 are directed(Bool), false by default, weights(Ws), a number for each edge
% of an edge list in its order, and vertices(Vs), ones with no edges. An undirected ugraph's A-B
% and B-A are one edge. Vertices are numbered in standard order, which is the order of results
% given per vertex, as V-Value pairs.
%
% Weights are distances for paths, the diameter and the spanning tree, capacities for maxflow,
% and strengths for centrality and communities. Errors are igraph_error(Code, Message).
%
% Needs an igraph built with 64-bit integers, the default.

:- use_module(library(lists)).
:- use_module(library(assoc)).
:- use_module(library(yall)).

:- foreign_struct(igraph_vs, [sint32, sint64, sint64, sint64]).

:- use_foreign_module('libigraph.so', [
	igraph_setup([], sint),
	igraph_version([-ptr,-sint,-sint,-sint], void),
	igraph_strerror([sint], ccstr),
	igraph_set_error_handler([ptr], ptr),
	igraph_set_warning_handler([ptr], ptr),
	igraph_rng_default([], ptr),
	igraph_rng_seed([ptr,uint64], sint),
	igraph_create([ptr,ptr,sint64,bool], sint),
	igraph_destroy([ptr], void),
	igraph_vcount([ptr], sint64),
	igraph_ecount([ptr], sint64),
	igraph_vector_init([ptr,sint64], sint),
	igraph_vector_init_array([ptr,ptr,sint64], sint),
	igraph_vector_destroy([ptr], void),
	igraph_vector_int_init([ptr,sint64], sint),
	igraph_vector_int_init_array([ptr,ptr,sint64], sint),
	igraph_vector_int_destroy([ptr], void),
	igraph_vector_int_list_init([ptr,sint64], sint),
	igraph_vector_int_list_destroy([ptr], void),
	igraph_vector_int_list_size([ptr], sint64),
	igraph_vector_int_list_get_ptr([ptr,sint64], ptr),
	igraph_matrix_init([ptr,sint64,sint64], sint),
	igraph_matrix_destroy([ptr], void),
	igraph_degree([ptr,ptr,igraph_vs,sint,sint], sint),
	igraph_strength([ptr,ptr,igraph_vs,sint,sint,ptr], sint),
	igraph_betweenness([ptr,ptr,ptr,igraph_vs,bool,bool], sint),
	igraph_closeness([ptr,ptr,ptr,ptr,igraph_vs,sint,ptr,bool], sint),
	igraph_pagerank([ptr,ptr,ptr,-double,double,bool,igraph_vs,sint,ptr], sint),
	igraph_eigenvector_centrality([ptr,ptr,-double,sint,ptr,ptr], sint),
	igraph_hub_and_authority_scores([ptr,ptr,ptr,-double,ptr,ptr], sint),
	igraph_connected_components([ptr,ptr,ptr,-sint64,sint], sint),
	igraph_is_connected([ptr,-bool,sint], sint),
	igraph_community_multilevel([ptr,ptr,double,ptr,ptr,ptr], sint),
	igraph_community_leiden_simple([ptr,ptr,sint,double,double,bool,sint64,ptr,-sint64,-double], sint),
	igraph_community_label_propagation([ptr,ptr,sint,ptr,ptr,ptr,sint], sint),
	igraph_modularity([ptr,ptr,ptr,double,bool,-double], sint),
	igraph_get_shortest_path_dijkstra([ptr,ptr,ptr,sint64,sint64,ptr,sint], sint),
	igraph_distances_dijkstra([ptr,ptr,igraph_vs,igraph_vs,ptr,sint], sint),
	igraph_diameter([ptr,ptr,-double,ptr,ptr,ptr,ptr,bool,bool], sint),
	igraph_density([ptr,ptr,-double,bool], sint),
	igraph_transitivity_undirected([ptr,-double,sint], sint),
	igraph_girth([ptr,-double,ptr], sint),
	igraph_minimum_spanning_tree([ptr,ptr,ptr,sint], sint),
	igraph_maxflow_value([ptr,-double,sint64,sint64,ptr,ptr], sint),
	igraph_maximal_cliques([ptr,ptr,sint64,sint64,sint64], sint),
	igraph_largest_cliques([ptr,ptr], sint),
	igraph_clique_number([ptr,-sint64], sint),
	igraph_vertex_coloring_greedy([ptr,ptr,sint], sint),
	igraph_isomorphic([ptr,ptr,-bool], sint),
	igraph_subisomorphic([ptr,ptr,-bool], sint),
	igraph_count_automorphisms([ptr,ptr,-double], sint),
	igraph_is_dag([ptr,-bool], sint),
	igraph_topological_sorting([ptr,ptr,sint], sint),
	igraph_bridges([ptr,ptr], sint),
	igraph_articulation_points([ptr,ptr], sint)
	]).

:- initialization(catch(igraph_setup(_), _, true)).

:- dynamic(handlers_/2).

% igraph's own error handler abort()s, so each call runs with the ones that ignore errors and
% warnings, which free what the failed call held, and its return code says what went wrong

handlers_ok_(E, W) :-
	handlers_(E, W),
	!.
handlers_ok_(E, W) :-
	'$dlopen'('libigraph.so', 0, H),
	'$dlsym'(H, igraph_error_handler_ignore, E),
	'$dlsym'(H, igraph_warning_handler_ignore, W),
	assertz(handlers_(E, W)).

quiet_(Goal) :-
	handlers_ok_(E, W),
	setup_call_cleanup(
		(	igraph_set_error_handler(E, E0),
			igraph_set_warning_handler(W, W0)
		),
		Goal,
		(	igraph_set_error_handler(E0, _),
			igraph_set_warning_handler(W0, _)
		)).

% Rc, an igraph_error_t, as an exception unless 0

rc_(0, _) :- !.
rc_(Rc, Ctx) :-
	igraph_strerror(Rc, Msg),
	throw(error(igraph_error(Rc, Msg), Ctx)).

call_(Goal, Ctx) :-
	quiet_(call(Goal, Rc)),
	rc_(Rc, Ctx).

igraph_version(Major-Minor-Patch) :-
	igraph_version(_, Major, Minor, Patch).

igraph_seed(Seed) :-
	must_be_(integer, Seed, igraph_seed/1),
	igraph_rng_default(R),
	call_(igraph_rng_seed(R, Seed), igraph_seed/1).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Graphs

% igraph(Graph, Vertices, Index, Edges, Weights, Directed): the igraph_t, v(V0, ...) by index, an
% assoc of each vertex's index, e(A-B, ...) by edge id, and an igraph_vector_t of weights or 0

igraph_new(Spec, G) :-
	igraph_new(Spec, G, []).

igraph_new(Spec, G, Options) :-
	must_be_(list, Options, igraph_new/3),
	option_(directed(D), Options, false),
	option_(vertices(Vs0), Options, []),
	must_be_(boolean, D, igraph_new/3),
	must_be_(list, Vs0, igraph_new/3),
	spec_(Spec, D, Vs0, Vs, Es, Kind, igraph_new/3),
	(	memberchk(weights(Ws), Options)
	->	(	Kind == edges -> true
		;	throw(error(domain_error(edge_list, Spec), igraph_new/3))
		),
		weights_(Ws, Es, igraph_new/3)
	;	Ws = []
	),
	new_(Vs, Es, Ws, D, G).

option_(O, Os, _) :- memberchk(O, Os), !.
option_(O, _, Default) :- arg(1, O, Default).

% The vertices, sorted, and edges of a ugraph or edge list

spec_(Spec, _, _, _, _, _, Ctx) :-
	\+ is_list(Spec),
	!,
	(	var(Spec) -> throw(error(instantiation_error, Ctx))
	;	throw(error(type_error(list, Spec), Ctx))
	).
spec_(Spec, D, Vs0, Vs, Es, ugraph, Ctx) :-
	Spec = [_-Ns|_],
	is_list(Ns),
	!,
	ugraph_edges_(Spec, Es0, Ks, Ctx),
	(	D == true -> Es = Es0
	;	maplist(undirected_, Es0, Es1),
		sort(Es1, Es)
	),
	edge_vertices_(Es, Ns1),
	append([Ks, Ns1, Vs0], All),
	sort(All, Vs).
spec_(Spec, _, Vs0, Vs, Spec, edges, Ctx) :-
	(	member(E, Spec), \+ (nonvar(E), E = _-_)
	->	(	var(E) -> throw(error(instantiation_error, Ctx))
		;	throw(error(type_error(pair, E), Ctx))
		)
	;	true
	),
	edge_vertices_(Spec, Ns),
	append(Ns, Vs0, All),
	sort(All, Vs).

ugraph_edges_([], [], [], _).
ugraph_edges_([P|Ps], Es, [V|Ks], Ctx) :-
	(	nonvar(P), P = V-Ns, is_list(Ns) -> true
	;	throw(error(type_error(ugraph_entry, P), Ctx))
	),
	findall(V-N, member(N, Ns), Es0),
	append(Es0, Es1, Es),
	ugraph_edges_(Ps, Es1, Ks, Ctx).

undirected_(A-B, E) :- ( A @> B -> E = B-A ; E = A-B ).

edge_vertices_(Es, Vs) :-
	findall(V, (member(A-B, Es), (V = A ; V = B)), Vs).

weights_(Ws, Es, Ctx) :-
	must_be_(list, Ws, Ctx),
	length(Es, N),
	(	length(Ws, N) -> true
	;	throw(error(domain_error(weights_for_each_edge, Ws), Ctx))
	),
	(	member(W, Ws), \+ number(W)
	->	throw(error(type_error(number, W), Ctx))
	;	true
	).

new_(Vs, Es, Ws, D, igraph(P, VT, Index, ET, WV, D)) :-
	Ctx = igraph_new/3,
	length(Vs, NV),
	numlist_(0, NV, Is),
	pairs_(Vs, Is, VIs),
	list_to_assoc(VIs, Index),
	VT =.. [v|Vs],
	ET =.. [e|Es],
	findall(I, (member(A-B, Es), (get_assoc(A, Index, I) ; get_assoc(B, Index, I))), Flat),
	bool_int_(D, DI),
	block_(64, P),
	catch(
		(	with_int_vec_(Flat, EV, call_(igraph_create(P, EV, NV, DI), Ctx)),
			(	Ws == [] -> WV = 0
			;	maplist(to_float_, Ws, Fs),
				vec_from_(double, Fs, WV, Ctx)
			)
		),
		Err,
		(	'$free_struct_pointer'(P), throw(Err) )).

igraph_free(igraph(P, _, _, _, W, _)) :-
	!,
	igraph_destroy(P),
	'$free_struct_pointer'(P),
	(	W =:= 0 -> true
	;	free_vec_(double, W)
	).
igraph_free(G) :-
	throw(error(type_error(igraph, G), igraph_free/1)).

% Goal on G, a handle, or a graph from Spec for this call alone

with_graph_(Spec, G, Goal, _) :-
	nonvar(Spec),
	Spec = igraph(_, _, _, _, _, _),
	!,
	G = Spec,
	call(Goal).
with_graph_(Spec, G, Goal, Ctx) :-
	catch(igraph_new(Spec, G0), error(E, igraph_new/3), throw(error(E, Ctx))),
	setup_call_cleanup(true, (G = G0, call(Goal)), igraph_free(G0)).

igraph_vertices(Spec, Vs) :-
	with_graph_(Spec, igraph(_, VT, _, _, _, _), VT =.. [_|Vs], igraph_vertices/2).

igraph_edges(Spec, Es) :-
	with_graph_(Spec, igraph(_, _, _, ET, _, _), ET =.. [_|Es], igraph_edges/2).

vertex_index_(Index, V, I, Ctx) :-
	(	get_assoc(V, Index, I) -> true
	;	var(V) -> throw(error(instantiation_error, Ctx))
	;	throw(error(existence_error(vertex, V), Ctx))
	).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Centrality, each per vertex as V-Value

vs_all_([igraph_vs, 0, 0, 0, 0]).
vs_1_(I, [igraph_vs, 3, I, 0, 0]).

% Edges at each vertex, a self-loop counted twice

igraph_degree(Spec, Ps) :-
	vs_all_(All),
	per_vertex_(Spec, int, [G,_,R,Rc]>>igraph_degree(G, R, All, 3, 1, Rc), Ps, igraph_degree/2).

% Weights at each vertex, or its degree for an unweighted graph

igraph_strength(Spec, Ps) :-
	vs_all_(All),
	per_vertex_(Spec, double, [G,W,R,Rc]>>igraph_strength(G, R, All, 3, 1, W, Rc), Ps, igraph_strength/2).

igraph_betweenness(Spec, Ps) :-
	vs_all_(All),
	per_vertex_(Spec, double, [G,W,R,Rc]>>igraph_betweenness(G, W, R, All, 1, 0, Rc), Ps, igraph_betweenness/2).

% Inverse average distance to the vertices reachable, following edges out; 0.0 for one that reaches
% none, where igraph has NaN

igraph_closeness(Spec, Ps) :-
	vs_all_(All),
	per_vertex_(Spec, double, [G,W,R,Rc]>>igraph_closeness(G, R, 0, 0, All, 1, W, 1, Rc), Ps0, igraph_closeness/2),
	maplist(nan_zero_, Ps0, Ps).

nan_zero_(V-X, V-Y) :- ( X =:= X -> Y = X ; Y = 0.0 ).

igraph_pagerank(Spec, Ps) :-
	igraph_pagerank(Spec, 0.85, Ps).

igraph_pagerank(Spec, Damping, Ps) :-
	must_be_(number, Damping, igraph_pagerank/3),
	D is float(Damping),
	vs_all_(All),
	per_vertex_(Spec, double, [G,W,R,Rc]>>igraph_pagerank(G, W, R, _, D, 1, All, 2, 0, Rc), Ps, igraph_pagerank/3).

igraph_eigenvector_centrality(Spec, Ps) :-
	per_vertex_(Spec, double, [G,W,R,Rc]>>igraph_eigenvector_centrality(G, R, _, 2, W, 0, Rc), Ps, igraph_eigenvector_centrality/2).

igraph_hubs_authorities(Spec, Hs, As) :-
	Ctx = igraph_hubs_authorities/3,
	with_graph_(Spec, igraph(G, VT, _, _, W, _),
		with_vec_(double, H,
			with_vec_(double, A,
				(	call_(igraph_hub_and_authority_scores(G, H, A, _, W, 0), Ctx),
					vec_to_(double, H, HL),
					vec_to_(double, A, AL),
					by_vertex_(VT, HL, Hs),
					by_vertex_(VT, AL, As)
				), Ctx), Ctx), Ctx).

% call(Goal, G, W, R, Rc) with an empty result vector R of Kind, read back per vertex; a lambda's
% free variables are copied, so what it binds is lost, and so results come back through R alone

per_vertex_(Spec, Kind, Goal, Ps, Ctx) :-
	with_graph_(Spec, igraph(G, VT, _, _, W, _),
		with_vec_(Kind, R,
			(	call_(call(Goal, G, W, R), Ctx),
				vec_to_(Kind, R, L),
				by_vertex_(VT, L, Ps)
			), Ctx), Ctx).

by_vertex_(VT, L, Ps) :-
	VT =.. [_|Vs],
	pairs_(Vs, L, Ps).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Components and communities, each a list of vertex lists

igraph_components(Spec, Cs) :-
	components_(Spec, 1, Cs, igraph_components/2).

igraph_strong_components(Spec, Cs) :-
	components_(Spec, 2, Cs, igraph_strong_components/2).

components_(Spec, Mode, Cs, Ctx) :-
	with_graph_(Spec, igraph(G, VT, _, _, _, _),
		with_vec_(int, M,
			(	call_(igraph_connected_components(G, M, 0, _, Mode), Ctx),
				vec_to_(int, M, L),
				groups_(VT, L, Cs)
			), Ctx), Ctx).

igraph_is_connected(Spec) :-
	Ctx = igraph_is_connected/1,
	with_graph_(Spec, igraph(G, _, _, _, _, _),
		call_(igraph_is_connected(G, B, 1), Ctx), Ctx),
	B =:= 1.

% Method: louvain, leiden or label_propagation, each maximizing modularity; random, as igraph_seed/1 fixes

igraph_communities(Spec, Method, Cs) :-
	Ctx = igraph_communities/3,
	must_be_(atom, Method, Ctx),
	(	memberchk(Method, [louvain, leiden, label_propagation]) -> true
	;	throw(error(domain_error(community_method, Method), Ctx))
	),
	with_graph_(Spec, igraph(G, VT, _, _, W, _),
		with_vec_(int, M,
			(	community_(Method, G, W, M, Ctx),
				vec_to_(int, M, L),
				groups_(VT, L, Cs)
			), Ctx), Ctx).

community_(louvain, G, W, M, Ctx) :-
	call_(igraph_community_multilevel(G, W, 1.0, M, 0, 0), Ctx).
community_(leiden, G, W, M, Ctx) :-
	call_(igraph_community_leiden_simple(G, W, 0, 1.0, 0.01, 0, -1, M, _, _), Ctx).
community_(label_propagation, G, W, M, Ctx) :-
	call_(igraph_community_label_propagation(G, M, 3, W, 0, 0, 0), Ctx).

% Q, the modularity of a partition of the vertices into communities, each a list of them

igraph_modularity(Spec, Cs, Q) :-
	Ctx = igraph_modularity/3,
	must_be_(list, Cs, Ctx),
	with_graph_(Spec, igraph(G, VT, Index, _, W, D),
		(	functor(VT, _, NV),
			membership_(Cs, Index, NV, L, Ctx),
			bool_int_(D, DI),
			with_int_vec_(L, M, call_(igraph_modularity(G, M, W, 1.0, DI, Q), Ctx))
		), Ctx).

membership_(Cs, Index, NV, L, Ctx) :-
	findall(I-C, (nth0(C, Cs, Vs), member(V, Vs), vertex_index_(Index, V, I, Ctx)), Ps0),
	keysort(Ps0, Ps),
	pairs_(Is, L, Ps),
	(	numlist_(0, NV, Is) -> true
	;	throw(error(domain_error(partition_of_vertices, Cs), Ctx))
	).

% Vertex lists by membership number, in order of first member

groups_(VT, L, Gs) :-
	VT =.. [_|Vs],
	pairs_(L, Vs, Ps),
	group_first_(Ps, Gs).

group_first_([], []).
group_first_([K-V|Ps], [[V|Vs]|Gs]) :-
	partition_key_(Ps, K, Vs, Rest),
	group_first_(Rest, Gs).

partition_key_([], _, [], []).
partition_key_([K1-V|Ps], K, Vs, Rest) :-
	(	K1 == K -> Vs = [V|Vs1], partition_key_(Ps, K, Vs1, Rest)
	;	Rest = [K1-V|Rest1], partition_key_(Ps, K, Vs, Rest1)
	).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Paths and distances, weights as lengths; fails where none

igraph_shortest_path(Spec, From, To, Path) :-
	Ctx = igraph_shortest_path/4,
	with_graph_(Spec, igraph(G, VT, Index, _, W, _),
		(	vertex_index_(Index, From, F, Ctx),
			vertex_index_(Index, To, T, Ctx),
			with_vec_(int, P,
				(	call_(igraph_get_shortest_path_dijkstra(G, P, 0, F, T, W, 1), Ctx),
					vec_to_(int, P, Is)
				), Ctx),
			Is \== [],
			maplist(vertex_(VT), Is, Path)
		), Ctx).

vertex_(VT, I, V) :- I1 is I + 1, arg(I1, VT, V).

igraph_distance(Spec, From, To, D) :-
	Ctx = igraph_distance/4,
	with_graph_(Spec, igraph(G, _, Index, _, W, _),
		(	vertex_index_(Index, From, F, Ctx),
			vertex_index_(Index, To, T, Ctx),
			vs_1_(F, VF),
			vs_1_(T, VT),
			with_matrix_(M,
				(	call_(igraph_distances_dijkstra(G, M, VF, VT, W, 1), Ctx),
					vec_to_(double, M, [D0])
				), Ctx)
		), Ctx),
	finite_(D0),
	(	W \== 0 -> D = D0 ; D is truncate(D0) ).

% The longest of the shortest paths, between vertices connected

igraph_diameter(Spec, D) :-
	Ctx = igraph_diameter/2,
	with_graph_(Spec, igraph(G, _, _, _, W, _),
		call_(igraph_diameter(G, W, D0, 0, 0, 0, 0, 1, 1), Ctx), Ctx),
	finite_(D0),
	(	W \== 0 -> D = D0 ; D is truncate(D0) ).

igraph_density(Spec, D) :-
	Ctx = igraph_density/2,
	with_graph_(Spec, igraph(G, _, _, _, _, _),
		call_(igraph_density(G, 0, D, 0), Ctx), Ctx).

% Triangles over connected triples, 0.0 if there are none

igraph_transitivity(Spec, T) :-
	Ctx = igraph_transitivity/2,
	with_graph_(Spec, igraph(G, _, _, _, _, _),
		call_(igraph_transitivity_undirected(G, T, 1), Ctx), Ctx).

% The length of the shortest cycle; fails for a forest

igraph_girth(Spec, N) :-
	Ctx = igraph_girth/2,
	with_graph_(Spec, igraph(G, _, _, _, _, _),
		call_(igraph_girth(G, N0, 0), Ctx), Ctx),
	finite_(N0),
	N0 > 0.0,
	N is truncate(N0).

finite_(X) :- X =:= X, X < 1.0e308, X > -1.0e308.

% The edges of a spanning tree, a minimum one by weight

igraph_mst(Spec, Es) :-
	edge_set_([G,W,R,Rc]>>igraph_minimum_spanning_tree(G, R, W, 0, Rc), Spec, Es, igraph_mst/2).

igraph_bridges(Spec, Es) :-
	edge_set_([G,_,R,Rc]>>igraph_bridges(G, R, Rc), Spec, Es, igraph_bridges/2).

edge_set_(Goal, Spec, Es, Ctx) :-
	with_graph_(Spec, igraph(G, _, _, ET, W, _),
		with_vec_(int, R,
			(	call_(call(Goal, G, W, R), Ctx),
				vec_to_(int, R, Is0),
				msort(Is0, Is),
				maplist(edge_(ET), Is, Es)
			), Ctx), Ctx).

edge_(ET, I, E) :- I1 is I + 1, arg(I1, ET, E).

% The maximum flow from Source to Target, weights as capacities, each 1 if unweighted

igraph_maxflow(Spec, Source, Target, Flow) :-
	Ctx = igraph_maxflow/4,
	with_graph_(Spec, igraph(G, _, Index, _, W, _),
		(	vertex_index_(Index, Source, S, Ctx),
			vertex_index_(Index, Target, T, Ctx),
			(	S =:= T -> throw(error(domain_error(distinct_vertices, Source-Target), Ctx))
			;	true
			),
			call_(igraph_maxflow_value(G, Flow, S, T, W, 0), Ctx)
		), Ctx).

igraph_articulation_points(Spec, Vs) :-
	Ctx = igraph_articulation_points/2,
	with_graph_(Spec, igraph(G, VT, _, _, _, _),
		with_vec_(int, R,
			(	call_(igraph_articulation_points(G, R), Ctx),
				vec_to_(int, R, Is0),
				msort(Is0, Is),
				maplist(vertex_(VT), Is, Vs)
			), Ctx), Ctx).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Cliques, colouring and structure

igraph_cliques(Spec, Cs) :-
	cliques_([G,R,Rc]>>igraph_maximal_cliques(G, R, 0, 0, -1, Rc), Spec, Cs, igraph_cliques/2).

igraph_largest_cliques(Spec, Cs) :-
	cliques_([G,R,Rc]>>igraph_largest_cliques(G, R, Rc), Spec, Cs, igraph_largest_cliques/2).

cliques_(Goal, Spec, Cs, Ctx) :-
	with_graph_(Spec, igraph(G, VT, _, _, _, _),
		with_int_list_(R,
			(	call_(call(Goal, G, R), Ctx),
				igraph_vector_int_list_size(R, N),
				N1 is N - 1,
				findall(C,
					(	between(0, N1, I),
						igraph_vector_int_list_get_ptr(R, I, P),
						vec_to_(int, P, Is0),
						msort(Is0, Is),
						maplist(vertex_(VT), Is, C)
					), Cs0),
				msort(Cs0, Cs)
			), Ctx), Ctx).

igraph_clique_number(Spec, N) :-
	Ctx = igraph_clique_number/2,
	with_graph_(Spec, igraph(G, _, _, _, _, _),
		call_(igraph_clique_number(G, N), Ctx), Ctx).

% A colour, from 0, for each vertex as V-Colour, no two adjacent the same, by the greedy heuristic

igraph_coloring(Spec, Ps) :-
	per_vertex_(Spec, int, [G,_,R,Rc]>>igraph_vertex_coloring_greedy(G, R, 0, Rc), Ps, igraph_coloring/2).

igraph_isomorphic(Spec1, Spec2) :-
	iso_(igraph_isomorphic, Spec1, Spec2, igraph_isomorphic/2).

% Whether Spec2 is isomorphic to a subgraph of Spec1

igraph_subisomorphic(Spec1, Spec2) :-
	iso_(igraph_subisomorphic, Spec1, Spec2, igraph_subisomorphic/2).

iso_(F, Spec1, Spec2, Ctx) :-
	with_graph_(Spec1, igraph(G1, _, _, _, _, _),
		with_graph_(Spec2, igraph(G2, _, _, _, _, _),
			call_(call(F, G1, G2, B), Ctx), Ctx), Ctx),
	B =:= 1.

igraph_automorphisms(Spec, N) :-
	Ctx = igraph_automorphisms/2,
	with_graph_(Spec, igraph(G, _, _, _, _, _),
		call_(igraph_count_automorphisms(G, 0, N0), Ctx), Ctx),
	N is truncate(N0).

igraph_is_dag(Spec) :-
	Ctx = igraph_is_dag/1,
	with_graph_(Spec, igraph(G, _, _, _, _, _),
		call_(igraph_is_dag(G, B), Ctx), Ctx),
	B =:= 1.

% The vertices of a DAG with each before those its edges go to; fails if there's a cycle

igraph_topological_sort(Spec, Vs) :-
	Ctx = igraph_topological_sort/2,
	with_graph_(Spec, igraph(G, VT, _, _, _, _),
		(	call_(igraph_is_dag(G, B), Ctx),
			B =:= 1,
			with_vec_(int, R,
				(	call_(igraph_topological_sorting(G, R, 1), Ctx),
					vec_to_(int, R, Is),
					maplist(vertex_(VT), Is, Vs)
				), Ctx)
		), Ctx).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% igraph's vectors, matrices and lists, which the caller allocates: zeroed blocks of uint64, larger
% than the structs, a vector being three pointers and igraph_t about 180 bytes

block_(N, P) :-
	length(Zs, N),
	maplist(=(0), Zs),
	'$array_to_pointer'(uint64, Zs, P).

elem_(double, double).
elem_(int, sint64).

init_(double, V, Rc) :- igraph_vector_init(V, 0, Rc).
init_(int, V, Rc) :- igraph_vector_int_init(V, 0, Rc).

destroy_(double, V) :- igraph_vector_destroy(V).
destroy_(int, V) :- igraph_vector_int_destroy(V).

new_vec_(Kind, V, Ctx) :-
	block_(4, V),
	init_(Kind, V, Rc),
	(	Rc =:= 0 -> true
	;	'$free_struct_pointer'(V), rc_(Rc, Ctx)
	).

free_vec_(Kind, V) :-
	destroy_(Kind, V),
	'$free_struct_pointer'(V).

with_vec_(Kind, V, Goal, Ctx) :-
	setup_call_cleanup(new_vec_(Kind, V, Ctx), Goal, free_vec_(Kind, V)).

% An igraph vector holding List, which must be freed with free_vec_/2

vec_from_(Kind, L, V, Ctx) :-
	elem_(Kind, T),
	length(L, N),
	block_(4, V),
	setup_call_cleanup(
		'$array_to_pointer'(T, L, D),
		(	Kind == double -> igraph_vector_init_array(V, D, N, Rc)
		;	igraph_vector_int_init_array(V, D, N, Rc)
		),
		'$free_struct_pointer'(D)),
	(	Rc =:= 0 -> true
	;	'$free_struct_pointer'(V), rc_(Rc, Ctx)
	).

with_int_vec_(L, V, Goal) :-
	setup_call_cleanup(vec_from_(int, L, V, igraph_new/3), Goal, free_vec_(int, V)).

% The elements of a vector, from its begin and end pointers

vec_to_(Kind, V, L) :-
	elem_(Kind, T),
	'$pointer_to_array'(ptr, V, 3, [B, _, E]),
	N is (E - B) // 8,
	(	N =:= 0 -> L = []
	;	'$pointer_to_array'(T, B, N, L)
	).

% A matrix starts with its data vector, column by column

with_matrix_(M, Goal, Ctx) :-
	setup_call_cleanup(
		(	block_(8, M),
			igraph_matrix_init(M, 0, 0, Rc),
			rc_(Rc, Ctx)
		),
		Goal,
		(	igraph_matrix_destroy(M),
			'$free_struct_pointer'(M)
		)).

with_int_list_(L, Goal, Ctx) :-
	setup_call_cleanup(
		(	block_(8, L),
			igraph_vector_int_list_init(L, 0, Rc),
			rc_(Rc, Ctx)
		),
		Goal,
		(	igraph_vector_int_list_destroy(L),
			'$free_struct_pointer'(L)
		)).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

numlist_(L, H, []) :- L >= H, !.
numlist_(L, H, [L|T]) :- L1 is L + 1, numlist_(L1, H, T).

pairs_([], [], []).
pairs_([K|Ks], [V|Vs], [K-V|Ps]) :- pairs_(Ks, Vs, Ps).

to_float_(X, F) :- F is float(X).

bool_int_(true, 1).
bool_int_(false, 0).

must_be_(_, X, Ctx) :- var(X), !, throw(error(instantiation_error, Ctx)).
must_be_(integer, X, Ctx) :- !, (integer(X) -> true ; throw(error(type_error(integer, X), Ctx))).
must_be_(number, X, Ctx) :- !, (number(X) -> true ; throw(error(type_error(number, X), Ctx))).
must_be_(atom, X, Ctx) :- !, (atom(X) -> true ; throw(error(type_error(atom, X), Ctx))).
must_be_(boolean, X, Ctx) :- !, ((X == true ; X == false) -> true ; throw(error(type_error(boolean, X), Ctx))).
must_be_(list, X, Ctx) :- (is_list(X) -> true ; throw(error(type_error(list, X), Ctx))).
