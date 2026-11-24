% A long list of operator terms reads in linear time: each element is reduced
% when its comma is reached, not the whole list once per operator at the ].

:- initialization(main).

elem(I, A) :-
	K is I mod 3,
	J is I + 1,
	(	K =:= 0 -> format(atom(A), "~d-~d", [I, J])
	;	K =:= 1 -> format(atom(A), "- f(~d)", [I])
	;	format(atom(A), "a = f(~d) * ~d", [I, J])
	).

main :-
	N = 200000,
	findall(A, (between(1, N, I), elem(I, A)), As),
	atomic_list_concat(As, ',', Body),
	atomic_list_concat(['[', Body, ']'], Text),
	read_term_from_atom(Text, L, []),
	length(L, Len),
	nth1(1, L, E1),
	nth1(2, L, E2),
	nth1(3, L, E3),
	last(L, EN),
	writeq([Len, E1, E2, E3, EN]), nl.
