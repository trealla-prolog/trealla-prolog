:- help(subsumes_term(+term,+term), [iso(true)]).

subsumes_term(General, Specific) :-
	\+ \+ (
		term_variables(Specific, SVs1),
		unify_with_occurs_check(General, Specific),
		term_variables(SVs1, SVs2),
		SVs1 == SVs2
	).

:- meta_predicate(countall(0,?)).
:- help(countall(:callable,?integer), [iso(true)]).

countall(_, N) :-
	can_be(N, integer, countall/2, _),
	integer(N),
	(N >= 0 -> true; throw(error(domain_error(not_less_than_zero, N), countall/2))),
	fail.
countall(G, N) :-
	'$countall'(call(G), N0),
	N = N0.

:- meta_predicate(call_cleanup(0,0)).
:- help(call_cleanup(:callable,:callable), [iso(false)]).

call_cleanup(G, C) :-
	(var(C) -> throw(error(instantiation_error, call_cleanup/3)); true),
	'$register_cleanup'(ignore(C)),
	'$call_cleanup'(
		call(G),
		Err,
		(catch(ignore(C), _, true), throw(Err))
	).

:- meta_predicate(setup_call_cleanup(0,0,0)).
:- help(setup_call_cleanup(:callable,:callable,:callable), [iso(false)]).

setup_call_cleanup(S, G, C) :-
	once(S),
	(var(C) -> throw(error(instantiation_error, setup_call_cleanup/3)); true),
	'$register_cleanup'(ignore(C)),
	'$call_cleanup'(
		call(G),
		Err,
		(catch(ignore(C), _, true), throw(Err))
	).

:- help(succ(?integer,+integer), [iso(false)]).
:- help(succ(+integer,-integer), [iso(false)]).

succ(I, S) :-
    can_be(not_less_than_zero, I),
    can_be(not_less_than_zero, S),
    (   integer(S) ->
        S > 0,
        I is S-1
    ;   integer(I) ->
        S is I+1
    ;   instantiation_error(succ/2)
    ).

:- help(cfor(+evaluable,+evaluable,-var), [iso(false),desc('C-style for loop')]).

cfor(I0, J0, K) :-
	I is I0,
	J is J0,
	between(I, J, K).

:- meta_predicate(call_det(0,?)).
:- help(call_det(:callable,?boolean), [iso(false)]).

call_det(G, Det) :-
	'$get_level'(L1),
	call(G),
	'$get_level'(L2),
	(L1 = L2 -> Det = true; Det = false).

goal_expansion(call_det(G, Det), Goal) :-
	nonvar(G),
	!,
	Goal = ('$get_level'(L1), call(G), '$get_level'(L2), (L1 = L2 -> Det = true; Det = false)),
	true.
goal_expansion(call_det(G, V), call_det(G, V)).

:- meta_predicate(findall(?,0,-,?)).
:- help(findall(+term,:callable,-list,+list), [iso(false)]).

findall(T, G, B, Tail) :-
	can_be(B, list, findall/4, _),
	can_be(Tail, list, findall/4, _),
	findall(T, G, B0),
	append(B0, Tail, B), !.

:- meta_predicate(call_with_time_limit(+,0)).
:- help(call_with_time_limit(+number,:callable), [iso(false)]).

call_with_time_limit(Time, Goal) :-
	% 1000.0 not 1000: truncate/1 wants a float, so an INTEGER number of
	% seconds used to raise type_error(float, _) instead of applying a
	% limit. A sub-millisecond limit rounds down to 0, which is '$alarm's
	% cancel opcode rather than a duration, so floor it at 1ms. Negative
	% stays negative and '$alarm'/2 still raises domain_error.
	TimeMs0 is truncate(Time * 1000.0),
	(	TimeMs0 =:= 0 ->
		TimeMs = 1
	;	TimeMs = TimeMs0
	),
	'$alarm'(TimeMs, Timer),
	(	catch(once(Goal), E, ('$alarm'(0, Timer), throw(E))) ->
		'$alarm'(0, Timer)
	;	('$alarm'(0, Timer), fail)
	).

:- meta_predicate(time_out(0,+,-)).
:- help(time_out(:callable,+integer,?atom), [iso(false)]).

time_out(Goal, TimeMs0, Result) :-
	% 0ms is '$alarm's cancel opcode, not a duration - floor it so an
	% explicit zero times out at once instead of erroring, matching
	% call_with_time_limit/2.
	(	TimeMs0 =:= 0 ->
		TimeMs = 1
	;	TimeMs = TimeMs0
	),
	'$alarm'(TimeMs, Timer),
	(	catch(once(Goal), E, ('$alarm'(0, Timer), throw(E))) ->
		('$alarm'(0, Timer), Result = success)
	;	('$alarm'(0, Timer), fail)
	).

:- help(variant(+term,+term), [iso(false)]).

variant(X, Y) :-
	\+ \+ ( copy_term(X,XC),
		subsumes_term(XC,Y),
		subsumes_term(Y,XC)
	).

:- op(1105, xfy, '|').

:- meta_predicate(phrase(2, ?)).
:- meta_predicate(phrase(2, ?, ?)).
:- meta_predicate(phrase(3, ?, ?, ?)).
:- meta_predicate(phrase(4, ?, ?, ?, ?)).

%% phrase(+Body, ?Ls).

phrase(GRBody, S0) :-
    phrase(GRBody, S0, []).

%% phrase(+Body, ?Ls, ?Ls0).
%
% True iff Body describes part of the list Ls and the rest of Ls is Ls0.
%
% Example:
%
% ```
% ?- phrase(seq(X), "aaa", Y).
%    X = [], Y = "aaa"
% ;  X = "a", Y = "aa"
% ;  X = "aa", Y = "a"
% ;  X = "aaa", Y = [].
% ```

% '$dcg_body'/4 FAILS for anything that is not a 7.14 construct, which is
% what lets the last branch handle an ordinary non-terminal by appending
% the two arguments - so phrase(1, L) still reaches call/3 and reports
% type_error(callable, 1) from there. It throws only where ISO requires,
% including type_error(callable, T) for a non-callable in non-terminal
% position, which is the #1102 fix.

phrase(GRBody, S0, S) :-
    strip_module(GRBody, M, B),
    (   var(GRBody) ->
        instantiation_error(phrase/3)
    ;   '$dcg_body'(B, S0, S, Goal) ->
        call(M:Goal)
    ;   extend(B, [S0,S], Goal) ->
        call(M:Goal)
    ;   call(M:B, S0, S)
    ).

phrase(GRBody, Arg, S0, S) :-
    strip_module(GRBody, M, B),
    (   var(GRBody) ->
        instantiation_error(phrase/4)
    ;   extend(B, [Arg], B2),
        '$dcg_body'(B2, S0, S, Goal) ->
        call(M:Goal)
    ;   extend(B, [Arg,S0,S], Goal) ->
        call(M:Goal)
    ;   call(M:B, Arg, S0, S)
    ).

phrase(GRBody, Arg1, Arg2, S0, S) :-
    strip_module(GRBody, M, B),
    (   var(GRBody) ->
        instantiation_error(phrase/5)
    ;   extend(B, [Arg1,Arg2], B2),
        '$dcg_body'(B2, S0, S, Goal) ->
        call(M:Goal)
    ;   extend(B, [Arg1,Arg2,S0,S], Goal) ->
        call(M:Goal)
    ;   call(M:B, Arg1, Arg2, S0, S)
    ).

% As the reference does it: append the extra arguments to the body term
% first, then translate the result.

extend(B, Extra, B2) :-
    callable(B),
    B =.. L0,
    append(L0, Extra, L),
    B2 =.. L.

%% seq(Seq)//
%
% Describes a sequence.
%
% The walk is seq_//1, whose first clause is deliberately NOT a DCG rule:
% it is a hand-written seq_/3 guarding var(Xs), Cs0 == [], which is what
% terminates generation. Carried over from the reference verbatim.

% A whole compact string is its own answer; walking to it costs a frame per
% character. Copied, because close/1 unmaps the mapping.

seq(Xs, Cs0,Cs) :-
	(	string(Cs0), var(Xs), Cs == []
	->	'$own_string'(Cs0, Xs)
	;	seq_(Xs, Cs0, Cs)
	).

seq_(Xs, Cs0,Cs) :-
   var(Xs),
   Cs0 == [],
   !,
   Xs = [],
   Cs0 = Cs.
seq_([]) --> [].
seq_([E|Es]) --> [E], seq_(Es).

%% seqq(SeqOfSeqs)//
%
% Describes a sequence of sequences.

seqq([]) --> [].
seqq([Es|Ess]) --> seq(Es), seqq(Ess).

%% ...//
%
% Describes an arbitrary number of elements. The hand-written .../2
% clause below terminates generation, as with seq//1.
%
% Written as TWO rules rather than the reference's `[] | [_], ...`.
% Same solutions in the same order, but an in-body disjunction under deep
% recursion is quadratic in this engine, where two clauses are linear:
% skipping to a marker in an 80k-character string took 19s as a
% disjunction and is linear without it. Measured with the disjunction
% reproduced in plain Prolog, so this is not a DCG effect - see the note
% in docs/native-dcg-design.md §11.

...(Cs0,Cs) :-
   Cs0 == [],
   !,
   Cs0 = Cs.
... --> [].
... --> [_], ... .

% Inline phrase/3 at consult time, as the reference did. This MUST NOT
% throw: a compile-time expansion may not raise an error at a different
% moment than the runtime would, so a body whose translation would throw
% is declined here and left to fail at runtime instead, where it belongs
% (see section 5.3 of docs/native-dcg-design.md). Declining is just
% failing the hook, which leaves the ordinary phrase/3 call in place.

goal_expansion(phrase(GRBody, S0, S), Goal) :-
    nonvar(GRBody),
    strip_module(GRBody, M, B),
    nonvar(B),
    catch(dcg_inline(B, S0, S, G), _, fail),
    (   GRBody = (_:_) ->
        Goal = M:G
    ;   Goal = G
    ).

% Translate a construct, or append the two arguments to an ordinary
% non-terminal. Deliberately NOT wrapped in its own catch: a body whose
% translation throws must propagate out to the catch above, which
% declines the expansion and leaves the runtime phrase/3 call in place.
%
% Falling back to extend/3 on a throw would be silently wrong - the body
% is a construct, so appending arguments to it builds nonsense like
% ','(A,B,S0,S). That is what issue #832's own test caught.

dcg_inline(B, S0, S, G) :-
    (   '$dcg_body'(B, S0, S, G0) ->
        G = G0
    ;   extend(B, [S0,S], G)
    ).

goal_expansion(phrase(GRBody, S0), phrase(GRBody, S0, [])).

% (-->)/2 behaves as if it didn't exist. We export (and define) it
% only so that clauses for (-->)/2 cannot be asserted when
% library(dcgs) is loaded.

(_-->_) :- throw(error(existence_error(procedure,(-->)/2),(-->)/2)).

% expand_term/2 is the expansion driver, not just a grammar-rule
% translator: the term_expansion/2 hook first, then translation of
% whatever came back, then identity. Only the middle step used to be
% here, so it failed on every term that was not a grammar rule and
% never consulted the hook at all.

expand_term(T, X) :-
	user:term_expansion(T, T0),
	!,
	expand_term_dcg(T0, X).
expand_term(T, X) :-
	expand_term_dcg(T, X).

% A list result has each element translated, as when consulting.

expand_term_dcg(T, X) :- var(T), !, X = T.
expand_term_dcg((H --> B), X) :- !, '$dcg_rule'((H --> B), X).
expand_term_dcg([T|Ts], [X|Xs]) :- !, expand_term_dcg(T, X), expand_term_dcg(Ts, Xs).
expand_term_dcg(T, T).

