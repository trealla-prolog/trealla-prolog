% A ground term built on the heap by the calling frame (a caught ball, a
% copy, a list) was taken for one from the clause source, so a tail call
% could reuse the frame and trim the heap out from under it.

:- initialization(main).

t2(E) :- nonvar(E), E = error(B, _), nonvar(B), functor(B, foo, _).

% The callee's head bound to a ball caught in the calling frame.
ball(N, E0) :- ( N =:= 0 -> t2(E0) ; catch(throw(error(foo(x), ctx)), E, true), N1 is N-1, ball(N1, E) ).
t_ball(R) :- ( ball(3, _) -> R = ok ; R = lost ).

% An older variable bound to a ground copy, then the frame reused.
older(0, _) :- !.
older(N, Out) :- ( N =:= 2 -> copy_term(f(g(h), [i, j]), Out) ; true ), N1 is N-1, older(N1, Out).
t_older(R) :- older(3, Out), copy_term(k(l, m, n, o), _), ( Out == f(g(h), [i, j]) -> R = ok ; R = lost ).

% The same with numlist/3.
nums(0, _) :- !.
nums(N, Out) :- ( N =:= 2 -> numlist(1, 5, Out) ; true ), N1 is N-1, nums(N1, Out).
t_nums(R) :- nums(3, Out), copy_term(k(l, m, n, o), _), ( Out == [1, 2, 3, 4, 5] -> R = ok ; R = lost ).

main :-
	forall(
		member(T, [t_ball, t_older, t_nums]),
		(	G =.. [T, R],
			(	catch(G, E, R = uncaught(E)) -> true ; R = failed ),
			write(T), write(': '), writeq(R), nl
		)
	).
