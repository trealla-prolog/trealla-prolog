% shift/1 with no reset/3 to return to, or whose nearest reset/3 has a Ball
% or Cont that doesn't unify, fails - as in Scryer. It used to leave its
% ball in q->ball on the way out, and catch/3 takes a retry with q->ball
% set for an exception: the next catcher backtracked into anywhere later
% in the query unified with the stale ball and succeeded again. After a
% Ball mismatch that ball pointed at reused cells, so the extra answer
% was a term from elsewhere. A mismatch also used up the reset/3, so a
% later shift in its goal could not find it.

:- initialization(main).

count_catch_answers(N) :-
	findall(X, catch((member(X,[1,2]) ; fail), _, true), L),
	length(L, N).

n_bare(R) :- ( shift(x) -> R = shifted ; R = failed ).
n_catch(R) :- ( catch(shift(x), E, true) -> R = caught(E) ; R = failed ).
n_runtime(R) :- G = catch(shift(x), E, true), ( call(G) -> R = caught(E) ; R = failed ).
n_later_catch(N) :- ( shift(x) -> true ; true ), count_catch_answers(N).
n_later_runtime(N) :- G = (shift(x) -> true ; true), call(G), count_catch_answers(N).

m_ball(R) :- ( reset(shift(a), b, _) -> R = matched ; R = failed ).
m_cont(R) :- ( reset(shift(a), a, foo) -> R = matched ; R = failed ).
m_nested(R) :- ( reset(reset(shift(a), b, _), B, _) -> R = outer(B) ; R = failed ).
m_later_catch(N) :- ( reset(shift(a), b, _) -> true ; true ), count_catch_answers(N).
m_retry(R) :- ( reset((member(X,[a,b]), shift(X)), b, _) -> R = matched(X) ; R = failed ).

ok_shift(B) :- reset(shift(a), B, cont(_)).
ok_none(C) :- reset(true, _, C).

main :-
	forall(
		member(T, [n_bare, n_catch, n_runtime, n_later_catch, n_later_runtime,
			m_ball, m_cont, m_nested, m_later_catch, m_retry, ok_shift, ok_none]),
		(	G =.. [T, R],
			(	catch(G, E, R = uncaught(E)) -> true ; R = failed_outright ),
			write(T), write(': '), writeq(R), nl
		)
	).
