% Issue #1160: a procedure can't have an arity above max_procedure_arity, so
% abolishing one is an empty operation, not a representation_error.

:- initialization(main).

main :-
	check(huge_arity, abolish(p/10000)),
	check(dcg_indicator, abolish(p//10000)),
	check(bigint_arity, abolish(p/1267650600228229401496703205376)),
	check(options, abolish(p/10000, [])),
	% the other errors still stand
	check(negative_arity, catch(abolish(p/(-1)), error(domain_error(not_less_than_zero, -1), _), true)),
	check(bad_option, catch(abolish(p/10000, [bogus(x)]), error(domain_error(stream_option, bogus(x)), _), true)),
	% and abolishing an ordinary predicate still works
	check(ordinary, (assertz(q(1)), abolish(q/1), \+ catch(q(_), _, fail))),
	halt.

check(Name, Goal) :-
	(   call(Goal)
	->  write(Name), write('_ok'), nl
	;   write('FAIL: '), write(Name), nl
	).
