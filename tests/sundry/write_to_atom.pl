% write_term_to_atom/3 and write_canonical_to_atom/3 give atoms, write_term_to_chars/3 and write_canonical_to_chars/3 strings

:- initialization(main).

kind(X, K) :- ( atom(X) -> K = atom ; '$string'(X) -> K = string ; K = other ).

main :-
	T = f(a, 'b c', "s", [1,2], -(1), 1+2*3),
	write_term_to_atom(A1, T, [quoted(true)]), kind(A1, K1), format("write_term_to_atom: ~w ~w~n", [K1, A1]),
	write_canonical_to_atom(A2, T, []), kind(A2, K2), format("write_canonical_to_atom: ~w ~w~n", [K2, A2]),
	write_term_to_chars(T, [quoted(true)], C1), kind(C1, K3), format("write_term_to_chars: ~w ~s~n", [K3, C1]),
	write_canonical_to_chars(C2, T, []), kind(C2, K4), format("write_canonical_to_chars: ~w ~s~n", [K4, C2]),
	( write_canonical_to_atom('f(a)', f(a), []) -> R = yes ; R = no ),
	format("write_canonical_to_atom with the atom given: ~w~n", [R]).
