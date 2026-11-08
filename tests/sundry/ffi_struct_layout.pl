% '$struct_to_pointer'/2 packed fields end to end, without the padding C puts before a field to align it or
% after the last to round the struct up, so a struct with a gap came out the wrong shape and short. Each is
% checked against the same struct with its padding spelled out as zeroed fields. Fields are 4 bytes at most,
% as wider ones are only 4-aligned on 32-bit x86. A nested struct or string field was skipped; a string's text
% now goes in the same block, which strsep() reads through, and an unbound one is NULL. A struct name that was
% no atom was read as a string anyway. Skipped where no libc opens.

:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(ffi_bound/0).
:- dynamic(has_strsep/0).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail),
		catch(foreign_struct(gap_before, [uint8, uint32]), _, fail)
	->	'$register_predicate'(H, memcmp, [ptr,ptr,ulong], sint),
		foreign_struct(gap_before_spelled, [uint8, uint8, uint16, uint32]),
		foreign_struct(gaps_mixed, [uint8, uint16, uint8, uint32]),
		foreign_struct(gaps_mixed_spelled, [uint8, uint8, uint16, uint8, uint8, uint16, uint32]),
		foreign_struct(gap_after, [uint32, uint8]),
		foreign_struct(gap_after_spelled, [uint32, uint8, uint8, uint16]),
		foreign_struct(inner_s, [uint16, uint32]),
		foreign_struct(outer_s, [uint8, inner_s, uint8]),
		foreign_struct(outer_s_spelled, [uint8, uint8, uint16, uint16, uint16, uint32, uint8, uint8, uint16]),
		foreign_struct(str_first, [cstr, uint32]),
		(	'$register_predicate'(H, strsep, [ptr, cstr], ccstr)
		->	assertz(has_strsep)
		;	true
		),
		assertz(ffi_bound)
	;	true
	).

t(Label, Goal) :-
	(   \+ ffi_bound
	->  R = ok
	;   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

same_bytes(L1, L2, Size) :-
	'$struct_to_pointer'(L1, P1),
	'$struct_to_pointer'(L2, P2),
	memcmp(P1, P2, Size, R),
	'$free_struct_pointer'(P1),
	'$free_struct_pointer'(P2),
	R =:= 0.

main :-
	t(gap_before_field, same_bytes([gap_before, 200, 70000], [gap_before_spelled, 200, 0, 0, 70000], 8)),
	t(gaps_between_fields, same_bytes([gaps_mixed, 1, 515, 3, 100000], [gaps_mixed_spelled, 1, 0, 515, 3, 0, 0, 100000], 12)),
	t(gap_after_last_field, same_bytes([gap_after, 123456, 9], [gap_after_spelled, 123456, 9, 0, 0], 8)),
	t(too_many_values, catch(('$struct_to_pointer'([gap_after, 1, 2, 3], P), '$free_struct_pointer'(P), fail), error(representation_error(ffi_struct_fields), _), true)),
	t(nested_field, same_bytes([outer_s, 7, [inner_s, 515, 100000], 9], [outer_s_spelled, 7, 0, 0, 515, 0, 100000, 9, 0, 0], 16)),
	t(too_many_nested_values, catch(('$struct_to_pointer'([outer_s, 7, [inner_s, 1, 2, 3], 9], P2), '$free_struct_pointer'(P2), fail), error(representation_error(ffi_struct_fields), _), true)),
	t(string_field, (\+ has_strsep -> true ; string_field_read(A, B), A == hello, B == world)),
	t(unbound_string_field_is_null, (\+ has_strsep -> true ; '$struct_to_pointer'([str_first, _, 5], P3), (strsep(P3, ',', _) -> R3 = text ; R3 = null), '$free_struct_pointer'(P3), R3 == null)),
	t(unbound_struct_name, catch(('$struct_to_pointer'([_, 1], P4), '$free_struct_pointer'(P4), fail), error(instantiation_error, _), true)),
	t(non_atom_struct_name, catch(('$struct_to_pointer'([1, 1], P5), '$free_struct_pointer'(P5), fail), error(type_error(atom, 1), _), true)).

% The struct's first field is the string's pointer, so the struct's own pointer is a char ** for strsep().

string_field_read(A, B) :-
	'$struct_to_pointer'([str_first, 'hello,world', 5], P),
	strsep(P, ',', A),
	strsep(P, ',', B),
	'$free_struct_pointer'(P).
