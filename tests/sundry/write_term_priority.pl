% priority(P) brackets a term whose operator's priority is above P, as in SWI and GNU.

:- initialization(main).

w(T, P) :- write(P), write(' '), write_term(T, [priority(P), quoted(true)]), nl.

e(Opts) :- catch(write_term(a, Opts), E, true), writeq(E), nl.

main :-
	w((a:-b), 1200), w((a:-b), 1199), w((a:-b), 999),
	w(a+b, 500), w(a+b, 499), w(a+b, 0),
	w(-a, 200), w(-a, 199), w(\+a, 0), w(- - a, 0), w(-1, 0),
	w((a,b), 1000), w((a,b), 999), w(a=b, 699), w(1-2, 0), w(a:b:c, 199),
	w(f(a+b), 0), w(f((a:-b)), 0), w([a|b], 0), w({a}, 0),
	atom_chars(Neck, [:,-]), w(Neck, 0),
	write_term((a:-b), [priority(999), ignore_ops(true)]), nl,
	write_term((a:-b), [priority(999), fullstop(true)]), nl,
	write_term(user_output, (a:-b), [priority(999)]), nl,
	write_term_to_atom(A, (a:-b), [priority(999)]), writeq(A), nl,
	format("~W~n", [(a:-b), [priority(999)]]),
	writeq((a:-b)), nl,
	e([priority(1201)]), e([priority(-1)]), e([priority(a)]), e([priority(_)]).
