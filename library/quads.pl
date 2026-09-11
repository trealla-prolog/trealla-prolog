/** Quads: queries using answer descriptions.

  Quads are embedded tests: a '?- Query.' term in a source file
  followed by a term describing the expected toplevel answers.
  See https://github.com/trealla-prolog/trealla/issues/1063

  When a file is consulted the compiler records each quad as a fact

      '$quad'(Id, Query, VarNames, AnswerDescription, File, Line)

  in the module being consulted, Id being the label of a quad written
  'Name ?- Query.' and unbound otherwise. Nothing is executed at load
  time.
  This library interprets the recorded quads as tests:

      ?- use_module(library(quads)).
      ?- run_quads.             % run quads recorded in module user
      ?- run_quads(mymod).      % run quads recorded in some module

  run_quads/0 and run_quads/1 always succeed and print a report.
  run_quads_halt/0 halts the system with a non-zero exit code if
  any quad failed, for use in scripts:

      tpl file.pl -g 'use_module(library(quads)), run_quads_halt'

  Answer description syntax (see the issue for the full grammar):

      ?- member(X, [1,2,3]).
         X = 1
      ;  X = 2
      ;  X = 3.

  A quad may be labelled with a ground term, which reports name it:

      member_1 ?- member(X, [1,2,3]).
         X = 1
      ;  X = 2
      ;  X = 3.

      ?- fail.
         false.

      ?- atom_length(A, L).
         error(instantiation_error, _).

      ?- repeat.
         true
      ;  true
      ;  ... .

  ... accepts any further answers, and so does ad_infinitum, which
  states that there are infinitely many of them:

      ?- repeat.
         true
      ;  ad_infinitum.

  Both are annotations, so either may also be written as a conjunct of
  the last answer described ('X = 1, ... .').

  A query that does not terminate is described by loops:

      ?- repeat, fail.
         loops.

  Alternative acceptable outcomes are separated by (|)/2.

  other_answer_sequence marks that the answers of the companion
  alternative may appear in any order (ISO setof/3, 8.10.3). It is
  well formed only with a single alternative of at least two leaf
  answers (issue #1096):

      ?- setof(1, (Y=2 ; Y=1), L).
         Y = 2, L = [1]
      ;  Y = 1, L = [1]
      |  other_answer_sequence.

  A quad may carry more than one answer description; all of them
  must hold.

  An answer description annotated with 'unexpected' (or its synonym
  'inattendue') describes an answer that must *not* occur:

      ?- X = 1.
         X = 2, unexpected.

  It marks a single leaf answer as not the one given at that point,
  and the description ends there: nothing is claimed about the
  answers that follow, if any.

      ?- member(X, [a,b,c]).
         X = a
      ;  X = c, unexpected.

  sto marks an answer whose bindings are cyclic, which only unification
  without the occurs check can give (issue #1154). Its absence is an
  assertion too:

      ?- X = -X.
         sto, X = - - - ... .
         X = - - - ..., unexpected.

  A sto answer is matched as a rational tree, so it may state the cycle
  itself: 'sto, X = - - - X' holds of the query above. On an answer that
  is not a substitution (false, loops, an error) sto is not checked.

  outputs/1 records what the query writes to current output (issue
  #1082). Its argument is matched against the captured characters; a
  character list or double-quoted string is accepted for now (a DCG
  body also works via phrase/2). It is a conjunct of the answer, as in
  the English answer descriptions of ISO 13211-1:

      ?- write(abc), nl.
         outputs("abc\n"),
         true.

  An answer reports an answer *substitution*, so each equation binds a
  variable and no variable is bound twice within one answer. '1 = X'
  and 'X = 1, X = 2' are rejected as malformed, not run as tests.

  In a binding, ... stands for an unspecified subterm (issue #1088):

      ?- X = 1.
         X = ... .

      ?- length(L, 999).
         L = [_A,_B,_C|...].

  maybe marks that the query left some variable attributed - a
  constraint (dif, clpz, freeze, or any other attribute module) is
  still pending, not resolved into an ordinary binding. It says only
  that one exists, not which, or on what variable: a pending goal
  inside the query that names none of its own variables is still a
  maybe, as freeze/2 shows:

      ?- dif(X, Y), X = a.
         X = a, maybe.

      ?- freeze(_, false).
         maybe.

  Unlike unexpected and ... it is not stripped as a bare annotation:
  it is itself part of what the answer asserts, so 'maybe.' alone
  (no bindings at all) is also a valid, checked description. And since
  an answer describes an answer *completely* (issue #1067), the
  absence of maybe is itself an assertion - nothing is left pending -
  so plain 'true' does not also describe the freeze/2 answer above.

  A float answer may be described approximately (issue #1145):

      ?- X is 71/5.
         X ~~ '14.2000'.

  'V ~~ Spec' holds when V is a float within the interval Spec is
  written to: '14.2000' names [14.19995, 14.20005], inclusive at both
  ends. The trailing zeroes are what says so, which is why Spec is an
  atom - read as a float, '14.2000' and '14.2' would be the same term
  and the precision would be gone. Exponents are taken from the
  precision of the mantissa, so '1.42e1' names [14.15, 14.25], and
  negative values work the same way.

  A precision finer than a float can carry describes nothing, so it is
  malformed rather than merely unsatisfiable: the interval must hold
  three floats, one below the value, the value, and one above.
  (~~)/2 is exported by this module as a 700 xfx operator, so a file
  using it has to import library(quads) at load time. There is no
  (~~)/2 predicate.
*/

:- module(quads, [run_quads/0, run_quads/1, run_quads_halt/0,
		op(700, xfx, ~~)]).

:- use_module(library(lists)).
:- use_module(library(iso_ext)).
:- use_module(library(dcgs)).

run_quads :-
	run_quads(user).

% The count run_quads_halt/0 exits on is recorded by every run, a run
% over a module without quads included: leaving the previous count in
% place would halt non-zero on 'nothing to run'.

run_quads(M) :-
	quad_list(M, Qs),
	( Qs == [] ->
		write('quads: nothing to run.'), nl,
		Failed = 0
	;	run_list(Qs, M, 0, Passed, 0, Failed),
		Total is Passed + Failed,
		write('quads: '), write(Total), write(' run, '),
		write(Passed), write(' passed, '),
		write(Failed), write(' failed.'), nl
	),
	bb_put(quads_failed, Failed).

run_quads_halt :-
	run_quads,
	( bb_get(quads_failed, N), N > 0 -> halt(1) ; halt ).

quad_list(M, Qs) :-
	catch(
		findall(q(Id, Q, VNs, AD, F, L), M:'$quad'(Id, Q, VNs, AD, F, L), Qs),
		error(existence_error(_, _), _),
		Qs = []
	).

run_list([], _, P, P, F, F).
run_list([q(Id, Q, VNs, AD, File, Line)|T], M, P0, P, F0, F) :-
	( \+ \+ check_quad(M, Id, Q, VNs, AD, File, Line) ->
		P1 is P0 + 1, F1 = F0
	;	P1 = P0, F1 is F0 + 1
	),
	run_list(T, M, P1, P, F1, F).

% A quad passes if any (|)-alternative of its answer description
% matches. Same-named variables of the query term and the answer
% description term are first unified via the VarNames list.
%
% other_answer_sequence (issue #1096) is not itself an outcome: it
% annotates the remaining alternative so its leaf answers may match
% in any order. Well formed only with exactly one companion
% alternative that has at least two leaves.

check_quad(M, Id, Q, VNs, AD, File, Line) :-
	(	Id == '$bad_quad_identifier'
	->	report(bad_identifier, Id, Q, VNs, Id, File, Line),
		fail
	;	malformed(AD, Bad)
	->	report(malformed, Id, Q, VNs, Bad, File, Line),
		fail
	;	alternatives(AD, Alts0),
		peel_other_answer_sequence(Alts0, Alts, HasOAS),
		(	HasOAS == true
		->	(	Alts = [Alt],
				solutions(Alt, Sols),
				Sols = [_,_|_]
			->	(	\+ \+ check_alternative_any_order(M, Q, VNs, Alt)
				->	true
				;	report(failed, Id, Q, VNs, AD, File, Line),
					fail
				)
			;	report(malformed, Id, Q, VNs, other_answer_sequence, File, Line),
				fail
			)
		;	(	member(Alt, Alts),
				\+ \+ check_alternative(M, Q, VNs, Alt)
			->	true
			;	report(failed, Id, Q, VNs, AD, File, Line),
				fail
			)
		)
	).

peel_other_answer_sequence([], [], false).
peel_other_answer_sequence([Alt|T], Rest, Has) :-
	(	Alt == other_answer_sequence
	->	peel_other_answer_sequence(T, Rest, _),
		Has = true
	;	Rest = [Alt|Rest0],
		peel_other_answer_sequence(T, Rest0, Has0),
		Has = Has0
	).

check_alternative_any_order(M, Q, VNs, Alt) :-
	solutions(Alt, Sols),
	(	sols_have_output(Sols)
	->	Mode = capture
	;	Mode = plain
	),
	permutation(Sols, Perm),
	check_solutions(Perm, M, Q, VNs, 1, Mode, []).

% The parser used to reject a malformed answer description when the
% file was consulted (issue #1074). That aborted the load (issue #1078);
% both malformed answers and non-ground labels are now recorded so
% run_quads can report them. An answer reports a substitution: every
% equation binds a variable, and no variable is bound twice within one
% answer, so neither '1 = X' nor 'X = 1, X = 2' describes an answer.

malformed(AD, Bad) :-
	alternatives(AD, Alts),
	member(Alt, Alts),
	solutions(Alt, Sol),
	member(S, Sol),
	conj(S, Items),
	(	member(Bad, Items),
		\+ answer_item(Bad)
	->	true
	;	rebound(Items, Bad)
	->	true
	;	\+ ( member(I, Items), annotation(I, sto) ),
		unsolved(Items, Bad)
	).
% Input annotations (issue #1099). An answer may say what the query
% reads: inputs/1 is the characters it consumes, peeks/1 the single
% character it looks at without consuming, waits that it asks for a
% character none is there to supply. Only one of each may appear in an
% answer, since a query is run once against one input.
%
% 'peeks(C), waits' cannot occur: a peek leaves C unconsumed, so every
% later read returns it and the query can never be left waiting. To
% wait it would have to have consumed C, which is 'inputs([C]), waits'.

malformed(AD, Bad) :-
	solution_items(AD, Items),
	(	member(I, Items),
		nonvar(I),
		I = inputs(Cs),
		\+ chars_list(Cs)
	->	Bad = I
	;	member(I, Items),
		nonvar(I),
		I = peeks(Ps),
		\+ one_char(Ps)
	->	Bad = I
	;	member(Name, [inputs, peeks, outputs]),
		twice(Items, Name, Bad)
	->	true
	;	twice_atom(Items, waits, Bad)
	->	true
	;	member(I, Items), nonvar(I), I = peeks(_),
		member(J, Items), J == waits
	->	Bad = (I, waits)
	).

solution_items(AD, Items) :-
	alternatives(AD, Alts),
	member(Alt, Alts),
	solutions(Alt, Sols),
	member(S, Sols),
	conj(S, Items).

% A proper list of one-character atoms. inputs/1 takes characters, not
% a DCG body: an answer says exactly what was read, and a nonterminal
% may stand for more than one string.

chars_list(Cs) :- var(Cs), !, fail.
chars_list([]).
chars_list([C|Cs]) :- nonvar(C), atom(C), atom_length(C, 1), chars_list(Cs).

one_char(Ps) :- nonvar(Ps), Ps = [C], nonvar(C), atom(C), atom_length(C, 1).

twice(Items, Name, Bad) :-
	named_items(Items, Name, [_,Bad|_]).

named_items([], _, []).
named_items([I|T], Name, Out) :-
	(	nonvar(I),
		functor(I, Name, 1)
	->	Out = [I|Out0]
	;	Out = Out0
	),
	named_items(T, Name, Out0).

twice_atom(Items, Atom, Atom) :-
	same_atoms(Items, Atom, N),
	N > 1.

same_atoms([], _, 0).
same_atoms([I|T], Atom, N) :-
	same_atoms(T, Atom, N0),
	( I == Atom -> N is N0 + 1 ; N = N0 ).

% other_answer_sequence belongs only as a (|)-alternative of its own,
% not as a conjunct of a leaf answer.
malformed(AD, other_answer_sequence) :-
	alternatives(AD, Alts),
	member(Alt, Alts),
	Alt \== other_answer_sequence,
	solutions(Alt, Sols),
	member(S, Sols),
	conj(S, Items),
	member(I, Items),
	I == other_answer_sequence.

answer_item(I) :- var(I), !, fail.
answer_item(V = _) :- !, var(V).
answer_item(V ~~ Spec) :- !, var(V), approx_spec(Spec, _, _, _).
answer_item(I) :- atom(I), answer_atom(I), !.
answer_item(outputs(_)) :- !.
answer_item(inputs(_)) :- !.
answer_item(peeks(_)) :- !.
answer_item(I) :- expected_ball(I, _), !.

answer_atom(true).
answer_atom(false).
answer_atom(...).
answer_atom(loops).
answer_atom(ad_infinitum).
answer_atom(sto).
answer_atom(unexpected).
answer_atom(inattendue).
answer_atom(other_answer_sequence).
answer_atom(waits).
answer_atom(maybe).

% Report the equation that rebinds, not the one it clashes with.

rebound([I|T], Bad) :-
	(	binds(I, V),
		lhs_item(T, V, Bad0)
	->	Bad = Bad0
	;	rebound(T, Bad)
	).

lhs_item([I|T], V, Bad) :-
	(	binds(I, V2),
		V2 == V
	->	Bad = I
	;	lhs_item(T, V, Bad)
	).

% An equation binds its variable, and so does 'V ~~ Spec' (issue
% #1145), only approximately. Both count wherever a binding does.

binds(I, V) :- nonvar(I), I = (V0 = _), !, var(V0), V = V0.
binds(I, V) :- nonvar(I), I = (V0 ~~ _), var(V0), V = V0.

% A substitution is idempotent, so no variable it binds occurs in what
% it binds another to. 'X = f(Y), Y = 1' is not an answer, the answer
% being 'X = f(1), Y = 1' (issue #1081). Report the equation that is
% not in solved form, not the one binding the variable it mentions.

unsolved(Items, Bad) :-
	bound_vars(Items, Vs),
	member(Bad, Items),
	nonvar(Bad),
	Bad = (_ = Val),
	term_variables(Val, Rs),
	member(R, Rs),
	var_member(R, Vs),
	!.

bound_vars([], []).
bound_vars([I|T], Vs) :-
	(	binds(I, V)
	->	Vs = [V|Vs0]
	;	Vs = Vs0
	),
	bound_vars(T, Vs0).

% A quad written 'Name ?- Query.' is identified by Name, which is
% reported so a suite can be read without counting line numbers
% (issue #1071). An unlabelled quad leaves Id unbound. The module is
% not printed (plain '?- Query.'), so report/7 does not take it —
% keeping an unused M warned as a singleton on use_module (issue #1085).

report(Why, Id, Q, VNs, What, File, Line) :-
	link_names(VNs),
	write('quads: '), write_why(Why), write(' '),
	(	var(Id)
	->	true
	;	Id == '$bad_quad_identifier'
	->	true
	;	write_term(Id, [quoted(true)]), write(', ')
	),
	write(File), write(':'), write(Line), nl,
	write('   ?- '), write_term(Q, [variable_names(VNs), quoted(true)]), write('.'), nl,
	write('   '), write_what(Why, What, VNs), nl.

write_why(failed) :- write('FAILED').
write_why(malformed) :- write('MALFORMED').
write_why(bad_identifier) :- write('BAD_ID').

write_what(failed, What, VNs) :-
	write(expected), write(': '),
	write_term(What, [variable_names(VNs), quoted(true)]).
write_what(malformed, What, VNs) :-
	write('not an answer'), write(': '),
	write_term(What, [variable_names(VNs), quoted(true)]).
write_what(bad_identifier, _, _) :-
	write('identifier is not ground').

% An answer description may carry the annotation 'unexpected',
% meaning the answer it describes must *not* occur. It attaches to a
% single leaf answer, not to a disjunction, so
%
%     ?- foo(X).
%        X = 1
%     ;  X = 2, unexpected.
%
% asserts that the first answer is X = 1 and that, if there is a second
% answer, it is not X = 2. A quad documenting a known bug therefore
% fails while the bug is present and passes once it is fixed, which is
% what lets a quad be filed verbatim as a bug report.

% 'unexpected' says only that this leaf answer is not the one given
% *here*, so the description ends there and claims nothing about what
% follows - not even that nothing does (issue #1141).

% Annotations may appear anywhere in the conjunction, most naturally
% as a trailing conjunct, so they are removed wherever they occur.

drop_annotation([], _, [], false).
drop_annotation([I|T], Ann, Kept, F) :-
	drop_annotation(T, Ann, Kept0, F0),
	(	annotation(I, Ann)
	->	Kept = Kept0, F = true
	;	Kept = [I|Kept0], F = F0
	).

annotation(I, unexpected) :- ( I == unexpected ; I == inattendue ), !.
annotation(I, Ann) :- I == Ann.

rebuild_conj([], true).
rebuild_conj([I], I) :- !.
rebuild_conj([I|T], (I , R)) :-
	rebuild_conj(T, R).

link_names([]).
link_names([N=V|T]) :-
	( member(N2=V2, T), N2 == N -> V = V2 ; true ),
	link_names(T).

alternatives('|'(A, B), [A|T]) :- !,
	alternatives(B, T).
alternatives(A, [A]).

solutions((A ; B), [A|T]) :- !,
	solutions(B, T).
solutions(A, [A]).

conj((A , B), [A|T]) :- !,
	conj(B, T).
conj(A, [A]).

check_alternative(M, Q, VNs, Alt) :-
	solutions(Alt, Sols),
	(	sols_have_output(Sols)
	->	Mode = capture
	;	Mode = plain
	),
	check_solutions(Sols, M, Q, VNs, 1, Mode, []).

sols_have_output([S|T]) :-
	(	has_outputs(S)
	->	true
	;	sols_have_output(T)
	).

has_outputs(Sol) :-
	conj(Sol, Items),
	member(I, Items),
	nonvar(I),
	I = outputs(_),
	!.

% Walk the expected solutions, requesting the Nth answer of the
% query for the Nth description. After the last description the
% query must yield no further answer, unless ... or ad_infinitum said
% otherwise.
%
% Mode = capture when any solution of this alternative uses outputs/1:
% each call_nth(N) is captured in full, and only the suffix beyond the
% previous answer's capture is matched — call_nth re-runs prior
% branches, so a naive capture of the Nth answer would include their
% output too (issue #1084). PrevCs is that prior capture (initially
% []). The final 'none' probe is silenced the same way.

check_solutions([], M, Q, VNs, N, plain, _) :- !,
	attempt(M, Q, VNs, N, none, no_output).
check_solutions([], M, Q, VNs, N, capture, _) :-
	attempt(M, Q, VNs, N, none, silence).
check_solutions([Sol0|T], M, Q, VNs, N, Mode, PrevCs) :-
	conj(Sol0, Items0),
	drop_annotation(Items0, unexpected, Items, Unexpected),
	drop_annotation(Items, sto, Items1, Sto),
	drop_more(Items1, Items2, More),
	take_output(Items2, Items3, Output),
	take_input(Items3, Items4, Input),
	rebuild_conj(Items4, Sol),
	( More == true, Items4 == [] ->
		true							% any further answers accepted
	; \+ no_input(Input) ->
		T == [],					% one input, so one described answer
		solution_expect(Items4, Sol, Sto, Expect0),
		input_expect(Input, Expect0, Expect),
		expect_on_input(Unexpected, Input,
			expect(no, M, Q, VNs, N, Expect, Output, Mode, PrevCs, _))
	; Items4 = [loops] ->
		expect(Unexpected, M, Q, VNs, N, loops, Output, Mode, PrevCs, _)
	; Items4 = [false] ->
		T == [],
		expect(Unexpected, M, Q, VNs, N, none, Output, Mode, PrevCs, _)
	; expected_ball(Sol, Ball) ->
		T == [],
		expect(Unexpected, M, Q, VNs, N, ball(Ball), Output, Mode, PrevCs, _)
	;	with_sto(Sto, Items4, Items5),
		expect(Unexpected, M, Q, VNs, N, solution(Items5), Output, Mode, PrevCs, FullCs),
		(	More == true
		->	true					% described, then anything further
		;	Unexpected == true
		->	true					% nothing is claimed past it (issue #1141)
		;	N1 is N + 1,
			check_solutions(T, M, Q, VNs, N1, Mode, FullCs)
		)
	).

% Both ... and ad_infinitum say that further answers are accepted, the
% latter that there are infinitely many; neither is checked beyond that.
% They are annotations rather than answers, so, like unexpected and sto,
% they are recognised wherever they occur in the conjunction: 'X = 1,
% ... .' describes the answer X = 1 and accepts whatever follows it.

drop_more(Items0, Items, More) :-
	drop_annotation(Items0, ..., Items1, Ellipsis),
	drop_annotation(Items1, ad_infinitum, Items, Infinitely),
	( Ellipsis == true -> More = true ; More = Infinitely ).

% outputs/1 is stripped before matching the rest of the answer so that
% 'outputs("3"), instantiation_error' still classifies as a ball, and
% the captured characters are checked separately (issue #1082).

% inputs/1, peeks/1 and waits are stripped the way outputs/1 is, and
% collected into one spec: what the query consumes, the character it
% may look at without consuming, and whether it is left asking for a
% character (issue #1099).

take_input(Items0, Items, in(In, Peek, Waits)) :-
	take_input_(Items0, Items, no_input, In, no_peek, Peek, false, Waits).

take_input_([], [], In, In, Peek, Peek, W, W).
take_input_([I|T], Rest, In0, In, Peek0, Peek, W0, W) :-
	(	nonvar(I), I = inputs(Cs)
	->	Rest = Rest0, In1 = chars(Cs), Peek1 = Peek0, W1 = W0
	;	nonvar(I), I = peeks([P])
	->	Rest = Rest0, In1 = In0, Peek1 = char(P), W1 = W0
	;	I == waits
	->	Rest = Rest0, In1 = In0, Peek1 = Peek0, W1 = true
	;	Rest = [I|Rest0], In1 = In0, Peek1 = Peek0, W1 = W0
	),
	take_input_(T, Rest0, In1, In, Peek1, Peek, W1, W).

no_input(in(no_input, no_peek, false)).

% The outcome a solution describes, independently of how the query is
% given its input.

solution_expect(Items, _, _, loops) :- Items == [loops], !.
solution_expect(Items, _, _, none) :- Items == [false], !.
solution_expect(_, Sol, _, ball(Ball)) :- expected_ball(Sol, Ball), !.
solution_expect(Items, _, Sto, solution(Items1)) :- with_sto(Sto, Items, Items1).

% sto is dropped to classify an answer, and put back where its bindings are checked.

with_sto(true, Items, [sto|Items]).
with_sto(false, Items, Items).

% 'waits' says the query asks for a character that is not there. The
% sentinel is what answers it: a query that reads on reaches 0xff and
% raises a representation error, and one that does not never sees it.
% So waiting is an outcome that can be described from a plain file,
% and it is not the timeout that 'loops' is.

input_expect(in(_, _, true), _, ball(Ball)) :- !,
	expected_ball(representation_error(character), Ball).
input_expect(_, Expect, Expect).

% Run Goal with current input on a file holding the described
% characters, then the character the query may peek at, then 0xff.
% Nothing in the file is a substitute for checking afterwards: the
% sentinel catches reading too much, and what is left in the stream
% catches reading too little.
%
% Standard Prolog throughout, deliberately (issue #1099): the harness
% has to be able to run on the systems whose conformity it reports on.

% What an answer describes now includes what the query read, so
% 'unexpected' has to negate the whole of it. Negating only the answer
% and then conjoining the input checks makes an answer and its
% unexpected twin *both* fail whenever the input claim is the wrong
% part:
%
%     ?- read(X).
%        inputs("1. "), X = 1.
%        inputs("1. "), X = 1, unexpected.
%
% Trealla's read/1 leaves the layout character after the end token
% unread, so the first is wrong. Exactly one of the two has to hold.

expect_on_input(true, Input, Goal) :- !,
	\+ run_on_input(Input, Goal).
expect_on_input(_, Input, Goal) :-
	run_on_input(Input, Goal).

run_on_input(in(In, Peek, Waits), Goal) :-
	input_chars(In, Cs),
	input_file(Cs, Peek, File),
	current_input(Old),
	open(File, read, S, []),
	setup_call_cleanup(
		set_input(S),
		(	call(Goal),
			(	Waits == true
			->	true				% the query consumed the sentinel itself
			;	left_unread(Peek, S)
			)
		),
		(	set_input(Old),
			catch(close(S), _, true),
			catch(delete_file(File), _, true)
		)
	).

input_chars(no_input, []).
input_chars(chars(Cs), Cs).

input_file(Cs, Peek, File) :-
	File = 'tmp.quads-input',
	open(File, write, S, []),
	put_chars(Cs, S),
	( Peek = char(P) -> put_char(S, P) ; true ),
	close(S),
	open(File, append, B, [type(binary)]),
	put_byte(B, 0xff),
	close(B).

put_chars([], _).
put_chars([C|Cs], S) :- put_char(S, C), put_chars(Cs, S).

% What the query did not consume. With peeks(P) the peeked character
% must still be there - a peek that took it is exactly the bug #1101
% was about - and the sentinel behind it. Without a peeks, the sentinel
% must come at once: anything else means the query read less than the
% answer says it did.

left_unread(no_peek, S) :- !,
	sentinel_next(S).
left_unread(char(P), S) :-
	catch(get_char(S, C), _, fail),
	C == P,
	sentinel_next(S).

sentinel_next(S) :-
	catch(( get_char(S, _), fail ),
		error(representation_error(character), _),
		true).

take_output([], [], no_output).
take_output([I|T], Rest, Out) :-
	(	nonvar(I),
		I = outputs(E)
	->	take_output(T, Rest, Out0),
		(	Out0 == no_output
		->	Out = outputs(E)
		;	Out0 = outputs(E),
			Out = Out0
		)
	;	take_output(T, Rest0, Out),
		Rest = [I|Rest0]
	).

% An 'unexpected' answer must not be the one the query produces there.

expect(true, M, Q, VNs, N, Expect, Output, Mode, PrevCs, FullCs) :- !,
	\+ expect_do(M, Q, VNs, N, Expect, Output, Mode, PrevCs, FullCs).
expect(_, M, Q, VNs, N, Expect, Output, Mode, PrevCs, FullCs) :-
	expect_do(M, Q, VNs, N, Expect, Output, Mode, PrevCs, FullCs).

expect_do(M, Q, VNs, N, Expect, Output, plain, PrevCs, PrevCs) :- !,
	(	Output = outputs(Expected)
	->	attempt(M, Q, VNs, N, Expect, outputs(Expected))
	;	attempt(M, Q, VNs, N, Expect, no_output)
	).
expect_do(M, Q, VNs, N, Expect, Output, capture, PrevCs, FullCs) :-
	attempt_capture(M, Q, VNs, N, Expect, FullCs),
	append(PrevCs, Delta, FullCs),
	(	Output = outputs(Expected)
	->	output_matches(Expected, Delta)
	;	true
	).

% Request the Nth answer of Q and check the outcome. Every call is
% bindings-transparent (\+ \+) and time-limited, so nonterminating
% queries are caught and nothing leaks between attempts. When the
% description names outputs/1, current output is captured for the
% duration of the attempt (issue #1082). 'silence' captures and
% discards, used when probing for a further answer after one that wrote.

attempt(M, Q, VNs, N, Expect, no_output) :- !,
	outcome(M, Q, VNs, N, Expect, Outcome),
	match_outcome(Q, VNs, Expect, Outcome).
attempt(M, Q, VNs, N, Expect, silence) :- !,
	attempt_capture(M, Q, VNs, N, Expect, _).
attempt(M, Q, VNs, N, Expect, outputs(Expected)) :-
	attempt_capture(M, Q, VNs, N, Expect, Cs),
	output_matches(Expected, Cs).

attempt_capture(M, Q, VNs, N, Expect, Cs) :-
	setup_call_cleanup(
		'$capture_output',
		outcome(M, Q, VNs, N, Expect, Outcome),
		'$capture_output_to_chars'(Cs)
	),
	match_outcome(Q, VNs, Expect, Outcome).

% The query is run once per attempt. An attempt that neither matches
% nor throws is 'none': a second probe telling a mismatched answer
% apart from no answer at all named an outcome match_outcome/4 acts on
% nowhere, and under capture it wrote the query's output a second time,
% so 'outputs("a"), false' saw "aa" and no failing query could
% describe what it had written (issue #1118).

outcome(M, Q, VNs, N, Expect, Outcome) :-
	catch(
		( call_with_time_limit(1.0, \+ \+ attempt_match(M, Q, VNs, N, Expect)) ->
			Outcome = matched
		;	Outcome = none
		),
		Ball0,
		( timeout_ball(Ball0) -> Outcome = loops ; Outcome = ball(Ball0) )
	).

% Expected is a character list (or double-quoted string under
% double_quotes(chars)), optionally a DCG body via phrase/2.
% For multi-answer quads only the per-answer suffix is passed here
% (issue #1084).

output_matches(Expected, Cs) :-
	(	Expected == Cs
	->	true
	;	Expected = Cs
	->	true
	;	catch(phrase(Expected, Cs), _, fail)
	).

% An answer description must describe the answer *completely*: the
% bindings of the query's named variables have to be a variant of the
% ones the description gives (issue #1067). Checking only the variables
% a description happens to mention would accept
%
%     ?- X = f(Y,Z), Y = Z.
%        X = f(Y,Y).
%
% which says nothing about Z. The query is solved in one copy and the
% description applied to a second, so the two witnesses stay
% independent and can be compared. Matching is variant-like, except
% that ... stands for an unspecified subterm — the same rule as for
% error balls — so 'X = ...' and 'L = [_A,_B,_C|...]' hold (issue
% #1088). A variable in a binding denotes a variable in the answer,
% and so does one inside an error term, which ball_matches/3 pairs
% one-to-one with a variable of the ball.

attempt_match(M, Q, VNs, N, solution(Items)) :- !,
	witness(Q, VNs, W),
	copy_term(Q-W, Q1-W1),
	'$mark_start'(Mark),
	call_nth(M:Q1, N),
	( memberchk(maybe, Items) -> some_attributed(Mark) ; \+ some_attributed(Mark) ),
	% sto says the bindings are cyclic, and its absence that they are not (issue #1154)
	( memberchk(sto, Items) -> \+ acyclic_term(W1) ; acyclic_term(W1) ),
	copy_term(qd(Q,W,VNs,Items), qd(Q2,W2,VNs2,Items2)),
	link_names(VNs2),
	bound_in_query(Items2, Q2),
	apply_equations(Items2),
	ball_matches([], W2, W1).
attempt_match(M, Q, _, N, _) :-
	call_nth(M:Q, N).

% maybe (issue #1128) asserts that the query left some variable
% attributed - a constraint (dif, clpz, freeze, any attribute module)
% still pending, not resolved into an ordinary binding. It names no
% particular variable or module, only that one exists, so it is
% checked against everything the query attributed rather than just the
% witness: 'freeze(_, false)' pends on a variable local to freeze/2,
% one the query never names and the witness never sees, yet it is
% exactly a 'maybe' - the query is not really answered by plain 'true'
% while that goal is still waiting to run. '$mark_start'/1 and
% '$list_attributed'/2 are the same pair call_residue_vars/2 uses to
% find attributed variables regardless of where a goal put them.
%
% An answer describes an answer *completely* (issue #1067), so the
% absence of 'maybe' is itself an assertion: nothing is left pending.
% 'freeze(_, false)' is therefore described by 'maybe' and by nothing
% else - a bare 'true' does not also hold of it, any more than a wrong
% binding would.

some_attributed(Mark) :-
	'$list_attributed'(Mark, Vs),
	Vs \== [].

% An answer substitution binds variables of the query, so a description
% binding anything else does not describe an answer of it (issue #1077):
% '?- true.' is answered by 'true', never by 'X = 1'. Such a binding was
% simply dropped by witness/3 below, which reports only the query's own
% variables, and so could never make a quad fail. Names are linked
% first, so a description variable sharing a name with one of the query
% is one of the query's.

bound_in_query(Items, Q) :-
	term_variables(Q, QVs),
	bound_vars(Items, Vs),
	vars_among(Vs, QVs).

vars_among([], _).
vars_among([V|T], QVs) :-
	var_member(V, QVs),
	vars_among(T, QVs).

% The named variables of the query: exactly the bindings a toplevel
% would report. Anonymous variables are not recorded in VarNames.

witness(Q, VNs, W) :-
	term_variables(Q, QVs),
	query_vars(VNs, QVs, W).

query_vars([], _, []).
query_vars([_=V|T], QVs, W) :-
	(	var_member(V, QVs)
	->	W = [V|W0]
	;	W = W0
	),
	query_vars(T, QVs, W0).

var_member(V, [X|Xs]) :-
	( V == X -> true ; var_member(V, Xs) ).

% Only an equation binding a variable contributes to the witness;
% 'true' and the annotations that survive this far contribute nothing.
% A non-variable left side is not applied at all: unifying '1 = X'
% would bind X and make the malformed description appear to hold.

apply_equations([]).
apply_equations([Item|T]) :-
	(	nonvar(Item),
		Item = (V = Val),
		var(V)
	->	V = Val
	;	nonvar(Item),
		Item = (V ~~ Spec),
		var(V)
	->	V = '$approx'(Spec)
	;	true
	),
	apply_equations(T).

match_outcome(_, _, solution(_), matched).
match_outcome(_, _, none, none).
match_outcome(_, _, loops, loops).
match_outcome(Q, VNs, ball(B), ball(B0)) :-
	\+ \+ (
		link_names(VNs),
		term_variables(Q, QVs),
		ball_matches(QVs, B, B0)
	).

% The described ball must be a variant of the one actually thrown,
% except that ... stands for an unspecified subterm. A variable in a
% description denotes an actual variable, here as anywhere else, so
%
%     error(instantiation_error, _)
%
% requires the implementation-defined second argument to be unbound.
% To leave it unspecified write error(instantiation_error, ...), or the
% concise shorthand instantiation_error (#1068).
%
% Being a variant means the correspondence between the variables of the
% description and those of the ball is one-to-one, so a variable
% recurring in the description describes one recurring in the ball
% (issue #1080): 'error(X,X)' does not describe error(_,_), and
% 'f(X,Y)' does not describe f(A,A).
%
% A description variable shared with the query is that variable of the
% query, not a fresh one, so it describes only itself. throw/1 copies
% the ball, so a variable of the query never occurs in it and
%
%     ?- throw(f(X)).
%        throw(f(X)).
%
% fails, whereas 'throw(f(_))' holds -- as at the toplevel, which
% reports the copy, not X.

% A cyclic description is matched coinductively, taking a pair of subterms met again on the way down as matching (issue #1154).

ball_matches(QVs, P, A) :-
	( acyclic_term(P) -> S = acyclic ; S = [] ),
	ball_match(QVs, P, A, S, [], _).

ball_match(_, P, _, _, B, B) :- P == ..., !.
ball_match(QVs, P, A, _, B0, B) :-
	var(P), !,
	(	var_member(P, QVs)
	->	P == A,
		B = B0
	;	var(A),
		pair_vars(P, A, B0, B)
	).
% What 'V ~~ Spec' left in place of a binding: the answer has to have a
% float there, within the interval Spec is written to (issue #1145).
ball_match(_, '$approx'(Spec), A, _, B, B) :- !, approx_match(Spec, A).
ball_match(_, P, A, _, B, B) :- \+ compound(P), !, P == A.
% Walk with functor/arg rather than (=..)/2: univ on a list whose
% elements share variables can fail to decompose reliably here, which
% made complete answer descriptions such as 'X = f(Y,Y), Z = Y' fail
% to match after switching solutions to ball_matches/3 (#1088).
ball_match(QVs, P, A, S0, B0, B) :-
	compound(A),
	functor(P, F, N),
	functor(A, F, N),
	(	seen_pair(S0, P, A)
	->	B = B0
	;	see_pair(S0, P, A, S),
		ball_args(QVs, 1, N, P, A, S, B0, B)
	).

ball_args(_, I, N, _, _, _, B, B) :- I > N, !.
ball_args(QVs, I, N, P, A, S, B0, B) :-
	arg(I, P, Pi),
	arg(I, A, Ai),
	ball_match(QVs, Pi, Ai, S, B0, B1),
	I1 is I + 1,
	ball_args(QVs, I1, N, P, A, S, B1, B).

seen_pair([P0-A0|T], P, A) :-
	( P0 == P, A0 == A -> true ; seen_pair(T, P, A) ).

see_pair(acyclic, _, _, acyclic) :- !.
see_pair(S, P, A, [P-A|S]).

% A description variable always corresponds to the same ball variable,
% and no two of them to the same one.

pair_vars(P, A, B0, B) :-
	(	pair_left(B0, P, A0)
	->	A0 == A,
		B = B0
	;	\+ pair_right(B0, A),
		B = [P-A|B0]
	).

pair_left([P0-A0|T], P, A) :-
	( P0 == P -> A = A0 ; pair_left(T, P, A) ).

pair_right([_-A0|T], A) :-
	( A0 == A -> true ; pair_right(T, A) ).

% Errors may be written in full or in the customary shorthand

expected_ball(error(E, Impl), error(E, Impl)).
expected_ball(throw(B), B).
expected_ball(instantiation_error, error(instantiation_error, ...)).
expected_ball(type_error(T, C), error(type_error(T, C), ...)).
expected_ball(domain_error(D, C), error(domain_error(D, C), ...)).
expected_ball(existence_error(T, C), error(existence_error(T, C), ...)).
expected_ball(permission_error(O, T, C), error(permission_error(O, T, C), ...)).
expected_ball(evaluation_error(E), error(evaluation_error(E), ...)).
expected_ball(system_error, error(system_error, ...)).
expected_ball(syntax_error(E), error(syntax_error(E), ...)).
expected_ball(representation_error(R), error(representation_error(R), ...)).
expected_ball(resource_error(R), error(resource_error(R), ...)).
expected_ball(uninstantiation_error(C), error(uninstantiation_error(C), ...)).

timeout_ball(B) :-
	nonvar(B),
	B = error(E, _),
	nonvar(E),
	functor(E, time_limit_exceeded, _).

% Approximate float answers (issue #1145). 'V ~~ Spec' describes a
% float to the precision Spec is written to: Spec names the interval
% within half of its last decimal place, inclusive at both ends, so
% '14.2000' is [14.19995, 14.20005] and '14.2' is [14.15, 14.25].
% Spec is an atom because reading it as a float would lose exactly the
% thing it is there to say.
%
% The bounds are ratios of unbounded integers and the float is compared
% against them exactly. Rounding a bound to a float first would not do:
% the nearest float to a bound may sit on the wrong side of it, and
% then a float inside the interval reads as outside, or the reverse.

approx_match(Spec, A) :-
	float(A),
	approx_spec(Spec, Lo, Hi, Den),
	float_in(A, Lo, Hi, Den).

% Lo/Den and Hi/Den are the exact bounds. A spec finer than the floats
% it describes states more than any of them can carry, so it is
% malformed rather than merely unsatisfiable: the interval has to hold
% three floats - one below the value it names, that value, and one
% above - and all three have to exist, which they do not at the top of
% the float range.

approx_spec(Spec, Lo, Hi, Den) :-
	approx_bounds(Spec, Lo, Hi, Den, V),
	float_in(V, Lo, Hi, Den),
	catch(( prev_float(V, P), next_float(V, N) ), _, fail),
	float_in(P, Lo, Hi, Den),
	float_in(N, Lo, Hi, Den).

approx_bounds(Spec, Lo, Hi, Den, V) :-
	atom(Spec),
	atom_chars(Spec, Cs),
	phrase(float_spec(Sign, Ds, Places, Exp), Cs),
	digits_value(Ds, 0, M0),
	M is Sign * M0,
	K is Places - Exp,
	(	K >= 0
	->	Den is 2 * 10^K, Scale = 1
	;	Den = 2, Scale is 10^(-K)
	),
	Lo is (2*M - 1) * Scale,
	Hi is (2*M + 1) * Scale,
	catch(atom_number(Spec, V), _, fail),
	float(V).

% ISO float syntax, less the '+' a number token may not carry anyway:
% digits, a fraction, and an optional exponent. Places is how many
% decimal places the mantissa is written to, which the exponent shifts.

float_spec(S, Ds, Places, Exp) -->
	spec_sign(S), digits(I), ['.'], digits(F), spec_exponent(Exp),
	{ append(I, F, Ds), length(F, Places) }.

spec_sign(-1) --> ['-'], !.
spec_sign(1) --> [].

exp_sign(-1) --> ['-'], !.
exp_sign(1) --> ['+'], !.
exp_sign(1) --> [].

spec_exponent(Exp) -->
	exp_char, exp_sign(S), digits(Ds), !,
	{ digits_value(Ds, 0, V), Exp is S * V }.
spec_exponent(0) --> [].

exp_char --> [e].
exp_char --> ['E'].

digits([D|Ds]) --> digit(D), more_digits(Ds).

more_digits([D|Ds]) --> digit(D), !, more_digits(Ds).
more_digits([]) --> [].

digit(D) --> [C],
	{ char_code(C, X), X >= 0'0, X =< 0'9, D is X - 0'0 }.

digits_value([], V, V).
digits_value([D|Ds], V0, V) :-
	V1 is V0 * 10 + D,
	digits_value(Ds, V1, V).

% Lo/Den =< F =< Hi/Den, exactly. A float is N/2^K for integers N and
% K, so multiplying out compares unbounded integers and nothing rounds.

float_in(F, Lo, Hi, Den) :-
	float_ratio(F, N, K),
	D is 1 << K,
	N * Den >= Lo * D,
	N * Den =< Hi * D.

% F = N / 2^K. Doubling a float is exact and stops at an integer after
% at most as many steps as the format has fraction bits; the fuel bounds
% it for anything that is not a finite float after all.

float_ratio(F, N, K) :-
	float_ratio_(F, 0, 1100, K, M),
	N is truncate(M).

float_ratio_(F, K0, Fuel, K, M) :-
	Fuel > 0,
	(	whole_float(F)
	->	K = K0, M = F
	;	F1 is F * 2,
		K1 is K0 + 1,
		Fuel1 is Fuel - 1,
		float_ratio_(F1, K1, Fuel1, K, M)
	).

% Nothing at or above 2^53 has a fraction left to shift out. Testing
% that first also keeps the doubling away from float_integer_part/1,
% which here truncates towards an int64 and so reports 1.0e30 as very
% far from whole.

whole_float(F) :- abs(F) >= 9007199254740992.0, !.
whole_float(F) :- float_fractional_part(F) =:= 0.0.

% The neighbouring floats, without needing to know the format: halve a
% step while it still moves X, and the last one that did lands on the
% neighbour. Standard Prolog throughout, so a quad suite stays runnable
% on the systems it reports on. At the ends of the range the step
% overflows instead, and approx_spec/4 reads that as malformed.

next_float(X, Y) :- up_step(X, 1.0, D), Y is X + D.
prev_float(X, Y) :- down_step(X, 1.0, D), Y is X - D.

up_step(X, D0, D) :-
	(	X + D0 > X
	->	up_shrink(X, D0, D)
	;	D1 is D0 * 2,
		up_step(X, D1, D)
	).

up_shrink(X, D0, D) :-
	D1 is D0 / 2,
	(	D1 > 0.0,
		X + D1 > X
	->	up_shrink(X, D1, D)
	;	D = D0
	).

down_step(X, D0, D) :-
	(	X - D0 < X
	->	down_shrink(X, D0, D)
	;	D1 is D0 * 2,
		down_step(X, D1, D)
	).

down_shrink(X, D0, D) :-
	D1 is D0 / 2,
	(	D1 > 0.0,
		X - D1 < X
	->	down_shrink(X, D1, D)
	;	D = D0
	).
