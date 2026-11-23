% An evaluable's name and arity is free for a predicate, as in SWI, Scryer and GNU: asserta, retract, retractall,
% abolish and listing raised permission errors for abs/1 or max/2 as if they were static builtins. Arithmetic is
% unchanged, and a real builtin predicate is still refused.

:- initialization(main).

t(Label, Goal) :-
	(   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

main :-
	t(asserta, (asserta(abs(1)), abs(1))),
	t(assertz, (assertz(max(1, 2)), max(1, 2))),
	t(asserta_ref, (asserta(sign(x), Ref), sign(x), erase(Ref))),
	t(assertz_ref, (assertz(sign(y), Ref2), sign(y), erase(Ref2))),
	t(clause, clause(abs(1), true)),
	t(listing, (with_output_to(string(S), listing(abs/1)), sub_string(S, _, _, _, "abs(1)"))),
	t(retract, (retract(abs(1)), \+ abs(_))),
	t(retractall_defined, (retractall(max(_, _)), \+ max(_, _))),
	t(retractall_new, retractall(min(_, _))),
	t(abolish, abolish(sign/1)),
	t(arithmetic, (X is abs(-3) + max(1, 2) + min(4, 5), X == 9)),
	t(builtin_refused, catch((asserta(atom_length(a, 1)), fail), error(permission_error(modify, static_procedure, atom_length/2), _), true)),
	t(builtin_retract_refused, catch((retract(atom_length(_, _)), fail), error(permission_error(modify, static_procedure, atom_length/2), _), true)).
