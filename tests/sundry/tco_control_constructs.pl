% A callee using if-then-else, soft-cut, \+, ignore/1 or \= pinned its frame, so a tail-recursive caller kept a frame per iteration.

:- initialization(main).

f_ite(N) :- ( N > 0 -> true ; true ).
f_soft(N) :- ( N > 0 *-> true ; true ).
f_not(N) :- \+ N < 0.
f_ignore(N) :- ignore(N < 0).
f_neq(N) :- N \= zzz.
f_rt_ite(N) :- G = (N > 0 -> true ; true), call(G).
f_rt_soft(N) :- G = (N > 0 *-> true ; true), call(G).

l_ite(0) :- !.
l_ite(N) :- f_ite(N), M is N-1, l_ite(M).
l_soft(0) :- !.
l_soft(N) :- f_soft(N), M is N-1, l_soft(M).
l_not(0) :- !.
l_not(N) :- f_not(N), M is N-1, l_not(M).
l_ignore(0) :- !.
l_ignore(N) :- f_ignore(N), M is N-1, l_ignore(M).
l_neq(0) :- !.
l_neq(N) :- f_neq(N), M is N-1, l_neq(M).
l_rt_ite(0) :- !.
l_rt_ite(N) :- f_rt_ite(N), M is N-1, l_rt_ite(M).
l_rt_soft(0) :- !.
l_rt_soft(N) :- f_rt_soft(N), M is N-1, l_rt_soft(M).

frames(Name) :-
	statistics(max_frames, F),
	write(Name), write(': '),
	(	F < 1000 -> write(ok) ; write(frames(F)) ), nl.

% Backtracking into a choicepoint of the body past one of these, with frames used up in between, still sees its frame.

churn(0) :- !.
churn(N) :- N1 is N-1, churn(N1), true.

t_ite(X, Y) :- member(X, [1,2,3]), ( X > 0 -> true ; true ), Y = X.
t_not(X, Y) :- member(X, [1,2,3]), \+ X = 0, Y = X.
t_rt_ite(X, Y) :- member(X, [1,2,3]), G = (X > 0 -> Z = X ; Z = none), call(G), Y = Z.
t_rt_soft(X, Y) :- member(X, [1,2,3]), G = (member(W, [X]) *-> Z = W ; Z = none), call(G), Y = Z.

answers(T) :-
	findall(X-Y, (call(T, X, Y), churn(50)), L),
	write(T), write(': '),
	(	L == [1-1,2-2,3-3] -> write(ok) ; writeq(L) ), nl.

main :-
	l_ite(100000), frames(ite),
	l_soft(100000), frames(soft),
	l_not(100000), frames(not),
	l_ignore(100000), frames(ignore),
	l_neq(100000), frames(neq),
	l_rt_ite(100000), frames(rt_ite),
	l_rt_soft(100000), frames(rt_soft),
	forall(member(T, [t_ite, t_not, t_rt_ite, t_rt_soft]), answers(T)).
