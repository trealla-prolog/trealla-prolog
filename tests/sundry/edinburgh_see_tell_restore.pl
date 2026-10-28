% see/1, tell/1 and append/1 take user, a stream or an alias, so what seeing/1 and
% telling/1 gave can be restored; a file they opened is switched back to, not
% reopened; tell(user) no longer creates a file named user.

:- initialization(main).

main :-
	open('tmp_see_in.txt', write, W), write(W, 'a. b. c.'), close(W),
	check(see_user, (seeing(U), see(user), seeing(U2), U2 == user, see(U))),
	check(see_stream, (see('tmp_see_in.txt'), seeing(S), see(user), see(S), read(X1), X1 == a, seen)),
	check(see_reuse, (see('tmp_see_in.txt'), read(Y1), see(user), see('tmp_see_in.txt'), read(Y2), seen, Y1-Y2 == a-b)),
	check(see_alias, (open('tmp_see_in.txt', read, S3, [alias(edin_in)]), see(edin_in), read(Z1), Z1 == a, see(user), close(S3))),
	check(tell_user, (telling(T0), tell(user), telling(T1), T1 == user, tell(T0))),
	check(tell_reuse, (tell('tmp_tell_out.txt'), write(x), tell(user), tell('tmp_tell_out.txt'), write(y), told, file_text('tmp_tell_out.txt', O1), O1 == xy)),
	check(tell_stream, (tell('tmp_tell_out2.txt'), telling(TS), tell(user), tell(TS), write(z), told, file_text('tmp_tell_out2.txt', O2), O2 == z)),
	check(append_after_told, (append('tmp_tell_out.txt'), write(w), told, file_text('tmp_tell_out.txt', O3), O3 == xyw)),
	check(seen_closes, (see('tmp_see_in.txt'), seeing(S5), seen, \+ is_stream(S5))),
	check(see_after_seen_reopens, (see('tmp_see_in.txt'), read(Q1), seen, see('tmp_see_in.txt'), read(Q2), seen, Q1-Q2 == a-a)),
	check(see_missing, catch((see('tmp_no_such_file.txt'), fail), error(existence_error(source_sink, _), _), true)),
	check(see_var, catch((see(_), fail), error(instantiation_error, _), true)),
	check(tell_var, catch((tell(_), fail), error(instantiation_error, _), true)),
	check(no_user_file, \+ exists_file(user)),
	delete_file('tmp_see_in.txt'), delete_file('tmp_tell_out.txt'), delete_file('tmp_tell_out2.txt').

file_text(F, A) :-
	open(F, read, S), get_all(S, Cs), close(S), atom_codes(A, Cs).

get_all(S, Cs) :-
	get_code(S, C),
	(   C == -1 -> Cs = [] ; Cs = [C|R], get_all(S, R) ).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
