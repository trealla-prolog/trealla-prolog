/**
Provides predicate `dif/2`. `dif/2` is a constraint that is true only if both of its
arguments are different terms.
*/

:- module(dif, [dif/2]).

:- use_module(library(atts)).
:- use_module(library(dcgs)).
:- use_module(library(lists), [append/3]).

:- attribute dif/1.

put_dif_att(Var, X, Y) :-
    (   get_atts(Var, +dif(Z)) ->
	    sort([X \== Y | Z], NewZ),
	    put_atts(Var, +dif(NewZ))
    ;   put_atts(Var, +dif([X \== Y]))
    ).

dif_set_variables([], _, _).
dif_set_variables([Var|Vars], X, Y) :-
    put_dif_att(Var, X, Y),
    dif_set_variables(Vars, X, Y).

remove_goal([], _, []).
remove_goal([G0|G0s], Goal0, Goals) :-
    (   G0 == Goal0 ->
        remove_goal(G0s, Goal0, Goals)
    ;   Goals = [G0|Goals1],
        remove_goal(G0s, Goal0, Goals1)
    ).

vars_remove_goal([], _).
vars_remove_goal([Var|Vars], Goal0) :-
    (  get_atts(Var, +dif(Goals0)) ->
       remove_goal(Goals0, Goal0, Goals),
       (   Goals = [] ->
           put_atts(Var, -dif(_))
       ;   put_atts(Var, +dif(Goals))
       )
    ;  true
    ),
    vars_remove_goal(Vars, Goal0).

% Apart is whether X \= Y, which wakes attribute hooks and applies the occurs_check flag; the sto watch is paused, and restored even on a throw.
trial_apart(X, Y, Apart) :-
    '$sto_watch'(W, false),
    catch(( X \= Y -> A = true ; A = false ), E, ('$sto_watch'(_, W), throw(E))),
    '$sto_watch'(_, W),
    Apart = A.

% Inspect a plain copy: \=/2 on the attributed terms sees this very
% constraint. Acyclic constraints need no remove-and-repost cycle.
reinforce_goals([], []).
reinforce_goals([(L \== R)|Goals0], [Goal|Goals]) :-
    Goal = (
        copy_term_nat(L-R, LC-RC),
        '$dif_apart'(LC, RC, Apart),
        ( var(Apart) -> dif:trial_apart(LC, RC, Apart) ; true ),
        (   Apart == true ->
            term_variables(L-R, Vars),
            dif:vars_remove_goal(Vars, L \== R)
        ;   acyclic_term(LC),
            acyclic_term(RC),
            unify_with_occurs_check(LC, RC) ->
            L \== R
        ;   term_variables(L-R, Vars),
            dif:vars_remove_goal(Vars, L \== R),
            dif:dif(L, R)
        )
    ),
    reinforce_goals(Goals0, Goals).

append_goals([], _).
append_goals([Var|Vars], Goals) :-
    (   get_atts(Var, +dif(VarGoals)) ->
	    append(Goals, VarGoals, NewGoals0),
	    sort(NewGoals0, NewGoals)
    ;   NewGoals = Goals
    ),
    put_atts(Var, +dif(NewGoals)),
    append_goals(Vars, Goals).

verify_attributes(Var, Value, Goals) :-
    (   get_atts(Var, +dif(Goals0)) ->
	    term_variables(Value, ValueVars),
	    append_goals(ValueVars, Goals0),
        reinforce_goals(Goals0, Goals)
    ;   Goals = []
    ).

%% dif(?X, ?Y).
%
% True iff X and Y are different terms. Unlike `\=/2`, `dif/2` is more declarative because if X and Y can
% unify but they're not yet equal, the decision is delayed, and prevents X and Y to become equal later.
% Examples:
%
% ```
% ?- dif(a, a).
%    false.
% ?- dif(a, b).
%    true.
% ?- dif(X, b).
%    dif:dif(X,b).
% ?- dif(X, b), X = b.
%    false.
% ```
% dif/2 is defined for any two terms, so its trial unifications are not
% subject to occurs check (UWN's dif table, o2 and o3).
dif(X, Y) :-
    X \== Y,
    '$dif_apart'(X, Y, Apart),
    (   Apart == true -> true
    ;   var(Apart), trial_apart(X, Y, true) -> true
    ;   term_variables(dif(X,Y), Vars),
        dif_set_variables(Vars, X, Y)
    ).

gather_dif_goals(_, []) --> [].
gather_dif_goals(V, [(X \== Y) | Goals]) -->
    (  { term_variables(X-Y, [V0 | _]),
         V == V0 } ->
       [dif:dif(X, Y)]
    ;  []
    ),
    gather_dif_goals(V, Goals).

attribute_goals(X) -->
    { get_atts(X, +dif(Goals)) },
    gather_dif_goals(X, Goals),
    { put_atts(X, -dif(_)) }.
