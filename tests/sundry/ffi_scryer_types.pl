% Scryer's type spellings - i8..i64, u8..u64, f32, f64, longlong, ulonglong and char - name the same C types as
% Trealla's own, so a Scryer foreign declaration loads unchanged. Widths and signedness are checked by packing a
% C array under one name and reading it back under another. Skipped where no libc opens.

:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(ffi_bound/0).

:- (	libc(L), catch('$dlopen'(L, 0, _), _, fail)
	->	use_foreign_module(L, [abs([i32], i32), toupper([i32], i32)]),
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

% Values packed as type In read back as type Out.
as(In, Vs, Out, Ws) :-
	length(Vs, N),
	'$array_to_pointer'(In, Vs, P),
	'$pointer_to_array'(Out, P, N, Ws),
	'$free_struct_pointer'(P).

main :-
	t(call, (abs(-6, X), X == 6, toupper(0'a, Y), Y == 0'A)),
	t(i8, (as(i8, [-1, 127, -128], sint8, [-1, 127, -128]), as(i8, [-1, -128], u8, [255, 128]))),
	t(i16, (as(i16, [-1, -32768], uint16, [65535, 32768]), as(u16, [65535], sint16, [-1]))),
	t(i32, (as(i32, [-1], u32, [4294967295]), as(u32, [4294967295], sint32, [-1]))),
	t(i64, (as(i64, [-1], u64, [18446744073709551615]), as(u64, [18446744073709551615], sint64, [-1]))),
	t(longlong, (as(longlong, [-1], ulonglong, [18446744073709551615]), as(ulonglong, [1], sint64, [1]))),
	t(char, (as(char, [0'A, 0'z], uint8, [0'A, 0'z]), as(uchar, [0'A], char, [0'A]))),
	t(f32, (as(f32, [1.5, -0.25], float, [1.5, -0.25]), as(float, [0.1], f32, [F]), F =\= 0.1)),
	t(f64, (as(f64, [0.1], double, [0.1]), as(double, [1.0e300], f64, [1.0e300]))).
