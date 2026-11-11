:- initialization(main).

:- use_module(library(pio)).
:- use_module(library(dcgs)).
:- use_module(library(between)).
:- use_module(library(lists)).

% Each solution of phrase_to_file/2 rewrites the file, so it holds just
% that solution; a grammar that fails or raises leaves the file alone.
% Scryer issue #3505.

num(X) --> { number_chars(X, Cs) }, seq(Cs).

contents(File, Cs) :- phrase_from_file(seq(Cs), File).

main :-
	File = 'tmp.ptf',
	forall(( phrase_to_file(({between(1, 3, X)}, num(X)), File),
	         contents(File, Cs) ),
	       ( write(X-Cs), nl )),
	contents(File, Last), write(last(Last)), nl,
	forall(( phrase_to_file(({member(X, [100, 2])}, num(X)), File),
	         contents(File, Cs) ),
	       ( write(X-Cs), nl )),
	phrase_to_file("keep", File),
	( phrase_to_file(([a], {fail}), File) -> true ; write(failed), nl ),
	contents(File, K1), write(K1), nl,
	catch(phrase_to_file(([a], {throw(oops)}), File), E, (write(caught(E)), nl)),
	contents(File, K2), write(K2), nl,
	catch(phrase_to_file([_], File), E2, (write(caught(E2)), nl)),
	contents(File, K3), write(K3), nl,
	delete_file(File).
