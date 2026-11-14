% Asserting into a module before its library is loaded, as scryer#3507 does with clpz:monotonic,
% made an empty module that use_module/1 then took as loaded: no clpz operators or exports.

:- initialization(main).

:- assertz(clpz:monotonic).
:- use_module(library(clpz)).
:- assertz(assoc:extra).
:- use_module(library(assoc)).

main :-
	catch(X #= Y, error(E, C), true), functor(C, F, N), print(E-F/N), nl,
	#A #= #B + 1, B = 2, print(A), nl,
	empty_assoc(T), put_assoc(k, T, v, T1), get_assoc(k, T1, V), print(V), nl,
	( assoc:extra -> print(kept) ; print(lost) ), nl.
