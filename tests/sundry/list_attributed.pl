:- use_module(library(freeze)).
:- use_module(library(dif)).

% '$list_attributed'/2 lists only live attributed variables, never a slot of a frame given up, or reused smaller, since.

live(Vs) :- forall(member(V, Vs), (var(V), get_atts(V, As), As = [_|_])).

report(Name, G) :- ( catch(G, E, (print(Name-error(E)), nl, fail)) -> R = ok ; R = no ), write(Name-R), nl.

merge(X) :- freeze(X, true), freeze(Y, true), X = Y.
listed(Vs) :- '$list_attributed'(0, Vs).

:- initialization(report(merged, (freeze(X,true), freeze(Y,true), X = Y, '$list_attributed'(0, Vs), live(Vs), sort(Vs, [Z]), Z == X))).
:- initialization(report(given_up_frames, (merge(X), listed(Vs), live(Vs), sort(Vs, [Z]), Z == X))).
:- initialization(report(dif_chain, (dif(X,Y), dif(Y,Z), '$list_attributed'(0, Vs), live(Vs), sort(Vs, S), msort([X,Y,Z], S)))).
:- initialization(report(residue_local, (call_residue_vars(freeze(_, false), Vs), length(Vs, 1), live(Vs)))).
:- initialization(report(residue_none, (call_residue_vars(true, Vs), Vs == []))).
