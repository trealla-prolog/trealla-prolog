% An identity hook on a rule is called once.

:- dynamic(te_calls/1).

te_calls(0).

term_expansion((te_rule(X) :- B), (te_rule(X) :- B)) :-
	retract(te_calls(N)), N1 is N + 1, assertz(te_calls(N1)).

te_rule(X) :- X = 1.
