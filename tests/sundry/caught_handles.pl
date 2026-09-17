% Engine handles print as '$engine'(N), and a caught ball gave them back as that
% compound rather than the handle, where '$stream'(N) was already restored.

:- initialization(main).

same(Name, A, B) :-
	(	A == B
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' differs: '), writeq(A), nl
	).

main :-
	engine_create(x, true, E),
	catch(throw(ball(E)), ball(E1), true),
	same(thrown_engine, E1, E),
	engine_destroy(E),
	catch(engine_next(E, _), error(existence_error(_, E2), _), true),
	same(engine_in_error, E2, E).
