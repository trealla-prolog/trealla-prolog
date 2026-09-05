:- initialization(main).
:- use_module(library(quads)).

% Issue #1145: 'V ~~ Spec' describes a float to the precision Spec is
% written to. Spec is an atom so its trailing zeroes survive reading -
% as a float, '14.2000' and '14.2' would be the same term.

1 ?- X is 71/5.
   X ~~ '14.2000'.

2 ?- X is 71/5.
   X ~~ '14.2'.

% an exponent takes its precision from the mantissa

3 ?- X is 71/5.
   X ~~ '1.42e1'.

4 ?- X is -71/5.
   X ~~ '-14.2000'.

% deliberately failing: 14.2 is not within [14.25, 14.35]

5 ?- X is 71/5.
   X ~~ '14.3'.

% The bounds are the exact decimals, not their nearest floats. The
% float nearest 14.19995 lies below that decimal, so it is outside
% '14.2000'; the one nearest 14.20005 is below its bound too, so it is
% inside. Comparing against rounded bounds would get one of these wrong.

6 ?- X = 14.20005.
   X ~~ '14.2000'.

% deliberately failing: the same asymmetry, the other way

7 ?- X = 14.19995.
   X ~~ '14.2000'.

% A precision finer than a float can carry describes nothing: the
% interval must hold a float below the value, the value, and one above.

8 ?- X = 1.0.
   X ~~ '1.000000000000000'.

9 ?- X = 1.0.
   X ~~ '1.0000000000000000'.

% the expectation must be an atom, must spell a float, and must not
% overflow; the observed value must be a float

10 ?- X is 71/5.
   X ~~ 14.2.

11 ?- X is 71/5.
   X ~~ '14'.

12 ?- X is 71/5.
   X ~~ '1.0e400'.

13 ?- X = 14.
   X ~~ '14.0'.

% ~~ binds, so binding the same variable twice is a rebinding

14 ?- X is 71/5.
   X = 14.2, X ~~ '14.2'.

% run_quads names the file as it was consulted, so the report would
% otherwise depend on the path this was invoked with. Strip the
% directories from the file:line token only - test1099.pl's version
% drops everything before any '/' at all, which here would eat the
% queries as well and report '?- X is 5'.

strip_dirs(Cs, Out) :- strip_dirs(Cs, [], Out).

strip_dirs([], W, Out) :- flush(W, [], Out).
strip_dirs([C|Cs], W, Out) :-
	(	( C == ' ' ; C == '\n' )
	->	flush(W, [C|Out0], Out),
		strip_dirs(Cs, [], Out0)
	;	strip_dirs(Cs, [C|W], Out)
	).

% W is the token, reversed. A file reference keeps its base name only.

flush(W, Tail, Out) :-
	reverse(W, T),
	(	file_ref(T)
	->	before_slash(W, RB), reverse(RB, T2)
	;	T2 = T
	),
	append(T2, Tail, Out).

file_ref(T) :- append(_, S, T), append(".pl:", _, S), !.

before_slash([], []).
before_slash([C|Cs], Out) :-
	(	C == (/)
	->	Out = []
	;	Out = [C|Out0],
		before_slash(Cs, Out0)
	).

main :-
	with_output_to(chars(Cs), run_quads),
	strip_dirs(Cs, Out),
	atom_chars(A, Out),
	write(A).
