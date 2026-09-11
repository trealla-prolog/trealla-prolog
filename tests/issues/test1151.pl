% Issue #1151: check_pressure() shrank q->slots to fit only the current sp, ignoring choicepoints that still needed slots far above it.
%
% https://github.com/trealla-prolog/trealla-prolog/issues/1151

:- initialization(main).

deep3(0, G) :- !, G.
deep3(N, G) :- M is N - 1, catch(deep3(M, G), -, true).

main :-
	(   between(1, 3, _), deep3(100000, true), fail
	;   true
	),
	format("ok~n", []).
