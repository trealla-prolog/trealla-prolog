% A tabled call inside findall/3 that completes on its own is fine. One
% that has to wait on a table outside the findall/3, through a positive
% call or tnot/1, raises existence_error(reset, _), as in SWI, where it
% used to fail. The tables it left half-built are reset, so calling
% again raises again.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

% Completes inside the findall/3.

:- table path/2.

edge(a, b).
edge(b, c).
edge(c, a).

path(X, Y) :- edge(X, Y).
path(X, Y) :- path(X, Z), edge(Z, Y).

reach(X, Ys) :- findall(Y, path(X, Y), Ys0), msort(Ys0, Ys).

% Waits on the table that holds the findall/3.

:- table p/1, q/1.

p(X) :- findall(Y, q(Y), L), member(X, L).

q(1).
q(X) :- p(X).

% The same through tnot/1.

:- table w/1, x/1.

w(1) :- findall(_, x(_), _).

x(1) :- tnot(w(1)).

t(Name, G) :-
	catch(( G -> R = G ; R = false ), E,
		( E = error(F, _), functor(F, N, 2), arg(1, F, A) -> R = caught(N, A) ; R = caught(E) )),
	write(Name), write(': '), writeq(R), nl.

main :-
	t(reach, reach(a, _)),
	t(positive, p(_)),
	t('positive again', p(_)),
	t(tnot, w(1)),
	t('tnot again', w(1)),
	t('reach after', reach(b, _)).
