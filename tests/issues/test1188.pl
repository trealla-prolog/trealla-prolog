% Issue #1188: seq//1 answers any proper list in one step when the rest is [], not only a compact string.

:- initialization(main).

main :-
	check(cons_list, (phrase(seq(Xs1), [a,b,c]), Xs1 == [a,b,c])),
	check(compact_string, (phrase(seq(Xs2), "abc"), Xs2 == [a,b,c])),
	check(bound_xs, phrase(seq([a,b]), [a,b])),
	check(bound_xs_differs, \+ phrase(seq([a,x]), [a,b])),
	check(partial_xs, (Xs3 = [a|T3], phrase(seq(Xs3), [a,b,c]), T3 == [b,c])),
	check(variable_elements, (phrase(seq(Xs4), [A,B]), Xs4 == [A,B])),
	check(det, (call_det(phrase(seq(_), [a,b,c]), D5), D5 == true)),
	check(rest_open, (findall(Xs6-R6, phrase(seq(Xs6), [a,b], R6), L6), L6 == [[]-[a,b],[a]-[b],[a,b]-[]])),
	check(partial_list, (findall(Xs7, limit(3, phrase(seq(Xs7), [a|_])), L7), L7 = [[a]|_])),
	check(not_a_list, \+ phrase(seq(_), [a|b])),
	check(not_walked, (length(L8, 100000), maplist(=(a), L8), statistics(inferences, I0),
		phrase(seq(_), L8), statistics(inferences, I1), I1 - I0 < 1000)).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
