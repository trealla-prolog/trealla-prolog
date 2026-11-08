% Registered foreign predicates filled a fixed table of 1000 unchecked, a new slot even for one registered again,
% starting on use_foreign_module/2's own entry. Struct names pointed into the atom pool, which moves as it grows
% (ASan catches struct_after_atoms), and use_foreign_module/2 as a goal read a bound variable as garbage.
% Skipped where no libc opens.

:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(libc_handle/2).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	assertz(libc_handle(L, H))
	;	true
	).

len(A, N) :- strlen(A, N).

t(Label, Goal) :-
	(   \+ libc_handle(_, _)
	->  R = ok
	;   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

main :-
	forall(
		member(L, [goal_after_registration, many_registrations, new_signature, struct_declared_again, struct_after_atoms]),
		t(L, case(L))).

case(goal_after_registration) :-
	libc_handle(L, H),
	'$register_predicate'(H, labs, [slong], slong),
	use_foreign_module(L, [abs([sint], sint)]),
	abs(-3, X), X == 3.
case(many_registrations) :-
	libc_handle(_, H),
	forall(between(1, 1500, _), '$register_predicate'(H, labs, [slong], slong)),
	labs(-4, X), X == 4.
case(new_signature) :-
	libc_handle(_, H),
	'$register_predicate'(H, strlen, [cstr], uint64),
	length(Cs, 300), maplist(=(a), Cs), atom_chars(A, Cs),
	len(A, 300),
	'$register_predicate'(H, strlen, [cstr], uint8),
	len(A, 44).
case(struct_declared_again) :-
	libc_handle(_, H),
	foreign_struct(rereg_s, [sint32, sint32]),
	foreign_struct(rereg_s, [sint8, sint8, sint8, sint8]),
	'$register_predicate'(H, memcmp, [ptr,cstr,ulong], sint),
	'$struct_to_pointer'([rereg_s,1,2,3,4], P),
	memcmp(P, '\x1\\x2\\x3\\x4\', 4, R),
	'$free_struct_pointer'(P),
	R == 0.
case(struct_after_atoms) :-
	libc_handle(_, H),
	foreign_struct(pool_s, [sint8, sint8, sint8, sint8]),
	forall(between(1, 50000, I), (format(atom(A), "pool_atom_~w", [I]), term_to_atom(_, A))),
	'$register_predicate'(H, memcmp, [ptr,cstr,ulong], sint),
	'$struct_to_pointer'([pool_s,1,2,3,4], P),
	memcmp(P, '\x1\\x2\\x3\\x4\', 4, R),
	'$free_struct_pointer'(P),
	R == 0.
