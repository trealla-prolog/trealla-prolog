% The residual program (DESIGN-tabling-phase2.md item 6, stage D):
% delays_residual_program/2, call_residual_program/2, answer_residual/2,
% and call_delays/2's order, the latest literal first.
%
% Checked against SWI's library(wfs), with deliberate differences. A
% delay names a stored answer, so a later binding of its variables does
% not show: p1(b, _) below reads p13a(a,b,_) where SWI reads
% p13a(a,b,c). A goal with no table gives no clause, where SWI gives
% foo :- ???. An unbound answer_residual/2 goal raises. And errors name
% the predicate in their context.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

:- table win/1, ag/1, p1/2, p13a/3, dlt/2, eee/2, pp/0, qq/0, rr/0.

win(X) :- move(X, Y), tnot(win(Y)).

move(a,b). move(b,a). move(b,c). move(c,d). move(a,c).

% Two delay lists for one answer, and undefined/0 at the bottom.

ag(p6) :- ag(t6).
ag(t6) :- ag(p6).
ag(p6) :- tnot(ag(r6)).
ag(q6) :- ag(s6), ag(u6).
ag(u6) :- ag(q6).
ag(s6) :- tnot(ag(p6)).
ag(r6) :- tnot(ag(q6)).
ag(p6) :- tnot(ag(p7)).
ag(p7) :- tnot(ag(p8)).
ag(p8) :- undefined.

p1(X, _) :- p13a(a, X, Z), Z = c.
p1(X, _) :- p13a(f(b), X, Z), Z = f(c).
p13a(_, b, _) :- undefined.

dlt(X, X).
dlt(X, Y) :- dlt(X, Z), eee(Z, Y).
eee(a, b) :- undefined.
eee(b, a) :- undefined.

pp :- tnot(qq), rr.
qq :- tnot(pp).
rr :- tnot(qq).

show(G) :-
	forall(call_residual_program(G, P),
	       ( numbervars(G-P, 0, _),
	         writeq(G), nl,
	         forall(member(C, P), (write('   '), writeq(C), nl)) )).

e(G) :-
	catch(G, error(E, _), (\+ \+ (numbervars(E, 0, _), writeq(E)), nl)).

main :-
	show(win(a)),
	show(ag(p6)),
	show(ag(s6)),
	show(p1(b, _)),
	show(dlt(a, b)),
	show(pp),
	call_delays(pp, D1), writeq(D1), nl,
	call_delays((win(a), win(b), tnot(win(a))), D2), writeq(D2), nl,
	call_delays(ag(p6), D3), delays_residual_program(D3, P3), length(P3, N3), writeq(N3), nl,
	findall(R, answer_residual(ag(p6), R), Rs), writeq(Rs), nl,
	findall(X-R, answer_residual(win(X), R), XRs), writeq(XRs), nl,
	( delays_residual_program(true, P4) -> writeq(P4) ; write(no) ), nl,
	( delays_residual_program(undefined, P5) -> writeq(P5) ; write(no) ), nl,
	( delays_residual_program(foo, P6) -> writeq(P6) ; write(no) ), nl,
	e(answer_residual(move(_, _), _)),
	e(delays_residual_program(_, _)),
	e(answer_residual(_, _)).
