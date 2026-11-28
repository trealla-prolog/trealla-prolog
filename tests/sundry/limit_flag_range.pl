% task_time_slice and the tabling limits are kept in 32 bits. A value past
% 4294967295 was stored wrapped, 4294967296 as 0 (no limit at all); now it
% is a flag_value error, as a positive bigint is instead of "not less than
% zero".

:- initialization(main).

try(F, V) :-
	X is V,
	catch(( set_prolog_flag(F, X), current_prolog_flag(F, Y), R = ok(Y) ), error(E, _), R = E),
	writeq(F-R), nl.

main :-
	forall(
		member(F-Off, [task_time_slice-0, max_table_answer_size-infinite, max_table_subgoal_size-infinite, max_answers_for_subgoal-infinite]),
		(	try(F, 4294967295),
			try(F, 4294967296),
			try(F, 2^70),
			try(F, -1),
			try(F, -(2^70)),
			set_prolog_flag(F, Off)
		)).
