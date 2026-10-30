% tnot/1 and the well-founded semantics (DESIGN-tabling-phase2.md item 6,
% stages A and B).
%
% Every truth value below was checked against SWI's tnot/1 and
% call_delays/2. A loop through negation no longer raises: what the
% well-founded semantics leaves open comes out undefined, a success
% whose call_delays/2 is not true.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

t(Label, G) :-
	catch(( call_delays(G, D) ->
	        ( D == true -> R = true ; R = undefined )
	      ; R = false
	      ), error(E, _), R = E),
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
% a positive call. Each is undefined.

:- table a/0, b/0, s/0, u/0, v/0, w/0.

a :- tnot(b).
b :- tnot(a).
s :- tnot(s).
u :- tnot(v).
v :- w.
w :- u.

% Dynamically stratified: delaying, then simplification, decides it.

:- table pp/0, qq/0.

pp :- tnot(qq).
qq :- tnot(pp), fail.

% The game on a cyclic graph: a and b undefined, c won, d lost.

:- table cwin/1.

cwin(X) :- cmove(X, Y), tnot(cwin(Y)).

cmove(a,b). cmove(b,a). cmove(b,c). cmove(c,d). cmove(a,c).

% A positive delay on an answer that simplification later settles: x
% reads y while y is still conditional.

:- table x/0, y/0, z/0.

x :- y.
y :- tnot(z).
z :- tnot(y), fail.

% An answer simplification removes: f1 holds only while g1 might not,
% and g1 turns out true through h1.

:- table f1/0, g1/0, h1/0, z1/0.

f1 :- tnot(g1).
g1 :- tnot(f1).
g1 :- h1.
h1 :- tnot(z1).
z1 :- tnot(h1), fail.

% Undefined is inherited: a positive call, and tnot/1 of a complete
% table holding only conditional answers.

:- table pa/0, na/0.

pa :- a.
na :- tnot(a).

% Negation return: an incomplete table that already holds an
% unconditional answer makes tnot/1 fail outright.

:- table p2/0, q2/0, p3/0, q3/0.

p2 :- tnot(q2).
q2.
q2 :- p2.
p3 :- tnot(q3).
q3 :- p3.
q3.

% Mode-directed tables negate on existence, and refuse a conditional
% answer: min over an answer that may not hold means nothing.

:- table cost(_,min), ccost(_,min).

cost(x, 3).
cost(x, 2).

ccost(x, 3) :- undefined.
ccost(x, 5).

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

	t('mutual loop a', a),
	t('mutual loop b', b),
	t('mutual loop again', a),
	t('self loop', s),
	t('loop via positive call u', u),
	t('loop via positive call v', v),
	t('dynamically stratified pp', pp),
	t('dynamically stratified qq', qq),
	t('cwin(a)', cwin(a)),
	t('cwin(b)', cwin(b)),
	t('cwin(c)', cwin(c)),
	t('cwin(d)', cwin(d)),
	t('positive delay x', x),
	t('then y', y),
	t('then z', z),
	t('simplified away f1', f1),
	t('then g1', g1),
	t('then h1', h1),
	t('then z1', z1),
	t('undefined', undefined),
	t('positive call on undefined', pa),
	t('tnot of undefined', na),
	findall(X-D, call_delays(cwin(X), D), CDs),
	length(CDs, NCDs),
	write('cwin answers: '), write(NCDs), nl,

	t('negation return, active', p2),
	t('then q2', q2),
	t('negation return, escaped', p3),
	t('then q3', q3),

	t('tnot(cost(x,_))', tnot(cost(x,_))),
	t('tnot(cost(y,_))', tnot(cost(y,_))),
	t('conditional answer in min table', ccost(x, _)),

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
