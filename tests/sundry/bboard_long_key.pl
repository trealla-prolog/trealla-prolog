% A blackboard key longer than the internal key buffer was silently truncated, so two
% different long keys could collide; an over-long key now throws representation_error.

:- initialization(main).

long_atom(N, A) :-
	length(Codes, N),
	maplist(=(0'a), Codes),
	atom_codes(A, Codes).

check(Name, Goal, Expected) :-
	catch((call(Goal) -> Got = succeeded ; Got = failed), error(Formal, _), Got = Formal),
	(	Got == Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' got '), writeq(Got), nl
	).

main :-
	% a short key round-trips as before
	check(short_key, (bb_put(k, 42), bb_get(k, 42)), succeeded),
	% an over-long key is rejected rather than silently truncated
	long_atom(2000, K),
	check(long_put, bb_put(K, v), representation_error(bb_key)),
	check(long_get, bb_get(K, _), representation_error(bb_key)),
	check(long_b_put, bb_b_put(K, v), representation_error(bb_key)),
	check(long_delete, bb_delete(K, _), representation_error(bb_key)).
