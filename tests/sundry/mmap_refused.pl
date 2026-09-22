% A mapping that cannot be made has to give the stream slot back. open/4
% commits the slot before it maps, and a failed mmap used to return false
% from there: the goal failed silently and the slot stayed taken, so 1024
% of them exhausted MAX_STREAMS and every later open/4 raised
% resource_error(too_many_streams).

:- initialization(main).

% An append stream has no read access, and MAP_PRIVATE needs it: the one
% mmap failure that can be provoked without running out of memory.

make_file(File) :-
	open(File, write, S),
	write(S, abc),
	close(S).

check(Name, Goal, Expected) :-
	catch((call(Goal) -> Got = succeeded ; Got = failed), error(Formal, _), Got = Formal),
	(	Got = Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' got '), writeq(Got), nl
	).

% Closing on success as well, so the slot count is what is under test
% even where a write-only mapping is allowed.

bomb(_, 0) :- !.
bomb(File, N) :-
	catch(( open(File, append, S, [mmap(_)]) -> close(S) ; true ), _, true),
	N1 is N-1,
	bomb(File, N1).

main :-
	File = 'tmp.mmrefused',
	make_file(File),
	check(refused, open(File, append, _, [mmap(_)]), permission_error(input, stream, File)),
	check(slots_survive, ( bomb(File, 2000), open(File, read, S, []), close(S) ), succeeded),
	( catch(delete_file(File), _, true) -> true ; true ).
