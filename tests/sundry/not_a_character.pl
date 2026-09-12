:- initialization(main).

% An octet that cannot be part of a UTF-8 sequence is not a character,
% and ISO 13211-1 8.12.1.3 i makes reading one a representation error.
% get_char/2 and peek_char/2 said so already; read/2 reported a syntax
% error, and a character left behind in the line read/2 had buffered
% was decoded silently and then reported as the end of the file
% (issue #1099).
%
% The octet right behind an end token is the one read/2 has to peek at
% to confirm it (6.4.8). That position used to escape the check: 0xff
% became a syntax error and every other ill-formed octet was parsed
% around and left in the stream unremarked.
%
% Nor is a quoted token or a comment exempt: inside quotes the octet was
% a syntax error, and inside a comment it was skipped along with the
% comment, so the term after it was read as if nothing were wrong.
%
% Each check states what it expects, so not_a_character.expected is a
% list of "ok" lines and a regression reads as "FAILED got ...".

% In the current directory, not /tmp: Windows and WASI have no such
% path. Deleted at the end of main/0.

tmpfile('tmp.not_a_character.txt').

% Write Text, then the octet 0xff behind it.

write_sentinel_file(Text) :-
	write_byte_file(Text, 0xff).

% ...or any other octet: 0xfe, 0x80 and 0xc0 can no more begin a UTF-8
% sequence than 0xff can, and 0xff is the only one that was ever noticed.

write_byte_file(Text, Byte) :-
	write_byte_file(Text, Byte, '').

% ...with After behind the octet, so skipping it would read a term.

write_byte_file(Text, Byte, After) :-
	tmpfile(F),
	open(F, write, S, []),
	write(S, Text),
	close(S),
	open(F, append, B, [type(binary)]),
	put_byte(B, Byte),
	close(B),
	open(F, append, A, []),
	write(A, After),
	close(A).

check(Name, Goal, Expected) :-
	tmpfile(F),
	open(F, read, S, []),
	(	catch(call(Goal, S, Got), E, Got = threw(E))
	->	true
	;	Got = failed
	),
	catch(close(S), _, true),
	(	Got == Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' FAILED got '), writeq(Got),
		write(' wanted '), writeq(Expected), nl
	).

rep_err(G, threw(error(representation_error(character), G))).

% --------------------------------------------------------------------

get_one(S, C) :- get_char(S, C).
peek_one(S, C) :- peek_char(S, C).
read_one(S, T) :- read(S, T).

% read/1, then what the harness looks for behind the term it read

read_then_get(S, C) :- read(S, _), get_char(S, C).
read_then_get2(S, C) :- read(S, _), get_char(S, _), get_char(S, C).

% a peek does not consume, so peeking twice throws twice

peek_twice(S, C) :-
	catch(peek_char(S, _), _, true),
	peek_char(S, C).

main :-
	% nothing but the sentinel
	write_sentinel_file(''),
	rep_err(get_char/2, GetErr),
	check(get_char, get_one, GetErr),
	rep_err(peek_char/2, PeekErr),
	check(peek_char, peek_one, PeekErr),
	rep_err(read/2, ReadErr),
	check(read, read_one, ReadErr),

	% a term, a space, then the sentinel: read/2 takes the whole line
	% into the parser's buffer, and the space and the sentinel must
	% still be found behind it
	write_sentinel_file('1. '),
	check(read_then_get, read_then_get, ' '),
	check(read_then_get2, read_then_get2, GetErr),

	% the octet sits where read/2 must peek to confirm the end token,
	% so it is met there rather than parsed around
	write_sentinel_file('1.'),
	check(read_end_token, read_one, ReadErr),
	write_byte_file('1.', 0xfe),
	check(read_end_token_fe, read_one, ReadErr),
	write_byte_file('1.', 0x80),
	check(read_end_token_80, read_one, ReadErr),
	write_byte_file('1.', 0xc0),
	check(read_end_token_c0, read_one, ReadErr),

	% a layout character between the two is peeked instead, and the
	% octet stays in the stream for a later read to meet
	write_sentinel_file('1.\n'),
	check(read_past_layout, read_one, 1),

	% inside a quoted token, a line comment or a block comment
	write_byte_file('''a', 0xff, '''. '),
	check(read_quoted, read_one, ReadErr),
	write_byte_file('''a', 0x80, '''. '),
	check(read_quoted_80, read_one, ReadErr),
	write_byte_file('% c', 0xff, '\n1. '),
	check(read_line_comment, read_one, ReadErr),
	write_byte_file('/* c', 0xff, ' */ 1. '),
	check(read_block_comment, read_one, ReadErr),

	% peeking is idempotent
	write_sentinel_file(''),
	check(peek_twice, peek_twice, PeekErr),

	% valid multi-byte text is unaffected
	write_sentinel_file('héllo. '),
	check(read_accented, read_one, 'héllo'),
	write_sentinel_file('''é''. '),
	check(read_accented_quoted, read_one, 'é'),
	write_sentinel_file('% é\n1. '),
	check(read_accented_line_comment, read_one, 1),
	write_sentinel_file('/* é */ 1. '),
	check(read_accented_block_comment, read_one, 1),

	tmpfile(F),
	catch(delete_file(F), _, true).
