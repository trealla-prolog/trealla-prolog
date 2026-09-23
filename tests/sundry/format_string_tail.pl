:- initialization(main).

main :-
	atom_chars(ab_cd, L), L = [_,_|T], T = [H|T2],
	format("~s|~s|~n", [T, [H|T2]]),
	format("~s|~n", [[x,y|"z!"]]),
	format("~3s|~n", [[a|"bcdefg"]]),
	format("~s|~n", [[]]).
