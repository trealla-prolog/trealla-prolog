:- initialization(main).

% Quads: queries using answer descriptions (issue #1063).
% These are recorded at load time and interpreted by library(quads).

:- use_module(library(dif)).
:- use_module(library(freeze)).

foo(bar).
loop :- loop.

?- member(X, [1,2,3]).
   X = 1
;  X = 2
;  X = 3.

?- foo(X).
   X = bar.

?- foo(baz).
   false.

?- X = 1, Y = 2.
   X = 1, Y = 2.

?- atom_length(A, L).
   instantiation_error.

?- atom_length(abc, L).
   L = 3.

?- between(1, 100, X).
   X = 1
;  X = 2
;  ... .

?- repeat.
   true
;  true
;  ..., ad_infinitum.

?- catch(throw(ball), E, true).
   E = ball
|  error(system_error, ...).

?- length(L, 2).
   L = [_,_].

?- loop.
   loops.

% the 'unexpected' annotation: the described answer must not occur

?- X = 1.
   X = 2, unexpected.

?- member(X, [1,2,3]).
   X = 4, unexpected
;  ... .

?- X = 1.
   X = 2, unexpected
|  X = 3, unexpected.

% short forms of the ISO errors (issue #1066)

?- undefined_pred_xyz(1).
   existence_error(procedure, undefined_pred_xyz/1).

?- assertz(atom_length(a,b)).
   permission_error(modify, static_procedure, atom_length/2).

?- X is 1//0.
   evaluation_error(zero_divisor).

?- atom_length(1, L).
   type_error(atom, 1).

% an answer description must describe the answer completely (issue #1067)

?- X = f(Y,Z), Y = Z.
   X = f(Y,Y), Z = Y.

?- X = 1, Y = 2.
   X = 1, Y = 2.

% a quad may carry several answer descriptions; all must hold

?- X = 1.
   X = 2, unexpected.
   X = 1.

% 'inattendue' is a synonym for 'unexpected'

?- X = 1.
   X = 2, inattendue.

% a deliberately failing quad, to test reporting

?- member(X, [1,2]).
   X = 1
;  X = 99.

% a deliberately failing 'unexpected' quad: this answer does occur

?- X = 1.
   X = 1, unexpected.

% a deliberately failing quad: too general, says nothing about Z

?- X = f(Y,Z), Y = Z.
   X = f(Y,Y).

% a quad may be labelled with a ground term (issue #1071)

member_1 ?- member(X, [1,2,3]).
   X = 1
;  X = 2
;  X = 3.

'a quoted label' ?- foo(X).
   X = bar.

label(with, args) ?- X = 1.
   X = 1.

% a labelled quad that fails, to test reporting

member_2 ?- member(X, [1,2]).
   X = 1
;  X = 99.

% A described ball must be a variant of the one actually thrown, so the
% correspondence between their variables is one-to-one (issue #1080)

?- throw(error(_,_)).
   error(X,Y).

?- throw(f(A,A)).
   throw(f(X,X)).

% throw/1 copies the ball, so an anonymous description variable
% describes the copy

?- throw(f(X)).
   throw(f(_)).

% a deliberately failing quad: one description variable cannot describe
% two distinct variables of the ball

ball_1 ?- throw(error(_A,_B)).
   error(X,X).

% a deliberately failing quad: nor two description variables one
% variable of the ball

ball_2 ?- throw(f(A,A)).
   throw(f(X,Y)).

% deliberately failing quads: a variable shared with the query is that
% variable of the query, which a copied ball never contains

ball_3 ?- throw(f(X)).
   throw(f(X)).

ball_4 ?- throw(error(type_error(atom,[X,Y]),[])).
   type_error(atom,[X,X]).

% An answer substitution is idempotent, so no variable it binds occurs
% in what it binds another to (issue #1081): the answer here is
% 'X = f(1), Y = 1', not 'X = f(Y), Y = 1'. The parser rejects the
% latter when the file is consulted, which aborts the load, so that
% case is tests/issues/test1081.pl.

?- X = f(Y), Y = 1.
   X = f(1), Y = 1.

% one variable may still be bound to another that the answer leaves free

?- X = Y.
   X = Y.

% a description annotated 'sto' is exempt, a cyclic term being what it
% states

?- X = f(X).
   X = f(X), sto.

% outputs/1 records what the query writes to current output (issue
% #1082). A list of characters (or double-quoted string) is enough for
% now; it may be conjoined with true or an error description.

outputs_1 ?- write(abc), nl.
   outputs("abc\n"),
   true.

outputs_2 ?- call((write(3), X)).
   outputs("3"),
   instantiation_error.

% Per-answer output under disjunction: only the characters produced for
% that answer are matched, not the cumulative capture of call_nth
% (issue #1084).

outputs_3 ?- put_char(a) ; put_char(b).
   outputs("a")
;  outputs("b").

% deliberately failing: wrong output

outputs_4 ?- write(abc), nl.
   outputs("nope"),
   true.

% '...' in a binding stands for an unspecified subterm (issue #1088).

ellipsis_1 ?- X = 1.
   X = ... .

ellipsis_2 ?- length(L, 999).
   L = [_A,_B,_C|...].

% deliberately failing: '...' does not make a different functor match

ellipsis_3 ?- X = 1.
   X = f(...).

% ad_infinitum accepts any further answers, as '...' does, and either
% may be written as a conjunct of the answer it follows.

more_1 ?- repeat.
   true
;  ad_infinitum.

more_2 ?- member(X, [1,2,3]).
   X = 1, ... .

% deliberately failing: the answer described alongside '...' must hold

more_3 ?- member(X, [1,2,3]).
   X = 9, ... .

% maybe marks that the query left some variable attributed - a
% pending constraint of any kind, not resolved into an ordinary binding
% - once the query has answered (issue #1128). It names no particular
% variable or attribute module, only that one exists - not even one of
% the query's own, as maybe_4 shows: freeze/2's pending goal sits on a
% variable local to it, never named by the query.

maybe_1 ?- dif(X, Y), X = a.
   X = a, maybe.

maybe_2 ?- dif(X, Y).
   maybe.

% deliberately failing: X = 1 leaves nothing attributed

maybe_3 ?- X = 1.
   X = 1, maybe.

% Since an answer describes an answer completely (issue #1067), the
% absence of maybe is itself an assertion - nothing is left pending -
% so a bare 'true' does not equally describe the answer to maybe_4.

maybe_4 ?- freeze(_, false).
   maybe.
   true, unexpected.

% 'unexpected' marks the leaf answer given at that point as not the one
% the query has there, and the description ends: what follows it, if
% anything, is not described (issue #1141).

unexpected_1 ?- member(X, [a,b,c]).
   X = a
;  X = c, unexpected.

% deliberately failing: X = b is the answer at that point

unexpected_2 ?- member(X, [a,b,c]).
   X = a
;  X = b, unexpected.

% A float answer may be described to the precision the expectation is
% written to (issue #1145). It is an atom so that its trailing zeroes
% survive being read - as a float, '14.2000' and '14.2' are one term.
% The operator comes from library(quads), so it has to be imported
% before any quad using it is read.

:- use_module(library(quads)).

approx_1 ?- X is 71/5.
   X ~~ '14.2000'.

approx_2 ?- X is 71/5.
   X ~~ '14.2'.

approx_3 ?- X is -71/5.
   X ~~ '-1.42e1'.

% the bounds are the exact decimals: the float nearest 14.20005 is
% below that decimal, so it is inside [14.19995, 14.20005]

approx_4 ?- X = 14.20005.
   X ~~ '14.2000'.

% deliberately failing: 14.2 is not within [14.25, 14.35]

approx_5 ?- X is 71/5.
   X ~~ '14.3'.

% deliberately malformed: no float lies in an interval that narrow

approx_6 ?- X = 1.0.
   X ~~ '1.0000000000000000'.

% An answer description must describe an answer *substitution*, so each
% equation binds a variable and no variable is bound twice within one
% answer (issue #1074). The parser rejects a malformed description when
% the file is consulted, which aborts the load, so that case is
% tests/issues/test1074.pl; a '$quad' fact can also be asserted by hand,
% and library(quads) makes the same check on those.

% run_quads names each file as it was consulted, so the report would
% otherwise depend on where in the tree this one sits and have to be
% reissued every time it moves. Keep the base name only.
% 'hand-written.pl' has no directory to strip and passes through
% unchanged. Only the file:line token is stripped: dropping everything
% before any '/' at all, the way tests/issues/test1099.pl does, would
% take the queries with it and report '?- X is 5' for 'X is 71/5'.

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
	use_module(library(quads)),
	assertz('$quad'(hand_1, (X=1), ['X'=X], (1 = X), 'hand-written.pl', 1)),
	assertz('$quad'(hand_2, (Y=1), ['Y'=Y], (Y = 1, Y = 2), 'hand-written.pl', 2)),
	assertz('$quad'(hand_3, (Z=1), ['Z'=Z], (Z = 1), 'hand-written.pl', 3)),
	assertz('$quad'(hand_4, (V=f(W),W=1), ['V'=V,'W'=W], (V = f(W), W = 1), 'hand-written.pl', 4)),
	with_output_to(chars(Cs), run_quads),
	strip_dirs(Cs, Out),
	atom_chars(A, Out),
	write(A).
