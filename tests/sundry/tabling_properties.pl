% predicate_property/2 answers tabled, tabled(Mode) and incremental as
% SWI does: tabled(variant) for every table, moded ones too,
% tabled(incremental) and tabled(shared) as declared, and incremental for
% an incremental table or dynamic predicate. None of them was answered,
% and asking for incremental or tabled(_) raised a domain error.
% Results match SWI.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

:- dynamic([q/1], [incremental(true)]).
:- dynamic(d/1).
:- table plain/1.
:- table inc/1 as incremental.
:- table sh/1 as shared.
:- table mn(_,min).
:- table lat(_,lattice(j/3)).
:- table ix(index,_,max).
:- table g//1.

j(A, B, C) :- C is max(A, B).

plain(1). inc(X) :- q(X). sh(1). mn(a,1). lat(a,1). ix(a,b,1). g(1) --> [].
q(1). d(1).

show(G) :-
	findall(P, (predicate_property(G, P), (memberchk(P, [tabled, incremental, dynamic]) ; P = tabled(_))), Ps),
	msort(Ps, S),
	(G = M:H -> functor(H, N, A), PI = M:N/A ; functor(G, N, A), PI = N/A),
	write(PI: S), nl.

ask(G, P) :-
	catch((predicate_property(G, P) -> R = yes ; R = no), error(E, _), R = E),
	functor(G, N, A), copy_term(P, P1), numbervars(P1, 0, _),
	write(N/A-P1: R), nl.

main :-
	forall(
		member(G, [plain(_), inc(_), sh(_), mn(_,_), lat(_,_), ix(_,_,_), g(_,_,_), q(_), d(_), j(_,_,_), user:inc(_)]),
		show(G)),
	forall(
		member(G-P, [inc(_)-incremental, plain(_)-incremental, plain(_)-tabled, d(_)-tabled,
			q(_)-incremental, plain(_)-tabled(variant), inc(_)-tabled(incremental), mn(_,_)-tabled(_),
			undefined-tabled, write(_)-incremental, write(_)-tabled(_), nosuch(_)-tabled]),
		ask(G, P)),
	findall(N/A, (predicate_property(H, tabled), functor(H, N, A), memberchk(N, [plain, inc, sh])), L1),
	msort(L1, S1), write(enumerated_tabled: S1), nl,
	findall(N/A, (predicate_property(H, incremental), functor(H, N, A), memberchk(N, [inc, q, d])), L2),
	msort(L2, S2), write(enumerated_incremental: S2), nl.
