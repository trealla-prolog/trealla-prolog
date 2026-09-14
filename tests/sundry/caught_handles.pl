% Map and engine handles print as '$map'(N), and a caught ball gave them back as that
% compound rather than the handle, where '$stream'(N) was already restored.

:- initialization(main).

same(Name, A, B) :-
	(	A == B
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' differs: '), writeq(A), nl
	).

main :-
	map_create(M, []),
	catch(throw(ball(M)), ball(M1), true),
	same(thrown_map, M1, M),
	map_set(M1, k, v),
	map_get(M, k, V),
	same(caught_map_works, V, v),
	map_close(M),
	catch(map_count(M, _), error(existence_error(_, M2), _), true),
	same(map_in_error, M2, M),
	engine_create(x, true, E),
	catch(throw(ball(E)), ball(E1), true),
	same(thrown_engine, E1, E),
	engine_destroy(E),
	catch(engine_next(E, _), error(existence_error(_, E2), _), true),
	same(engine_in_error, E2, E).
