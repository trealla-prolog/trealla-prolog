% Delimited continuations: reset/3 runs a goal until it calls
% shift/1, then hands back the ball and cont(Rest), the rest of
% the goal, to resume later; none if the goal never shifted.
% That is enough to write effect handlers in plain Prolog.

% An iterator: yield/1 hands each item to whoever is summing.
yield(X) :- shift(yield(X)).

from_list([]).
from_list([X|Xs]) :- yield(X), from_list(Xs).

sum_yields(Goal, Sum) :-
	reset(Goal, Ball, Cont),
	(	Cont == none -> Sum = 0
	;	Ball = yield(X), Cont = cont(Rest),
		sum_yields(Rest, Sum0),
		Sum is X + Sum0
	).

% State as an effect: get_state/1 and set_state/1 ask a handler.
get_state(S) :- shift(get_state(S)).
set_state(S) :- shift(set_state(S)).

run_state(Goal, S0, S) :-
	reset(Goal, Cmd, Cont),
	(	Cont == none -> S = S0
	;	Cont = cont(Rest),
		(	Cmd = get_state(S0) -> run_state(Rest, S0, S)
		;	Cmd = set_state(S1) -> run_state(Rest, S1, S)
		)
	).

incr :- get_state(N), N1 is N + 1, set_state(N1).
%?- sum_yields(from_list([1,2,3,4]), Sum).
%?- run_state((incr, incr, get_state(X), incr), 0, Final).
