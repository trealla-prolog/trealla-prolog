% current_table/2: enumeration, SWI-style variant lookup, and stale handles after abolish.

:- use_module(library(tabling)).
:- use_module(library(lists)).

:- initialization(main).

:- table path/2.
path(X,Y) :- path(X,Z), edge(Z,Y).
path(X,Y) :- edge(X,Y).
edge(a,b). edge(b,c). edge(c,a).

report(Name, Goal) :-
	(  catch(Goal, E, (print_message(error, E), fail)) ->
	   format("~w: ok~n", [Name])
	;  format("~w: FAILED~n", [Name])
	).

test_none :-
	\+ current_table(_, _).

test_enumerate :-
	path(a, _), path(_, _),
	findall(V, current_table(V, _), Vs),
	length(Vs, 2),
	member(P, Vs), variant(P, path(a,_)),
	member(Q, Vs), variant(Q, path(_,_)).

test_variant_lookup :-
	findall(Z, current_table(path(a,Z), _), [Z1]),
	var(Z1),
	\+ current_table(path(b,_), _),
	current_table(user:path(_,_), _).

test_stale :-
	current_table(path(a,_), T),
	abolish_all_tables,
	\+ tabling:'$tbl_variant'(T, _),
	\+ current_table(_, _).

main :-
	report('no tables', test_none),
	report(enumerate, test_enumerate),
	report('variant lookup', test_variant_lookup),
	report('stale handle', test_stale).
