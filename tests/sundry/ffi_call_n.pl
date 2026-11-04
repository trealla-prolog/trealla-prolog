% A foreign predicate exported from a module was hidden by the empty entry its export left, so call/N, maplist/N,
% an M:Name closure and a goal compiled before a run-time load all raised existence_error. Skipped without libc.

:- initialization(main).
:- use_module('ffi_call_n/ffi_strlen').
:- use_module('ffi_call_n/ffi_strlen', [strlen/2 as sl]).

t(Label, Goal) :-
	(   \+ ffi_bound
	->  R = ok
	;   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

main :-
	t(direct, (strlen(abc, N1), N1 == 3)),
	t(call_1, (call(strlen(abc, N2)), N2 == 3)),
	t(call_2, (call(strlen(abc), N3), N3 == 3)),
	t(call_3, (call(strlen, abc, N4), N4 == 3)),
	t(maplist, (maplist(strlen, [a,bb,ccc], Ns), Ns == [1,2,3])),
	t(qualified_call_3, (call(ffi_strlen:strlen, abc, N5), N5 == 3)),
	t(renamed_call_3, (call(sl, abcd, N6), N6 == 4)),
	t(loaded_at_run_time, (use_module('ffi_call_n/ffi_atoi'), atoi('42', N7), N7 == 42)).
