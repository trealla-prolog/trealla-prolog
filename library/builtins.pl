:- pragma(builtins, [once(true)]).
:- use_module(library(error)).
:- use_module(library(lists)).
:- use_module(library(iso_ext)).
:- use_module(library(gensym)).

:- dynamic(atexit/0).
:- help(halt, [iso(true)]).

halt :-
	ignore(atexit),
	'$halt'.

:- help(halt(+integer), [iso(true)]).

halt(Status) :-
	ignore(atexit),
	'$halt'(Status).

:- help(term_variables(+term,-list,?tail), [iso(false)]).

term_variables(P1, P2, P3) :-
	term_variables(P1, P4),
	append(P4, P3, P2).

length(Xs0, N) :-
   '$skip_max_list'(M, N, Xs0,Xs),
   !,
   (  Xs == [] -> N = M
   ;  nonvar(Xs) -> var(N), Xs = [_|_], resource_error(finite_memory, length/2)
   ;  nonvar(N) -> R is N-M, length_rundown(Xs, R)
   ;  N == Xs -> failingvarskip(Xs), resource_error(finite_memory, length/2)
   ;  length_addendum(Xs, N, M)
   ).
length(_, N) :-
   integer(N), !,
   domain_error(not_less_than_zero, N, length/2).
length(_, N) :-
   type_error(integer, N, length/2).

length_rundown(Xs, 0) :- !, Xs = [].
length_rundown(Vs, N) :-
    '$unattributed_var'(Vs), % unconstrained
    !,
    '$det_length_rundown'(Vs, N).
length_rundown([_|Xs], N) :- % force unification
    N1 is N-1,
    length(Xs, N1). % maybe some new info on Xs

failingvarskip(Xs) :-
    '$unattributed_var'(Xs), % unconstrained
    !.
failingvarskip([_|Xs0]) :- % force unification
    '$skip_max_list'(_, _, Xs0,Xs),
    (  nonvar(Xs) -> Xs = [_|_]
	 ;  failingvarskip(Xs)
    ).

length_addendum([], N, N).
length_addendum([_|Xs], N, M) :-
    M1 is M + 1,
    length_addendum(Xs, N, M1).

:- help(length(?term,?integer), [iso(false), desc('Number of elements in list.')]).

% Blackboard predicates. The raw ops ('$bb_put' etc) store a flat
% copy of a term, dropping variable attributes. These wrappers use
% copy_term/3 to residualize attribute goals, store them alongside the
% term, and call them again on retrieval so attributes (eg. freeze/2,
% dif/2, clp(Z) domains) survive the blackboard. Terms without
% attributed variables take the raw path unchanged.

bb_b_put(K, T) :-
	'$bb_b_put'(K, '$bb'(T)).

% Attributed variables inside T survive the blackboard: '$bb_put' stores
% an augmented image carrying the raw attributes of every reachable
% attributed variable (transitively, through attribute values too) and
% '$bb_get' re-attaches them to the freshly imported variables. This is
% done natively - see bif_bboard.c.

bb_put(K, T) :-
	'$bb_put'(K, '$bb'(T)).

bb_get(K, T) :-
	'$bb_get'(K, '$bb'(T)).

bb_delete(K, T) :-
	'$bb_get'(K, '$bb'(T)),
	'$bb_delete'(K, _).

bb_update(K, O, N) :-
	'$bb_get'(K, '$bb'(O)),
	bb_put(K, N).

:- help(writeln(+term), [iso(false),deprecated(true)]).
:- help(writeln(+stream,+term), [iso(false),deprecated(true)]).

writeln(T) :- write_term(stdout,T,[nl(true)]).		% SWI-compatible
writeln(S, T) :- write_term(S,T,[nl(true)]).		% SWI-compatible

:- help(predicate_property(+callable,+term), [iso(true)]).

read_from_chars(Cs, T) :-
    read_term_from_chars(Cs, T, []).

:- help(read_from_chars(+chars,?term), [iso(false)]).

predicate_property(P, A) :-
	nonvar(P), atom(A), !,
	must_be(P, callable, predicate_property/2, _),
	'$legacy_predicate_property'(P, A).
predicate_property(P, A) :-
	'$load_properties',
	(	var(A) ->
		true
	; 	(Controls = [
			built_in,choice_construct,
			discontiguous,private,static,
			dynamic,foreign,multifile,
			meta_predicate(_),imported_from(_),template(_),
			iso,visible,tabled
			],
			( memberchk(A, Controls) ->
				true
			;	throw(error(domain_error(predicate_property, A), P))
			)
		)
	),
	must_be(P, callable, predicate_property/2, _),
	(	P = (M:P2) ->
		M:'$predicate_property'(predicate, P2, A)
	;	'$predicate_property'(predicate, P, A)
	).

:- help(evaluable_property(+callable,+term), [iso(true)]).

evaluable_property(P, A) :-
	nonvar(P), atom(A), !,
	must_be(P, callable, evaluable_property/2, _),
	'$legacy_evaluable_property'(P, A).
evaluable_property(P, A) :-
	'$load_properties',
	(	var(A) ->
		true
	; 	(Controls = [iso,built_in,static,dynamic,template(_),template(_,_)],
			(memberchk(A, Controls) ->
				true
			;	(
				must_be(A, callable, evaluable_property/2, _),
				throw(error(domain_error(evaluable_property, A), P))
				)
			)
		)
	),
	must_be(P, callable, evaluable_property/2, _),
	(	P = (M:P2) ->
		M:'$predicate_property'(function, P2, A)
	;	'$predicate_property'(function, P, A)
	).

:- help(current_prolog_flag(+callable,+term), [iso(true)]).

current_prolog_flag(P, A) :-
	nonvar(P), !,
	'$legacy_current_prolog_flag'(P, A).
current_prolog_flag(P, A) :-
	'$load_flags',
	'$current_prolog_flag'(P, A).

:- help(argv(-list), [iso(false)]).
:- help(raw_argv(-list), [iso(false)]).

argv(L) :- current_prolog_flag(argv, L).
raw_argv(L) :- current_prolog_flag(raw_argv, L).

'$post_unify_hook' :-
	'$undo_trail'(Vars, State),
	process_vars_(Vars, [], Goals),
	'$redo_trail'(State),
	maplist(call, Goals).

process_vars_([], Goals, Goals).
process_vars_([Var-Val|Vars], SoFar, Goals) :-
	(	get_atts(Var, Atts) ->
		process_var_(Atts, Var, Val, SoFar, MoreGoals),
		process_vars_(Vars, MoreGoals, Goals)
	;	process_vars_(Vars, SoFar, Goals)
	).

process_var_(Atts, Var, Val, SoFar, Goals) :-
	% A variable may carry several attributes owned by the same module
	% (e.g. clpb/clpb_hash). verify_attributes/3 is a per-module hook, so
	% it must be called once per distinct module, not once per attribute.
	atts_modules_(Atts, Ms0),
	sort(Ms0, Ms),
	process_modules_(Ms, Var, Val, SoFar, Goals).

atts_modules_([], []).
atts_modules_([Att|Atts], Ms) :-
	functor(Att, F, A),
	(	attribute(M, F, A) ->
		Ms = [M|Ms0]
	;	Ms = Ms0
	),
	atts_modules_(Atts, Ms0).

process_modules_([], _, _, Goals, Goals).
process_modules_([M|Ms], Var, Val, SoFar, Goals) :-
	M:verify_attributes(Var, Val, NewGoals0),
	modularize(NewGoals0, M, NewGoals),
	append(SoFar, NewGoals, MoreGoals),
	process_modules_(Ms, Var, Val, MoreGoals, Goals).

% Goals run in the order verify_attributes/3 returned them, so build the
% list head-first: the accumulator this used to prepend to reversed it.

modularize([], _, []).
modularize([H|T], M, [Goal|Goals]) :-
	functor(H, F, _),
	(F = ',' ->
		Goal = H
	;
		Goal = M:H
	),
	modularize(T, M, Goals).

term_attvars_([], VsIn, VsIn).
term_attvars_([H|T], VsIn, VsOut) :-
	(	'$attributed_var'(H) ->
		term_attvars_(T, [H|VsIn], VsOut)
	;	term_attvars_(T, VsIn, VsOut)
	).

term_attributed_variables_(Term, Vs) :-
	can_be(Vs, list, term_attributed_variables_/2, _),
	term_variables(Term, Vs0),
	term_attvars_(Vs0, [], Vs).

:- help(call_residue_vars(@goal,-list), [iso(false)]).
:- meta_predicate(call_residue_vars(0,?)).

call_residue_vars(G, Ls) :-
	'$mark_start'(Mark),
	G,
	'$list_attributed'(Mark, Ls0),
	sort(Ls0, Ls).

copy_term(Term, Copy, Gs) :-
	copy_term_with_attributes(Term, Copy),
	term_attributed_variables_(Copy, Vs),
	collect_goals_(Vs, [], Gs).

collect_goals_(_, [], GsIn, GsIn).
collect_goals_(V, [H|T], GsIn, GsOut) :-
	nonvar(H),
	H =.. [M, _],
	catch(M:attribute_goals(V, Goal0, []), _, Goal0 = put_atts(V, +H)),
	!,
	(Goal0 = [H2] -> Goal = H2 ; Goal = Goal0),
	collect_goals_(V, T, [Goal|GsIn], GsOut).
/*
collect_goals_(V, [H|T], GsIn, GsOut) :-
	nonvar(H),
	H =.. [F, _],
	attribute(M, F, 1),
	Goal = M:put_atts(V, H),
	collect_goals_(V, T, [Goal|GsIn], GsOut).
*/
collect_goals_(V, [_|T], GsIn, GsOut) :-
	collect_goals_(V, T, GsIn, GsOut).

collect_goals_([], GsIn, GsIn).
collect_goals_([V|T], GsIn, GsOut) :-
	get_atts(V, Ls),
	collect_goals_(V, Ls, GsIn, GsOut2),
	collect_goals_(T, GsOut2, GsOut).

flatten(List, FlatList) :-
	flatten_(List, [], FlatList0),
	!,
	FlatList = FlatList0.

flatten_(Var, Tl, [Var|Tl]) :-
	var(Var),
	!.
flatten_([], Tl, Tl) :- !.
flatten_([Hd|Tl], Tail, List) :-
	!,
	flatten_(Hd, FlatHeadTail, List),
	flatten_(Tl, Tail, FlatHeadTail).
flatten_(NonList, Tl, [NonList|Tl]).

:- help(flatten(?list,?list), [iso(false), desc('Does as it says.')]).

% Debugging...

print_goals_(_, []).
print_goals_(Any, [Goal|Goals]) :-
	(Any -> write(',\n   ') ; true),
	write(Goal),
	(Goals == [] -> true ;	write(', ')),
	print_goals_(false, Goals).

dump_attvars_([], []).
dump_attvars_(Vars, Gs) :-
	collect_goals_(Vars, [], Gs).

dump_attvars_(Any) :-
	'$list_attributed'(0, Vs0),
	sort(Vs0, Vs),
	dump_attvars_(Vs, Gs0),
	flatten(Gs0, Gs1),
	sort(Gs1, Gs),
	print_goals_(Any, Gs).

sort(A, B) :-
	'$sort'(A, B).

:- help(sort(+term,?term), [iso(true)]).

msort(A, B) :-
	'$msort'(A, B).

:- help(msort(+term,?term), [iso(true)]).

keysort(A, B) :-
	'$keysort'(A, B).

:- help(keysort(+term,?term), [iso(true)]).

sort(A, B, C, D) :-
	'$sort'(A, B, C, D).

:- help(sort(+term,+atom,+list,?term), [iso(false)]).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%

:- meta_predicate(bagof(-,0,?)).
:- help(bagof(+term,:callable,?list), [iso(true)]).

/**
 * bagof(T, X1^…^Xn^G, L): [ISO 8.10.2]
 * The predicate determines all the solutions to the goal G,
 * whereby collecting copies of the template T and the
 * witness. The predicate then repeatedly succeeds for
 * the witness and the list of associated templates.
 */
% bagof(+Term, +Goal, -List)
bagof(T, G, L) :-
	(var(L) -> true; must_be(L, list_or_partial_list, bagof/3, _)),
	acyclic_term(G),
	sys_globals_kernel_(T^G, W, H),
	findall(W-[+T], H, J),
	sys_same_vars_(J, _),
	keysort(J, K),
	sys_enum_runs_(K, W, L).

:- meta_predicate(setof(-,0,?)).
:- help(setof(+term,+callable,?list), [iso(true)]).

/**
 * setof(T, X1^…^Xn^G, L): [ISO 8.10.3]
 * The predicate determines all the solutions to the goal G,
 * whereby collecting copies of the template T and the
 * witness. The predicate then repeatedly succeeds for
 * the witness and the set of associated templates.
 */
% setof(+Term, +Goal, -List)
setof(T, G, L) :-
	(var(L) -> true; must_be(L, list_or_partial_list, setof/3, _)),
	acyclic_term(G),
	sys_globals_kernel_(T^G, W, H),
	findall(W-[+T], H, J),
	sys_same_vars_(J, _),
	sort(J, K),
	sys_enum_runs_(K, W, L).

% sys_same_vars_(+Pairs, +List)
sys_same_vars_([K-_|L], V) :-
	term_variables(K, V, _),
	sys_same_vars_(L, V).
sys_same_vars_([], _).

% sys_enum_runs_(+Pairs, +Term, -List)
sys_enum_runs_([K-[+V]|L], W, Q) :-
	sys_key_run_(L, K, R, H),
	(K = W, Q = [V|R], (H = [], !; true); sys_enum_runs_(H, W, Q)).

% sys_key_run_(+Pairs, +Term, -List, -Pairs)
sys_key_run_([K-[+V]|L], J, [V|R], H) :- K == J, !,
	sys_key_run_(L, J, R, H).
sys_key_run_(L, _, [], L).

/********************************************************************/
/* Helpers                                                          */
/********************************************************************/

% sys_goal_split_(+Goal, -List, -Goal)
sys_globals_kernel_(G, W, H) :-
	sys_goal_split_(G, I, H),
	term_variables(H, A),
	term_variables(I, B),
	sys_var_subtract_(A, B, W).

% sys_goal_split_(+Goal, -List, -Goal)
sys_goal_split_(G, [], G) :- var(G), !.
sys_goal_split_(V^G, [V|L], H) :- !,
	sys_goal_split_(G, L, H).
sys_goal_split_(G, [], G).

% sys_var_subtract_(+List, +List, -List)
sys_var_subtract_([X|L], R, T) :-
	member(Y, R), Y == X, !,
	sys_var_subtract_(L, R, T).
sys_var_subtract_([X|L], R, [X|S]) :-
	sys_var_subtract_(L, R, S).
sys_var_subtract_([], _, []).

%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Edinburgh...

:- help(get0(?integer), [iso(false),deprecated(true)]).
:- help(get0(+stream,?integer), [iso(false),deprecated(true)]).
:- help(get0(+term), [iso(false),deprecated(true)]).
:- help(get0(+stream,+term), [iso(false),deprecated(true)]).
:- help(put(+integer), [iso(false),deprecated(true)]).
:- help(put(+stream,+integer), [iso(false),deprecated(true)]).
:- help(see(+filename), [iso(false),deprecated(true)]).
:- help(tell(+filename), [iso(false),deprecated(true)]).
:- help(append(+filename), [iso(false),deprecated(true)]).

get0(C) :- get_code(C).
get0(S, C) :- get_code(S, C).
display(T) :- write_canonical(T).
display(S, T) :- write_canonical(S, T).
put(C) :- put_code(C).
put(S,C) :- put_code(S, C).
see(F) :- open(F, read, S), set_input(S).
tell(F) :- open(F, write, S), set_output(S).
append(F) :- open(F, append, S), set_output(S).

%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

'$portray_term'(S, T) :-
	compound(T), !,
	functor(T, _, Args),
	T =.. [Functor|_],
	writeq(S, Functor), write(S, '('),
	(between(1, Args, I),
		arg(I, T, Arg),
		'$portray'(S, Arg),
		(I < Args -> write(S, ',') ; true),
		fail  ; true),
	write(S, ')').
'$portray_term'(S, T) :-
	writeq(S, T).

'$portray'(S, T) :-
	current_output(S0),
	set_output(S),
	((catch(once(portray(T)), _, write(T)), !) ; '$portray_term'(S, T)),
	set_output(S0).

'$portray'(T) :-
	current_output(S),
	((catch(once(portray(T)), _, write(T)), !) ; '$portray_term'(S, T)).

:- help(print(+term), [iso(false)]).
:- help(print(+stream,+term), [iso(false)]).

print(T) :- current_output(S), format(S, "~p", [T]).
print(S, T) :- format(S, "~p", [T]).

:- help(open(+atom,+atom,--stream), [iso(true)]).

open(F, M, S) :- open(F, M, S, []).

:- meta_predicate(engine_create(?,0,?)).
:- meta_predicate(engine_create(?,0,?,?)).

engine_create(T, G, S) :- '$engine_create'(T, G, S, []).
engine_create(T, G, S, L) :- '$engine_create'(T, G, S, L).

:- help(engine_create(+term,+callable,?stream), [iso(false)]).
:- help(engine_create(+term,+callable,?stream,+list), [iso(false)]).

engine_post(E, T, R) :-
	engine_post(E, T),
	engine_next(E, R).

current_engine(E) :-
	stream_property(E, engine(true)).

:- help(chars_base64(+atom,?atom,+list), [iso(false)]).

chars_base64(Plain, Base64, Opts) :- base64(Plain, Base64, Opts).

:- help(chars_urlenc(+atom,?atom,+list), [iso(false)]).

chars_urlenc(Plain, Url, Opts) :- urlenc(Plain, Url, Opts).

:- help(term_to_atom(+term,?atom), [iso(false)]).

term_to_atom(T, S) :- write_term_to_chars(T, [], S).

:- help(absolute_filename(+atom,?atom), [iso(false)]).

absolute_file_name(R, A) :- absolute_file_name(R, A, []).

:- help('$client'(+atom,-atom,-atom,--stream), [iso(false)]).

'$client'(Url, S) :- '$client'(Url, _, _, S, []).

:- help('$client'(+atom,-atom,-atom,--stream), [iso(false)]).

'$client'(Url, Host, Path, S) :- '$client'(Url, Host, Path, S, []).

:- help('$server'(+atom,--stream), [iso(false)]).

'$server'(Host, S) :- '$server'(Host, S, []).

:- help(load_files(+list), [iso(false)]).

load_files(Files) :- load_files(Files,[]).

:- help(load_files(+list), [iso(false)]).

consult(Files) :- load_files(Files,[]).

:- help(reconsult(+list), [iso(false),deprecated(true)]).

reconsult(Files) :- load_files(Files,[]).

:- help(deconsult(+list), [iso(false),deprecated(true)]).

deconsult(Files) :- unload_files(Files).

:- help('?='(+term,+term), [iso(false)]).

?=(X, Y) :- \+ unifiable(X, Y, [_|_]).

:- help(atom_number(?atom,?number), [iso(false)]).

atom_number(A, N) :-
	atom(A),
	atom_codes(A,Codes), number_codes(N, Codes),
	!.
atom_number(A, N) :-
	number(N),
	number_codes(N,Codes), atom_codes(A, Codes).


'$skip_list'(Skip, Xs0, Xs) :- '$skip_max_list'(Skip,_, Xs0, Xs).

:- help(term_hash(+term,+list,-integer), [iso(false)]).

term_hash(Term, _Opts, Hash) :- term_hash(Term, Hash).

read_term_from_chars_(T, Cs, Rest) :-
	'$read_term_from_chars'(T, [], Cs, Rest).

read_term_from_chars_(T, Opts, Cs, Rest) :-
	'$read_term_from_chars'(T, Opts, Cs, Rest).

:- help(read_from_atom(+atom,?term), [iso(false)]).

read_from_atom(A, T) :- read_term_from_atom(A, T, []).

with_output_to(chars(Cs), Goal) :-
	setup_call_cleanup(
		'$capture_output',
		once(Goal),
		'$capture_output_to_chars'(Cs)
	), !.

with_output_to(string(Cs), Goal) :-
	setup_call_cleanup(
		'$capture_output',
		once(Goal),
		'$capture_output_to_chars'(Cs)
	), !.

with_output_to(atom(Cs), Goal) :-
	setup_call_cleanup(
		'$capture_output',
		once(Goal),
		'$capture_output_to_atom'(Cs)
	), !.

:- help(numbervars(+term,+integer,?integer), [iso(false)]).

numbervars(Term, N0, N) :-
   must_be(N0, integer, numbervars/3, _),
   can_be(N, integer, numbervars/3, _),
   term_variables(Term, Vars),
   numberlist_(Vars, N0, N).

numberlist_([], N, N).
numberlist_(['$VAR'(N0)|Vars], N0, N) :-
   N1 is N0+1,
   numberlist_(Vars, N1, N).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%

thread_property(P) :-
	thread_self(Id),
	thread_property(Id, P).

:- meta_predicate(with_mutex(-,0)).

with_mutex(Id, Goal) :-
	setup_call_cleanup(mutex_lock(Id), once(Goal), mutex_unlock(Id)).

:- meta_predicate(thread_create(0,-)).

thread_create(Goal, Id) :-
	thread_create(Goal, Id, []).

thread_join(Id) :-
	thread_join(Id, []).

thread_send_message(Term) :-
	thread_self(Id),
	thread_send_message(Id, Term).

thread_get_message(Term) :-
	thread_self(Id),
	thread_get_message(Id, Term).

thread_peek_message(Term) :-
	thread_self(Id),
	thread_peek_message(Id, Term).

xthread_statistics(K, V) :-
	thread_self(Id),
	thread_statistics(Id, K, V).

message_queue_create(Id) :-
	message_queue_create(Id, []).

mutex_create(Id) :-
	mutex_create(Id, []).

%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% NOTE: this doesn't display var names properly...

:- help(pretty(+predicateindicator), [iso(false)]).

pretty(PI) :-
	nonvar(PI),
	(   PI = Name/Arity0 ->
		Arity = Arity0
	;   PI = Name//Arity0 ->
		Arity is Arity0 + 2
	;   type_error(predicate_indicator, PI, listing/1)
	),
	functor(Head, Name, Arity),
	\+ \+ clause(Head, _), % only true if there is at least one clause
	(   clause(Head, Body),
		(   Body == true ->
			portray_clause(Head)
		;   portray_clause((Head :- Body))
		),
		false
	;   true
	).

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%

:- help(current_op(?integer,?atom,?atom), [iso(true)]).

current_op(A, B, C) :- var(A), var(B), var(C),
	!,
	( '$ops_dirty' -> (retractall('$op'(_,_,_)),'$load_ops') ; true ),
	'$op'(C, B, A).
current_op(_, _, C) :- nonvar(C), \+ atom(C),
	!, throw(error(type_error(atom,C), current_op/3)).
current_op(_, B, _) :- nonvar(B), \+ atom(B),
	!, throw(error(domain_error(operator_specifier, B), current_op/3)).
current_op(_, B, _) :- nonvar(B),
	\+ memberchk(B,[xf, yf, fx, fy, xfx, xfy, yfx]),
	!, throw(error(domain_error(operator_specifier, B), current_op/3)).
current_op(A, _, _) :- nonvar(A),
	\+ integer(A),
	!, throw(error(domain_error(operator_priority, A), current_op/3)).
current_op(A, _, _) :- nonvar(A),
	\+ (A >= 0),
	!, throw(error(domain_error(operator_priority, A), current_op/3)).
current_op(A, _, _) :- nonvar(A),
	\+ (A =< 1200),
	!, throw(error(domain_error(operator_priority, A), current_op/3)).
current_op(A, B, C) :-
	( '$ops_dirty' -> (retractall('$op'(_,_,_)),'$load_ops') ; true ),
	'$op'(C, B, A).

%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%

:- help(sre_match_all_in_file(+pattern,+filename,-list), [iso(false)]).

sre_match_all_in_file(Pat, Filename, L) :-
	setup_call_cleanup(
		open(Filename, read, S, [mmap(Cs)]),
		sre_match_all(Pat, Cs, L),
		close(S)
	).

:- help(sre_match_all(+pattern,+text,-list), [iso(false)]).

sre_match_all(Pat, Text, L) :-
	sre_compile(Pat, Reg),
	sre_match_all_(Reg, Text, [], L2),
	reverse(L2, L).

sre_match_all_(_, [], L, L) :- !.
sre_match_all_(Reg, TextIn, L0, L) :-
	sre_matchp(Reg, TextIn, Match, TextOut),
	(	TextOut \= [] ->
		sre_match_all_(Reg, TextOut, [Match|L0], L)
	;	L = L0
	).

:- help(sre_match_all_pos_in_file(+pattern,+filename,-list), [iso(false)]).

sre_match_all_pos_in_file(Pat, Filename, L) :-
	setup_call_cleanup(
		open(Filename, read, S, [mmap(Cs)]),
		sre_match_all_pos(Pat, Cs, L),
		close(S)
	).

:- help(sre_match_all_pos(+pattern,+subst,-list), [iso(false)]).

sre_match_all_pos(Pat, Text, L) :-
	sre_compile(Pat, Reg),
	sre_match_all_pos_(Reg, Text, 0, [], L2),
	reverse(L2, L).

sre_match_all_pos_(_, [], _, L, L) :- !.
sre_match_all_pos_(Reg, TextIn, Offset, L0, L) :-
	sre_matchp(Reg, TextIn, Match, TextOut),
	string_length(TextIn, N0),
	string_length(Match, N1),
	string_length(TextOut, N2),
	Pos is N0 - (N1 + N2) + Offset,
	Pos2 is Pos + 1,
	( TextOut \= [] ->
		sre_match_all_pos_(Reg, TextOut, Pos2, [Pos-N1|L0], L)
	;	L = L0
	).

:- help(sre_subst_all_in_file(+pattern,+filename,+subst,-list), [iso(false)]).

sre_subst_all_in_file(Pat, Filename, Subst, L) :-
	setup_call_cleanup(
		open(Filename, read, S, [mmap(Cs)]),
		sre_subst_all(Pat, Cs, Subst, L),
		close(S)
	).

:- help(sre_subst_all(+pattern,+text,+subst,-text), [iso(false)]).

sre_subst_all(Pat, Text, Subst, L) :-
	sre_compile(Pat, Reg),
	sre_subst_all_(Reg, Text, Subst, [], L0),
	reverse(L0, L1),
	append(L1, L).

sre_subst_all_(_, [], _, L, L) :- !.
sre_subst_all_(Reg, TextIn, Subst, L0, L) :-
	sre_substp(Reg, TextIn, Prefix, TextOut),
	(	TextOut \= [] ->
		sre_subst_all_(Reg, TextOut, Subst, [Subst,Prefix|L0], L)
	;	L = [Prefix|L0]
	).

%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% SWI-compatible library(option)

option(Opt, Options, _Default) :-
	memberchk(Opt, Options), !.
option(Opt, _, Default) :-
	functor(Opt, _, 1),
	arg(1, Opt, Default).

option(Opt, Options) :-
	memberchk(Opt, Options).

findnsols(P1,P2,P3,P4) :-
	copy_term(f(P2,P3), f(P2b,P3b)),
	'$findnsols'(P1,P2b,P3b,P4).

:- help(findnsols(+integer,+term,+callable,?list), [iso(false)]).

process_wait(Pid, Status, Opts) :-
	'$process_wait'(Pid, Status, Opts).

:- help(process_wait(+integer,-term,?list), [iso(false)]).

process_wait(Pid, Status) :-
	'$process_wait'(Pid, Status, []).

:- help(process_wait(+integer,-term), [iso(false)]).

thread_join(Tid, Status) :-
	'$thread_join'(Tid, Status).

:- help(thread_join(+thread,-term), [iso(false)]).

:- meta_predicate(sys_forall(0,0)).

sys_forall(Cond, Action) :-
	\+ (Cond, \+ Action).

