% Walking an options list built by append/3, write_term's variable_names check and task_recv/2 read each tail in the
% wrong frame (the head's, or the first cell's), so a list whose cells span frames hung or raised instantiation_error.
% Each case is its own clause because whether the stray frame slot is harmful depends on the clause's variables.

:- initialization(main).

t(Label, Goal) :-
	(   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

vnames_term(S) :- X = f(Y), term_variables(X, _), append(['Y'=Y], [], Ns), with_output_to(string(S), write_term(X, [variable_names(Ns)])).
vnames_two(S) :- term_variables(f(A,B), _), append(['A'=A], ['B'=B], Ns), with_output_to(string(S), write_term(f(A,B), [variable_names(Ns)])).
vnames_three(S) :- term_variables(f(A,B,C), _), append(['A'=A,'B'=B], ['C'=C], Ns), with_output_to(string(S), write_term(f(A,B,C), [variable_names(Ns)])).
vnames_var(S) :- term_variables(Y, _), append(['Y'=Y], [], Ns), with_output_to(string(S), write_term(Y, [variable_names(Ns)])).
recv_timeout :- append([timeout(0.01), timeout(0.01)], [], Os), \+ task_recv(_, Os).

main :-
	t(vnames_term, (vnames_term(S1), S1 == "f(Y)")),
	t(vnames_two, (vnames_two(S2), S2 == "f(A,B)")),
	t(vnames_three, (vnames_three(S3), S3 == "f(A,B,C)")),
	t(vnames_var, (vnames_var(S4), S4 == "Y")),
	t(recv_timeout, recv_timeout).
