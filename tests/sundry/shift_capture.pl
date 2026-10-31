% shift/1 from inside with_output_to/2 to a reset/3 outside it raises
% existence_error(reset, Ball), as in SWI: a continuation can't take the
% capture with it. It used to leave current output captured, so nothing
% the query wrote after it was seen. A reset/3 inside the capture, or a
% capture around both, is unaffected.

:- initialization(main).

t(G, R) :-
	catch(( reset(G, B, C), ( C == none -> R = none ; R = shifted(B) ) ), E,
		( E = error(F, _) -> R = caught(F) ; R = caught(E) )).

c_string(R) :- t(with_output_to(string(_), shift(b)), R).
c_atom(R) :- t(with_output_to(atom(_), shift(b)), R).
c_chars(R) :- t(with_output_to(chars(_), (write(x), shift(b))), R).
c_deeper(R) :- t(with_output_to(string(_), deeper), R).
c_inner_reset(R-A) :- t(with_output_to(atom(A), reset((write(x), shift(b), write(y)), _, _)), R).
c_around(A) :- with_output_to(atom(A), (t(shift(b), R), write(R))).
c_after_exit(R) :- t((with_output_to(string(_), true), shift(b)), R).

deeper :- write(x), shift(b).

% Raised inside a findall/3 inside a capture: both are closed.

c_both(R) :- t(with_output_to(string(_), findall(X, (member(X, [1,2]), shift(b)), _)), R).

main :-
	forall(
		member(T, [c_string, c_atom, c_chars, c_deeper, c_inner_reset,
			c_around, c_after_exit, c_both]),
		(	G =.. [T, R],
			(	catch(G, E, R = uncaught(E)) -> true ; R = failed_outright ),
			write(T), write(': '), writeq(R), nl
		)
	).
