% bb_delete/2 returned the backtrackable (bb_b_put) value under a key while actually
% deleting the plain (bb_put) one, so it reported deleting a value that was still there.
% It now returns and deletes the same (non-backtrackable) entry, atomically. Attribute
% preservation across bb_delete is covered by tests/tests/test0104.

:- initialization(main).

r(Name, Goal) :-
	( catch(Goal, E, (write(Name), write(' THREW '), writeq(E), nl, fail)) -> W = ok ; W = failed ),
	write(Name), write(' '), write(W), nl.

main :-
	% plain value: deleted and returned, then gone
	r(plain, (bb_put(k1, val), bb_delete(k1, val), \+ bb_get(k1, _))),
	% a backtrackable value shadowing nothing: bb_delete finds no plain entry, fails, leaves it
	r(backtrackable_only, (bb_b_put(k2, live), \+ bb_delete(k2, _), bb_get(k2, live))),
	% both present: bb_delete returns and removes the PLAIN one, leaving the backtrackable
	r(coexist, (bb_put(k3, plain), bb_b_put(k3, back), bb_delete(k3, plain), bb_get(k3, back))).
