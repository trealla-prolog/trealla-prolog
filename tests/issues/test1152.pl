% Issue #1152: a throw/1 ball unwinding through many nested catch/3
% frames could be mistaken for the interpreter's own internal
% $abort/unwind control-throw, aborting the whole query instead of
% reaching the outer catcher.
%
% find_exception_handler() peeked at the cell right after the ball to
% check for those sentinels, a check only valid for the error(Sentinel,
% Context) shape throw_error3() itself produces. A bare user ball like
% throw(bar) has no such cell, so the peek read whatever heap memory
% happened to follow it - and deep enough recursion made that memory
% spell out "$abort" often enough to reproduce.
%
% https://github.com/trealla-prolog/trealla-prolog/issues/1152

:- initialization(main).

deep(0, G) :- !, call(G).
deep(N, G) :- M is N - 1, catch(deep(M, G), foo, true).

main :-
	(   catch(deep(5000, throw(bar)), Ball, true)
	->  format("caught ~q~n", [Ball])
	;   format("failed~n", [])
	).
