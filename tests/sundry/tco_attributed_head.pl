% A tail call whose head bound an attributed variable reused the frame before the hook read the binding back, so
% the hook saw another value or failed an assertion. And once any hook had run, the trail was never trimmed again.

:- use_module(library(freeze)).
:- use_module(library(dif)).
:- initialization(main).

fact(a).

t_fact :- freeze(X, (write(woke(X)), nl)), fact(X).

alt(c).
alt(d) :- write(alt(d)), nl.

t_dif :- dif(X, c), alt(X).

count(_, 0) :- !.
count(X, N) :- M is N-1, count(X, M).

t_loop :- freeze(X, (write(loop_woke(X)), nl)), count(x, 3), last_is([p,q,r], X).

last_is([X], X) :- !.
last_is([_|T], X) :- last_is(T, X).

t_nested :- freeze(X, Y = X), freeze(Y, (write(nested(Y)), nl)), fact(X).

t_fail :- freeze(X, X == b), \+ fact(X), write(fail_undone), nl.

t_throw :- freeze(X, throw(woken(X))), catch(fact(X), E, (write(caught(E)), nl)).

walk(_, 0) :- !.
walk(X, N) :- M is N-1, walk(X, M).

trails(Name, Goal) :-
	call(Goal),
	statistics(max_trails, T),
	write(Name), write(': '),
	(	T < 1000 -> write(ok) ; write(trails(T)) ), nl.

main :-
	t_fact, t_dif, t_loop, t_nested, t_fail, t_throw,
	atom_concat(abcdefghijklmnopqrstuvwxyz, '0123456789', A),
	trails(after_hook, walk(A, 100000)),
	trails(after_hook_choice, (walk(A, 100000) ; true)),
	trails(hook_each_call, (between(1, 1000, _), freeze(V, true), fact(V), fail ; true)).
