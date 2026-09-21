% Two things a sweep against SWI turned up, plus one that it got wrong.
%
%   round/1 rounds a tie towards positive infinity: round(-2.5) is -2, not -3. SWI rounds
%   away from zero and disagrees, but the ISO examples do not cover negative halves and
%   the ECLiPSe tests the Logtalk conformance suite carries expect this. Pinned here
%   because it looks like a bug and is not.
%
%   atom_number/2 let number_codes/2's syntax error out, so the usual
%   ( atom_number(A,N) -> ... ; ... ) raised instead of taking the other branch.
%
%   an evaluable that is not one named the accumulator rather than itself, printing
%   type_error(evaluable, dummy/0).

:- initialization(main).

chk(Name, Got, Want) :-
	(  Got == Want -> true ; format("~w: got ~q wanted ~q~n", [Name, Got, Want]), fail ).

val(G, V) :- ( catch(G, error(E,_), V = err(E)) -> true ; V = failed ), ( var(V) -> V = unbound ; true ).

main :-
	(	val(X1 is round(-2.5), X1), chk(round_neg, X1, -2),
		val(X2 is round(-3.5), X2), chk(round_neg_half, X2, -3),
		val(X3 is round(2.5), X3), chk(round_pos, X3, 3),
		val(atom_number(foo, X4), X4), chk(atom_number_atom, X4, failed),
		val(atom_number('12abc', X5), X5), chk(atom_number_partial, X5, failed),
		val(atom_number('42', X6), X6), chk(atom_number_ok, X6, 42),
		val(_ is integer(2.5), X7), chk(evaluable_name, X7, err(type_error(evaluable, integer/1)))
	->	format("arith_atom_edges: ok~n")
	;	true
	).
