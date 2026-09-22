% A mapping that cannot be made has to give the stream slot back. open/4
% commits the slot before it maps, and a failed mmap used to return false
% from there: the goal failed silently and the slot stayed taken, so 1024
% of them exhausted MAX_STREAMS and every later open/4 raised
% resource_error(too_many_streams).

:- initialization(main).

% Mapping an append stream is the only mmap failure that can be provoked
% without running out of memory - and only where MAP_PRIVATE insists on a
% readable fd, which POSIX does not require and the BSDs do not do. So the
% check is not that it fails but that it never fails SILENTLY: open/4 maps
% or says why, where before it just failed and kept the slot.

make_file(File) :-
	open(File, write, S),
	write(S, abc),
	close(S).

no_silent_failure(File) :-
	catch(open(File, append, S, [mmap(_)]), error(_, _), S = none),
	(	S == none
	->	true
	;	close(S)
	).

check(Name, Goal, Expected) :-
	catch((call(Goal) -> Got = succeeded ; Got = failed), error(Formal, _), Got = Formal),
	(	Got = Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' got '), writeq(Got), nl
	).

% A failed open must not leave alias(current_input) naming the slot it has
% just given back - reading from it crashed. Nothing to check on a platform
% that allows the mapping after all, where the alias is real.

std_alias_kept(File) :-
	current_input(In0),
	(	catch(open(File, append, S, [alias(current_input), mmap(_)]), _, fail)
	->	set_input(In0), close(S)		% mapped after all: put the input back
	;	current_input(In1),
		In0 == In1
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
	check(no_silent_failure, no_silent_failure(File), succeeded),
	check(std_alias_kept, std_alias_kept(File), succeeded),
	check(slots_survive, ( bomb(File, 2000), open(File, read, S, []), close(S) ), succeeded),
	( catch(delete_file(File), _, true) -> true ; true ).
