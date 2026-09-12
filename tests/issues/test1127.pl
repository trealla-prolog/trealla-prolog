:- initialization(main).

% Issue #1127: dif/2 silently posted no constraint when the variable it
% had to delay on already carried an attribute.
%
% '$undo_trail'/2 marks a cyclically-bound variable's occurrences inside
% its own value (FLAG_VAR_CYCLIC) so that term_variables/2 called from
% verify_attributes/3 skips the variable being bound - see tests/tests/
% test0098.pl, which is what the mark was added for. The mark was never
% cleared, so it stayed on the heap cells afterwards and hid that
% variable from every later term_variables/2.
%
% dif(A,C) tests A \= C first; with B attributed that unification runs
% the post-unify hook, which left the mark on the B inside A. dif/2's
% own term_variables(dif(A,C),Vars) then found [] instead of [B] and
% dif_set_variables/3 stored nothing, so dif(A,C) succeeded
% unconstrained and B=[[]|B] had nothing left to check.

:- use_module(library(dif)).
:- use_module(library(freeze)).

% B=[[]|B] makes A and C both the infinite list of [], so A == C and
% dif(A,C) must fail - with or without a prior attribute on B. Closing
% that cycle makes every query here subject to occurs check (issue #1154).
1 ?- call((A=[[]|B],C=[[]|A],dif(A,C),B=[[]|B])).
   sto, false.

2 ?- freeze(B,true), call((A=[[]|B],C=[[]|A],dif(A,C),B=[[]|B])).
   sto, false.

3 ?- dif(B,999), A=[[]|B], C=[[]|A], dif(A,C), B=[[]|B].
   sto, false.

% UWN's original: the two branches must agree, so this must not succeed.
inconsistent :-
	freeze(B,true),
	\+ (B=[[]|B],A=[[]|B],C=[[]|A],dif(A,C)),
	\+ \+ call((A=[[]|B],C=[[]|A],dif(A,C),B=[[]|B])).

4 ?- inconsistent.
   sto, false.

% The mark must not outlive the hook: B is still unbound afterwards, so
% term_variables/2 has to keep reporting it inside A.
mark_cleared :-
	dif(B,999), A=[[]|B], C=[[]|A],
	\+ \+ A = C,				% attempted, then undone
	term_variables(A, Vs),
	Vs == [B].

5 ?- mark_cleared.
   sto, maybe.

main :-
	use_module(library(quads)),
	run_quads.
