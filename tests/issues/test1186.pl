% Issue #1186: Windows had random floats all 0.0, delete_file/1 passing over a directory and no current_host/1;
% getenv/2 raised for a bound value everywhere.

:- initialization(main).

:- use_module(library(sockets)).

main :-
	setenv(tpl1186, abc),
	check(getenv_bound_fails, \+ getenv(tpl1186, xyz)),
	check(getenv_bound_matches, getenv(tpl1186, abc)),
	findall(F, (between(1, 20, _), random(F)), Fs),
	sort(Fs, Us),
	check(random_varies, (length(Us, N), N > 15)),
	check(random_in_range, forall(member(F, Fs), (F >= 0.0, F =< 1.0))),
	findall(I, (between(1, 200, _), random_between(1, 3, I)), Is),
	check(random_between_all, (sort(Is, Js), Js == [1,2,3])),
	set_seed(42), random(A1), set_seed(42), random(A2),
	check(seed_repeats, A1 == A2),
	D = tmp1186dir,
	catch(delete_file('tmp1186dir/f'), _, true),
	catch(delete_directory(D), _, true),
	make_directory(D),
	check(delete_empty_dir, (delete_file(D), \+ exists_directory(D))),
	make_directory(D),
	open('tmp1186dir/f', write, S), close(S),
	check(delete_full_dir_raises, catch((delete_file(D), fail), error(permission_error(delete, file, _), _), true)),
	delete_file('tmp1186dir/f'),
	delete_directory(D),
	(	current_prolog_flag(os, wasi)
	->	check(current_host, true)		% no gethostname() there
	;	check(current_host, (current_host(H), atom(H), H \== ''))
	).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
