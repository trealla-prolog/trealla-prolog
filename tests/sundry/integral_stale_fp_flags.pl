% truncate, round, ceiling and floor found an overflow from the FP exception flags without clearing them first, so
% a flag earlier arithmetic left set, as exp(-1000.0) leaves FE_UNDERFLOW, sent a small float down the bigint path,
% which truncated it unrounded: round(2.5) gave 2 and floor(-2.5) gave -2. Each now follows an underflow.

:- initialization(main).

main :-
	forall(
		member(G, [round(2.5), round(-2.5), round(1999999.9999999993), floor(-2.5), ceiling(-2.5), ceiling(2.5),
			truncate(-2.7), round(1.0e30), floor(-1.0e19)]),
		(	_ is exp(-1000.0),
			Y is G,
			format("~q = ~q~n", [G, Y])
		)).
