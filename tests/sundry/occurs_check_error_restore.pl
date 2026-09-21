% unify_with_occurs_check/2 restored occurs_check(error) as true, so later cyclic unifications failed instead of throwing.

:- initialization(main).

cyclic(Name) :-
	catch((Y = f(Y) -> R = succeeded ; R = failed), error(E, _), R = threw(E)),
	write(Name:R), nl.

main :-
	set_prolog_flag(occurs_check, error),
	cyclic(before),
	(unify_with_occurs_check(_, _) -> true ; true),
	cyclic(after_success),
	set_prolog_flag(occurs_check, error),
	(unify_with_occurs_check(X, f(X)) -> R = succeeded ; R = failed),
	write(uwoc_cyclic:R), nl,
	cyclic(after_failure),
	set_prolog_flag(occurs_check, false).
