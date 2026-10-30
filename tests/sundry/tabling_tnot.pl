% tnot/1, stratified only (DESIGN-tabling-phase2.md item 6, stage A).
%
% Answers below were checked against SWI's tnot/1. A loop through the
% negation, which SWI answers with the well-founded semantics, must
% raise here rather than answer: a wrong answer is the failure mode
% this stage exists to remove.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

t(Label, G) :-
	catch((G -> R = true ; R = false), error(E, _), R = E),
	write(Label), write(': '), writeq(R), nl.

% ---------------------------------------------------------------------
% Stratified: the negated table completes first (XSB's p06).

:- table p/0, q/0, r/0.

p :- q, tnot(r).
q.
r :- fail.

% Existential when not ground, as in SWI.

:- table f/1, g/1.

f(X) :- X == zz.
g(b).

% A game on a DAG: every negated call completes in its own SCC.

:- table win/1.

win(X) :- move(X, Y), tnot(win(Y)).

move(a,b). move(b,c). move(c,d). move(a,c).

% ---------------------------------------------------------------------
% Loops through negation: a mutual pair, a self loop, and one closed by
% a positive call, which shows up as the negated SCC escaping.

:- table a/0, b/0, s/0, u/0, v/0, w/0.

a :- tnot(b).
b :- tnot(a).
s :- tnot(s).
u :- tnot(v).
v :- w.
w :- u.

% Negation return: an incomplete table that already holds an answer
% makes tnot/1 fail instead of raising, active (p2) or escaped (p3).

:- table p2/0, q2/0, p3/0, q3/0.

p2 :- tnot(q2).
q2.
q2 :- p2.
p3 :- tnot(q3).
q3 :- p3.
q3.

% Mode-directed tables negate on existence too.

:- table cost(_,min).

cost(x, 3).
cost(x, 2).

% Incremental: the negated table is looked up like a positive call, so
% the table->table edge invalidation walks covers it.

:- dynamic(edge/2).
:- incremental(edge/2).
:- table reach/2 as incremental.
:- table unreachable/1 as incremental.

reach(X, Y) :- edge(X, Y).
reach(X, Y) :- reach(X, Z), edge(Z, Y).

unreachable(X) :- member(X, [a,b,c]), tnot(reach(a, X)).

edge(a, b).

untabled.

main :-
	t('stratified p', p),
	t('tnot(g(_))', tnot(g(_))),
	t('tnot(g(a))', tnot(g(a))),
	t('tnot(f(_))', tnot(f(_))),
	t('win(a)', win(a)),
	t('win(b)', win(b)),
	t('win(c)', win(c)),
	t('win(d)', win(d)),
	findall(X, (member(X, [a,b,c,d]), tnot(win(X))), Ls),
	write('losers in findall: '), writeq(Ls), nl,

	t('mutual loop', a),
	t('mutual loop again', a),
	t('self loop', s),
	t('loop via positive call', u),
	t('stratified after loops', p),

	t('negation return, active', p2),
	t('then q2', q2),
	t('negation return, escaped', p3),
	t('then q3', q3),

	t('tnot(cost(x,_))', tnot(cost(x,_))),
	t('tnot(cost(y,_))', tnot(cost(y,_))),

	findall(X, unreachable(X), U1),
	write('unreachable: '), writeq(U1), nl,
	assertz(edge(b, c)),
	findall(X, unreachable(X), U2),
	write('unreachable after edge(b,c): '), writeq(U2), nl,

	t('untabled', tnot(untabled)),
	t('qualified untabled', tnot(foo:untabled)),
	t('qualified tabled', tnot(user:g(b))),
	t('variable', tnot(_)),
	t('not callable', tnot(1)),

	set_prolog_flag(tabling, false),
	t('tabling off', tnot(r)),
	set_prolog_flag(tabling, true).
