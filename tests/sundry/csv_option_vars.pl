% The CSV predicates read every option's argument without dereferencing it, so
% trim(T), numbers(N) and the rest were ignored when the value came from a
% variable. Each now parses exactly as with the literal option.

:- initialization(main).

main :-
	check(trim, (parse_csv_line(" a , b ", L0, [trim(true)]),
		T = true, parse_csv_line(" a , b ", L1, [trim(T)]), L0 == L1)),
	check(numbers, (parse_csv_line("1,2", L0, [numbers(true)]),
		N = true, parse_csv_line("1,2", L1, [numbers(N)]), L0 == L1)),
	check(trim_changes_something, (parse_csv_line(" a , b ", L0, []),
		T = true, parse_csv_line(" a , b ", L1, [trim(T)]), L0 \== L1)).

check(Name, Goal) :-
	(	\+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->	format("~w ok~n", [Name])
	;	format("~w FAILED~n", [Name])
	).
