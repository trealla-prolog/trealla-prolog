% '$pointer_to_struct'/3 reads a declared struct from memory, the inverse of '$struct_to_pointer'/2: each field
% where C lays it out, pointers as integers, cstr fields as text and NULL as 0. '$struct_to_pointer'/2 given
% fewer fields allocated only up to the last, so reading the whole struct ran past the block; it now covers the
% whole struct, zeroed. Skipped where there is no FFI.

:- initialization(main).

:- dynamic(ffi_bound/0).

:- (	catch((foreign_struct(mix, [sint8, double, cstr, ptr, sint32, uint8, float]),
		foreign_struct(outer, [sint32, mix])), _, fail)
	->	assertz(ffi_bound)
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
	forall(member(L, [round_trip, null_text, utf8_text, fewer_fields, unknown, nested, null_pointer]), t(L, case(L))).

back(Fs, L) :-
	setup_call_cleanup('$struct_to_pointer'(Fs, P), '$pointer_to_struct'(mix, P, L), '$free_struct_pointer'(P)).

case(round_trip) :- back([mix, -3, 2.5, hello, 4096, 7, 255, 0.5], L), L == [mix, -3, 2.5, hello, 4096, 7, 255, 0.5].
case(null_text) :- back([mix, 1, 1.0, 0, 0, 0, 0, 0.0], L), L == [mix, 1, 1.0, 0, 0, 0, 0, 0.0].
case(utf8_text) :- back([mix, 0, 0.0, 'héllo wörld', 0, 0, 0, 0.0], [_, _, _, T|_]), T == 'héllo wörld'.
case(fewer_fields) :- back([mix, 9], L), L == [mix, 9, 0.0, 0, 0, 0, 0, 0.0].
case(unknown) :- catch('$pointer_to_struct'(nosuch, 1, _), error(existence_error(foreign_struct, nosuch), _), true).
case(nested) :- catch('$pointer_to_struct'(outer, 1, _), error(domain_error(flat_foreign_struct, outer), _), true).
case(null_pointer) :- catch('$pointer_to_struct'(mix, 0, _), error(domain_error(non_null_pointer, 0), _), true).
