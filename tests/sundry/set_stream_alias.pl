% set_stream/2 with a single alias(A) took A from whichever stream held it, where the
% list form refuses as open/4 does; and the list form stopped at its first alias or type.

:- initialization(main).

tmpfile(1, 'tmp.set_stream_alias_1').
tmpfile(2, 'tmp.set_stream_alias_2').
tmpfile(3, 'tmp.set_stream_alias_3').

check(Name, Goal, Expected) :-
	catch((call(Goal) -> Got = succeeded ; Got = failed), error(Formal, _), Got = Formal),
	(	Got = Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' got '), writeq(Got), nl
	).

holder(Alias, S) :-
	stream_property(S0, alias(Alias)), !,
	S = S0.

main :-
	tmpfile(1, F1), tmpfile(2, F2), tmpfile(3, F3),
	open(F1, write, S1, [alias(ssa_one)]),
	open(F2, write, S2),
	open(F3, write, S3),
	check(single_taken, set_stream(S2, alias(ssa_one)), permission_error(modify, stream_alias, alias(ssa_one))),
	check(single_taken_holder, (holder(ssa_one, H1), H1 == S1), succeeded),
	check(list_taken, set_stream(S2, [alias(ssa_one)]), permission_error(modify, stream_alias, alias(ssa_one))),
	check(list_taken_holder, (holder(ssa_one, H2), H2 == S1), succeeded),
	check(single_own_alias, set_stream(S1, alias(ssa_one)), succeeded),
	check(list_alias_then_type, set_stream(S2, [alias(ssa_two), type(binary)]), succeeded),
	check(list_alias_applied, (holder(ssa_two, H3), H3 == S2), succeeded),
	check(list_type_applied, stream_property(S2, type(binary)), succeeded),
	check(list_type_then_alias, set_stream(S3, [type(binary), alias(ssa_three)]), succeeded),
	check(list_alias_applied_last, (holder(ssa_three, H4), H4 == S3), succeeded),
	check(list_unknown_skipped, set_stream(S3, [nodelay(true), type(text)]), succeeded),
	check(list_type_after_unknown, stream_property(S3, type(text)), succeeded),
	close(S1), close(S2), close(S3),
	forall(tmpfile(_, F), catch(delete_file(F), _, true)).
