% Engine predicates raised the errors of the streams engines are built on, or not_an_engine and
% no_data types of their own; they now raise SWI-Prolog's terms, and this file passes there too.

:- initialization(main).

check(Name, Goal, Expected) :-
	catch((call(Goal) -> Got = succeeded ; Got = failed), error(Formal, _), Got = Formal),
	(	Got = Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' got '), writeq(Got), nl
	).

main :-
	engine_create(x, true, E),
	engine_destroy(E),
	check(next_destroyed, engine_next(E, _), existence_error(engine, E)),
	check(post_destroyed, engine_post(E, t), existence_error(engine, E)),
	check(destroy_destroyed, engine_destroy(E), existence_error(engine, E)),
	check(next_not_engine, engine_next(user_input, _), existence_error(engine, user_input)),
	check(post_not_engine, engine_post(user_input, t), existence_error(engine, user_input)),
	check(destroy_not_engine, engine_destroy(user_input), existence_error(engine, user_input)),
	check(next_unknown, engine_next(no_such_engine, _), existence_error(engine, no_such_engine)),
	check(next_var, engine_next(_, _), instantiation_error),
	check(destroy_var, engine_destroy(_), instantiation_error),
	check(next_integer, engine_next(42, _), type_error(engine, 42)),
	check(next_compound, engine_next(f(x), _), type_error(engine, f(x))),
	engine_create(x, true, E2, [alias(eng)]),
	check(alias_taken, engine_create(y, true, _, [alias(eng)]), permission_error(create, engine, eng)),
	check(alias_taken_atom, engine_create(y, true, eng), permission_error(create, engine, eng)),
	check(alias_not_atom, engine_create(y, true, _, [alias(1)]), type_error(atom, 1)),
	check(stack_option, (engine_create(y, true, E4, [stack(1000000)]), engine_destroy(E4)), succeeded),
	% SWI-Prolog rejects unknown options only with unknown_option set to error, a flag Trealla doesn't have
	catch(set_prolog_flag(unknown_option, error), _, true),
	check(unknown_option, engine_create(y, true, _, [bogus(1)]), domain_error(engine_option, bogus(1))),
	check(unknown_atom_option, engine_create(y, true, _, [foo]), domain_error(engine_option, foo)),
	engine_destroy(E2),
	engine_create(F, catch(engine_fetch(_), error(F, _), true), E3),
	engine_next(E3, F3),
	(	F3 = existence_error(term, delivery, E3)
	->	write('fetch_no_data ok'), nl
	;	write('fetch_no_data got '), writeq(F3), nl
	),
	engine_destroy(E3).
