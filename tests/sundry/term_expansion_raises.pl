% A term whose term_expansion/2 raised was loaded as it was, where SWI drops it; and when the error came from a
% builtin, not throw/1, the hook's query still looked successful, so its unbound result stopped the load with an
% instantiation error and the rest of the file was lost. Either way the error is reported and the term dropped.

:- initialization(main).

term_expansion(te_thrown(X), _) :- X == 2, throw(error(type_error(integer, x), here/0)).
term_expansion(te_builtin(X), _) :- X == 2, atom_length(_, _).

te_thrown(1).
te_thrown(2).
te_thrown(3).

te_builtin(1).
te_builtin(2).
te_builtin(3).

te_last.

main :-
	findall(X, te_thrown(X), L1), writeq(thrown(L1)), nl,
	findall(X, te_builtin(X), L2), writeq(builtin(L2)), nl,
	( catch(te_last, _, fail) -> writeq(rest_loaded) ; writeq(rest_lost) ), nl.
