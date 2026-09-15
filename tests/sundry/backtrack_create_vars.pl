% A builtin making variables in an older frame moved its overflow run above a choicepoint that then rewound sp under it, re issue #841.

:- initialization(main).

value(x, f(a)-0).
value(y, f(b)-7).
value(z, f(c)-9).

my_member(X, [X|_]).
my_member(X, [_|T]) :- my_member(X, T).

report(Name, Goal) :- write(Name), write(': '), ( call(Goal) -> write(ok) ; write(corrupt) ), nl.

t_functor :- maplist(value, [z,x,y], _), functor(T, f, 2), arg(1, T, a), arg(2, T, b), my_member(_, [1,2,3]), functor(_, g, 2), report(functor, T == f(a,b)), fail.
t_functor.

t_copy_term :- maplist(value, [z,x,y], _), copy_term(f(_,_), T), T = f(a,b), my_member(_, [1,2,3]), copy_term(g(_,_), _), report(copy_term, T == f(a,b)), fail.
t_copy_term.

t_sort :- maplist(value, [z,x,y], A), '$sort'(A, B), my_member(_, [1,2,3]), '$sort'([f(1)-1, f(2)-2], _), report(sort, B == [f(a)-0, f(b)-7, f(c)-9]), fail.
t_sort.

main :- t_functor, t_copy_term, t_sort.
