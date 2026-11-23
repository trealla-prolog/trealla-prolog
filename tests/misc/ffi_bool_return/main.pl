% bool returns against bools.c, which ../ffi_bool_return.sh builds. A bool return took an extra argument bound
% to 0 or 1; as in Scryer it is now a test, the call succeeding on true and failing on false, with outputs bound
% only when it succeeds. A bool argument, and an evaluable's bool return, are still 0 or 1.

:- initialization(main).

:- foreign_struct(pt, [sint32, sint32]).

:- use_foreign_module('./tmp_ffi_bool_return.so', [
	is_even([sint32], bool),
	always([], bool),
	never([], bool),
	halve([sint32, -sint32], bool),
	same_pt([pt, pt], bool)
	]).

:- '$dlopen'('./tmp_ffi_bool_return.so', 0, H),
	'$register_predicate'(H, flip, [bool], bool),
	'$register_function'(H, is_odd, [sint32], bool).

t(Label, Goal) :-
	(   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

main :-
	t(true_succeeds, is_even(4)),
	t(false_fails, \+ is_even(3)),
	t(no_arguments, (always, \+ never)),
	t(no_extra_argument, catch((is_even(4, _), fail), error(existence_error(procedure, is_even/2), _), true)),
	t(output_bound_on_true, (halve(10, H), H == 5)),
	t(fails_on_false, \+ halve(7, _)),
	t(struct_arguments, (same_pt([pt, 1, 2], [pt, 1, 2]), \+ same_pt([pt, 1, 2], [pt, 2, 1]))),
	t(register_predicate, (flip(0), \+ flip(1))),
	t(evaluable, (X is is_odd(3), X == 1, Y is is_odd(4), Y == 0)).
