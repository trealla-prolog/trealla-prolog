:- initialization(main).

% Issue #1154: sto says the query is subject to occurs check, as p.p.1.4 of UWN's prologue has it, and is checked on every outcome.

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

% deliberately failing: nothing here is subject to occurs check

8 ?- X = - - - Y.
   sto, X = - - - ... .

% deliberately failing: sto does not excuse the rest of the answer

9 ?- X = -X.
   sto, false.

% p.p.1.4 verbatim: sto leads each answer sequence and holds to its end

10 ?- member(X, X).
   sto, % occurs-check
   loops
|  sto, % rational trees
   X = [X|_A]
;  X = [_A,X|_B]
;  X = [_A,_B,X|_C]
;  ..., ad_infinitum
|  sto, % literal substitutions
   X = [_A|_B]
;  X = [_A,[_A,[_A|_B]|_C]|_C]
;  X = [_A,_B,[_A,_B,[_A,_B|_C]|_D]|_D]
;  ..., ad_infinitum.

% p.p.1.4's tentative extension, sto factored out of the alternatives

11 ?- member(X, X).
   sto,
   (  loops        % occurs-check
   |  X = [X|_A]   % rational trees
   ;  X = [_A,X|_B]
   ;  X = [_A,_B,X|_C]
   ;  ..., ad_infinitum
   |  X = [_A|_B]  % literal substitutions
   ;  X = [_A,[_A,[_A|_B]|_C]|_C]
   ;  X = [_A,_B,[_A,_B,[_A,_B|_C]|_D]|_D]
   ;  ..., ad_infinitum
   ).

% subject to occurs check with no cyclic binding left in the answer

12 ?- \+ \+ X = f(X).
   sto, true.

% deliberately failing

13 ?- \+ \+ X = f(X).
   true.

14 ?- X = f(X), false.
   sto, false.

% deliberately failing

15 ?- X = f(X), false.
   false.

16 ?- X = f(X), throw(x).
   sto, throw(x).

% deliberately failing

17 ?- X = f(X), throw(x).
   throw(x).

% sto holds from the answer that says it

18 ?- X = 1 ; X = f(X).
   X = 1
;  sto, X = f(X).

% deliberately failing: the first answer comes before any cycle

19 ?- X = 1 ; X = f(X).
   sto, X = 1
;  X = f(X).

% unify_with_occurs_check/2 is defined for such terms, so it is not sto

20 ?- unify_with_occurs_check(X, f(X)).
   false.

% nc#46 of UWN's number_chars page, sto on each alternative outcome

21 ?- L=['1'|L], number_chars(N,L).
sto, ... ; ... .
  sto, false % occurs-check
| sto, representation_error(term)
| sto, loops % rational trees
| sto, type_error(list,['1'|...])
| sto, resource_error(...)
| sto, instantiation_error. % literal substitutions

% the occurs-check alternative of p.p.1.4

22 ?- set_prolog_flag(occurs_check, true).
   true.

23 ?- member(X, X).
   sto, loops.

% deliberately failing

24 ?- member(X, X).
   loops.

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
