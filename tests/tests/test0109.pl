:- initialization(main).

% Clauses with a var in an indexed argument must still be found once a
% predicate crosses the index threshold (50 for facts). idx1 is keyed on
% Arg1 and idx2 on Arg2, and index_cmpkey() calls a var equal to
% anything, so a var-headed argument breaks the skiplist's ordering and
% the descent can walk straight past the clause.

:- dynamic(p/2).
:- dynamic(q/2).
:- dynamic(r/2).
:- dynamic(s/2).
:- dynamic(t/2).
:- dynamic(u/2).
:- dynamic(v/3).
:- dynamic(w/3).

% var Arg1, asserted before the threshold
setup_p :- assertz(p(_, varclause)), fail.
setup_p :- between(2,600,I), assertz(p(I,ground)), fail.
setup_p.

% var Arg1, asserted after the threshold
setup_q :- between(2,600,I), assertz(q(I,ground)), fail.
setup_q :- assertz(q(_, varclause)), fail.
setup_q.

% var Arg2, asserted before the threshold
setup_r :- assertz(r(vc, _)), fail.
setup_r :- between(2,600,I), assertz(r(I,I)), fail.
setup_r.

% var Arg2, asserted after the threshold
setup_s :- between(2,600,I), assertz(s(I,I)), fail.
setup_s :- assertz(s(vc, _)), fail.
setup_s.

% two var-headed clauses; retracting one must not un-flag the predicate
setup_t :- between(1,600,I), assertz(t(I,ground)), fail.
setup_t :- assertz(t(_, v1)), fail.
setup_t :- assertz(t(_, v2)), fail.
setup_t.

% the only var-headed clause; retracting it must clear the flag and
% hand the predicate back its index, without changing any answers
setup_u :- between(1,600,I), assertz(u(I,ground)), fail.
setup_u :- assertz(u(_, vc)), fail.
setup_u.

% Arg1 and Arg2 contain variables in some clauses, but Arg3 does not.
% The floating secondary index must choose Arg3 and retain the matching
% var-Arg1 clause instead of falling back to the whole predicate chain.
setup_v :- assertz((v(_, Y, hook) :- Y = wildcard)), fail.
setup_v :- between(1,600,I), assertz(v(I, value, other)), fail.
setup_v :- assertz(v(7, value, hook)), fail.
setup_v.

% All heads and the lookup are ground. This exercises the exact whole-head
% index, which restores selectivity when many clauses share Arg1.
setup_w :- between(1,600,I), assertz(w(shared, I, value(I))), fail.
setup_w.

main :-
	setup_p, setup_q, setup_r, setup_s, setup_t, setup_u, setup_v, setup_w,
	findall(Y, p(7,Y), LP), write(LP), nl,
	findall(Y, q(7,Y), LQ), write(LQ), nl,
	findall(X, r(X,7), LR), write(LR), nl,
	findall(X, s(X,7), LS), write(LS), nl,
	retract(t(_,v1)),
	findall(Y, t(7,Y), LT), write(LT), nl,
	retract(u(_,vc)),
	findall(Y, u(7,Y), LU), write(LU), nl,
	findall(Y, v(7,Y,hook), LV), write(LV), nl,
	( w(shared, 7, value(7)) -> write(head_indexed) ; write(head_missing) ), nl.
