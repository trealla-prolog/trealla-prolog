% task_call/N must pass its arguments to the task by value.
%
% The task is a query of its own, with its own frames. Arguments used to
% be copied with append_to_tmp(), which copies variable cells by
% reference - and those references point into the *spawning* query's
% frames, which the task cannot resolve. A bound variable sitting inside
% a compound therefore arrived unbound:
%
%     N = 7, task_call(p, '$future'(N))   ==>  task sees '$future'(_)
%
% A term written out with its values already in place survived, which is
% why this went unnoticed - only a variable *reference* was lost, not a
% value. library(concurrent) hit it squarely, since future/3 builds
% '$future'(N) with N bound, and left a NOTE about "a bug to do with
% passing variables in task_call/1" plus a write/read round trip through
% an atom to work around it.
%
% The task has to judge the term itself and report only a verdict. Pass
% the term back instead and the test cannot fail: recording it clones it,
% and while the spawning query is still sitting in task_wait/0 its frames are
% intact, so a dangling reference quietly resolves on the way out.
%
% Verdicts came back over send/1 and were read with recv/1 until GUSTTO
% phase 1 removed both. They go through the shared database now; the
% property under test is unchanged, only the reporting channel differs.

:- initialization(main).

:- dynamic(verdict/1).

% Report a verdict, not the term: recording Got would clone it on the way
% out and a lost binding would look intact by the time it is printed.

check(Id, Expect, Got) :-
	(	Got == Expect -> assertz(verdict(Id-ok))
	;	\+ ground(Got) -> assertz(verdict(Id-'binding lost in transit'))
	;	assertz(verdict(Id-'wrong value'))
	).

drain(L) :- drain_([], L0), msort(L0, L).
drain_(Acc, L) :- ( retract(verdict(X)) -> drain_([X|Acc], L) ; L = Acc ).

report(Id-ok) :- !, format("~w: ok~n", [Id]).
report(Id-Bad) :- format("~w: FAILED ~q~n", [Id,Bad]).

main :-
	N = 7,
	A = f(42),
	Inner = g(N),
	task_call(check, int,       42,           42),
	task_call(check, atm,       foo,          foo),
	task_call(check, lit,       f(42),        f(42)),
	task_call(check, via_var,   f(42),        A),
	task_call(check, bound_var, '$future'(7), '$future'(N)),
	task_call(check, nested,    h(g(7),7),    h(Inner,N)),
	task_call(check, in_list,   [7,f(42),x],  [N,A,x]),
	task_wait,
	drain(L),
	maplist(report, L),
	vars.

% Cloning has to happen once over the whole goal. Doing it per argument
% restarts clone_term_to_tmp()'s variable generation each time, which
% lets unrelated variables collide - distinct ones alias, shared ones
% come apart - and that corruption reaches the point of a segfault.

alias_probe(X, Y) :- X = 1, (var(Y) -> assertz(verdict(distinct-ok)) ; assertz(verdict(distinct-aliased))).
shared(X, Y) :- X = 1, (Y == 1 -> assertz(verdict(shared-ok)) ; assertz(verdict(shared-'came apart'))).
ingoal(G, X) :- call(G), (X == 7 -> assertz(verdict(goal_var-ok)) ; assertz(verdict(goal_var-'came apart'))).

vars :-
	task_call(alias_probe, _, _), task_wait, drain(L1), maplist(report, L1),
	task_call(shared, Z, Z), task_wait, drain(L2), maplist(report, L2),
	G = (W = 7), task_call(ingoal, G, W), task_wait, drain(L3), maplist(report, L3).
