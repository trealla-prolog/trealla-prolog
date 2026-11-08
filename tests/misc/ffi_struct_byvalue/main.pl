% Structs by value against structs.c, which ../ffi_struct_byvalue.sh builds. Fields were packed end to end, not
% at C's alignment, and a nested struct took an argument slot of its own, so it and every later argument went
% to the wrong place and its type was the inner struct's. A returned struct did not read a nested one back, and
% a string or pointer field was passed as whatever word its cell held, so an unbound one crashed the C side; it
% takes an address or text, else NULL. A struct name was read as a string whatever it was. A uint64 field past
% 2^63 was packed from a bigint's address and read back negative.

:- initialization(main).

:- foreign_struct(pad, [uint8, double]).
:- foreign_struct(inner, [uint8, double]).
:- foreign_struct(outer, [sint32, inner, uint8]).
:- foreign_struct(named, [uint8, cstr]).
:- foreign_struct(no_c_type, [uint8, -sint]).
:- foreign_struct(u64pair, [uint64, uint64]).

:- '$dlopen'('./tmp_ffi_struct_byvalue.so', 0, H),
	'$register_predicate'(H, pad_sum, [pad], double),
	'$register_predicate'(H, pad_then_int, [pad, sint32], sint32),
	'$register_predicate'(H, two_pads, [pad, pad], double),
	'$register_predicate'(H, make_pad, [uint8, double], pad),
	'$register_predicate'(H, outer_sum, [outer, sint32], double),
	'$register_predicate'(H, outer_out, [outer, -sint32], void),
	'$register_predicate'(H, make_outer, [sint32, uint8, double, uint8], outer),
	'$register_predicate'(H, named_len, [named], sint32),
	'$register_predicate'(H, named_len_or_null, [named], sint32),
	'$register_predicate'(H, greeting, [], ptr),
	'$register_predicate'(H, u64pair_swap, [u64pair], u64pair).

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
	t(output_after_nested, (outer_out([outer, 1, [inner, 2, 0.5], 4], Z), Z == 4)),
	t(nested_return, (make_outer(1, 2, 0.5, 4, O), O = [N1, 1, [N2, 2, 0.5], 4], atom_string(outer, N1), atom_string(inner, N2))),
	t(string_field_argument, (named_len([named, 1, hello], L), L == 6)),
	t(string_field_address, (greeting(A), integer(A), named_len([named, 1, A], L2), L2 == 6)),
	t(unbound_string_field_is_null, (named_len_or_null([named, 1, _], L3), L3 == -1)),
	t(uint64_fields_past_int64, (u64pair_swap([u64pair, 18446744073709551615, 9223372036854775808], U),
		U = [_, 9223372036854775808, 18446744073709551615])),
	t(unbound_struct_argument, catch(pad_sum(_, _), error(instantiation_error, _), true)),
	t(unbound_struct_name, catch(pad_sum([_, 3, 0.5], _), error(instantiation_error, _), true)),
	t(non_atom_struct_name, catch(pad_sum([1, 3, 0.5], _), error(type_error(atom, 1), _), true)),
	t(field_of_no_c_type, \+ '$struct_to_pointer'([no_c_type, 1, 2], _)).
