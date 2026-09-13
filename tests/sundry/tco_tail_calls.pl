% Any last call may reuse the caller's frame, not only a recursive one.
% These are the shapes where reuse must not change the answer.

:- initialization(main).
:- use_module(library(freeze)).
:- dynamic(fact/1).

% Choicepoints left by an earlier goal of the caller.
nd(0, []) :- !.
nd(N, [X|T]) :- member(X, [a,b]), N1 is N-1, nd2(N1, T).
nd2(N, T) :- nd(N, T).
r_nd(L) :- findall(T, nd(3, T), L).

% Logical update view: a clause added before the tail call is seen after it.
lu(0) :- !.
lu(N) :- assertz(fact(N)), N1 is N-1, lu2(N1).
lu2(N) :- fact(N0), N0 =:= N+1, !, lu(N).
r_lu(L) :- retractall(fact(_)), lu(5), findall(X, fact(X), L).

% Cut inside a tail-called predicate.
cut_a(X) :- cut_b(X).
cut_b(X) :- member(X, [1,2,3]), !.
r_cut(L) :- findall(X, cut_a(X), L).

% The caller's variable bound to a structure from the callee's head.
out_a(X) :- out_b(X).
out_b(f(Y)) :- Y = 1.
r_out(X) :- out_a(X).

% The caller passes a structure holding its own variable, and that variable.
sh_a(R) :- Z = g(V), sh_b(Z, V, R).
sh_b(g(A), B, R) :- A = 1, R = B.
r_share(R) :- sh_a(R).

% An exception from a tail-called predicate.
ex_a(N) :- ex_b(N).
ex_b(N) :- ( N > 2 -> throw(big(N)) ; true ).
r_ex(R) :- catch((ex_a(1), ex_a(5)), E, R = E).

% A caught exception passed to a tail call.
ball_a(R) :- catch(throw(error(foo(x), ctx)), E, true), ball_b(E, R).
ball_b(error(B, _), R) :- functor(B, R, _).
r_ball(R) :- ball_a(R).

% Backtracking into a predicate reached by a tail call.
bt_a(X) :- bt_b(X).
bt_b(X) :- between(1, 3, X).
r_bt(L) :- findall(X, bt_a(X), L).

% A callee with fewer variables than the caller.
fw_a(R) :- A = 1, B = 2, C = 3, D = [A,B,C], fw_b(D, R).
fw_b(D, D).
r_fewer(R) :- fw_a(R).

% A callee with more variables than the caller.
mv_a(R) :- mv_b(R).
mv_b(R) :- A = 1, B = 2, C is A+B, D = [A,B,C], E = e(D), F = f(E), R = F.
r_more(R) :- mv_a(R).

% Tail calls through call/N and both branches of an if-then-else.
cn_a(0, R) :- !, R = done.
cn_a(N, R) :- N1 is N-1, ( N1 mod 2 =:= 0 -> call(cn_b, N1, R) ; cn_b(N1, R) ).
cn_b(N, R) :- cn_a(N, R).
r_calln(R) :- cn_a(1000, R).

% Attributed variables passed down a chain of tail calls.
at_a(X, R) :- freeze(X, R = woke(X)), at_b(X).
at_b(X) :- at_c(X).
at_c(X) :- X = 1.
r_freeze(R) :- at_a(_, R).

% Bigints and strings moved between clauses of different sizes.
bg_a(R) :- X is 2^100, bg_b(X, R).
bg_b(X, R) :- Y is X+1, Z = [X,Y], bg_c(Z, R).
bg_c([X,Y], R) :- R is Y-X.
r_bigint(R) :- bg_a(R).
st_a(R) :- string_concat("a long enough string to be ", "reference counted", S), st_b(S, R).
st_b(S, R) :- string_concat(S, "!", T), st_c(T, R).
st_c(T, R) :- string_length(T, R).
r_string(R) :- st_a(R).

% An unbound variable of the caller returned in a structure.
ub_a(R) :- ub_b(_, R).
ub_b(X, R) :- R = h(X, X).
r_unbound(R) :- ub_a(T), T = h(A, B), ( var(A), A == B -> R = shared ; R = broken ).

% A tail call in a clause that still has alternatives.
alt_a(X) :- alt_b(X).
alt_a(X) :- X = second.
alt_b(first).
r_alt(L) :- findall(X, alt_a(X), L).

% A chain of three predicates.
c1(0, R) :- !, R = end.
c1(N, R) :- c2(N, x, R).
c2(N, X, R) :- atom(X), c3(N, R).
c3(N, R) :- N1 is N-1, c1(N1, R).
r_chain(R) :- c1(1000, R).

main :-
	forall(
		member(T, [r_nd, r_lu, r_cut, r_out, r_share, r_ex, r_ball, r_bt, r_fewer, r_more,
			r_calln, r_freeze, r_bigint, r_string, r_unbound, r_alt, r_chain]),
		(	G =.. [T, R],
			(	catch(G, E, R = uncaught(E)) -> true ; R = failed ),
			write(T), write(': '), writeq(R), nl
		)
	).
