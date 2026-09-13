:- initialization(main).
:- use_module(library(quads)).

% Issue #1157: a non-variable left of ~~ binds nothing, it is only tested against Spec.

1 ?- X = 0.04.
   X ~~ '0.0'.

2 ?- true.
   0.04 ~~ '0.0'.

% alongside a binding, on either side of it

3 ?- X = 1.
   X = 1, 0.04 ~~ '0.0'.

4 ?- X = 1.
   0.04 ~~ '0.0', X = 1.

% deliberately failing: 0.14 is not within [-0.05, 0.05]

5 ?- true.
   0.14 ~~ '0.0'.

6 ?- true.
   0.14 ~~ '0.0', unexpected.

% deliberately failing: an integer is not a float, as for a binding in test1145.pl

7 ?- true.
   14 ~~ '14.0'.

% deliberately malformed: Spec must still be an atom

8 ?- true.
   0.04 ~~ 0.0.

% run_quads names the file as it was consulted, so keep the base name only, as tests/issues/test1156.pl does.

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
	with_output_to(chars(Cs), run_quads),
	strip_dirs(Cs, Out),
	atom_chars(A, Out),
	write(A).
