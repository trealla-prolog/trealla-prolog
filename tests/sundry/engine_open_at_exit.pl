% An engine still open at exit was destroyed after the threads, tables and modules
% it needs, and segfaulted. The output always matched: a pass needs the clean exit.

:- use_module(library(tabling)).
:- initialization(main).

:- table t/1.

t(X) :- between(1, 3, X).

main :-
	engine_create(x, true, _),
	write(never_started), nl,
	engine_create(X, member(X, [a,b,c]), E1),
	engine_next(E1, A),
	write(suspended(A)), nl,
	engine_create(x, fail, E2),
	\+ engine_next(E2, _),
	write(exhausted), nl,
	engine_create(Y, engine_fetch(Y), E3),
	engine_post(E3, hello),
	write(posted), nl,
	engine_create(Z, t(Z), E5),
	engine_next(E5, T),
	write(tabled(T)), nl.
