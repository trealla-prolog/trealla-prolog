:- initialization(main).

:- use_module(library(pio)).
:- use_module(library(dcgs)).
:- use_module(library(lists)).

% phrase_from_stream/2 reads the rest of a stream lazily, a chunk of
% 4096 chars at a time. A repositionable stream re-reads a chunk from
% its position when backtracking comes back for it; any other stream
% keeps the chunks it has read. Both must describe the same list.

write_bytes([], _).
write_bytes([B|Bs], S) :- put_byte(S, B), write_bytes(Bs, S).

make_file(File, Bytes) :-
	open(File, write, S, [type(binary)]),
	write_bytes(Bytes, S),
	close(S).

make_text(File, Text) :-
	open(File, write, S),
	write(S, Text),
	close(S).

lines([L|Ls]) --> line(L), !, lines(Ls).
lines([]) --> [].

line([C|Cs]) --> [C], { C \== '\n' }, !, line(Cs).
line([]) --> ['\n'].

count(N0, N) --> [_], !, { N1 is N0 + 1 }, count(N1, N).
count(N, N) --> [].

% Nondeterministic all the way along: backtracks over every chunk.
last(C) --> anything, [C].

anything --> [].
anything --> [_], anything.

probe(File, Opts, Skip, GRBody) :-
	open(File, read, S, Opts),
	length(Skipped, Skip),
	maplist(get_char(S), Skipped),
	(	catch(phrase_from_stream(GRBody, S), E, (write(Opts-threw(E)), nl, fail))
	->	writeq(Opts-GRBody),
		(	at_end_of_stream(S) -> write(' at_end') ; write(' not_at_end') ),
		nl
	;	write(Opts-failed), nl
	),
	close(S).

both(File, Skip, GRBody) :-
	\+ \+ probe(File, [], Skip, GRBody),
	\+ \+ probe(File, [reposition(false)], Skip, GRBody).

main :-
	File = 'tmp.pfs',

	% Short text with a multi-byte char, from the start and from partway.
	make_text(File, 'one\ntwé\n'),
	both(File, 0, lines(_)),
	both(File, 2, lines(_)),
	both(File, 0, "one\ntwé\n"),
	both(File, 0, "one"),
	nl,

	% Several chunks, started from the start and from one char short
	% of the first boundary, and the last char found by backtracking.
	length(Xs, 10000), maplist(=(0'x), Xs),
	append(Xs, [0xC3, 0xA9, 0'E, 0'N, 0'D], Long),
	make_file(File, Long),
	both(File, 0, count(0, _)),
	both(File, 4095, count(0, _)),
	both(File, 0, last(_)),
	nl,

	% Nothing left to read.
	make_file(File, []),
	both(File, 0, count(0, _)),
	make_file(File, [0'a]),
	both(File, 1, count(0, _)),
	nl,

	% A binary stream yields octets.
	make_file(File, [0'c, 0xC3, 0xA9]),
	probe(File, [type(binary)], 0, count(0, _)),
	nl,

	% Not a stream.
	catch(phrase_from_stream(count(0, _), foo), error(E, _), (write(E), nl)),

	delete_file(File).
