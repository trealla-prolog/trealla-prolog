% thread_create/3 and message_queue_create/2 read an option's argument without
% checking it had one. Needs real threads, so this test belongs in tests/misc.

:- initialization(main).

check(Name, Goal, Expected) :-
	catch((call(Goal) -> Got = succeeded ; Got = failed), error(Formal, _), Got = Formal),
	(	Got = Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' got '), writeq(Got), nl
	).

main :-
	check(thread_alias, thread_create(true, _, [alias]), domain_error(thread_option, alias)),
	check(thread_alias_two, thread_create(true, _, [alias(a, b)]), domain_error(thread_option, alias(a, b))),
	check(thread_at_exit, thread_create(true, _, [at_exit]), domain_error(thread_option, at_exit)),
	check(thread_detached, thread_create(true, _, [detached]), domain_error(thread_option, detached)),
	check(queue_alias, message_queue_create(_, [alias]), domain_error(queue_option, alias)),
	check(queue_alias_two, message_queue_create(_, [alias(a, b)]), domain_error(queue_option, alias(a, b))),
	check(mutex_alias, mutex_create(_, [alias]), domain_error(mutex_option, alias)),
	check(thread_valid, (thread_create(true, T, [alias(opt_t), detached(false)]), thread_join(T, _)), succeeded),
	check(queue_valid, (message_queue_create(Q, [alias(opt_q)]), message_queue_destroy(Q)), succeeded).
