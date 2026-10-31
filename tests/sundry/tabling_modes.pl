% Mode-directed tabling with SWI's modes (DESIGN-tabling-phase2.md
% item 2): first (or -), last, min, max, sum, lattice(PI) and po(PI),
% `index` and `+` for key arguments, and more than one moded argument,
% joined argument by argument.
%
% Every answer below was checked against SWI. The error contexts differ:
% Trealla names the predicate.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

:- table f1(_, first), f2(index, -), l1(+, last), s1(_, sum).

f1(a, 1). f1(a, 2). f1(b, 3).
f2(a, 1). f2(a, 2).
l1(a, 1). l1(a, 2). l1(b, 3).
s1(a, 1). s1(a, 2). s1(a, 4). s1(b, 5).

% Shortest path by lattice, as in SWI's manual, over a graph with a cycle.

:- table path(_, _, lattice(shortest/3)).

edge(a,b). edge(b,c). edge(a,d). edge(d,e). edge(e,c). edge(c,a).

path(X, Y, [X,Y]) :- edge(X, Y).
path(X, Y, P) :- path(X, Z, P0), edge(Z, Y), append(P0, [Y], P).

shortest(P1, P2, P) :-
	length(P1, L1), length(P2, L2),
	( L1 < L2 -> P = P1 ; P = P2 ).

% lattice with the arity left out or qualified; po either way round.

:- table la1(_, lattice(maxl)), la2(_, lattice(user:maxl/3)).
:- table po1(_, po('<'/2)), po2(_, po(>)).

maxl(A, B, C) :- C is max(A, B).

la1(a, 4). la1(a, 9). la1(a, 7).
la2(a, 4). la2(a, 9). la2(a, 7).
po1(a, 1). po1(a, 3). po1(a, 2).
po2(a, 1). po2(a, 3). po2(a, 2).

% Two moded arguments, each joined on its own.

:- table two(_, min, max), mix(_, sum, first).

two(a, 3, 1). two(a, 1, 5). two(b, 2, 2).
mix(a, 1, x). mix(a, 2, y). mix(a, 4, z).

% A lattice that fails keeps what is stored; one that throws raises.

:- table lf(_, lattice(never/3)), lt(_, lattice(boom/3)).

never(_, _, _) :- fail.
boom(_, _, _) :- throw(boom).

lf(a, 1). lf(a, 2).
lt(a, 1). lt(a, 2).

% Answers holding variables, through the join.

:- table lv(_, lattice(newest/3)).

newest(_, N, N).

lv(a, f(_)). lv(a, g(_, _)).

% A moded argument bound when called, at the top and inside a table.

:- table cost(_, min), via(_, min).

cost(x, 3). cost(x, 2).
via(x, C) :- cost(x, 2), C = 0.

all(G) :-
	findall(G, G, L0),
	msort(L0, L),
	writeq(L), nl.

err(G) :-
	catch((G, write(no_error)), error(E, _), writeq(E)),
	nl.

main :-
	all(f1(_, _)),
	all(f2(_, _)),
	all(l1(_, _)),
	all(s1(_, _)),
	all(path(a, c, _)),
	all(path(a, _, _)),
	all(la1(_, _)),
	all(la2(_, _)),
	all(po1(_, _)),
	all(po2(_, _)),
	all(two(_, _, _)),
	all(mix(_, _, _)),
	all(lf(_, _)),
	catch(all(lt(_, _)), B, (writeq(caught(B)), nl)),
	findall(X, lv(a, X), [V]), functor(V, VN, VA), writeq(VN/VA), nl,
	all(cost(_, _)),
	err(cost(x, 3)),
	err(via(x, _)),
	err(phrase(tabling:wrappers(bad(_, foo)), _)),
	err(phrase(tabling:wrappers(bad(_, lattice(maxl/2))), _)).
