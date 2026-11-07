% Structs by value against structs.c, which ../ffi_struct_byvalue.sh builds. Fields were packed end to end, not
% at C's alignment, and a nested struct took an argument slot of its own, so it and every later argument went
% to the wrong place and its type was the inner struct's.

:- initialization(main).

:- foreign_struct(pad, [uint8, double]).
:- foreign_struct(inner, [uint8, double]).
:- foreign_struct(outer, [sint32, inner, uint8]).

:- '$dlopen'('./tmp_ffi_struct_byvalue.so', 0, H),
	'$register_predicate'(H, pad_sum, [pad], double),
	'$register_predicate'(H, pad_then_int, [pad, sint32], sint32),
	'$register_predicate'(H, two_pads, [pad, pad], double),
	'$register_predicate'(H, make_pad, [uint8, double], pad),
	'$register_predicate'(H, outer_sum, [outer, sint32], double),
	'$register_predicate'(H, outer_out, [outer, -sint32], void).

t(Label, Goal) :-
	(   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

main :-
	t(padded_argument, (pad_sum([pad, 3, 0.5], R1), R1 =:= 3.5)),
	t(argument_after_struct, (pad_then_int([pad, 3, 0.5], 4, R2), R2 == 43)),
	t(two_structs, (two_pads([pad, 1, 0.5], [pad, 2, 0.25], R3), R3 =:= 24.0)),
	t(padded_return, (make_pad(7, 2.25, S), S = [N, 7, 2.25], atom_string(pad, N))),
	t(nested_argument, (outer_sum([outer, 1, [inner, 2, 0.5], 4], 5, R4), R4 =:= 5007.5)),
	t(output_after_nested, (outer_out([outer, 1, [inner, 2, 0.5], 4], Z), Z == 4)).
