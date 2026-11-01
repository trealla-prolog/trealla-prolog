:- initialization(main).
:- use_module(library(dif)).
:- use_module(library(freeze)).

% dif/2 and \=/2 decide a trial unification in C unless an attribute's hooks or the occurs_check flag must have a say.

% A second dif/2 on a constrained variable wakes the first one's hook, which can find the pair already apart.
t(dif_hooks_1, X, (dif(f(X),f(a)), dif(X,a))).
t(dif_hooks_2, X, (dif(X,a), dif(f(X),f(a)))).
t(dif_hooks_3, X-Y, (dif(X,Y), dif(X,Y))).
t(dif_hooks_4, X-Y, (dif(X,Y), dif(f(X),f(Y)))).
t(ne_hooks_1, X, (freeze(X,fail), X \= a)).
t(ne_hooks_2, X, (dif(X,a), X \= a)).
t(ne_hooks_3, X, (dif(X,a), X \= b)).
t(flag_true, X, setup_call_cleanup(set_prolog_flag(occurs_check,true), dif(X,f(X)), set_prolog_flag(occurs_check,false))).
t(flag_error, E, setup_call_cleanup(set_prolog_flag(occurs_check,error), catch(dif(X,f(X)),E,true), set_prolog_flag(occurs_check,false))).

% The sto watch sees \=/2's cycles but not dif/2's, and survives a throw out of dif/2.
t(sto_ne, S, ('$sto_begin', (X \= -X -> true ; true), '$sto_end'(S))).
t(sto_dif, S, ('$sto_begin', dif(X,-X), '$sto_end'(S))).
t(sto_after_throw, S, ('$sto_begin', setup_call_cleanup(set_prolog_flag(occurs_check,error), catch(dif(X,f(X)),_,true), set_prolog_flag(occurs_check,false)), Y = -Y, '$sto_end'(S))).

flat([], []) :- !.
flat([L|Ls], Fs) :- !, flat(L, F1), flat(Ls, F2), append(F1, F2, Fs).
flat(G, [G]).

main :-
    forall(t(Name, V, G),
      ( (   catch(G, Err, true) ->
            (   nonvar(Err) -> R = error(Err)
            ;   copy_term([V], [V1], Gs0), flat(Gs0, Gs), R = yes(V1, Gs)
            )
        ;   R = no
        ),
        numbervars(R, 0, _),
        write_term(Name-R, [quoted(true), numbervars(true)]), nl
      )).
