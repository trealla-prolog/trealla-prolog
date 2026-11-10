% '$array_to_pointer'/3 packs a list as a C array of one number or pointer type, for C functions that take arrays
% longer than the 255 fields '$struct_to_pointer'/2 can pack. Each value is checked as a struct field is. Read
% back through memcpy and memcmp, and through '$pointer_to_array'/4, its inverse. A cstr array is char*s with
% the text in the same block; '$text_to_pointer'/3 and '$pointer_to_text'/3 carry counted UTF-8 bytes, NULs and
% all. Skipped where no libc opens.

:- use_module(library(lists)).
:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(ffi_bound/0).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	'$register_predicate'(H, memcpy, [-double, ptr, ulong], ptr),
		'$register_predicate'(H, memcmp, [ptr, ptr, ulong], sint),
		'$register_predicate'(H, strlen, [ptr], ulong),
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
	t(cstr_array, ('$array_to_pointer'(cstr, [abc, "dé", 0, ""], P10), '$pointer_to_array'(ptr, P10, 4, [A, B, Z, D]),
		strlen(A, LA), strlen(B, LB), strlen(D, LD), '$pointer_to_text'(B, LB, SB), '$free_struct_pointer'(P10),
		[LA, LB, Z, LD] == [3, 3, 0, 0], SB == "dé")),
	t(cstr_empty, ('$array_to_pointer'(cstr, [], P11), '$free_struct_pointer'(P11))),
	t(cstr_compound, raises('$array_to_pointer'(cstr, [f(x)], _), type_error(atom, f(x)))),
	t(cstr_nonzero_int, raises('$array_to_pointer'(cstr, [1], _), type_error(atom, 1))),
	t(cstr_unbound, raises('$array_to_pointer'(cstr, [_], _), instantiation_error)),
	t(text_round_trip, ('$text_to_pointer'("a\0\bé", P12, L12), '$pointer_to_text'(P12, L12, S12), strlen(P12, N12),
		'$free_struct_pointer'(P12), L12 == 5, N12 == 1, S12 == "a\0\bé")),
	t(text_atom, ('$text_to_pointer'(abc, P13, L13), '$pointer_to_text'(P13, 2, S13), '$free_struct_pointer'(P13),
		L13 == 3, S13 == "ab")),
	t(text_empty, ('$text_to_pointer'("", P14, L14), strlen(P14, N14), '$free_struct_pointer'(P14), L14-N14 == 0-0)),
	t(text_compound, raises('$text_to_pointer'(f(x), _, _), type_error(atom, f(x)))),
	t(text_none, ('$pointer_to_text'(0, 0, S15), S15 == [])),
	t(text_null, raises('$pointer_to_text'(0, 1, _), domain_error(non_null_pointer, 0))),
	t(text_negative, raises('$pointer_to_text'(8, -1, _), domain_error(not_less_than_zero, -1))),
	t(unknown_type, raises('$array_to_pointer'(bogus, [1], _), domain_error(foreign_type, bogus))),
	t(read_back, (numlist(1, 600, Ns), '$array_to_pointer'(sint, Ns, P5), '$pointer_to_array'(sint, P5, 600, Ns2),
		'$free_struct_pointer'(P5), Ns2 == Ns)),
	t(read_bytes, ('$array_to_pointer'(uint16, [258], P6), '$pointer_to_array'(uint16, P6, 1, [V6]),
		'$free_struct_pointer'(P6), V6 == 258)),
	t(read_doubles, ('$array_to_pointer'(double, [1.5, -2.0], P7), '$pointer_to_array'(double, P7, 2, D7),
		'$free_struct_pointer'(P7), D7 == [1.5, -2.0])),
	t(read_uint64_bigint, ('$array_to_pointer'(uint64, [18446744073709551615], P8), '$pointer_to_array'(uint64, P8, 1, U8),
		'$free_struct_pointer'(P8), U8 == [18446744073709551615])),
	t(read_none, ('$pointer_to_array'(sint, 0, 0, E9), E9 == [])),
	t(read_null, raises('$pointer_to_array'(sint, 0, 1, _), domain_error(non_null_pointer, 0))),
	t(read_negative, raises('$pointer_to_array'(sint, 8, -1, _), domain_error(not_less_than_zero, -1))),
	t(read_string_type, raises('$pointer_to_array'(cstr, 8, 1, _), domain_error(foreign_type, cstr))).
