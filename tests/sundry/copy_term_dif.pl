:- initialization(main).
:- use_module(library(dif)).
:- use_module(library(freeze)).

% copy_term/3 keeps every dif/2 goal on the copied variables, written once, whichever of a pair's variables is copied.

t(bare, (dif(X,a), copy_term(X,C,Gs)), C-Gs).
t(bare_second, (dif(_,Y), copy_term(Y,C,Gs)), C-Gs).
t(bare_first, (dif(X,_), copy_term(X,C,Gs)), C-Gs).
t(inner, (dif(f(X),f(a)), copy_term(X,C,Gs)), C-Gs).
t(pair, (dif(X,Y), copy_term(X-Y,C,Gs)), C-Gs).
t(pair_reversed, (dif(X,Y), copy_term(Y-X,C,Gs)), C-Gs).
t(chain, (dif(X,Y), dif(Y,Z), copy_term(X-Y-Z,C,Gs)), C-Gs).
t(middle, (dif(_,Y), dif(Y,_), copy_term(Y,C,Gs)), C-Gs).
t(with_freeze, (dif(_,Y), freeze(Y,true), copy_term(Y,C,Gs)), C-Gs).
t(no_originals, (dif(X,Y), copy_term(Y,_,[dif:dif(A,B)]), \+ A == X, \+ B == X, \+ B == Y), ok).

main :-
    forall(t(Name, G, R),
      ( (   G -> numbervars(R, 0, _), Out = R ; Out = no ),
        write_term(Name-Out, [quoted(true), numbervars(true)]), nl
      )).
