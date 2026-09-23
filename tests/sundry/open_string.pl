:- initialization(main).

main :-
	open_string("héllo\nworld\n", S1), getline(S1, L1), getline(S1, L2), get_char(S1, E1), close(S1),
	atom_chars(A1, L1), atom_codes(A1, Cs1), writeq([Cs1,L2,E1]), nl,
	open_string(`abc. f(X,Y).`, S2), read(S2, T1), read(S2, T2), read(S2, T3), close(S2),
	T2 = f(A, B), (var(A), var(B), A \== B -> V = vars ; V = novars),
	writeq([T1,V,T3]), nl,
	open_string("", S3), get_char(S3, E3), close(S3), writeq(E3), nl,
	open_string(abc, S4), get_char(S4, C4), close(S4), writeq(C4), nl,
	catch(open_string(f(x), _), E5, true), writeq(E5), nl,
	open_string("x", S6), catch(put_char(S6, a), error(permission_error(A6, B6, _), _), true), close(S6), writeq(A6/B6), nl,
	(between(1, 2000, _), open_string("text", S7), getline(S7, _), close(S7), fail ; true),
	writeln(done).
