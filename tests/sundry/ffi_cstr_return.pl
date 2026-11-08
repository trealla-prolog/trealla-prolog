% A cstr return is the C side's to give away, and it was released with TPL_free(), which reads a header tpl's own
% allocator puts in front: any string from C's malloc() aborted the process. A ccstr return must not be freed at all.
% Skipped where no libc opens.

:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(ffi_bound/0).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	'$register_predicate'(H, strdup, [cstr], cstr),
		'$register_predicate'(H, strchr, [cstr, sint], ccstr),
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
	forall(member(L, [malloced, many, kept]), t(L, case(L))).

case(malloced) :-
	strdup(hello, S), S == hello.
case(many) :-
	forall(between(1, 2000, I), (atom_number(A, I), strdup(A, S), S == A)).
case(kept) :-
	Src = abcde,
	strchr(Src, 0'c, S1), strchr(Src, 0'c, S2),
	S1 == cde, S2 == cde, Src == abcde.
