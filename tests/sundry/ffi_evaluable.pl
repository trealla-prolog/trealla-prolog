% '$register_function'/4 registers a C function as an arithmetic evaluable. It passed
% libffi a NULL return type and one argument too few, so the first use of one segfaulted.
% The registration is a directive, since a goal compiled before it cannot see it.
% Skipped where no libc opens.

:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(ffi_bound/0).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	'$register_function'(H, labs, [slong], slong),
		'$register_function'(H, fabs, [double], double),
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
	t(integer_return, (X is labs(-7), X == 7)),
	t(double_return, (Y is fabs(-2.5), Y == 2.5)),
	t(in_an_expression, (Z is labs(-2) + fabs(-0.5) * 2, Z == 3.0)).
