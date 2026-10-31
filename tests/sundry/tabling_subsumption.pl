% Answer subsumption (DESIGN-tabling-phase2.md item 2): ":- table
% path(_,_,min)" aggregates at insert instead of storing every answer.
%
% The two hard parts the doc calls out, both exercised below:
%
%   1. The answer trie is keyed on every argument EXCEPT the aggregated
%      one, so two answers agreeing on the rest collide and combine
%      (test_min_shortest_path, test_max).
%   2. An existing answer can be UPDATED in place, and every consumer
%      that already read the old value must run again - test_floor_
%      fixpoint is the one that actually depends on this: it needs
%      several rounds of re-pairing to reach the true minimum, and
%      was caught wrong (converging one round early) by a version of
%      this file's own C code with that re-pairing turned off.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

% ---------------------------------------------------------------------
% 1. Tabled shortest path over a DAG. Every (X,Y) pair must collapse to
% its MINIMUM cost, including multi-hop pairs computed through nested
% tabled sub-calls (a-d goes through b and c both).

:- table path1(_,_,min).

edge1(a,b,3). edge1(a,c,1). edge1(c,b,1). edge1(b,d,5).
path1(X,Y,C) :- edge1(X,Y,C).
path1(X,Y,C) :- edge1(X,Z,C1), path1(Z,Y,C2), C is C1+C2.

test_min_shortest_path :-
	findall(X-Y-C, path1(X,Y,C), All),
	msort(All, Sorted),
	(	Sorted == [a-b-2, a-c-1, a-d-7, b-d-5, c-b-1, c-d-6] ->
		write('min shortest path: ok')
	;	write('min shortest path: FAILED'), nl, write(Sorted)
	),
	nl.

% ---------------------------------------------------------------------
% 2. max works the other direction, and a graph with a genuine cycle
% (not a DAG) must still converge rather than loop.

:- table path2(_,_,min).

edge2(a,b,1). edge2(b,a,1). edge2(a,c,100). edge2(b,c,1).
path2(X,Y,C) :- edge2(X,Y,C).
path2(X,Y,C) :- edge2(X,Z,C1), path2(Z,Y,C2), C is C1+C2.

test_cyclic_min :-
	findall(X-Y-C, path2(X,Y,C), All),
	msort(All, Sorted),
	(	Sorted == [a-a-2, a-b-1, a-c-2, b-a-1, b-b-2, b-c-1] ->
		write('cyclic min: ok')
	;	write('cyclic min: FAILED'), nl, write(Sorted)
	),
	nl.

:- table best(_,max).

score(a,3). score(a,7). score(a,2). score(b,5).
best(X,S) :- score(X,S).

test_max :-
	(	findall(S, best(a,S), [7]), findall(S, best(b,S), [5]) ->
		write('max: ok')
	;	write('max: FAILED')
	),
	nl.

% ---------------------------------------------------------------------
% 3. The core worklist-protocol change: an EXISTING answer improved in
% place must be re-delivered to every current suspension, not just new
% ones (see '$tbl_pop_worklist' in src/bif_tabling.c - the "updated
% answers x all suspensions" pass). p and r mutually improve each
% other's minimum, bottoming out at the floor (500) only if r's
% suspension on p keeps getting re-paired across MULTIPLE rounds as p
% keeps improving. Skipping that re-pairing converges one round early,
% at 999 - wrong, but not obviously so (still terminates, still a
% plausible-looking number), which is exactly the kind of bug an
% output-only single-round test would miss.

:- table p(min), r(min).

p(1000).
p(V) :- r(V0), V is V0.
r(V) :- p(V0), V0 > 500, V is V0 - 1.

test_floor_fixpoint :-
	(	findall(V, p(V), [500]), findall(V, r(V), [500]) ->
		write('floor fixpoint: ok')
	;	write('floor fixpoint: FAILED')
	),
	nl.

% ---------------------------------------------------------------------
% 4. Zero aggregation markers (eg. dup(_,_)) is documentation-only and
% degrades to plain variant tabling - matches SWI's leniency, and
% means a spec need not be rewritten just because a mode was dropped.

:- table dup(_,_).

dup(a,1). dup(a,1). dup(a,2).

test_zero_markers :-
	(	findall(X-Y, dup(X,Y), [a-1,a-2]) ->
		write('zero markers: ok')
	;	write('zero markers: FAILED')
	),
	nl.

% ---------------------------------------------------------------------
% 5. A mode SWI does not have must raise SWI's error rather than be
% taken, silently, as part of the key. (More than one moded argument
% was refused here too; it is joined argument by argument now, in
% tabling_modes.pl.)

% NB the `fail` inside the catch is load-bearing: catch(G,_,true)
% succeeds when G merely SUCCEEDS as well as when it throws, so without
% it this passes whether or not the error is actually raised - which is
% exactly how the first version of this test (and the `as` one below)
% passed against code that raised nothing at all.

test_unknown_mode_rejected :-
	(	catch(
		  ( phrase(tabling:wrappers(bad_mode(_,foo)), _), fail ),
		  error(domain_error(tabled_mode, foo), _),
		  true
		) ->
		write('unknown mode rejected: ok')
	;	write('unknown mode rejected: FAILED')
	),
	nl.

% ---------------------------------------------------------------------
% 5b. ":- table Spec as Option" must not be silently MISREAD.
% `p/1 as Opt` is a compound whose functor is `as`/2, so before the
% guard above excluded that shape it matched the mode-spec clause -
% tabling a predicate literally named `as`/2 and leaving p/1 UNTABLED,
% with no diagnostic. Silently-untabled is the worst outcome here: the
% program still runs, just without termination guarantees.
%
% An UNRECOGNISED option must be refused rather than quietly accepted -
% taking one we do not implement would leave the caller believing they
% got a behaviour they did not. Deliberately a nonsense option rather
% than a not-yet-built one: this check had to be rewritten twice as
% `incremental` and then `shared` were implemented under it.

test_as_option_rejected :-
	(	catch(
		  ( phrase(tabling:wrappers(as_opt/1 as no_such_option), _), fail ),
		  error(domain_error(table_option, no_such_option), _),
		  true
		) ->
		write('as option rejected: ok')
	;	write('as option rejected: FAILED')
	),
	nl.

% incremental+shared is refused: publication promises a completed table
% is never written again, and invalidation writes to it.

test_incremental_shared_refused :-
	(	catch(
		  ( phrase(tabling:wrappers(as_both/1 as (incremental,shared)), _), fail ),
		  error(domain_error(table_option, incremental_and_shared), _),
		  true
		) ->
		write('incremental+shared refused: ok')
	;	write('incremental+shared refused: FAILED')
	),
	nl.

test_as_incremental_accepted :-
	(	catch(phrase(tabling:wrappers(as_inc/1 as incremental), Cs), _, fail),
		Cs = [_|_] ->
		write('as incremental accepted: ok')
	;	write('as incremental accepted: FAILED')
	),
	nl.

% ---------------------------------------------------------------------
% 6. Re-declaring the same predicate's mode spec is a no-op (matches
% the existing "already tabled" idempotency for plain Name/Arity), not
% a second registration.

:- table idem(min).
:- table idem(min).

ival(5). ival(3).
idem(V) :- ival(V).

test_idempotent :-
	findall(A-Mask-Op, tabling:'$tbl_moded_spec'(idem,A,Mask,Op,_), Specs),
	(	Specs == [1-1-min], findall(V, idem(V), [3]) ->
		write('idempotent: ok')
	;	write('idempotent: FAILED')
	),
	nl.

% ---------------------------------------------------------------------
% 7. max_answers_for_subgoal (item 1) bounds DISTINCT KEYS, not raw
% update attempts - repeatedly IMPROVING one key must not count against
% the limit, only a genuinely NEW key does.

:- table restrained(_,min).

rval(a,5). rval(a,3). rval(a,9). rval(a,1).
restrained(X,V) :- rval(X,V).

test_restraint_counts_keys :-
	set_prolog_flag(max_answers_for_subgoal, 1),
	(	findall(V, restrained(a,V), [1]) ->
		write('restraint counts keys: ok')
	;	write('restraint counts keys: FAILED')
	),
	nl,
	set_prolog_flag(max_answers_for_subgoal, infinite).

:- table restrained2(_,min).

rval2(a,5). rval2(a,3). rval2(b,9).
restrained2(X,V) :- rval2(X,V).

test_restraint_still_fires :-
	set_prolog_flag(max_answers_for_subgoal, 1),
	(	catch(
		  ( findall(_, restrained2(_,_), _), false ),
		  error(resource_error(max_answers_for_subgoal), _),
		  true
		) ->
		write('restraint still fires: ok')
	;	write('restraint still fires: FAILED')
	),
	nl,
	set_prolog_flag(max_answers_for_subgoal, infinite).

% ---------------------------------------------------------------------

main :-
	test_min_shortest_path,
	test_cyclic_min,
	test_max,
	test_floor_fixpoint,
	test_zero_markers,
	test_unknown_mode_rejected,
	test_as_option_rejected,
	test_as_incremental_accepted,
	test_incremental_shared_refused,
	test_idempotent,
	test_restraint_counts_keys,
	test_restraint_still_fires.
