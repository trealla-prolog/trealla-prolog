% '$array_to_pointer'/3 packs a list as a C array of one number or pointer type, for C functions that take arrays
% longer than the 255 fields '$struct_to_pointer'/2 can pack. Each value is checked as a struct field is. Read
% back through memcpy and memcmp. Skipped where no libc opens.

:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(ffi_bound/0).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	'$register_predicate'(H, memcpy, [-double, ptr, ulong], ptr),
		'$register_predicate'(H, memcmp, [ptr, ptr, ulong], sint),
		foreign_struct(three_ints, [sint32, sint32, sint32]),
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

halves(0, []) :- !.
halves(N, [F|Fs]) :- F is N * 0.5, N1 is N - 1, halves(N1, Fs).

raises(Goal, Error) :- catch((Goal, fail), error(Error, _), true).

main :-
	t(long_array, (halves(1000, Fs), '$array_to_pointer'(double, Fs, P), P999 is P + 999*8, memcpy(X, P999, 8, _),
		'$free_struct_pointer'(P), X == 0.5)),
	t(same_as_struct, ('$array_to_pointer'(sint32, [7, -8, 9], P1), '$struct_to_pointer'([three_ints, 7, -8, 9], P2),
		memcmp(P1, P2, 12, C), '$free_struct_pointer'(P1), '$free_struct_pointer'(P2), C =:= 0)),
	t(empty, ('$array_to_pointer'(uint8, [], P3), integer(P3), '$free_struct_pointer'(P3))),
	t(uint64_bigint, ('$array_to_pointer'(uint64, [18446744073709551615], P4), '$free_struct_pointer'(P4))),
	t(int_for_double, raises('$array_to_pointer'(double, [1], _), type_error(float, 1))),
	t(float_for_int, raises('$array_to_pointer'(sint, [1.5], _), type_error(integer, 1.5))),
	t(atom_for_ptr, raises('$array_to_pointer'(ptr, [a], _), type_error(integer, a))),
	t(unbound_value, raises('$array_to_pointer'(sint, [_], _), instantiation_error)),
	t(partial_list, raises('$array_to_pointer'(sint, [1|_], _), instantiation_error)),
	t(not_list, raises('$array_to_pointer'(sint, foo, _), type_error(list, foo))),
	t(string_type, raises('$array_to_pointer'(cstr, [a], _), domain_error(foreign_type, cstr))),
	t(unknown_type, raises('$array_to_pointer'(bogus, [1], _), domain_error(foreign_type, bogus))).
