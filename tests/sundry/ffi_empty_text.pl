% "" reads as [], which went to C as the text "[]": a cstr or ccstr argument, or text as a pointer, gets [] as "".
% The string "[]" is still two characters. Skipped where no libc opens.

:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(ffi_bound/0).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	'$register_predicate'(H, strlen, [cstr], sint64),
		'$register_predicate'(H, strdup, [ccstr], cstr),
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
	forall(member(L, [empty_string, nil, quoted_nil, empty_atom, nil_string, ccstr]), t(L, case(L))).

case(empty_string) :- strlen("", N), N =:= 0.
case(nil) :- strlen([], N), N =:= 0.
case(quoted_nil) :- strlen('[]', N), N =:= 0.
case(empty_atom) :- strlen('', N), N =:= 0.
case(nil_string) :- format(string(S), "[]", []), strlen(S, N), N =:= 2.
case(ccstr) :- strdup("", S), S == ''.
