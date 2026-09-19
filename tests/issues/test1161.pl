% Issue #1161: the tail after || is a priority 0 term, so || binds before any op.

:- initialization(main).

:- op(1, xfy, op).

t(S) :-
	catch((read_term_from_atom(S, T, []), write_canonical(T)), error(E, _), print(E)),
	nl.

main :-
	t('"abc"||1 op 2'),
	t('"abc"||(1 op 2)'),
	t('"abc"||1 op 2 op 3'),
	t('"abc"||a-1'),
	t('"abc"||- a'),
	t('"abc"||').
