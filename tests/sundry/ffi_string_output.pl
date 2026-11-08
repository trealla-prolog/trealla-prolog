% A -cstr output is the callee's to give away, as a cstr return is, and it was never freed; it is now, and a
% -ccstr one is left alone. Both slots started out pointing at the variable's name rather than NULL.
% Skipped where no libc opens, a case on its own where the libc lacks its function.

:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(libc_handle/1).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	assertz(libc_handle(H))
	;	true
	).

% Registers Name, or fails where the libc has no such function.

reg(Name, Args, Ret) :-
	libc_handle(H),
	catch('$register_predicate'(H, Name, Args, Ret), _, fail).

t(Label, Goal) :-
	(   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

main :-
	forall(member(L, [cstr_output, cstr_outputs_freed, ccstr_output]), t(L, case(L))).

case(cstr_output) :-								% asprintf() mallocs what it writes
	(	reg(asprintf, [-cstr, cstr, ..., sint], sint)
	->	asprintf(S, 'n=%d', 42, N), S == 'n=42', N == 4
	;	true
	).
case(cstr_outputs_freed) :-
	(	reg(asprintf, [-cstr, cstr, ..., sint], sint)
	->	forall(between(1, 2000, I), (asprintf(S, '%d', I, _), atom_number(S, I)))
	;	true
	).
case(ccstr_output) :-								% strtol()'s end points into its own argument
	(	reg(strtol, [cstr, -ccstr, sint], slong)
	->	Src = '42abc',
		strtol(Src, E1, 10, V1), strtol(Src, E2, 10, V2),
		E1 == abc, V1 == 42, E2 == abc, V2 == 42, Src == '42abc'
	;	true
	).
