% The main thread's alias, main, went into an alias map created only afterwards, so main
% named no thread: messages to it failed and another thread or queue could take it. Needs threads.

:- initialization(main).

check(Name, Goal, Expected) :-
	catch((call(Goal) -> Got = succeeded ; Got = failed), error(Formal, _), Got = Formal),
	(	Got = Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' got '), writeq(Got), nl
	).

child(Parent) :-
	catch((thread_send_message(main, from_child), R = sent), error(F, _), R = F),
	thread_send_message(Parent, child_result(R)).

main :-
	check(is_thread_main, is_thread(main), succeeded),
	check(main_alias_property, thread_property(main, alias(main)), succeeded),
	check(main_running, thread_property(main, status(running)), succeeded),
	check(send_to_main, (thread_send_message(main, hi), thread_get_message(hi)), succeeded),
	thread_self(Me),
	thread_create(child(Me), T, []),
	thread_get_message(child_result(R)),
	thread_join(T, _),
	check(child_sends_to_main, R == sent, succeeded),
	check(child_message_arrived, (thread_peek_message(from_child), thread_get_message(from_child)), succeeded),
	check(thread_alias_main, thread_create(true, _, [alias(main)]), permission_error(create, thread, main)),
	check(queue_alias_main, message_queue_create(_, [alias(main)]), permission_error(create, message_queue, main)).
