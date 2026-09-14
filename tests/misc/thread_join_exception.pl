% A thread's uncaught exception was captured against the thread query's frame and copied
% by reference into the joiner, so its variables read the joiner's frames: f(X,Y,X) came back
% ground and with the sharing lost. Import the detached ball instead. Needs threads.

:- initialization(main).

check(Name, Goal, Expected) :-
	catch((call(Goal) -> Got = succeeded ; Got = failed), error(Formal, _), Got = Formal),
	(	Got == Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' got '), writeq(Got), nl
	).

raise(Term) :- throw(Term).
raise_shared(X, Y) :- throw(f(X, Y, X)).

joined(Goal, Status) :-
	thread_create(Goal, T, []),
	thread_join(T, Status).

main :-
	% a structured error term round-trips unchanged
	check(error_term, (joined(raise(error(type_error(integer, foo), ctx)), Se), Se == exception(error(type_error(integer, foo), ctx))), succeeded),
	% a ground term round-trips unchanged
	check(ground_term, (joined(raise(my_error(reason(42))), Sg), Sg == exception(my_error(reason(42)))), succeeded),
	% variables survive: sharing kept, distinct kept, still unbound
	check(shared_vars, (joined(raise_shared(_, _), exception(f(A, B, C))), A == C, A \== B, var(A), var(B)), succeeded),
	% the caught exception's variable is still a usable fresh variable
	check(var_usable, (joined(raise(box(_)), exception(box(V))), var(V), V = filled, V == filled), succeeded),
	% two joined exceptions carry independent variables
	check(independent, (joined(raise(one(_)), exception(one(V1))), joined(raise(two(_)), exception(two(V2))), V1 \== V2, V1 = a, var(V2)), succeeded),
	% a thread that just fails is not an exception
	check(plain_fail, (joined(fail, Sf), Sf == false), succeeded),
	% a thread that succeeds reports true
	check(plain_true, (joined(true, St), St == true), succeeded).
