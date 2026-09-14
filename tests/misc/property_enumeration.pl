% The thread, message queue and mutex property enumerations began after id 0, skipping the
% main thread, and with nothing left to find they succeeded once with nothing bound. They also
% named an object by number even when it had an alias. Needs threads.

:- initialization(main).

check(Name, Goal, Expected) :-
	catch((call(Goal) -> Got = succeeded ; Got = failed), error(Formal, _), Got = Formal),
	(	Got = Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' got '), writeq(Got), nl
	).

main :-
	check(thread_aliases, (findall(A, thread_property(_, alias(A)), As0), As0 == [main]), succeeded),
	check(thread_statuses, (findall(T-S, thread_property(T, status(S)), Ss), Ss == [main-running]), succeeded),
	check(first_alias_bound, (thread_property(T0, alias(A0)), T0 == main, A0 == main), succeeded),
	check(enumerated_main_is_thread_self, (thread_self(Me), thread_property(T1, alias(main)), T1 == Me), succeeded),
	thread_create(thread_get_message(_), Held, [alias(pe_held)]),
	check(thread_aliases_two, (findall(A, thread_property(_, alias(A)), As), msort(As, Sorted), Sorted == [main, pe_held]), succeeded),
	check(thread_named_by_alias, (thread_property(T2, alias(pe_held)), T2 == Held), succeeded),
	thread_send_message(Held, done),
	thread_join(Held, _),
	check(no_queue_aliases, (findall(A, message_queue_property(_, alias(A)), Qs0), Qs0 == []), succeeded),
	message_queue_create(Q, [alias(pe_queue)]),
	check(queue_aliases, (findall(A, message_queue_property(_, alias(A)), Qs), Qs == [pe_queue]), succeeded),
	check(queue_named_by_alias, (message_queue_property(Q2, alias(pe_queue)), Q2 == Q), succeeded),
	message_queue_destroy(Q),
	check(mutex_no_unbound_solution, \+ (mutex_property(M0, alias(_)), var(M0)), succeeded),
	mutex_create(M, [alias(pe_mutex)]),
	check(mutex_aliases, (findall(A, mutex_property(_, alias(A)), Ms), memberchk(pe_mutex, Ms)), succeeded),
	check(mutex_named_by_alias, (mutex_property(M2, alias(pe_mutex)), M2 == M), succeeded),
	mutex_destroy(M).
