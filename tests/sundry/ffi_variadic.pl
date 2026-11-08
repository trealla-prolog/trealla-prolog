% A variadic C function was called through a fixed-argument cif, which on macOS arm64 puts the variadic arguments in
% registers where the callee reads the stack: curl_easy_setopt/3 lost its value. '...' in a signature now marks
% where the fixed arguments end, and the ones after it take C's promotions. Skipped where no libc opens.

:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(ffi_bound/0).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	'$register_predicate'(H, malloc, [ulong], ptr),
		'$register_predicate'(H, free, [ptr], void),
		'$register_predicate'(H, strstr, [ptr, cstr], ccstr),
		'$register_predicate'(H, sprintf, [ptr, cstr, ..., sint, cstr, double, slong], sint),
		'$register_predicate'(H, sprintf, [ptr, cstr, ..., float, uint8, sint16], sint),
		'$register_predicate'(H, sprintf, [ptr, cstr, ...], sint),
		assertz(ffi_bound)
	;	true
	).

% The text sprintf/N left in a fresh buffer, read back through strstr(Buf, "").

printed(Goal, Buf, Text) :-
	malloc(64, Buf),
	(call(Goal) -> strstr(Buf, '', Text), free(Buf) ; free(Buf), fail).

t(Label, Goal) :-
	(   \+ ffi_bound
	->  R = ok
	;   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

main :-
	forall(member(L, [mixed, promoted, none]), t(L, case(L))).

case(mixed) :-
	printed(sprintf(B, '%d|%s|%.2f|%ld', 42, abc, 1.5, -7, N), B, T),
	T == '42|abc|1.50|-7', N == 14.
case(promoted) :-
	printed(sprintf(B, '%.1f|%d|%d', 2.5, 200, -5, N), B, T),
	T == '2.5|200|-5', N == 10.
case(none) :-
	printed(sprintf(B, 'plain', N), B, T),
	T == plain, N == 5.
