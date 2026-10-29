:- initialization(main).
:- use_module(library(freeze)).
:- use_module(library(lists)).

% Frames are pinned for '$list_attributed'/2 only while a mark awaits it.
% A retry into the goal must restore that, so each answer still lists
% exactly the variables attributed for it, after work that reuses frames.

churn :- forall(between(1,200,K), (length(Xs,K), maplist(=(a), Xs))).

fz([]).
fz([V|Vs]) :- freeze(V, true), fz(Vs).

g(I, L) :- member(I, [1,2,3]), length(L, I), fz(L).

redo :- call_residue_vars(g(I, L), Vs), churn, length(Vs, N), length(L, M), write(I-N-M), nl, fail.
redo.

thrown :- catch(call_residue_vars((freeze(_,true), throw(e)), _), e, true), write(caught), nl.

nested :-
	call_residue_vars((freeze(_,true), call_residue_vars(freeze(B,true), In)), Out),
	length(In, NI), length(Out, NO), write(nested(NI,NO)), nl,
	( In = [V], V == B -> write(inner_is_b) ; write(inner_wrong) ), nl.

main :- redo, thrown, nested.
