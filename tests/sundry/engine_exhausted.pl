% Asking a finished engine for another answer failed again and again, where
% SWI-Prolog raises existence_error(engine, E) and still lets it be destroyed.

:- initialization(main).

show(threw(error(F, _))) :- !, ( F =.. [N, T|_] -> write(threw(N, T)) ; write(threw(F)) ).
show(R) :- writeq(R).

try(Name, G) :-
	catch((call(G) -> R = true ; R = false), E, R = threw(E)),
	write(Name), write(': '), show(R), nl.

main :-
	engine_create(x, true, E1),
	try(det_next1, engine_next(E1, _)),
	try(det_next2, engine_next(E1, _)),
	try(det_next3, engine_next(E1, _)),
	try(det_is_engine, is_engine(E1)),
	try(det_post, engine_post(E1, hi)),
	try(det_destroy, engine_destroy(E1)),
	engine_create(X, member(X, [a,b]), E2),
	try(nondet_next1, engine_next(E2, _)),
	try(nondet_next2, engine_next(E2, _)),
	try(nondet_next3, engine_next(E2, _)),
	try(nondet_next4, engine_next(E2, _)),
	try(nondet_destroy, engine_destroy(E2)),
	engine_create(x, fail, E3),
	try(fail_next1, engine_next(E3, _)),
	try(fail_next2, engine_next(E3, _)),
	try(fail_destroy, engine_destroy(E3)),
	engine_create(x, engine_yield(a), E4),
	try(yield_next1, engine_next(E4, _)),
	try(yield_next2, engine_next(E4, _)),
	try(yield_next3, engine_next(E4, _)),
	try(yield_next4, engine_next(E4, _)),
	try(yield_destroy, engine_destroy(E4)).
