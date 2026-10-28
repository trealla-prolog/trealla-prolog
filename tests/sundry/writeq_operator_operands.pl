% writeq/1 puts no spaces round an infix operator after a compound whose last argument
% is a symbol atom, f(/)=g as SWI and Scryer write it; and an operator atom built at
% runtime (atom_codes/2) is bracketed as an operand just as one read from source is,
% so the output reads back.

:- initialization(main).

main :-
	forall(member(T, [f(a) = g, f(/) = g, f(-) = g, g(f(/)) = a, f(:-) = a, {/} = a, f(/, -) = a, a = f(/)]), w(T)),
	atom_codes(B, [0'\\]), atom_codes(S, [0'/]), atom_codes(M, [0'-]), atom_codes(N, [0':, 0'-]),
	forall(member(T, [B/3, S/2, M/1, f(B) = a, S = a, a = M, - M, N/2, [B, S], B-B]), w(T)).

w(T) :-
	with_output_to(atom(A), writeq(T)),
	(	catch(term_to_atom(R, A), _, fail), R == T
	->	format("~w~n", [A])
	;	format("~w DOES NOT READ BACK~n", [A])
	).
