% An instantiation error from an unbound argument names the predicate in its
% context, as SWI and Scryer do, not the type that was expected there.

:- initialization(main).

main :-
	forall(member(G, [atom_codes(_,_), atom_length(_,_), functor(_,_,_), arg(_,f(a),_),
			_ is _, atom_chars(_,[a|_]), number_codes(_,_), sub_atom(_,_,_,_,_),
			assertz(_), call(_), msort(_,_), succ(_,_), atom_to_term(_,_,_)]),
		(	catch(G, error(E, C), true)
		->	writeq(E-C), nl
		;	writeq(G-no_error), nl
		)).
