% ~f ~e ~g refused every integer, as type_error(evaluable, 3/0), since 270abdb4 undid #100; SWI and Scryer take
% any number. ~f prints an integer exactly, a bigint included. A rational in ~d gave type_error(evaluable, 1r3/0).
% ~4096f of 1.0e300 printed 302 bytes past what it wrote into a 4096-byte buffer.

:- initialization(main).

t(F, A) :- catch((format(atom(O), F, A), writeq(O)), error(E, _), writeq(E)), nl.

main :-
	t("~2f", [3]), t("~2f", [1+2]), t("~3f|~0f|~f", [-7, 2^70+1, 0]), t("~2f", [2^70]), t("~4f", [1 rdiv 3]),
	t("~e|~2E|~g|~G", [3, 2^70, 1 rdiv 3, -5]), t("~2e", [-(2^70)]),
	t("~2f", [2^2000 rdiv 3]), t("~e", [2^2000]), t("~2f", [abc]), t("~2f", [_]),
	t("~d", [1 rdiv 3]), t("~D", [1 rdiv 3]), t("~*c", [1 rdiv 2, 0'x]),
	format(atom(A), "~4096f", [1.0e300]), atom_length(A, Len), sub_atom(A, _, 5, 0, End), writeq(Len-End), nl.
