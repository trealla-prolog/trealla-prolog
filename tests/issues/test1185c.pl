% Issue #1185 follow-up: stream_property/2 peeked written-to output files, which on Windows broke their next write,
% and make_directory_path/1 tried to make a path's drive prefix.

:- initialization(main).

main :-
	F = 'tmp1185c.txt',
	open(F, write, S),
	write_canonical(S, first), write(S, '.\n'),
	check(output_not_at_end, (stream_property(S, end_of_stream(E)), E == not)),
	check(enumerate_all, findall(X-P, stream_property(X, P), _)),
	check(write_after_peek, (write_canonical(S, hello(world)), write(S, '.\n'))),
	close(S),
	check(read_back, (open(F, read, S2), read(S2, T1), read(S2, T2), close(S2), T1-T2 == first-hello(world))),
	open(F, write, S4),
	close(S4),
	open(F, read, S3),
	check(input_peek_kept, (stream_property(S3, end_of_stream(E3)), E3 == at)),
	close(S3),
	delete_file(F),
	D = tmp1185cdir,
	catch(delete_directory('tmp1185cdir/a/b'), _, true),
	catch(delete_directory('tmp1185cdir/a'), _, true),
	catch(delete_directory(D), _, true),
	check(relative_path, (make_directory_path('tmp1185cdir/a/b'), exists_directory('tmp1185cdir/a/b'))),
	check(again_is_fine, make_directory_path('tmp1185cdir/a/b')),
	check(trailing_sep, make_directory_path('tmp1185cdir/a/b/')),
	(	current_prolog_flag(os, OS), memberchk(OS, [wasi, emscripten])
	->	check(absolute_path, true)		% only the working directory is visible there
	;	check(absolute_path, (absolute_file_name('tmp1185cdir/a/c', A), make_directory_path(A), exists_directory(A), delete_directory(A)))
	),
	delete_directory('tmp1185cdir/a/b'),
	delete_directory('tmp1185cdir/a'),
	delete_directory(D).

check(Name, Goal) :-
	(   \+ \+ catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
