% shift/1 can't take findall/3's collected solutions into a continuation,
% so a shift from inside findall/3, or bagof/3 and setof/3 built on it,
% to a reset/3 outside raises existence_error(reset, Ball), as in SWI.
% It used to capture findall's own $queue instruction, a continuation
% that only failed. findnsols/4 keeps its solutions the same way and
% raises too, where SWI's, written in Prolog, captures. A reset/3 inside
% the collector is unaffected, and so are \+, forall/2, once/1 and
% catch/3, as in SWI.

:- initialization(main).

t(G, R) :-
	catch(( reset(G, B, C), ( C == none -> R = none ; R = shifted(B) ) ), E,
		( E = error(F, _) -> R = caught(F) ; R = caught(E) )).

c_findall(R) :- t(findall(X, (member(X, [1,2]), shift(b)), _), R).
c_bagof(R) :- t(bagof(X, (member(X, [1,2]), shift(b)), _), R).
c_setof(R) :- t(setof(X, (member(X, [1,2]), shift(b)), _), R).
c_findall4(R) :- t(findall(X, (member(X, [1,2]), shift(b)), _, []), R).
c_findnsols(R) :- t(findnsols(1, X, (member(X, [1,2]), shift(b)), _), R).
c_deeper(R) :- t(findall(X, deeper(X), _), R).
c_inner_reset(R) :- t(findall(X, (member(X, [1,2]), reset(shift(b), _, _)), _), R).
c_not(R) :- t(\+ shift(b), R).
c_forall(R) :- t(forall(true, shift(b)), R).
c_once(R) :- t(once(shift(b)), R).
c_catch(R) :- t(catch(shift(b), _, true), R).

deeper(X) :- member(X, [1,2]), shift(b).

% The collector the error left is gone: an enclosing findall/3 goes on.

c_after(L) :- findall(X, (member(X, [1,2,3]), c_findall(_)), L).

main :-
	forall(
		member(T, [c_findall, c_bagof, c_setof, c_findall4, c_findnsols,
			c_deeper, c_inner_reset, c_not, c_forall, c_once, c_catch,
			c_after]),
		(	G =.. [T, R],
			(	catch(G, E, R = uncaught(E)) -> true ; R = failed_outright ),
			write(T), write(': '), writeq(R), nl
		)
	).
