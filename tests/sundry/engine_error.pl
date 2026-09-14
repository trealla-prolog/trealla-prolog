% An error raised inside an engine's goal unwinds the engine query to
% its bottom barrier, and that barrier was pushed before the goal was
% installed - so it restores a NULL instruction pointer. start() then
% ran proceed() on it and segfaulted. Any builtin-thrown error did it;
% an explicit throw/1 happened to survive.
%
% engine_next/2 now rethrows the error to its caller, as in SWI-Prolog,
% where it used to print it and fail. Execution still has to carry on -
% a pass needs both the output and a clean exit.

:- initialization(main).

check(Name, Goal) :-
	(	catch(call(Goal), E, (write(Name), write(' THREW '), writeq(E), nl, fail))
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' FAILED'), nl
	).

% engine_next/2 rethrows Ball, the call after finds the engine finished,
% and the engine is still well enough formed to destroy

erring(Goal, Ball) :-
	engine_create(x, Goal, E),
	catch((engine_next(E, _) -> Got = answer ; Got = failed), B, Got = threw(B)),
	catch((engine_next(E, _) -> Next = answer ; Next = failed), error(existence_error(engine, _), _), Next = finished),
	engine_destroy(E),
	Got = threw(Ball),
	Next == finished.

evaluable :- erring(_ is foo + 1, error(type_error(evaluable, foo/0), _)).

type :- erring(atom_length(1, _), error(type_error(atom, 1), _)).

no_data :- erring(engine_fetch(_), error(existence_error(term, delivery, _), _)).

explicit :- erring(throw(boom), boom).

% variables in the ball come back fresh and still distinct

fresh_vars :-
	erring(throw(f(_, g(_))), f(X, g(Y))),
	var(X), var(Y), X \== Y.

% an inner engine's error that the outer goal doesn't catch reaches the outer engine's caller

nested :- erring((engine_create(y, throw(deep), E2), engine_next(E2, _)), deep).

% an answer already yielded is still delivered, and the error on the
% way to the next one is rethrown there

after_yield :-
	engine_create(x, (engine_yield(one), _ is bar + 1), E),
	engine_next(E, A),
	A == one,
	catch((engine_next(E, _) -> Got = answer ; Got = failed), B, Got = threw(B)),
	engine_destroy(E),
	Got = threw(error(type_error(evaluable, bar/0), _)).

% an error the goal catches itself stays inside the engine

caught_inside :-
	engine_create(r(C), catch(throw(inner), C, true), E),
	engine_next(E, R),
	engine_destroy(E),
	R == r(inner).

main :-
	check(evaluable, evaluable),
	check(type, type),
	check(no_data, no_data),
	check(explicit, explicit),
	check(fresh_vars, fresh_vars),
	check(nested, nested),
	check(after_yield, after_yield),
	check(caught_inside, caught_inside).
