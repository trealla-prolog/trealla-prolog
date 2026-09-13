% A tail call into a clause with more variables than the calling frame has
% slots copied the new frame's slots down over themselves and released some
% already moved, so a bigint or string passed along was freed while in use.

:- initialization(main).

small(a, R) :- X = 5, small(X, R).
small(X, R) :- integer(X), A = 1, B = 2, C = 3, R is X+A+B+C.

big(a, R) :- X is 2^200 + 12345, big(X, R).
big(X, R) :- integer(X), A = 1, B = 2, C = 3, Y is X+A+B+C, R is Y - 2^200.

str(a, R) :- string_concat("a long enough string to be ", "reference counted", S), str(S, R).
str(X, R) :- string(X), A = 1, B = 2, C = 3, _ = [A,B,C], string_concat(X, "!", Y), string_length(Y, R).

t_small(R) :- small(a, R).
t_big(R) :- big(a, R).
t_str(R) :- str(a, R).
t_big_loop(R) :- ( forall(between(1, 1000, _), big(a, 12351)) -> R = ok ; R = corrupted ).

main :-
	forall(
		member(T, [t_small, t_big, t_str, t_big_loop]),
		(	G =.. [T, R],
			(	catch(G, E, R = uncaught(E)) -> true ; R = failed ),
			write(T), write(': '), writeq(R), nl
		)
	).
