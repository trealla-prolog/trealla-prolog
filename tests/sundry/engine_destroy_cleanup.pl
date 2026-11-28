% engine_destroy/1 freed an engine with its goal still open and skipped the
% cleanups it left. As in SWI they now run, as for a cut: newest first,
% seeing the goal's bindings, past no catch/3 Recovery, with an exception
% from one dropped and the older ones still run. A running engine,
% perhaps the caller itself, can't be destroyed or resumed.

:- initialization(main).

show(threw(error(F, _))) :- !, ( F =.. [N, A|_] -> write(threw(N, A)) ; write(threw(F)) ).
show(R) :- writeq(R).

try(Name, G) :-
	catch((call(G) -> R = true ; R = false), E, R = threw(E)),
	write(Name), write(': '), show(R), nl.

log(X) :- write('  '), writeq(X), nl.

nested(X-Y) :-
	setup_call_cleanup(true, member(X, [1,2]), log(c1)),
	member(_, [a,b]),
	setup_call_cleanup(true, member(Y, [1,2]), log(c2)).

open_then(G, E) :- engine_create(x, G, E), engine_next(E, _).

% A cleanup that backtracks over a cut which runs a nested cleanup.

cut_inner :- setup_call_cleanup(true, member(_, [1,2]), log(nested)), !.
cut_outer :- member(W, [p,q]), log(W), cut_inner, W == q.

main :-
	open_then(nested(_), E1),
	try(nested, engine_destroy(E1)),
	engine_create(x, setup_call_cleanup(true, (engine_yield(y), engine_yield(z)), log(yielded)), E2),
	engine_next(E2, _),
	try(at_yield, engine_destroy(E2)),
	engine_create(x, setup_call_cleanup(true, true, log('NOT_HERE')), E3),
	try(never_started, engine_destroy(E3)),
	open_then(setup_call_cleanup(true, member(_, [1]), log(exhausted)), E4),
	( engine_next(E4, _) -> true ; true ),
	try(exhausted, engine_destroy(E4)),
	open_then(setup_call_cleanup(true, true, log(det)), E5),
	try(det, engine_destroy(E5)),
	engine_create(V, setup_call_cleanup(true, member(V, [1,2]), log(v(V))), E6),
	engine_next(E6, _),
	try(bindings, engine_destroy(E6)),
	open_then(setup_call_cleanup(true, member(_, [1,2]), (member(W, [p,q]), log(W), W == q)), E7),
	try(backtracking_cleanup, engine_destroy(E7)),
	open_then(setup_call_cleanup(true, member(_, [1,2]), cut_outer), E7b),
	try(nested_cut_cleanup, engine_destroy(E7b)),
	open_then((catch(setup_call_cleanup(true, member(_, [1,2]), (log(throwing), throw(oops))), B, log(recovered(B))), log(answered)), E8),
	try(throwing_cleanup, engine_destroy(E8)),
	open_then((setup_call_cleanup(true, member(_, [1,2]), log(older)), setup_call_cleanup(true, member(_, [1,2]), throw(oops))), E9),
	try(older_after_throw, engine_destroy(E9)),
	open_then(setup_call_cleanup(true, member(_, [1,2]), (engine_self(_) -> log(self) ; log(no_self))), E10),
	try(self_in_cleanup, engine_destroy(E10)),
	open_then(setup_call_cleanup(true, member(_, [1,2]), catch(engine_yield(w), error(Y, _), log(Y))), E11),
	try(yield_in_cleanup, engine_destroy(E11)),
	open_then(setup_call_cleanup(true, member(_, [1,2]), log(closed)), E12),
	try(close, close(E12)),
	engine_create(x, (engine_self(S), try(destroy_self, engine_destroy(S)), try(next_self, engine_next(S, _))), E13),
	try(still_running, engine_next(E13, _)),
	try(destroyed_after, engine_destroy(E13)).
