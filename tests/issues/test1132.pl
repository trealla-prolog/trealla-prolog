% Issue #1132: max_arity is unbounded, but the actual procedure/database
% arity limit is exposed as max_procedure_arity. Exceeding it must throw
% representation_error(max_procedure_arity), not representation_error(max_arity).

:- initialization(main).

main :-
	check(max_arity_flag, current_prolog_flag(max_arity, unbounded)),
	check(max_procedure_arity_flag, (current_prolog_flag(max_procedure_arity, M), M == 255)),
	check(asserta_at_limit, at_limit_ok),
	check(asserta_over_limit, asserta_over_limit_error),
	check(assertz_over_limit, assertz_over_limit_error),
	check(abolish_over_limit, abolish_over_limit_succeeds),
	halt.

at_limit_ok :-
	functor(T, f, 255),
	asserta(T),
	retract(T).

asserta_over_limit_error :-
	current_prolog_flag(max_procedure_arity, M),
	A is M + 1,
	functor(T, f, A),
	catch(asserta(T), error(representation_error(max_procedure_arity), asserta/1), true).

assertz_over_limit_error :-
	current_prolog_flag(max_procedure_arity, M),
	A is M + 1,
	functor(T, f, A),
	catch(assertz(T), error(representation_error(max_procedure_arity), assertz/1), true).

% See issue #1160: no such procedure can exist, so abolishing it is a no-op.

abolish_over_limit_succeeds :-
	current_prolog_flag(max_procedure_arity, M),
	A is M + 1,
	abolish(f/A).

check(Name, Goal) :-
	(   call(Goal)
	->  write(Name), write('_ok'), nl
	;   write('FAIL: '), write(Name), nl
	).
