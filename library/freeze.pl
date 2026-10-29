:- module(freeze, [freeze/2, frozen/2]).

/** Provides the constraint `freeze/2`.
*/

:- use_module(library(atts)).
:- use_module(library(dcgs)).

:- meta_predicate(freeze(-, 0)).

:- attribute(frozen/1).

verify_attributes(Var, Other, Goals) :-
        get_atts(Var, frozen(Fa)), !,       % are we involved?
        (   var(Other) ->                   % must be attributed then
            (   get_atts(Other,  frozen(Fb)) % has a pending goal?
            ->  put_atts(Other,  frozen((call(Fb),call(Fa)))) % rescue conjunction
            ;   put_atts(Other,  frozen(Fa)) % rescue the pending goal
            ),
            Goals = []
        ;   Goals = [Fa]
        ).
verify_attributes(_, _, []).

%% freeze(Var, Goal)
%
%  Schedules Goal to be executed when Var is instantiated. This can
%  be useful to observe the exact moment a variable becomes bound to a
%  more concrete term, for example when creating animations of search
%  processes. Higher-level constructs such as `phrase_from_file/2` can
%  also be implemented with `freeze/2`, by scheduling a goal that
%  reads additional data from a file as soon as it is needed.

% The attribute goes on X itself: binding X to a fresh attributed
% variable left the toplevel answering T = _A, freeze:freeze(_B,G).
freeze(X, Goal) :-
    (   var(X) ->
        (   get_atts(X, frozen(G0)) ->
            put_atts(X, frozen((call(G0),call(Goal))))
        ;   put_atts(X, frozen(Goal))
        )
    ;   call(Goal)
    ).

attribute_goals(Var) -->
    { get_atts(Var, frozen(Goals)),
      put_atts(Var, -frozen(_)) },
    [freeze:freeze(Var, Goals)].

%% frozen(Var, Goal)

:- use_module(library(lists)).

:- help(freeze(-var,+goal), [iso(false)]).
:- help(frozen(@term,-goal), [iso(false)]).

frozen(Term, Goal) :-
	term_attributed_variables_(Term, Vs),
	collect_atts_(Vs, [], AttsList),
	collect_goals_(Vs, [], Gs),
	reapply_atts_(Vs, AttsList),
	( Gs = [] ->
		Goal = true
	;
		flatten(Gs, Gs2),
		list_to_conjunction(Gs2, Fresh),
		Fresh = Goal
	).

collect_atts_([], AttsList, AttsList).
collect_atts_([X|Tail], AttsList0, AttsList) :-
	get_atts(X, Atts),
	collect_atts_(Tail, [Atts|AttsList0], AttsList).

reapply_atts_([], _).
reapply_atts_([X|Tail], [Atts|Tail2]) :-
	put_atts(X, Atts),
	reapply_atts_(Tail, Tail2).

list_to_conjunction(List0, T) :-
	reverse(List0, List),
	toconjunction_(List, true, T).

toconjunction_([], In, In).
toconjunction_([H|T], true, Out) :- !,
	Out2 = H,
	toconjunction_(T, Out2, Out).
toconjunction_([H|T], In, Out) :-
	Out2 = (H, In),
	toconjunction_(T, Out2, Out).

conjunction_to_list(T, List) :-
	tolist_(T, List).

tolist_((T1,T2), [T1|Rest]) :- !,
	tolist_(T2, Rest).
tolist_(T, [T|[]]).

:- help(list_to_conjunction(?list,?list), [iso(false), desc('Does as it says.')]).
