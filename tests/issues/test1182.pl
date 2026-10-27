% Issue #1182: predicate_property/2 with an unbound head enumerates as in SWI instead of raising.

:- initialization(main).

:- dynamic(d/1).
foo(1).
'$hidden'(1).

main :-
	check(own_dynamic, (findall(H, predicate_property(H, dynamic), L), memberchk(d(_), L))),
	check(own_defined, (findall(H, predicate_property(H, defined), L), memberchk(foo(_), L))),
	check(no_hidden, \+ (predicate_property(H, defined), H = '$hidden'(_))),
	check(qualified, (predicate_property(user:H, dynamic), H = d(_))),
	check(system_builtins, (findall(H, predicate_property(system:H, built_in), L), length(L, N), N > 400)),
	check(system_c, predicate_property(system:atom_codes(_,_), built_in)),
	check(system_prolog, (predicate_property(system:H, built_in), H = writeln(_))),
	check(system_no_dollar, \+ (predicate_property(system:H, built_in), functor(H, N, _), sub_atom(N, 0, 1, _, '$'))),
	check(system_colon, (predicate_property(system:H, built_in), H = (_:_))),
	check(system_iso, (predicate_property(system:H, iso), H = atom_codes(_,_))),
	check(any_module, (predicate_property(M:_, built_in), M == system)),
	check(no_dups, (findall(H, predicate_property(system:H, built_in), L), length(L, N), sort(L, S), length(S, N))),
	throws(both_unbound, predicate_property(_, _)),
	throws(bad_property, predicate_property(_, nosuch)).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).

throws(Name, Goal) :-
	catch((Goal, format("~w did not throw~n", [Name])), error(E, _), format("~w threw ~q~n", [Name,E])).
