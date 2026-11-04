% set_stream/2 with a single alias(A) took A from whichever stream held it, where the
% list form refuses as open/4 does; and the list form stopped at its first alias or type.
% The three standard aliases are the exception and do move, as in SWI: Logtalk's lgtunit
% and its Jupyter kernel capture user_output and user_error that way (#1183), and closing
% the new holder gives the name back to stdin, stdout or stderr.

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
	forall(tmpfile(_, F), catch(delete_file(F), _, true)),
	std_aliases.

std_aliases :-
	stdfile(out, FO), stdfile(err, FE), stdfile(in, FI),
	open(FO, write, SO),
	check(std_alias_moves, set_stream(SO, alias(user_output)), succeeded),
	check(std_alias_holder, (holder(user_output, H1), H1 == SO), succeeded),
	check(stdout_lost_the_name, (current_output(O), \+ stream_property(O, alias(user_output))), succeeded),
	check(std_alias_write, write(user_output, diverted), succeeded),
	close(SO),
	check(close_gave_the_name_back, (current_output(O2), stream_property(O2, alias(user_output))), succeeded),
	check(std_alias_diverted, (file_text(FO, T1), T1 == diverted), succeeded),
	open(FE, write, SE),
	check(user_error_moves, set_stream(SE, alias(user_error)), succeeded),
	check(user_error_write, write(user_error, diverted), succeeded),
	close(SE),
	check(user_error_diverted, (file_text(FE, T2), T2 == diverted), succeeded),
	open(FI, write, W), write(W, 'a.'), close(W),
	open(FI, read, SI),
	check(user_input_moves, set_stream(SI, alias(user_input)), succeeded),
	check(user_input_read, (read(user_input, X), X == a), succeeded),
	close(SI),
	forall(stdfile(_, F), catch(delete_file(F), _, true)).

stdfile(out, 'tmp.set_stream_alias_out').
stdfile(err, 'tmp.set_stream_alias_err').
stdfile(in, 'tmp.set_stream_alias_in').

file_text(F, A) :-
	open(F, read, S), get_all(S, Cs), close(S), atom_codes(A, Cs).

get_all(S, Cs) :-
	get_code(S, C),
	(	C == -1 -> Cs = [] ; Cs = [C|R], get_all(S, R) ).
