:- initialization(main).

% Issue #1154: sto says an answer's bindings are cyclic and is checked, and an answer without it says they are not.

?- current_prolog_flag(occurs_check, false).
   true.

1 ?- X = -X.
   true, unexpected.

2 ?- X = -X.
   X = - - - ... , unexpected.

3 ?- X = -X.
   sto, X = - - - ... .

% deliberately failing: the third is + not -

4 ?- X = -X.
   sto, X = - - + ... .

% deliberately malformed: without sto a binding cannot state its own cycle

5 ?- X = -X.
   X = - - - X .

% a sto answer is matched as a rational tree, and - - - X is -X

6 ?- X = -X.
   sto, X = - - - X .

% deliberately failing: - - + X is not

7 ?- X = -X.
   sto, X = - - + X .

% deliberately failing: an acyclic answer is not sto

8 ?- X = - - - Y.
   sto, X = - - - ... .

% deliberately failing: sto no longer excuses the rest of the answer

9 ?- X = -X.
   sto, false.

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
