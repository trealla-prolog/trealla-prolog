% '$register_function'/4 registers a C function as an arithmetic evaluable. It passed
% libffi a NULL return type and one argument too few, so the first use of one segfaulted.
% The registration is a directive; a goal compiled before it can see it too since v3.14.53.
% Its numeric arguments were passed unevaluated, so labs(3-10) raised a type error; they are evaluated
% now, and an integer for a double converted. Skipped where no libc opens.

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
	t(in_an_expression, (Z is labs(-2) + fabs(-0.5) * 2, Z == 3.0)),
	t(evaluated_argument, (A is labs(3-10), A == 7)),
	t(foreign_in_argument, (B is labs(labs(-3) - 10), B == 7)),
	t(integer_for_double, (C is fabs(-3), C == 3.0)),
	t(unbound_argument, catch(_ is labs(_), error(instantiation_error, _), true)).
