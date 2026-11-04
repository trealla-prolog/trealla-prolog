% term_expansion/2's result is expanded again until the hook stops applying or returns it unchanged. A hook returning
% its input hung, and one returning a list holding its input crashed; that one now fails its load with an error.
% Each case is its own file, as the hooks are global and the last one fails the whole load.

:- initialization(main).

load(F) :-
	atom_concat('term_expansion_fixpoint/', F, Path),
	catch(consult(Path), _, true).

t(Label, Goal, Show) :-
	(   catch(Goal, E, true)
	->  (var(E) -> format("~w: ~q~n", [Label, Show]) ; format("~w: raised ~q~n", [Label, E]))
	;   format("~w: failed~n", [Label])
	).

defined(PI, Result) :-
	(current_predicate(PI) -> Result = defined ; Result = missing).

main :-
	load(identity),
	t(identity, findall(X, te_id(X), L1), L1),
	load(conditional),
	t(conditional, findall(X, te_cond(X), L2), L2),
	load(chain),
	t(chain, findall(X, te_chain(X), L3), L3),
	load(bound),
	t(bound, findall(X, te_bound(X), L4), L4),
	load(counting),
	t(counting, (te_calls(N), te_rule(Y)), calls(N)-Y),
	load(list),
	t(list, maplist(defined, [te_list/1, te_more/1, te_after/0], Rs), Rs).
