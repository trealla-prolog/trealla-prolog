% A full stop straight after symbol characters joined them into one token: portray_clause(+) wrote +.
% and portray_clause(a = '...') wrote a= ...., neither of which reads back. SWI and Scryer write + .

:- initialization(main).

main :-
	portray_clause(+),
	portray_clause(a = '...'),
	portray_clause(f(x) - (-)),
	write_term(a = #, [fullstop(true)]), nl,
	write_term('\\', [fullstop(true), nl(true)]),
	portray_clause(a = 'x.'),
	portray_clause(a = "+").
