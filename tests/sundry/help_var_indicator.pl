% help/1, help/2, module_help/2,3 and source_info/2 read the two halves of
% Name/Arity without dereferencing them, so an indicator built from variables
% was refused as type_error(predicate_indicator, ...) naming a good indicator.
% Each now answers exactly as it does for the literal indicator.

:- initialization(main).

:- use_module(library(lists)).

% A static predicate for source_info/2 to find.

t(_, _).

main :-
	check(help_1, same_output(help(listing/1), (N = listing, A = 1, help(N/A)))),
	check(help_2, same_output(help(listing/1, swi), (N = listing, A = 1, help(N/A, swi)))),
	check(module_help_2, same_output(module_help(lists, member/2),
		(N = member, A = 2, module_help(lists, N/A)))),
	check(module_help_3, same_output(module_help(lists, member/2, swi),
		(N = member, A = 2, module_help(lists, N/A, swi)))),
	check(source_info, (source_info(t/2, I0), N = t, A = 2, source_info(N/A, I1), I0 == I1)),
	check(bad_name_refused, (N = 1, A = 1, catch((help(N/A), fail),
		error(type_error(predicate_indicator, 1/1), _), true))),
	check(bad_arity_refused, (N = listing, A = x, catch((help(N/A), fail),
		error(type_error(predicate_indicator, listing/x), _), true))).

same_output(Literal, Built) :-
	with_output_to(string(S0), Literal),
	with_output_to(string(S1), Built),
	S0 \== "", S0 == S1.

check(Name, Goal) :-
	(	\+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->	format("~w ok~n", [Name])
	;	format("~w FAILED~n", [Name])
	).
