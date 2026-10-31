% Early completion: a ground call holding an unconditional answer has
% nothing more to give, so its producer is cut, its other clauses
% unrun. Without it each of these loops. Checked against SWI.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

% XSB's p29: w(0) holds by its first clause, and its second enumerates
% e(B, 0) without end.

:- table u/1, w/1.

e(s(0), s(s(0))).
e(s(0), 0).
e(s(A), s(s(A))) :- e(A, s(A)).
e(s(A), 0) :- e(A, 0).

w(A) :- tnot(u(A)).
w(A) :- e(B, A), tnot(w(B)).

u(0) :- fail.

% An untabled generator without end after a first answer, called
% directly and through another table.

:- table g/1, h/1.

nat(0).
nat(N) :- nat(M), N is M + 1.

g(a).
g(X) :- nat(N), X = N.

h(X) :- g(X).

% A ground call to a table whose open call has several answers.

:- table m/1.

m(X) :- member(X, [1,2,3]).

main :-
	( call_delays(w(0), D) -> writeq(w0-D) ; write(w0-false) ), nl,
	( g(a) -> write(ga) ; write(no) ), nl,
	( h(a) -> write(ha) ; write(no) ), nl,
	findall(X, m(X), Ms), writeq(Ms), nl,
	( m(2) -> write(m2) ; write(no) ), nl.
