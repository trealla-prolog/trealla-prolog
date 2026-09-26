% retract/1 and clause/2 on a predicate that is also being walked. Three bugs: a retry that resumed
% from a stale prefetch position (clauses answered, even retracted, twice), a call/1 barrier that
% released a predicate reference it never held (the walk lost a clause), and retract of a rule
% that dropped its caller's choicepoint when nothing matched.

:- dynamic(q/2).
:- dynamic(p/1).

:- initialization(main).

% 600 clauses, so the predicate is indexed; the compound second argument defeats the candidate
% filter, so retract's first candidate fails and its loop moves on.

setup :-
	retractall(q(_, _)),
	forall(between(1, 600, I), (A is I mod 3, B is I mod 7, assertz(q(A, B-I)))),
	once(q(0, _)).

walk(Name, G) :-
	setup,
	findall(I, (q(1, _-I), I < 20, (I =:= 4 -> call(G) ; true)), L),
	format("~w: ~w~n", [Name, L]).

main :-
	walk(retract, retract(q(1, _-7))),
	walk(call_retract, call(retract(q(1, _-7)))),
	walk(once_retract, once(retract(q(1, _-7)))),
	walk(clause, clause(q(1, _-7), true)),
	retractall(p(_)), assertz(p(1)), assertz(p(2)),
	findall(X, (member(X, [1, 2, 3]), \+ retract((p(1) :- foo))), L1),
	format("retract rule: ~w~n", [L1]),
	findall(X, (member(X, [1, 2, 3]), \+ retract(p(3))), L2),
	format("retract fact: ~w~n", [L2]).
