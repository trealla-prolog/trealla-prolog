% foreign_callback/4 makes a C function pointer that runs a Prolog goal, here handed to qsort and bsearch. An error,
% failure or bad answer in the goal is raised once the C function returns, C getting zero meanwhile and the rest of
% its calls skipped. Skipped where no libc opens.

:- use_module(library(yall)).
:- initialization(main).

libc('libc.so.6').		% glibc, where libc.so is a linker script
libc('libc.so.7').		% FreeBSD, likewise
libc('libc.so').		% macOS (opened as libc.dylib), OpenBSD
libc('msvcrt.so').		% Windows (opened as msvcrt.dll)

:- dynamic(ffi_bound/0).

:- (	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	'$register_predicate'(H, qsort, [ptr, ulong, ulong, ptr], void),
		'$register_predicate'(H, bsearch, [ptr, ptr, ulong, ulong, ptr], ptr),
		'$register_predicate'(H, memcpy, [-sint, ptr, ulong], ptr),
		'$register_predicate'(H, strlen, [ptr], ulong),
		foreign_struct(ints5, [sint, sint, sint, sint, sint]),
		assertz(ffi_bound)
	;	true
	).

peek(P, X) :- memcpy(X, P, 4, _).

read_ints(_, 0, []) :- !.
read_ints(P, N, [X|Xs]) :- peek(P, X), P1 is P + 4, N1 is N - 1, read_ints(P1, N1, Xs).

ord(<, -1).
ord(=, 0).
ord(>, 1).

cmp(A, B, R) :- peek(A, X), peek(B, Y), compare(O, X, Y), ord(O, R).
by_mod(K, A, B, R) :- peek(A, X), peek(B, Y), X1 is X mod K, Y1 is Y mod K, compare(O, X1-X, Y1-Y), ord(O, R).
other:desc(A, B, R) :- user:cmp(B, A, R).

:- dynamic(calls/1).

counted(G, A, B, R) :- retract(calls(N)), N1 is N + 1, assertz(calls(N1)), call(G, A, B, R).
thrower(_, _, _) :- throw(oops).
failer(_, _, _) :- fail.
bad(_, _, foo).
unbound(_, _, _).
talker(A, B, R) :- write(x), cmp(A, B, R).

% Sorts 5,3,9,1,7 with Closure as the comparator, counting its calls.

sorted(Closure, L) :-
	'$struct_to_pointer'([ints5, 5, 3, 9, 1, 7], P),
	foreign_callback(counted(Closure), [ptr, ptr], sint, F),
	retractall(calls(_)),
	assertz(calls(0)),
	call_cleanup(
		(qsort(P, 5, 4, F), read_ints(P, 5, L)),
		(foreign_callback_free(F), '$free_struct_pointer'(P))).

lambda_sorted(L) :- sorted([A,B,R]>>cmp(B,A,R), L).

nested(A, B, R) :- sorted(cmp, L), L == [1, 3, 5, 7, 9], cmp(A, B, R).

% Frees itself on its one call: bsearch over one element compares once.

:- dynamic(self/1).
selfish(A, B, R) :- retract(self(F)), foreign_callback_free(F), cmp(A, B, R).

one_shot(Found) :-
	'$struct_to_pointer'([ints5, 9, 1, 0, 0, 0], P),
	foreign_callback(selfish, [ptr, ptr], sint, F),
	assertz(self(F)),
	bsearch(P, P, 1, 4, F, R),
	'$free_struct_pointer'(P),
	(R =:= P -> Found = true ; Found = R),
	catch(foreign_callback_free(F), error(E, _), true),
	E = existence_error(foreign_callback, _).

% The key is an atom, passed as its text where it sits in the atom table, and the callback interns enough atoms to
% grow the table before reading it: the old table has to outlive the call. An ASan build reports it otherwise.

key_len(K, _, R) :-
	forall(between(1, 20000, I), (atomic_list_concat([ffi_callback_atom_, I], A), T =.. [A, x], arg(1, T, x))),
	strlen(K, Len),
	(Len =:= 23 -> R = 0 ; R = 1).

pinned_key(Found) :-
	'$struct_to_pointer'([ints5, 0, 0, 0, 0, 0], P),
	foreign_callback(key_len, [ptr, ptr], sint, F),
	bsearch('a key of 23 characters!', P, 1, 4, F, R),
	foreign_callback_free(F),
	'$free_struct_pointer'(P),
	(R =:= P -> Found = true ; Found = false).

% Each case gives its outcome, the goal or caught(E) or failed, and the comparator's calls: exactly one for a goal
% whose first call raised or failed, as the rest are skipped, and otherwise as many as the platform's qsort makes.

t(Label, Goal, Want) :-
	(   \+ ffi_bound
	->  R = ok
	;   retractall(calls(_)),
		assertz(calls(0)),
		(   catch(Goal, E, true)
		->  (var(E) -> Got = Goal ; Got = caught(E))
		;   Got = failed
		),
		calls(N0),
		(N0 > 1 -> N = many ; N = N0),
		(Got-N = Want -> R = ok ; R = Got-N)
	),
	format("~w: ~q~n", [Label, R]).

captured(Output) :-
	with_output_to(string(S), sorted(talker, _)),
	(sub_string(S, 0, 1, _, "x") -> Output = yes ; Output = no).

main :-
	t(ascending, sorted(cmp, _), sorted(_, [1,3,5,7,9])-many),
	t(closure, sorted(by_mod(4), _), sorted(_, [1,5,9,3,7])-many),
	t(lambda, lambda_sorted(_), lambda_sorted([9,7,5,3,1])-many),
	t(qualified, sorted(other:desc, _), sorted(_, [9,7,5,3,1])-many),
	t(nested, sorted(nested, _), sorted(_, [1,3,5,7,9])-many),
	t(captured, captured(_), captured(yes)-many),
	t(throw, sorted(thrower, _), caught(oops)-1),
	t(fail, sorted(failer, _), failed-1),
	t(bad_return, sorted(bad, _), caught(error(type_error(integer, foo), counted/4))-1),
	t(unbound_return, sorted(unbound, _), caught(error(instantiation_error, counted/4))-1),
	t(undefined, sorted(no_such_cmp, _), caught(error(existence_error(procedure, no_such_cmp/3), _))-1),
	t(one_shot, one_shot(_), one_shot(true)-0),
	t(pinned_key, pinned_key(_), pinned_key(true)-0),
	t(var_closure, foreign_callback(_, [], void, _), caught(error(instantiation_error, foreign_callback/4))-0),
	t(bad_type, foreign_callback(f, [bad], void, _), caught(error(domain_error(foreign_type, bad), foreign_callback/4))-0),
	t(text_return, foreign_callback(f, [cstr], cstr, _), caught(error(domain_error(foreign_type, cstr), foreign_callback/4))-0),
	t(text_output, foreign_callback(f, [-cstr], void, _), caught(error(domain_error(foreign_type, -cstr), foreign_callback/4))-0),
	t(void_arg, foreign_callback(f, [void], void, _), caught(error(domain_error(foreign_type, void), foreign_callback/4))-0),
	t(partial_list, foreign_callback(f, [ptr|_], void, _), caught(error(instantiation_error, foreign_callback/4))-0),
	t(not_callable, foreign_callback(3, [], void, _), caught(error(type_error(callable, 3), foreign_callback/4))-0),
	t(free_unknown, foreign_callback_free(12345), caught(error(existence_error(foreign_callback, 12345), foreign_callback_free/1))-0).
