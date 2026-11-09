% dynamic/1 called as a goal raised existence_error, being known only as a directive; it now
% takes what the directive takes, as SWI and Ciao do: N/A, N//A, M:Spec, conjunctions, lists
% and Spec as Opts. An error raised under call/1 was caught, then the walk went on reading the
% goal the catch had discarded. multifile/1 missed an ISO builtin whose name was built at run time.

:- initialization(main).

st(1).

main :-
	check(plain, (dynamic(g1/1), assertz(g1(a)), g1(a))),
	check(conjunction, (dynamic((g2/1, g3/2)), predicate_property(g3(_,_), dynamic))),
	check(list, (dynamic([g4/1, g5/0]), predicate_property(g5, dynamic))),
	check(empty_list, dynamic([])),
	check(qualified, (dynamic(gm:g6/1), predicate_property(gm:g6(_), dynamic))),
	check(qualified_conjunction, (dynamic(gm:(g7/1, g8/1)), predicate_property(gm:g8(_), dynamic))),
	check(only_in_its_module, \+ predicate_property(user:g8(_), dynamic)),
	check(dcg, (dynamic(g9//1), predicate_property(g9(_,_,_), dynamic))),
	check(from_variables, (N = g10, A = 2, M = gm2, dynamic(M:N/A), predicate_property(gm2:g10(_,_), dynamic))),
	check(built_name, (atom_concat(g, '11', N2), dynamic(N2/1), assertz(g11(b)), g11(b))),
	check(incremental, (dynamic(g12/1 as incremental), predicate_property(g12(_), incremental))),
	check(already_dynamic, dynamic(g1/1)),
	check(metacall, (G = dynamic(g13/1), call(G), predicate_property(g13(_), dynamic))),
	error(dynamic(_), instantiation_error),
	error(dynamic(g/_), instantiation_error),
	error(dynamic(_/1), instantiation_error),
	error(dynamic(_:g/1), instantiation_error),
	error(dynamic([g/1|_]), instantiation_error),
	error(dynamic(foo), type_error(predicate_indicator, foo)),
	error(dynamic((g14/1, foo)), type_error(predicate_indicator, foo)),
	error(dynamic([g15/1|bar]), type_error(predicate_indicator, bar)),
	error(dynamic(1/1), type_error(atom, 1)),
	error(dynamic(1:g/1), type_error(atom, 1)),
	error(dynamic(g/a), type_error(integer, a)),
	error(dynamic(g/(-1)), domain_error(not_less_than_zero, -1)),
	error((B is 2^70, dynamic(g/B)), representation_error(max_procedure_arity)),
	error(dynamic(g/256), representation_error(max_procedure_arity)),
	error(dynamic(g/1 as bar), domain_error(predicate_option, bar)),
	error(dynamic(g/1 as (incremental, bar)), domain_error(predicate_option, bar)),
	error(dynamic(atom_length/2), permission_error(modify, static_procedure, atom_length/2)),
	error(dynamic(st/1), permission_error(modify, static_procedure, st/1)),
	error((G2 = dynamic(g/1 as bar), call(G2)), domain_error(predicate_option, bar)),
	error((G3 = dynamic(g/1 as 3), G3), domain_error(predicate_option, 3)),
	error((atom_concat(atom_, length, N3), dynamic(N3/2)), permission_error(modify, static_procedure, atom_length/2)),
	error((atom_concat(atom_, length, N4), multifile(N4/2)), permission_error(modify, static_procedure, atom_length/2)).

check(Name, Goal) :-
	(	\+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->	format("~w ok~n", [Name])
	;	format("~w FAILED~n", [Name])
	).

error(Goal, Expected) :-
	catch((Goal, R = succeeded), error(E, _), R = E),
	(	subsumes_term(Expected, R)
	->	format("~q ok~n", [Expected])
	;	format("~q: expected ~q, got ~q~n", [Goal, Expected, R])
	).
