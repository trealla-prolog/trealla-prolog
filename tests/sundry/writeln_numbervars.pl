% writeln/1,2 write as write/1,2 do, then a newline, as in SWI, so
% '$VAR'(N) prints as a variable name. They printed $VAR(0).

:- initialization(main).

main :-
	T = f('$VAR'(0), '$VAR'(27), 'a b'),
	writeln(T),
	writeln(user_output, T),
	write(T), nl,
	with_output_to(atom(A), writeln(T)),
	write(A).
