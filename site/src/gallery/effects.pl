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

% State as an effect: get/1 and put/1 ask a handler for it.
get(S) :- shift(get(S)).
put(S) :- shift(put(S)).

run_state(Goal, S0, S) :-
	reset(Goal, Cmd, Cont),
	(	Cont == none -> S = S0
	;	Cont = cont(Rest),
		(	Cmd = get(S0) -> run_state(Rest, S0, S)
		;	Cmd = put(S1) -> run_state(Rest, S1, S)
		)
	).

incr :- get(N), N1 is N + 1, put(N1).
%?- sum_yields(from_list([1,2,3,4]), Sum).
%?- run_state((incr, incr, get(X), incr), 0, Final).
