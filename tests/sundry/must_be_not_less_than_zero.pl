% must_be(not_less_than_zero, X) wants an integer, as Scryer has it: a float or rational is a type error,
% not passed or a domain error, and so is any other non-integer.

:- initialization(main).

t(X) :-
	( catch(must_be(not_less_than_zero, X), error(E, _), true) -> (var(E) -> R = true ; R = E) ; R = false ),
	( var(X) -> S = var ; S = X ),
	format("~q: ~q~n", [S, R]).

main :-
	t(0), t(3), B is 2^70, t(B), t(-1),
	t(1.5), t(-1.5), t(0.0), Q is 1 rdiv 3, t(Q), t(a), t(f(x)), t(_),
	( catch(must_be(list(not_less_than_zero), [1, 1.5]), error(E, _), true) -> true ; E = failed ),
	format("list: ~q~n", [E]).
