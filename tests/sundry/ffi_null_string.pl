% A C function returning NULL for "no string" was handed to strlen() as if it were one,
% which crashed the process; it fails instead. Skipped where no libc opens.

:- initialization(main).
:- use_module('ffi_null_string/ffi_libc').

t(Label, Goal) :-
	(   \+ ffi_bound
	->  R = ok
	;   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

main :-
	t(found, (strchr(abcde, 0'c, S), atom(S), S == cde)),
	t(not_found, \+ strchr(abcde, 0'z, _)).
