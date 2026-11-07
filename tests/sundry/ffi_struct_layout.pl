% '$struct_to_pointer'/2 packed fields end to end, without the padding C puts before a field to align it or
% after the last to round the struct up, so a struct with a gap came out the wrong shape and short. Each is
% checked against the same struct with its padding spelled out as zeroed fields. Fields are 4 bytes at most,
% as wider ones are only 4-aligned on 32-bit x86. Skipped where no libc opens.

:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(ffi_bound/0).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail),
		catch(foreign_struct(gap_before, [uint8, uint32]), _, fail)
	->	'$register_predicate'(H, memcmp, [ptr,ptr,ulong], sint),
		foreign_struct(gap_before_spelled, [uint8, uint8, uint16, uint32]),
		foreign_struct(gaps_mixed, [uint8, uint16, uint8, uint32]),
		foreign_struct(gaps_mixed_spelled, [uint8, uint8, uint16, uint8, uint8, uint16, uint32]),
		foreign_struct(gap_after, [uint32, uint8]),
		foreign_struct(gap_after_spelled, [uint32, uint8, uint8, uint16]),
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
	t(too_many_values, catch(('$struct_to_pointer'([gap_after, 1, 2, 3], P), '$free_struct_pointer'(P), fail), error(representation_error(ffi_struct_fields), _), true)).
