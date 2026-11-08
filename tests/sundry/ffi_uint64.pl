% An unsigned 64-bit C value past 2^63 came back negative, strtoull("18446744073709551615") as -1, and could not be
% passed at all. It travels as a bigint both ways now, returned, as an argument, to and from an evaluable, and
% read back from a struct field.
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

reg(Kind, Name, Args, Ret) :-
	libc_handle(H),
	catch(call(Kind, H, Name, Args, Ret), _, fail).

t(Label, Goal) :-
	(   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

main :-
	forall(
		member(L, [return_max, return_past_int64, return_small, argument_max, argument_past_int64,
			argument_too_big, evaluable, evaluated_argument, struct_field]),
		t(L, case(L))).

case(return_max) :-
	(	reg('$register_predicate', strtoull, [cstr, ptr, sint], uint64)
	->	strtoull('18446744073709551615', 0, 10, N), N == 18446744073709551615
	;	true
	).
case(return_past_int64) :-
	(	reg('$register_predicate', strtoull, [cstr, ptr, sint], uint64)
	->	strtoull('9223372036854775808', 0, 10, N), N == 9223372036854775808
	;	true
	).
case(return_small) :-
	(	reg('$register_predicate', strtoull, [cstr, ptr, sint], uint64)
	->	strtoull('42', 0, 10, N), N == 42
	;	true
	).
case(argument_max) :-								% all ones is -1 to llabs()
	(	reg('$register_predicate', llabs, [uint64], uint64)
	->	llabs(18446744073709551615, N), N == 1
	;	true
	).
case(argument_past_int64) :-
	(	reg('$register_predicate', llabs, [uint64], uint64)
	->	llabs(9223372036854775813, N), N == 9223372036854775803
	;	true
	).
case(argument_too_big) :-
	(	reg('$register_predicate', llabs, [uint64], uint64)
	->	catch(llabs(18446744073709551616, _), error(type_error(integer, _), _), true)
	;	true
	).
case(evaluable) :-
	(	reg('$register_function', llabs, [uint64], uint64),
		reg('$register_function', strtoull, [cstr, ptr, sint], uint64)
	->	X is llabs(18446744073709551615), X == 1,
		Y is strtoull('18446744073709551615', 0, 10), Y == 18446744073709551615
	;	true
	).
case(evaluated_argument) :-
	(	reg('$register_function', llabs, [uint64], uint64)
	->	X is llabs(2^64 - 1), X == 1
	;	true
	).
case(struct_field) :-								% lldiv_t is two long longs; quot -1 is all ones
	(	libc_handle(_),
		foreign_struct(u64pair, [uint64, uint64]),
		reg('$register_predicate', lldiv, [sint64, sint64], u64pair)
	->	lldiv(-1, 1, P), P = [_, Q, R], Q == 18446744073709551615, R == 0
	;	true
	).
