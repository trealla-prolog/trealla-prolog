% limit/2 stops at N however many choices the goal leaves, fails for N =< 0, and keeps a cut in its goal local, as does offset/2; as SWI

:- initialization(main).

n(a).
n(b).
l([]).
l([X|Xs]) :- n(X), l(Xs).

t(T-G) :- findall(T, G, Ts), writeq(Ts), nl.

main :-
	findall(N, (between(1, 4, K), findall(L, limit(K, l(L)), Ls), length(Ls, N)), Ns), writeq(Ns), nl,
	t(L-X-limit(2, (member(X, [a,b]), member(L, [1,2,3])))),
	t(L-X-limit(4, (member(X, [a,b]), member(L, [1,2,3])))),
	t(X-limit(0, member(X, [a,b]))),
	t(X-limit(-1, member(X, [a,b]))),
	t(X-limit(5, member(X, [a,b]))),
	t(X-Y-(member(Y, [1,2]), limit(1, (member(X, [a,b]), !)))),
	t(X-Y-(member(Y, [1,2]), limit(2, (member(X, [a,b,c]), !)))),
	t(X-Y-limit(2, limit(3, (member(X, [a,b]), member(Y, [1,2,3]))))),
	t(X-Y-(limit(2, member(X, [a,b,c])), member(Y, [1,2]))),
	t(X-(member(Y, [1,2,3]), limit(1, member(X, [Y,Y])), Y >= 2)),
	t(X-limit(2, setup_call_cleanup(true, member(X, [a,b,c]), (write(cleanup), nl)))),
	t(X-Y-offset(1, (member(X, [a,b]), member(Y, [1,2])))),
	t(X-Y-(member(Y, [1,2]), offset(0, (member(X, [a,b,c]), !)))),
	t(X-Y-limit(3, offset(1, (member(X, [a,b]), member(Y, [1,2,3]))))),
	catch(limit(1, throw(ball)), E, (writeq(caught(E)), nl)).
