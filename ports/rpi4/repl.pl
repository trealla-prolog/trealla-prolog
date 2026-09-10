% An interactive toplevel over the serial console.
%
% The board has no other way in and no reset but the power lead, so nothing
% here may leave the loop: a syntax error, an unknown predicate or a thrown
% ball all report and carry on. Only end_of_file halts, and the bare-metal
% console never produces one - it is there so the same file can be driven
% from a pipe on the host.
%
% One solution per goal. Offering `;` would mean reading a key between
% answers, which is more machinery than a first bring-up console needs.

:- initialization(main).

main :-
	write('Trealla Prolog - bare metal console'), nl,
	repeat,
	  once(catch(step, Error, complain(Error))),
	fail.

step :-
	write('?- '),
	flush_output,
	read_term(user_input, Goal, [variable_names(Names)]),
	(	Goal == end_of_file
	->	nl, halt
	;	run(Goal, Names)
	).

% A thrown ball reports only itself - it did not fail, it never finished.

run(Goal, Names) :-
	catch(solve(Goal, Names), Error, complain(Error)).

solve(Goal, Names) :-
	(	call(Goal)
	->	report(Names)
	;	write(false), nl
	).

report([]) :- !, write(true), nl.
report(Names) :- bindings(Names).

bindings([]).
bindings([Name=Value|Rest]) :-
	write(Name), write(' = '), writeq(Value), nl,
	bindings(Rest).

complain(Error) :-
	write('% '), writeq(Error), nl.
