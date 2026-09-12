:- initialization(main).

% Issue #1156: under sto an answer may name a cyclic subterm with a variable of its own, as the toplevel does.

?- current_prolog_flag(occurs_check, false).
   true.

% the prologue's select/3 quad verbatim

1 ?- select(E, Xs, Xs).
   sto, % occurs-check
   loops
|  sto, % rational trees
   Xs = [E|Xs]
;  Xs = [_A|_B], _B = [E|_B]
;  ..., ad_infinitum
|  sto, % literal substitutions
   Xs = [E,E|_A]
;  Xs = [_A,E,E|_B]
;  ..., ad_infinitum.

% as the toplevel writes it, the same rational tree

2 ?- select(E, Xs, Xs).
   sto, Xs = [E|Xs]
;  Xs = [_A,E|_B], _B = [E|_B]
;  ... .

% reached through another such variable

3 ?- X = f(X).
   sto, X = f(_A), _A = f(_B), _B = f(_A).

% deliberately failing: g is not f

4 ?- X = f(X).
   sto, X = f(_A), _A = g(_A).

% deliberately failing: no binding of the query reaches _Z

5 ?- X = f(X).
   sto, X = f(X), _Z = 1.

% deliberately malformed: without sto a substitution cannot name its cycle

6 ?- X = f(X).
   X = f(_A), _A = f(_A).

% run_quads names the file as it was consulted, so keep the base name only, as tests/issues/test1141.pl does.

strip_dirs(Cs, Out) :- strip_dirs(Cs, [], Out).

strip_dirs([], W, Out) :- reverse(W, Out).
strip_dirs([C|Cs], W, Out) :-
	(	C == (/)
	->	strip_dirs(Cs, [], Out)
	;	C == ' '
	->	reverse([C|W], Pre), append(Pre, Out0, Out), strip_dirs(Cs, [], Out0)
	;	C == '\n'
	->	reverse([C|W], Pre), append(Pre, Out0, Out), strip_dirs(Cs, [], Out0)
	;	strip_dirs(Cs, [C|W], Out)
	).

main :-
	use_module(library(quads)),
	with_output_to(chars(Cs), run_quads),
	strip_dirs(Cs, Out),
	atom_chars(A, Out),
	write(A).
