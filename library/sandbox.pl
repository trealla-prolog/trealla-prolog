/*  Ported from SWI-Prolog's library(sandbox) to Trealla.

    Author:        Jan Wielemaker
    E-mail:        J.Wielemaker@vu.nl
    WWW:           http://www.swi-prolog.org
    Copyright (c)  2013-2024, VU University Amsterdam
                              CWI, Amsterdam
                              SWI-Prolog Solutions b.v
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in
       the documentation and/or other materials provided with the
       distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

:- module(sandbox, [
	safe_goal/1, safe_call/1,
	safe_call/2, safe_call/3, safe_call/4, safe_call/5, safe_call/6, safe_call/7, safe_call/8,
	safe_guard/2,
	safe_load/2, safe_load/3
	]).

:- use_module(library(lists)).
:- use_module(library(assoc)).
:- use_module(library(error)).
:- use_module(library(prolog_format)).

% The hooks are SWI's: an application adds clauses for them, qualified as
% sandbox:safe_primitive(...) and so on, after declaring them multifile.
%
% Where SWI tells a system predicate by its module, here it is one that
% predicate_property/2 reports built_in, which user code can't redefine.
% Such a predicate's home module is 'system', so a declaration may name
% it either bare or as system:Goal. A library predicate's is the module it
% is imported from, eg. lists:member(_,_).

:- multifile
	safe_primitive/1,				% Goal
	safe_meta_predicate/1,			% Module:Name/Arity
	safe_meta/2,					% Goal, Calls
	safe_meta/3,					% Goal, Context, Calls
	safe_global_variable/1,			% Name
	safe_directive/1,				% Module:Goal
	safe_prolog_flag/2.				% Name, Value

% Not dynamic, as in SWI: an asserted clause could whitelist anything.

% After the declarations, as library(yall) adds to safe_meta_predicate/1.

:- use_module(library(yall), [lambda_calls/3]).

:- meta_predicate
	safe_goal(:),
	safe_call(:),
	safe_call(:,?),
	safe_call(:,?,?),
	safe_call(:,?,?,?),
	safe_call(:,?,?,?,?),
	safe_call(:,?,?,?,?,?),
	safe_call(:,?,?,?,?,?,?),
	safe_call(:,?,?,?,?,?,?,?),
	safe_guard(:,-).

:- help(safe_goal(:callable), [iso(false), desc('Throw unless every predicate the goal can reach is whitelisted.')]).
:- help(safe_call(:callable), [iso(false), desc('Expand, guard, check with safe_goal/1, then call the goal.')]).
:- help(safe_call(:callable,?term), [iso(false), desc('safe_call/1 of the closure with the arguments added.')]).
:- help(safe_load(+atom,+term), [iso(false), desc('Load untrusted source into a module, refusing what it may not do.')]).
:- help(safe_load(+atom,+term,+list), [iso(false), desc('safe_load/2 with options: check(false) skips checking each clause body.')]).
:- help(safe_guard(:callable,-callable), [iso(false), desc('Put each goal position still a variable through safe_call/N.')]).

% safe_call(:Goal): expands then checks Goal, and calls it only if safe.

safe_call(Goal0) :-
	qualified(Goal0, M:Goal1),
	(	var(Goal1) -> instantiation_error(Goal1)
	;	true
	),
	expand_goal(Goal1, Goal2),
	guard(Goal2, M, Goal),
	safe_goal(M:Goal),
	call(M:Goal).

% safe_call(:Closure, ?A1, ...): safe_call/1 of Closure with the arguments
% added, so a closure can be checked at the time it's called, eg. by
% maplist/2. Not in SWI.

safe_call(G, A1) :- safe_closure(G, [A1]).
safe_call(G, A1, A2) :- safe_closure(G, [A1,A2]).
safe_call(G, A1, A2, A3) :- safe_closure(G, [A1,A2,A3]).
safe_call(G, A1, A2, A3, A4) :- safe_closure(G, [A1,A2,A3,A4]).
safe_call(G, A1, A2, A3, A4, A5) :- safe_closure(G, [A1,A2,A3,A4,A5]).
safe_call(G, A1, A2, A3, A4, A5, A6) :- safe_closure(G, [A1,A2,A3,A4,A5,A6]).
safe_call(G, A1, A2, A3, A4, A5, A6, A7) :- safe_closure(G, [A1,A2,A3,A4,A5,A6,A7]).

safe_closure(G0, Extra) :-
	qualified(G0, M:G1),
	closure_goal(G1, Extra, G),
	safe_call(M:G).

% The goal a closure makes with Extra added, inside any qualification.

closure_goal(G, _, _) :-
	var(G),
	!,
	instantiation_error(G).
closure_goal(M:G0, Extra, M:G) :-
	!,
	closure_goal(G0, Extra, G).
closure_goal(G0, Extra, G) :-
	lambda_goal(G0, Extra, G),
	!.
closure_goal(G0, Extra, G) :-
	G0 =.. L0,
	append(L0, Extra, L),
	G =.. L.

% safe_guard(:Goal, -Guarded): Goal with each goal position that is still
% a variable, which safe_goal/1 can't check, put through safe_call/N to
% be checked when it is called. It goes through control constructs and the
% goal arguments of meta-predicates, not into the predicates called. Guarded
% is qualified with Goal's module. Not in SWI.

safe_guard(Goal0, M:Goal) :-
	qualified(Goal0, M:Goal1),
	guard(Goal1, M, Goal).

guard(V, M, sandbox:safe_call(M:V)) :-
	var(V),
	!.
guard(M2:G, M, Guarded) :-
	!,
	(	atom(M2) -> Guarded = M2:G1, guard(G, M2, G1)
	;	Guarded = sandbox:safe_call(M:(M2:G))
	).
guard(G, M, Guarded) :-
	callable(G),
	guard_spec(G, M, Spec),
	!,
	G =.. [Name|Args],
	Spec =.. [_|Specs],
	guard_args(Specs, Args, M, Args1),
	Guarded =.. [Name|Args1].
guard(G, _, G).

guard_spec(G, M, Spec) :-
	system_predicate(M, G),
	generic_goal(G, Spec),
	safe_meta(Spec),
	!.
guard_spec(G, M, Spec) :-
	\+ system_predicate(M, G),
	predicate_property(M:G, meta_predicate(Spec)),
	!.

guard_args([], [], _, []).
guard_args([S|Ss], [A|As], M, [A1|As1]) :-
	guard_arg(S, A, M, A1),
	guard_args(Ss, As, M, As1).

guard_arg(S, A, M, sandbox:safe_call(M:A)) :-
	var(A),
	( integer(S) ; S == (^) ),
	!.
guard_arg(0, A, M, A1) :-
	!,
	guard(A, M, A1).
guard_arg(^, A, M, A1) :-
	!,
	guard_existential(A, M, A1).
guard_arg(_, A, _, A).

guard_existential(V, M, sandbox:safe_call(M:V)) :-
	var(V),
	!.
guard_existential(V^G, M, V^G1) :-
	!,
	guard_existential(G, M, G1).
guard_existential(G, M, G1) :-
	guard(G, M, G1).

% safe_goal(:Goal) is det: true if every predicate Goal can reach is safe,
% that is whitelisted by safe_primitive/1 or a safe meta-predicate whose
% goal arguments are themselves safe. It throws instantiation_error if it
% can't tell which predicate a goal position calls, and
% permission_error(call, sandboxed, G) for one that is not safe. It is
% also not allowed to call a module's predicate that it neither exports
% nor declares public.

safe_goal(Goal0) :-
	qualified(Goal0, M:Goal),
	nb_setval(sandbox_root, M),
	empty_assoc(Safe0),
	catch(safe(Goal, M, [], Safe0, _), E, true),
	!,
	nb_delete(sandbox_last_error),
	(	var(E) -> true
	;	throw(E)
	).
safe_goal(_) :-
	nb_current(sandbox_last_error, E),
	!,
	nb_delete(sandbox_last_error),
	throw(E).
safe_goal(G) :-
	throw(error(instantiation_error, sandbox(G, []))).

% The outermost qualification only: one inside, as in user:(priv:G), is a
% call into that module, which the check must see as one. An unqualified
% goal, as when called with no meta-predicate qualification, is in user.

qualified(M:G, M:G) :-
	atom(M),
	!.
qualified(G, user:G).

% safe(+Goal, +Module, +Parents, +Safe0, -Safe) is semidet.

safe(V, _, Parents, _, _) :-
	var(V),
	!,
	Error = error(instantiation_error, sandbox(V, Parents)),
	nb_setval(sandbox_last_error, Error),
	throw(Error).
safe(M:G, Ctx, Parents, Safe0, Safe) :-
	!,
	must_be(atom, M),
	must_be(callable, G),
	safe_qualified(M, G, Ctx, Parents, Safe0, Safe).

% A qualification inside another is the one that counts, and a control
% construct passes its qualification to the goals in it.

safe_qualified(_, M2:G, Ctx, Parents, Safe0, Safe) :-
	!,
	safe(M2:G, Ctx, Parents, Safe0, Safe).
safe_qualified(M, G, Ctx, Parents, Safe0, Safe) :-
	qualified_control(G, Gs),
	!,
	safe_qualified_list(Gs, M, Ctx, Parents, Safe0, Safe).
safe_qualified(M, G, Ctx, Parents, Safe0, Safe) :-
	safe_call_goal(G),
	home_module(M, G, sandbox),
	!,
	safe_call_called(G, Ctx, Parents, Called),
	safe_list(Called, Ctx, Parents, Safe0, Safe).
safe_qualified(M, G, Ctx, Parents, _, _) :-
	M \== Ctx,
	system_predicate(M, G),
	module_sensitive(M, G),
	!,
	throw(error(permission_error(call, sandboxed, M:G), sandbox(M:G, Parents))).
safe_qualified(M, G, _, Parents, Safe0, Safe) :-
	known_module(M:G, Parents),
	home_module(M, G, M2),
	(	(	safe_primitive(M2:G)
		;	safe_primitive(G),
			system_predicate(M, G)
		)
	->	Safe = Safe0
	;	(	predicate_property(M:G, exported)
		;	predicate_property(M:G, public)
		;	predicate_property(M:G, multifile)
		;	system_predicate(M, G)
		;	memberchk(M:_, Parents)
		)
	->	safe(G, M, Parents, Safe0, Safe)
	;	throw(error(permission_error(call, sandboxed, M:G), sandbox(M:G, Parents)))
	).
safe(G, M, Parents, Safe, Safe) :-
	catch(safe_primitive(G),
		error(instantiation_error, _),
		rethrow_instantiation_error([G|Parents])),
	system_predicate(M, G),
	!.
safe(G, M, Parents, Safe, Safe) :-
	known_module(M:G, Parents),
	home_module(M, G, M2),
	(	catch(safe_primitive(M2:G),
			error(instantiation_error, _),
			rethrow_instantiation_error([M2:G|Parents]))
	;	M2 \== system,
		predicate_property(M2:G, number_of_rules(0))
	),
	!.
safe(G, M, Parents, Safe0, Safe) :-
	system_predicate(M, G),
	safe_meta_call(G, M, Called),
	!,
	add_iso_parent(G, Parents, Parents1),
	safe_list(Called, M, Parents1, Safe0, Safe).
safe(G, M, Parents, Safe0, Safe) :-
	home_module(M, G, M2),
	safe_meta_call(M2:G, M, Called),
	!,
	safe_list(Called, M, Parents, Safe0, Safe).
safe(G, M, Parents, Safe0, Safe) :-
	goal_id(M:G, Id, Gen),
	(	get_assoc(Id, Safe0, _)
	->	Safe = Safe0
	;	put_assoc(Id, Safe0, true, Safe1),
		(	Gen == M:G
		->	safe_clauses(Gen, M, [Id|Parents], Safe1, Safe)
		;	catch(safe_clauses(Gen, M, [Id|Parents], Safe1, Safe),
				error(instantiation_error, Ctx),
				unsafe(Parents, Ctx))
		)
	),
	!.

unsafe(Parents, Var) :-
	var(Var),
	!,
	nb_setval(sandbox_last_error, error(instantiation_error, sandbox(_, Parents))),
	fail.
unsafe(_Parents, Ctx) :-
	Ctx = sandbox(_,_),
	nb_setval(sandbox_last_error, error(instantiation_error, Ctx)),
	fail.

safe_qualified_list([], _, _, _, Safe, Safe).
safe_qualified_list([G|Gs], M, Ctx, Parents, Safe0, Safe) :-
	safe(M:G, Ctx, Parents, Safe0, Safe1),
	safe_qualified_list(Gs, M, Ctx, Parents, Safe1, Safe).

qualified_control((A,B), [A,B]).
qualified_control((A;B), [A,B]).
qualified_control((A->B), [A,B]).
qualified_control((A*->B), [A,B]).
qualified_control(\+(A), [A]).
qualified_control(call(A), [A]).

% A system predicate whose meaning depends on the module it's called in, as
% assert, clause and findall do: one called as M:G from outside M acts on M.

module_sensitive(_, G) :-
	predicate_property(G, meta_predicate(Spec)),	% a system predicate's is the same everywhere
	Spec =.. [_|Args],
	member(A, Args),
	( integer(A) ; A == (:) ; A == (^) ; A == (//) ),
	!.

% safe_call/N checks its goal when called, so it's safe without knowing it
% now, but only in the module the checked code runs in, or the one it is
% walking: as M:G, which is how safe_guard/2 writes it, G then checked in
% M, whatever it's bound to. Another module's goal has to be known now.

safe_call_goal(G) :-
	compound(G),
	functor(G, safe_call, A),
	A >= 1, A =< 8.

safe_call_called(G, Ctx, Parents, Called) :-
	G =.. [_, C|Extra],
	length(Extra, N),
	(	var(C) ->
		throw(error(instantiation_error, sandbox(sandbox:G, Parents)))
	;	C = Q:_, atom(Q), ( Q == Ctx ; nb_current(sandbox_root, Q) ) ->
		Called = []
	;	C = Q:X, ( var(Q) ; \+ ground_goal_name(X) ) ->
		throw(error(permission_error(call, sandboxed, sandbox:G), sandbox(sandbox:G, Parents)))
	;	extend(N, C, Goal),
		Called = [Goal]
	).

% A goal whose predicate is known now, through any qualifications.

ground_goal_name(G) :- var(G), !, fail.
ground_goal_name(M:G) :- !, atom(M), ground_goal_name(G).
ground_goal_name(G) :- callable(G).

rethrow_instantiation_error(Parents) :-
	throw(error(instantiation_error, sandbox(_, Parents))).

% A system predicate, or a control construct, which are not redefinable.

system_predicate(_, G) :-
	control(G),
	!.
system_predicate(M, G) :-
	predicate_property(M:G, built_in),
	!.

control((_,_)).
control((_;_)).
control((_->_)).
control((_*->_)).
control(\+(_)).
control(!).
control(true).
control(fail).
control(call(_)).

% The module defining G as called in M: the one it is imported from, else
% 'system' for a system predicate, else M itself.

home_module(M, G, H) :-
	(	\+ control(G), predicate_property(M:G, imported_from(H0)) -> H = H0
	;	system_predicate(M, G) -> H = system
	;	H = M
	).

safe_clauses(G, M, Parents, Safe0, Safe) :-
	\+ system_predicate(M, G),
	predicate_property(M:G, interpreted),
	def_module(M:G, MD:QG),
	!,
	findall(Body, '$clause'(MD:QG, Body), Bodies),
	safe_bodies(Bodies, MD, Parents, Safe0, Safe).
safe_clauses(G, M, [_|Parents], _, _) :-
	(	system_predicate(M, G)
	;	predicate_property(M:G, visible)
	),
	!,
	throw(error(permission_error(call, sandboxed, G), sandbox(M:G, Parents))).
safe_clauses(_, _, [G|Parents], _, _) :-
	throw(error(existence_error(procedure, G), sandbox(G, Parents))).

known_module(M:_, _) :-
	current_module(M),
	!.
known_module(M:G, Parents) :-
	throw(error(permission_error(call, sandboxed, M:G), sandbox(M:G, Parents))).

add_iso_parent(G, Parents, Parents) :-
	is_control(G),
	!.
add_iso_parent(G, Parents, [G|Parents]).

is_control((_,_)).
is_control((_;_)).
is_control((_->_)).
is_control((_*->_)).
is_control(\+(_)).

% safe_bodies(+Bodies, +Module, +Parents, +Safe0, -Safe). A body goal
% qualified with another module is checked as the call into it that it is.

safe_bodies([], _, _, Safe, Safe).
safe_bodies([H|T], M, Parents, Safe0, Safe) :-
	copy_term(H, H2),
	safe(H2, M, Parents, Safe0, Safe1),
	safe_bodies(T, M, Parents, Safe1, Safe).

def_module(M:G, MD:QG) :-
	predicate_property(M:G, imported_from(MD)),
	!,
	meta_qualify(MD:G, M, QG).
def_module(M:G, M:QG) :-
	meta_qualify(M:G, M, QG).

% safe_list(+Called, +Module, +Parents, +Safe0, -Safe): goals called by a
% meta-predicate, unqualified where in the caller's own module.

safe_list([], _, _, Safe, Safe).
safe_list([H|T], M, Parents, Safe0, Safe) :-
	(	H = M2:H2,
		M == M2
	->	copy_term(H2, H3)
	;	copy_term(H, H3)
	),
	safe(H3, M, Parents, Safe0, Safe1),
	safe_list(T, M, Parents, Safe1, Safe).

% meta_qualify(:G, +M, -QG): qualifies G's goal arguments with M.

meta_qualify(MD:G, M, QG) :-
	predicate_property(MD:G, meta_predicate(Head)),
	!,
	G =.. [Name|Args],
	Head =.. [_|Q],
	qualify_args(Q, M, Args, QArgs),
	QG =.. [Name|QArgs].
meta_qualify(_:G, _, G).

qualify_args([], _, [], []).
qualify_args([H|T], M, [A|AT], [Q|QT]) :-
	qualify_arg(H, M, A, Q),
	qualify_args(T, M, AT, QT).

qualify_arg(S, M, A, Q) :-
	q_arg(S),
	!,
	qualify(A, M, Q).
qualify_arg(_, _, A, A).

q_arg(I) :- integer(I), !.
q_arg(:).
q_arg(^).
q_arg(//).

qualify(A, M, MZ:Q) :-
	strip_module(M:A, MZ, Q).

% goal_id(:Goal, -Id, -Gen) is nondet: an id for the goal proven safe, the
% most general form first, then the specific one.

goal_id(M:Goal, M:Id, Gen) :-
	!,
	goal_id(Goal, Id, Gen).
goal_id(Var, _, _) :-
	var(Var),
	!,
	instantiation_error(Var).
goal_id(Atom, Atom, Atom) :-
	atom(Atom),
	!.
goal_id(Term, _, _) :-
	\+ compound(Term),
	!,
	type_error(callable, Term).
goal_id(Term, Skolem, Gen) :-
	compound_name_arity(Term, Name, Arity),
	compound_name_arity(Skolem, Name, Arity),
	compound_name_arity(Gen, Name, Arity),
	copy_goal_args(1, Term, Skolem, Gen),
	(	Gen =@= Term
	->	!
	;	true
	),
	numbervars(Skolem, 0, _).
goal_id(Term, Skolem, Term) :-
	copy_term(Term, Skolem),
	numbervars(Skolem, 0, _).

% The most general form keeps module-qualified arguments, as they are
% likely to be called.

copy_goal_args(I, Term, Skolem, Gen) :-
	arg(I, Term, TA),
	!,
	arg(I, Skolem, SA),
	arg(I, Gen, GA),
	copy_goal_arg(TA, SA, GA),
	I2 is I + 1,
	copy_goal_args(I2, Term, Skolem, Gen).
copy_goal_args(_, _, _, _).

copy_goal_arg(Arg, SArg, Arg) :-
	copy_goal_arg(Arg),
	!,
	copy_term(Arg, SArg).
copy_goal_arg(_, _, _).

copy_goal_arg(Var) :- var(Var), !, fail.
copy_goal_arg(_:_).

% safe_primitive(?Goal) is nondet: Goal is safe to call, as it can't reach
% dangerous resources or upset the rest of the process. A bare goal is the
% system predicate of that name, a qualified one the predicate defined in
% that module.

safe_primitive(true).
safe_primitive(fail).
safe_primitive(false).
safe_primitive(repeat).
safe_primitive(!).
										% types
safe_primitive(var(_)).
safe_primitive(nonvar(_)).
safe_primitive(integer(_)).
safe_primitive(float(_)).
safe_primitive(rational(_)).
safe_primitive(number(_)).
safe_primitive(atom(_)).
safe_primitive(atomic(_)).
safe_primitive(compound(_)).
safe_primitive(callable(_)).
safe_primitive(ground(_)).
safe_primitive(cyclic_term(_)).
safe_primitive(acyclic_term(_)).
safe_primitive(is_stream(_)).
safe_primitive(is_list(_)).
safe_primitive(is_partial_list(_)).
safe_primitive(is_list_or_partial_list(_)).
safe_primitive(is_bigint(_)).
safe_primitive(must_be(_,_)).
safe_primitive(must_be(_,_,_,_)).
safe_primitive(can_be(_,_)).
safe_primitive(can_be(_,_,_,_)).
safe_primitive(error:must_be(_,_)).
safe_primitive(error:can_be(_,_)).
										% ordering
safe_primitive(@>(_,_)).
safe_primitive(@>=(_,_)).
safe_primitive(==(_,_)).
safe_primitive(@<(_,_)).
safe_primitive(@=<(_,_)).
safe_primitive(compare(_,_,_)).
safe_primitive(sort(_,_)).
safe_primitive(msort(_,_)).
safe_primitive(sort(_,_,_,_)).
safe_primitive(keysort(_,_)).
safe_primitive(=@=(_,_)).
safe_primitive(\=@=(_,_)).
safe_primitive(variant(_,_)).
										% unification and equivalence
safe_primitive(=(_,_)).
safe_primitive(\=(_,_)).
safe_primitive(?=(_,_)).
safe_primitive(unifiable(_,_,_)).
safe_primitive(unify_with_occurs_check(_,_)).
safe_primitive(\==(_,_)).
safe_primitive(dif:dif(_,_)).
										% arithmetic
safe_primitive(is(_,_)).
safe_primitive(>(_,_)).
safe_primitive(>=(_,_)).
safe_primitive(=:=(_,_)).
safe_primitive(=\=(_,_)).
safe_primitive(=<(_,_)).
safe_primitive(<(_,_)).
safe_primitive(between(_,_,_)).
safe_primitive(succ(_,_)).
safe_primitive(divmod(_,_,_,_)).
safe_primitive(random(_)).
safe_primitive(random_between(_,_,_)).
										% term handling
safe_primitive(arg(_,_,_)).
safe_primitive(nb_setarg(_,_,_)).
safe_primitive(functor(_,_,_)).
safe_primitive(_ =.. _).
safe_primitive(compound_name_arity(_,_,_)).
safe_primitive(compound_name_arguments(_,_,_)).
safe_primitive(copy_term(_,_)).
safe_primitive(copy_term(_,_,_)).
safe_primitive(copy_term_nat(_,_)).
safe_primitive(duplicate_term(_,_)).
safe_primitive(numbervars(_,_,_)).
safe_primitive(subsumes_term(_,_)).
safe_primitive(term_hash(_,_)).
safe_primitive(term_hash(_,_,_)).
safe_primitive(term_variables(_,_)).
safe_primitive(term_variables(_,_,_)).
safe_primitive(term_singletons(_,_)).
										% atoms
safe_primitive(atom_chars(_,_)).
safe_primitive(atom_codes(_,_)).
safe_primitive(sub_atom(_,_,_,_,_)).
safe_primitive(atom_concat(_,_,_)).
safe_primitive(atom_length(_,_)).
safe_primitive(atom_number(_,_)).
safe_primitive(atom_lower(_,_)).
safe_primitive(atom_upper(_,_)).
safe_primitive(char_code(_,_)).
safe_primitive(atomic_concat(_,_,_)).
safe_primitive(atomic_list_concat(_,_)).
safe_primitive(atomic_list_concat(_,_,_)).
safe_primitive(term_to_atom(_,_)).
safe_primitive(charsio:char_type(_,_)).
safe_primitive(charsio:code_type(_,_)).
										% numbers
safe_primitive(number_codes(_,_)).
safe_primitive(number_chars(_,_)).
										% strings
safe_primitive(string_codes(_,_)).
safe_primitive(string_concat(_,_,_)).
safe_primitive(string_length(_,_)).
safe_primitive(string_lower(_,_)).
safe_primitive(string_upper(_,_)).
safe_primitive(sub_string(_,_,_,_,_)).
safe_primitive(split_string(_,_,_,_)).
safe_primitive(split(_,_,_,_)).
safe_primitive(replace(_,_,_,_)).
safe_primitive(read_term_from_atom(_,_,_)).
safe_primitive(read_from_atom(_,_)).
safe_primitive(read_from_chars(_,_)).
safe_primitive(read_term_from_chars(_,_,_)).
safe_primitive(write_term_to_atom(_,_,_)).
safe_primitive(write_canonical_to_atom(_,_,_)).
safe_primitive(write_term_to_chars(_,_,_)).
safe_primitive(write_canonical_to_chars(_,_,_)).
safe_primitive(hex_chars(_,_)).
safe_primitive(octal_chars(_,_)).
safe_primitive(hex_bytes(_,_)).
safe_primitive(base64(_,_,_)).
safe_primitive(chars_base64(_,_,_)).
safe_primitive(urlenc(_,_,_)).
safe_primitive(chars_urlenc(_,_,_)).
safe_primitive(sre_compile(_,_)).
safe_primitive(sre_match(_,_,_,_)).
safe_primitive(sre_matchp(_,_,_,_)).
safe_primitive(sre_match_all(_,_,_)).
safe_primitive(sre_match_all_pos(_,_,_)).
safe_primitive(sre_subst(_,_,_,_)).
safe_primitive(sre_substp(_,_,_,_)).
safe_primitive(sre_subst_all(_,_,_,_)).
safe_primitive(crypto_data_hash(_,_,_)).
safe_primitive(crypto_n_random_bytes(_,_)).
safe_primitive(uuid(_)).
										% lists
safe_primitive(length(_,_)).
safe_primitive(lists:member(_,_)).
safe_primitive(lists:memberchk(_,_)).
safe_primitive(lists:select(_,_,_)).
safe_primitive(lists:selectchk(_,_,_)).
safe_primitive(lists:append(_,_)).
safe_primitive(lists:append(_,_,_)).
safe_primitive(lists:subtract(_,_,_)).
safe_primitive(lists:union(_,_,_)).
safe_primitive(lists:intersection(_,_,_)).
safe_primitive(lists:is_set(_)).
safe_primitive(lists:nth0(_,_,_)).
safe_primitive(lists:nth1(_,_,_)).
safe_primitive(lists:nth0(_,_,_,_)).
safe_primitive(lists:nth1(_,_,_,_)).
safe_primitive(lists:last(_,_)).
safe_primitive(lists:same_length(_,_)).
safe_primitive(lists:transpose(_,_)).
safe_primitive(lists:sum_list(_,_)).
safe_primitive(lists:max_list(_,_)).
safe_primitive(lists:min_list(_,_)).
safe_primitive(lists:list_sum(_,_)).
safe_primitive(lists:list_max(_,_)).
safe_primitive(lists:list_min(_,_)).
safe_primitive(lists:list_to_set(_,_)).
safe_primitive(lists:reverse(_,_)).
safe_primitive(lists:permutation(_,_)).
										% exceptions
safe_primitive(throw(_)).
										% misc
safe_primitive(current_prolog_flag(_,_)).
safe_primitive(current_op(_,_,_)).
safe_primitive(sleep(_)).
safe_primitive(thread_sleep(_)).
safe_primitive(thread_self(_)).
safe_primitive(get_time(_)).
safe_primitive(wall_time(_)).
safe_primitive(cpu_time(_)).
safe_primitive(date_time(_,_,_,_,_,_)).
safe_primitive(date_time(_,_,_,_,_,_,_)).
safe_primitive(posix_time(_)).
safe_primitive(posix_gmtime(_,_)).
safe_primitive(posix_localtime(_,_)).
safe_primitive(posix_mktime(_,_)).
safe_primitive(posix_strftime(_,_,_)).
safe_primitive(posix_strptime(_,_,_)).
safe_primitive(posix_ctime(_,_)).
safe_primitive(statistics(_,_)).
safe_primitive(thread_property(Id,_)) :-
	(	var(Id)
	->	instantiation_error(Id)
	;	thread_self(Id)
	).
safe_primitive(strip_module(_,_,_)).
										% database
safe_primitive(clause(H,_)) :- safe_clause(H).
safe_primitive(asserta(X)) :- safe_assert(X).
safe_primitive(assertz(X)) :- safe_assert(X).
safe_primitive(retract(X)) :- safe_assert(X).
safe_primitive(retractall(X)) :- safe_assert(X).
safe_primitive(current_predicate(X)) :- safe_current_predicate(X).
safe_primitive(predicate_property(Pred, _)) :-
	nonvar(Pred),
	Pred \= (_:_).
										% globals
safe_primitive(b_getval(_,_)).
safe_primitive(b_setval(Var,_)) :-
	safe_global_var(Var).
safe_primitive(nb_getval(_,_)).
safe_primitive(nb_setval(Var,_)) :-
	safe_global_var(Var).
safe_primitive(nb_current(_,_)).
										% output
safe_primitive(writeln(_)).

% safe_assert(+Term) is semidet: asserting a fact in the current module.

safe_assert(C) :- cyclic_term(C), !, fail.
safe_assert(X) :- var(X), !, fail.
safe_assert((_Head:-_Body)) :- !, fail.
safe_assert(_:_) :- !, fail.
safe_assert(H) :- expansion_hook(H), !, fail.
safe_assert(_).

% safe_clause(+Head) is semidet: clause/2 that doesn't cross a module
% boundary, which could read another module's private clauses.

safe_clause(H) :- var(H), !.
safe_clause(_:_) :- !, fail.
safe_clause(_).

safe_global_var(Name) :-
	var(Name),
	!,
	instantiation_error(Name).
safe_global_var(Name) :-
	safe_global_variable(Name).

safe_current_predicate(X) :-
	nonvar(X),
	X = _:_,
	!,
	fail.
safe_current_predicate(_).

% safe_meta(+Goal, -Called:list(callable)) is semidet: a meta-predicate
% that is safe if every goal in Called is.

safe_meta(with_output_to(Output, G), [G]) :-
	safe_output(Output),
	!.
safe_meta(format(Format), Calls) :-
	format_calls(Format, [], Calls).
safe_meta(format(Format, Args), Calls) :-
	format_calls(Format, Args, Calls).
safe_meta(format(Output, Format, Args), Calls) :-
	safe_output(Output),
	format_calls(Format, Args, Calls).
safe_meta(set_prolog_flag(Flag, Value), []) :-
	atom(Flag),
	safe_prolog_flag(Flag, Value).
safe_meta(freeze:freeze(_Var, Goal), [Goal]).
safe_meta(when:when(_Cond, Goal), [Goal]).
safe_meta(_:Lambda, [Goal]) :-
	lambda_goal(Lambda, Goal).
safe_meta(sandbox:G, Ctx, Called) :-
	safe_call_goal(G),
	safe_call_called(G, Ctx, [], Called).

safe_meta(phrase(NT, Xs0, Xs), [Goal]) :-
	expand_nt(NT, Xs0, Xs, Goal).
safe_meta(phrase(NT, Xs0), [Goal]) :-
	expand_nt(NT, Xs0, [], Goal).

% A yall lambda, Params>>Body or Free/Body with any extra arguments, calls
% its body with the parameters bound.

lambda_goal(Lambda, Goal) :-
	compound(Lambda),
	compound_name_arguments(Lambda, Name, [A1,A2|Extra]),
	L =.. [Name, A1, A2],
	lambda_goal(L, Extra, Goal).

lambda_goal(L, Extra, Goal) :-
	compound(L),
	(	L = (_>>_) -> true
	;	L = (Free/_), nonvar(Free), Free = {_}
	),
	lambda_calls(L, Extra, Goal).

% expand_nt(+NT, ?Xs0, ?Xs, -NewGoal): as the grammar body would compile,
% but an error if NT is not instantiated enough.

expand_nt(NT, _Xs0, _Xs, _NewGoal) :-
	strip_module(NT, _, Plain),
	var(Plain),
	!,
	instantiation_error(Plain).
expand_nt(NT, Xs0, Xs, NewGoal) :-
	expand_term((pseudo_nt --> NT), Clause),
	Clause = (pseudo_nt(Xs0c, Xsc) :- NewGoal0),
	(	var(Xsc), Xsc \== Xs0c
	->	Xs = Xsc, NewGoal1 = NewGoal0
	;	NewGoal1 = (NewGoal0, Xsc = Xs)
	),
	(	var(Xs0c)
	->	Xs0 = Xs0c,
		NewGoal = NewGoal1
	;	NewGoal = (Xs0 = Xs0c, NewGoal1)
	).

% safe_meta_call(+Goal, +Context, -Called:list(callable)) is semidet.

safe_meta_call(Goal, Context, Called) :-
	(	safe_meta(Goal, Called)
	->	true
	;	safe_meta(Goal, Context, Called)
	),
	!.
safe_meta_call(Goal, _, Called) :-
	Goal = M:Plain,
	compound(Plain),
	compound_name_arity(Plain, Name, Arity),
	safe_meta_predicate(M:Name/Arity),
	predicate_property(Goal, meta_predicate(Spec)),
	!,
	called(Spec, Plain, Called).
safe_meta_call(M:Goal, _, Called) :-
	!,
	generic_goal(Goal, Gen),
	safe_meta(M:Gen),
	called(Gen, Goal, Called).
safe_meta_call(Goal, _, Called) :-
	generic_goal(Goal, Gen),
	safe_meta(Gen),
	called(Gen, Goal, Called).

called(Gen, Goal, Called) :-
	compound_name_arity(Goal, _, Arity),
	called(1, Arity, Gen, Goal, Called).

called(I, Arity, Gen, Goal, Called) :-
	I =< Arity,
	!,
	arg(I, Gen, Spec),
	(	calling_meta_spec(Spec)
	->	arg(I, Goal, Called0),
		extend(Spec, Called0, G),
		Called = [G|Rest]
	;	Called = Rest
	),
	I2 is I + 1,
	called(I2, Arity, Gen, Goal, Rest).
called(_, _, _, _, []).

generic_goal(G, Gen) :-
	functor(G, Name, Arity),
	functor(Gen, Name, Arity).

calling_meta_spec(V) :- var(V), !, fail.
calling_meta_spec(I) :- integer(I), !.
calling_meta_spec(^).
calling_meta_spec(//).

extend(^, G, Plain) :-
	!,
	strip_existential(G, Plain).
extend(//, DCG, Goal) :-
	!,
	expand_nt(DCG, _, _, Goal).
extend(0, G, G) :- !.
extend(I, M:G0, M:G) :-
	!,
	G0 =.. List,
	length(Extra, I),
	append(List, Extra, All),
	G =.. All.
extend(I, G0, G) :-
	G0 =.. List,
	length(Extra, I),
	append(List, Extra, All),
	G =.. All.

strip_existential(Var, Var) :-
	var(Var),
	!.
strip_existential(M:G0, M:G) :-
	!,
	strip_existential(G0, G).
strip_existential(_^G0, G) :-
	!,
	strip_existential(G0, G).
strip_existential(G, G).

% safe_meta(?Template): a meta-predicate safe when the goals in its
% integer, ^ and // positions are.

safe_meta((0,0)).
safe_meta((0;0)).
safe_meta((0->0)).
safe_meta((0*->0)).
safe_meta(\+(0)).
safe_meta(call(0)).
safe_meta(call(1,*)).
safe_meta(call(2,*,*)).
safe_meta(call(3,*,*,*)).
safe_meta(call(4,*,*,*,*)).
safe_meta(call(5,*,*,*,*,*)).
safe_meta(call(6,*,*,*,*,*,*)).
safe_meta(call(7,*,*,*,*,*,*,*)).
safe_meta(once(0)).
safe_meta(ignore(0)).
safe_meta(forall(0,0)).
safe_meta(if(0,0,0)).
safe_meta(catch(0,*,0)).
safe_meta(findall(*,0,*)).
safe_meta(findall(*,0,*,*)).
safe_meta(findnsols(*,*,0,*)).
safe_meta(setof(*,^,*)).
safe_meta(bagof(*,^,*)).
safe_meta(^(*,0)).
safe_meta(countall(0,*)).
safe_meta(call_nth(0,*)).
safe_meta(call_det(0,*)).
safe_meta(limit(*,0)).
safe_meta(offset(*,0)).
safe_meta(call_cleanup(0,0)).
safe_meta(setup_call_cleanup(0,0,0)).
safe_meta(call_residue_vars(0,*)).
safe_meta(call_with_inference_limit(0,*,*)).
safe_meta(call_with_time_limit(*,0)).
safe_meta(time_out(0,*,*)).

% safe_output(+Output): with_output_to/2 and format/3 may write to these,
% not to a stream.

safe_output(Output) :-
	var(Output),
	!,
	instantiation_error(Output).
safe_output(atom(_)).
safe_output(string(_)).
safe_output(codes(_)).
safe_output(codes(_,_)).
safe_output(chars(_)).
safe_output(chars(_,_)).

% format_calls(+Format, +FormatArgs, -Calls): the goals of Format's ~@.

format_calls(Format, Args, Calls) :-
	is_list(Args),
	!,
	format_types(Format, Types),
	(	format_callables(Types, Args, Calls)
	->	true
	;	throw(error(format_error(Format, Types, Args), _))
	).
format_calls(Format, Arg, Calls) :-
	format_calls(Format, [Arg], Calls).

format_callables([], [], []).
format_callables([callable|TT], [G|TA], [G|TG]) :-
	!,
	format_callables(TT, TA, TG).
format_callables([_|TT], [_|TA], TG) :-
	format_callables(TT, TA, TG).

% safe_prolog_flag(+Flag, +Value): setting Flag to Value is safe, as these
% flags are each module's own.

safe_prolog_flag(occurs_check, _).
safe_prolog_flag(double_quotes, _).
safe_prolog_flag(unknown, _).

% safe_load(+Module, +Source) and safe_load(+Module, +Source, +Options):
% load untrusted source into Module, refusing what it may not do, which is
% what SWI's load_files/2 does with sandboxed(true). Source is string(Text),
% stream(S) or terms(List). Each term is read with Module's flags and
% expanded, then:
%
%	- a clause must be for Module and not a system predicate, and has
%	  its body guarded as by safe_guard/2;
%	- a directive is one of dynamic/discontiguous/multifile/public/
%	  meta_predicate/table/thread_local for Module's own predicates, op/3,
%	  as Module's operators are its own unless it exports them,
%	  set_prolog_flag/2 for a safe_prolog_flag/2, use_module/1,2 or
%	  ensure_loaded/1 of a library by a relative path, one the
%	  safe_directive/1 hook allows, or else a goal run by safe_call/1;
%	- initialization/1 goals run by safe_call/1 once the rest is loaded.
%
% Then, unless Options has check(false), each clause body loaded must pass
% safe_goal/1. A refusal throws, leaving what was loaded so far: load into
% a module of its own, eg. with in_temporary_module/3.

safe_load(M, Source) :-
	safe_load(M, Source, []).

safe_load(M, Source, Options) :-
	must_be(atom, M),
	must_be(list, Options),
	(	current_module(M) -> true
	;	'$create_module'(M)
	),
	setup_call_cleanup(
		open_source(Source, In, Close),
		load_terms(In, M, Bodies, Inits),
		Close),
	(	memberchk(check(false), Options) -> true
	;	forall(member(B, Bodies), safe_goal(M:B))
	),
	forall(member(G, Inits), safe_call(M:G)).

open_source(Source, _, _) :-
	var(Source),
	!,
	instantiation_error(Source).
open_source(string(Text), In, close(S)) :-
	!,
	(	atom(Text) -> atom_chars(Text, Cs)
	;	Cs = Text
	),
	open_string(Cs, S),
	In = stream(S).
open_source(stream(S), stream(S), true) :- !.
open_source(terms(Ts), terms(Ts), true) :- !.
open_source(Source, _, _) :-
	domain_error(sandbox_source, Source).

load_terms(In, M, Bodies, Inits) :-
	next_term(In, M, T, In1),
	(	T == end_of_file
	->	Bodies = [], Inits = []
	;	expand_term(T, X),
		(	is_list(X) -> Xs = X ; Xs = [X] ),
		load_expanded(Xs, M, Bodies, Bodies1, Inits, Inits1),
		load_terms(In1, M, Bodies1, Inits1)
	).

next_term(stream(S), M, T, stream(S)) :-
	M:read_term(S, T, []).
next_term(terms([]), _, end_of_file, terms([])) :- !.
next_term(terms([T|Ts]), _, T, terms(Ts)).

load_expanded([], _, Bs, Bs, Is, Is).
load_expanded([T|Ts], M, Bs0, Bs, Is0, Is) :-
	load_term(T, M, Bs0, Bs1, Is0, Is1),
	load_expanded(Ts, M, Bs1, Bs, Is1, Is).

load_term(T, _, _, _, _, _) :-
	var(T),
	!,
	instantiation_error(T).
load_term((:- D), M, Bs, Bs, Is0, Is) :-
	!,
	load_directive(D, M, Is0, Is).
load_term((?- D), M, Bs, Bs, Is0, Is) :-
	!,
	load_directive(D, M, Is0, Is).
load_term(C, M, [B|Bs], Bs, Is, Is) :-
	load_clause(C, M, B).

load_directive(D, _, _, _) :-
	var(D),
	!,
	instantiation_error(D).
load_directive(initialization(G), M, [G|Is], Is) :-
	!,
	must_be(callable, G),
	_ = M.
load_directive(initialization(G, When), M, Is0, Is) :-
	!,
	must_be(callable, G),
	(	When == now -> safe_call(M:G), Is0 = Is
	;	When == after_load -> Is0 = [G|Is]
	;	domain_error(initialization_type, When)
	).
load_directive(D, M, Is, Is) :-
	directive_kind(D, M, Kind),
	run_directive(Kind, D, M).

% directive_kind(+Directive, +Module, -Kind): how a directive is allowed,
% throwing if it isn't.

directive_kind(D, M, hook) :-
	safe_directive(M:D),
	!.
directive_kind(M2:D, M, Kind) :-
	!,
	(	M2 == M -> directive_kind(D, M, Kind)
	;	permission_error(execute, sandboxed_directive, (:- M2:D))
	).
directive_kind(D, M, decl) :-
	safe_pattr(D),
	!,
	D =.. [Attr, Preds],
	safe_pattr(Preds, Attr, M).
directive_kind(op(P, T, Names), _, op) :-
	!,
	(	safe_op_names(Names) -> true
	;	permission_error(execute, sandboxed_directive, (:- op(P, T, Names)))
	).
directive_kind(set_prolog_flag(Flag, Value), _, flag) :-
	!,
	(	atom(Flag), ground(Value), safe_prolog_flag(Flag, Value) -> true
	;	permission_error(execute, sandboxed_directive, (:- set_prolog_flag(Flag, Value)))
	).
directive_kind(D, _, load) :-
	directive_loads_file(D, File),
	!,
	safe_path(File).
directive_kind(D, _, _) :-
	refused_directive(D),
	!,
	permission_error(execute, sandboxed_directive, (:- D)).
directive_kind(_, _, goal).

run_directive(goal, D, M) :-
	!,
	safe_call(M:D).
run_directive(op, op(P, T, Names), M) :-
	!,
	M:op(P, T, Names).
run_directive(flag, set_prolog_flag(Flag, Value), M) :-
	!,
	M:set_prolog_flag(Flag, Value).
run_directive(load, D, M) :-
	!,
	M:D.
run_directive(_, D, M) :-
	format(atom(Text), ":- ~q.~n", [D]),
	load_text(Text, [module(M)]).

safe_pattr(dynamic(_)).
safe_pattr(discontiguous(_)).
safe_pattr(multifile(_)).
safe_pattr(public(_)).
safe_pattr(meta_predicate(_)).
safe_pattr(table(_)).
safe_pattr(thread_local(_)).

safe_pattr(Var, _, _) :-
	var(Var),
	!,
	instantiation_error(Var).
safe_pattr((A,B), Attr, M) :-
	!,
	safe_pattr(A, Attr, M),
	safe_pattr(B, Attr, M).
safe_pattr([], _, _) :- !.
safe_pattr([H|T], Attr, M) :-
	!,
	safe_pattr(H, Attr, M),
	safe_pattr(T, Attr, M).
safe_pattr(M2:G, Attr, M) :-
	!,
	(	M2 == M -> safe_pattr(G, Attr, M)
	;	Goal =.. [Attr, M2:G],
		permission_error(execute, sandboxed_directive, (:- Goal))
	).
safe_pattr(_, _, _).

safe_op_names(Name) :-
	atom(Name),
	!.
safe_op_names(Names) :-
	is_list(Names),
	forall(member(Name, Names), atom(Name)).

directive_loads_file(use_module(library(X)), X).
directive_loads_file(use_module(library(X), _), X).
directive_loads_file(ensure_loaded(library(X)), X).

refused_directive(module(_, _)).
refused_directive(use_module(_)).
refused_directive(use_module(_, _)).
refused_directive(ensure_loaded(_)).
refused_directive(include(_)).
refused_directive(consult(_)).
refused_directive(load_files(_, _)).

safe_path(X) :-
	var(X),
	!,
	instantiation_error(X).
safe_path(X) :-
	atom(X),
	!,
	(	\+ sub_atom(X, 0, _, 0, '..'),
		\+ sub_atom(X, 0, _, _, '/'),
		\+ sub_atom(X, 0, _, _, '../'),
		\+ sub_atom(X, _, _, 0, '/..'),
		\+ sub_atom(X, _, _, _, '/../')
	->	true
	;	permission_error(load, sandboxed, library(X))
	).
safe_path(A/B) :-
	!,
	safe_path(A),
	safe_path(B).
safe_path(X) :-
	permission_error(load, sandboxed, library(X)).

% load_clause(+Clause, +Module, -Body): asserts Clause in Module, its body
% guarded.

load_clause(C, _, _) :-
	cyclic_term(C),
	!,
	type_error(acyclic_term, C).
load_clause(M2:C, M, B) :-
	!,
	(	M2 == M -> load_clause(C, M, B)
	;	clause_pi(C, PI),
		permission_error(modify, sandboxed_procedure, M2:PI)
	).
load_clause((H0 :- B0), M, B) :-
	!,
	clause_head(H0, M, H),
	guard(B0, M, B),
	assertz(M:(H :- B)).
load_clause(H0, M, true) :-
	clause_head(H0, M, H),
	assertz(M:H).

clause_head(H, _, _) :-
	var(H),
	!,
	instantiation_error(H).
clause_head(M2:H0, M, H) :-
	!,
	(	M2 == M -> clause_head(H0, M, H)
	;	clause_pi(H0, PI),
		permission_error(modify, sandboxed_procedure, M2:PI)
	).
clause_head(H, _, _) :-
	\+ callable(H),
	!,
	type_error(callable, H).
clause_head(H, M, _) :-
	system_predicate(M, H),
	!,
	functor(H, N, A),
	permission_error(modify, static_procedure, N/A).
clause_head(H, _, _) :-
	expansion_hook(H),
	!,
	functor(H, N, A),
	permission_error(modify, sandboxed_procedure, N/A).
clause_head(H, _, H).

clause_pi((H :- _), PI) :- !, clause_pi(H, PI).
clause_pi(_:H, PI) :- !, clause_pi(H, PI).
clause_pi(H, N/A) :- callable(H), !, functor(H, N, A).
clause_pi(H, H).

permission_error(Action, Type, Culprit) :-
	throw(error(permission_error(Action, Type, Culprit), _)).

% An expansion hook rewrites code as it's loaded, and what it makes, not its
% body, decides what then runs, so sandboxed code may not define one.

expansion_hook(term_expansion(_, _)).
expansion_hook(goal_expansion(_, _)).
