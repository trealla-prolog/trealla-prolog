% A tabled call inside with_output_to/2 that has to wait on a table
% outside it raises existence_error(reset, _), as in SWI. It used to
% leave the capture behind: the string came back unbound, and what the
% resumed continuation wrote went to current output instead.

:- use_module(library(tabling)).

:- initialization(main).

:- table r/1, s/1.

r(X) :- with_output_to(string(_), s(X)).

s(1).
s(X) :- r(X).

% Completes inside the capture.

:- table n/1.

n(1).
n(X) :- n(Y), Y < 3, X is Y + 1.

inside(L) :- with_output_to(atom(_), findall(X, n(X), L)).

t(Name, G) :-
	catch(( G -> R = G ; R = false ), E,
		( E = error(F, _), functor(F, N, 2), arg(1, F, A) -> R = caught(N, A) ; R = caught(E) )),
	write(Name), write(': '), writeq(R), nl.

main :-
	t(inside, inside(_)),
	t(waits, r(_)),
	t('waits again', r(_)),
	t(visible, true).
