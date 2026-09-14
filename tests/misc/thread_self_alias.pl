% thread_self/1 answered '$thread'(N) even for a thread with an alias, main included, where
% thread_create/3 and SWI-Prolog give the alias. Needs threads.

:- initialization(main).

check(Name, Goal, Expected) :-
	catch((call(Goal) -> Got = succeeded ; Got = failed), error(Formal, _), Got = Formal),
	(	Got = Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' got '), writeq(Got), nl
	).

report_self(Parent) :-
	thread_self(Self),
	thread_send_message(Parent, self_is(Self)).

main :-
	check(main_self, (thread_self(S), S == main), succeeded),
	thread_self(Me),
	thread_create(report_self(Me), T1, [alias(tsa_named)]),
	thread_get_message(self_is(S1)),
	thread_join(T1, _),
	check(aliased_self, (T1 == tsa_named, S1 == tsa_named), succeeded),
	thread_create(report_self(Me), T2, []),
	thread_get_message(self_is(S2)),
	thread_join(T2, _),
	check(unaliased_self, S2 == T2, succeeded).
