% thread_create/3, message_queue_create/2 and mutex_create/2 answered bad options with the
% stream errors; they now raise SWI-Prolog's terms, and this file passes there too. Needs threads.

:- initialization(main).

check(Name, Goal, Expected) :-
	catch((call(Goal) -> Got = succeeded ; Got = failed), error(Formal, _), Got = Formal),
	(	Got = Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' got '), writeq(Got), nl
	).

main :-
	% SWI-Prolog rejects unknown options only with unknown_option set to error, a flag Trealla doesn't have
	catch(set_prolog_flag(unknown_option, error), _, true),
	check(thread_unknown, thread_create(true, _, [bogus(1)]), domain_error(thread_option, bogus(1))),
	check(thread_alias_not_atom, thread_create(true, _, [alias(1)]), type_error(atom, 1)),
	check(thread_alias_var, thread_create(true, _, [alias(_)]), instantiation_error),
	thread_create(thread_get_message(_), Held, [alias(toe_thread)]),
	check(thread_alias_taken, thread_create(true, _, [alias(toe_thread)]), permission_error(create, thread, toe_thread)),
	check(queue_alias_of_thread, message_queue_create(_, [alias(toe_thread)]), permission_error(create, message_queue, toe_thread)),
	thread_send_message(Held, done),
	thread_join(Held, _),
	check(thread_at_exit_not_callable, thread_create(true, _, [at_exit(1)]), type_error(callable, 1)),
	check(thread_detached_not_boolean, thread_create(true, _, [detached(maybe)]), type_error(bool, maybe)),
	check(queue_unknown, message_queue_create(_, [bogus(1)]), domain_error(queue_option, bogus(1))),
	check(queue_alias_not_atom, message_queue_create(_, [alias(1)]), type_error(atom, 1)),
	message_queue_create(HQ, [alias(toe_queue)]),
	check(queue_alias_taken, message_queue_create(_, [alias(toe_queue)]), permission_error(create, message_queue, toe_queue)),
	message_queue_destroy(HQ),
	check(mutex_unknown, mutex_create(_, [bogus(1)]), domain_error(mutex_option, bogus(1))),
	check(mutex_alias_not_atom, mutex_create(_, [alias(1)]), type_error(atom, 1)),
	mutex_create(HM, [alias(toe_mutex)]),
	check(mutex_alias_taken, mutex_create(_, [alias(toe_mutex)]), permission_error(create, mutex, toe_mutex)),
	mutex_destroy(HM),
	check(thread_valid, (thread_create(true, T, [alias(toe_ok), at_exit(true), detached(false)]), thread_join(T, _)), succeeded).
