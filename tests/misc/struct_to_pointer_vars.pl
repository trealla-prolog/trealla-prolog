% '$struct_to_pointer'/2 read the struct's name, its fields and each tail of the
% list without dereferencing them, so a list built through variables packed the
% variable cells: a name from a variable was looked up as some other struct, and
% fields from variables went in as garbage. library(plplot) copied its lists with
% findall/3 first to get round it. The bytes packed are now the same however the
% list was built, and plplot's arrays, whose elements are computed, come out as a
% literal list would. Skipped where no libc opens.

:- initialization(main).

:- use_module(library(plplot)).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(ffi_bound/0).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail),
		catch(foreign_struct(stp_test, [double, sint64, uint8]), _, fail)
	->	'$register_predicate'(H, memcmp, [ptr,ptr,ulong], sint),
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

% Packed without padding: 8 + 8 + 1 bytes.

same_bytes(L1, L2) :-
	'$struct_to_pointer'(L1, P1),
	'$struct_to_pointer'(L2, P2),
	memcmp(P1, P2, 17, R),
	'$free_struct_pointer'(P1),
	'$free_struct_pointer'(P2),
	R =:= 0.

% plplot:arrays_/2 against literal lists: a float vector, an int vector, and a
% matrix, whose table holds its rows' pointers (listed after it, last row first).

plplot_same(Arrays, Kind-Fields, Size) :-
	plplot:arrays_(Arrays, [P|Ptrs]),
	'$struct_to_pointer'([Kind|Fields], L),
	memcmp(P, L, Size, R),
	'$free_struct_pointer'(L),
	plplot:free_all_([P|Ptrs]),
	R =:= 0.

plplot_matrix :-
	plplot:arrays_([m([[1, 2.5], [3, 4]], M)], [M, R2, R1]),
	'$struct_to_pointer'([plptr_vector, R1, R2], T),
	'$struct_to_pointer'([plflt_vector, 1.0, 2.5], Row1),
	memcmp(M, T, 16, C1),
	memcmp(R1, Row1, 16, C2),
	'$free_struct_pointer'(T),
	'$free_struct_pointer'(Row1),
	plplot:free_all_([M, R2, R1]),
	C1 =:= 0, C2 =:= 0.

main :-
	t(name_from_variable, (N = stp_test, same_bytes([stp_test, 1.5, -7, 200], [N, 1.5, -7, 200]))),
	t(fields_from_variables, (X = 1.5, Y = -7, Z = 200, same_bytes([stp_test, 1.5, -7, 200], [stp_test, X, Y, Z]))),
	t(tail_from_variable, (T = [-7, 200], same_bytes([stp_test, 1.5, -7, 200], [stp_test, 1.5|T]))),
	t(different_fields_differ, \+ same_bytes([stp_test, 1.5, -7, 200], [stp_test, 2.5, -7, 200])),
	t(plplot_floats, plplot_same([f([1, 2.5, 3], _)], plflt_vector-[1.0, 2.5, 3.0], 24)),
	t(plplot_ints, plplot_same([i([1, 2.0, 3], _)], plint_vector-[1, 2, 3], 12)),
	t(plplot_matrix, plplot_matrix).
