% libffi returns an integer narrower than ffi_arg widened to a whole ffi_arg, and it was read at its own width
% from the start of the slot, which on a big-endian machine is the wrong end: atoi("42") came back as 0 on
% s390x. Negative returns check the sign survives the narrowing. Skipped where no libc opens.

:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(ffi_bound/0).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	'$register_predicate'(H, atoi, [cstr], sint),
		'$register_predicate'(H, strcmp, [cstr,cstr], sint),
		'$register_predicate'(H, toupper, [sint], sint),
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

main :-
	t(positive, (atoi('42', N1), N1 == 42)),
	t(negative, (atoi('-42', N2), N2 == -42)),
	t(large, (atoi('2147483647', N3), N3 == 2147483647)),
	t(most_negative, (atoi('-2147483648', N4), N4 == -2147483648)),
	t(sign_only, (strcmp(abc, abd, N5), N5 < 0, strcmp(abd, abc, N6), N6 > 0)),
	t(char_code, (toupper(0'a, N7), N7 == 0'A)).
