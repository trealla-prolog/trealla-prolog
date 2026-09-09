:- initialization(main).

% An answer may describe what the query reads: inputs/1 the characters
% it consumes, peeks/1 the single character it looks at without
% consuming, waits that it is left asking for a character none is
% there to supply.
%
% The query is run against a file holding those characters followed by
% the byte 0xff, which can never begin a UTF-8 sequence. Reading too
% much reaches the sentinel and raises a representation error; reading
% too little leaves characters in the stream, which the harness looks
% for afterwards. 'waits' is the case where reaching the sentinel is
% the described outcome.

% consumes exactly what it says

?- get_char(C).
   inputs("a"), C = a.

% reads nothing at all

?- true.
   inputs("").

% peeks without consuming

?- peek_char(C).
   peeks("a"), C = a.

% consumes, then looks one character past

?- get_char(A), peek_char(B).
   inputs("x"), peeks("y"), A = x, B = y.

% read/1 consumes the end token and peeks one past it, so that two
% dots would not be mistaken for one

?- read(G).
   inputs("writeq(a)."), peeks("\n"), G = writeq(a).

% is left asking for a character

?- get_char(_).
   inputs(""), waits.

% reads less than described: b is left in the stream

?- get_char(C).
   inputs("ab"), C = a.

% reads more than described: reaches the sentinel

?- get_char(C), get_char(_).
   inputs("a"), C = a.

% claims a peek but consumes

?- get_char(C).
   peeks("a"), C = a.

% claims to wait but does not

?- true.
   inputs(""), waits.

% peeks/1 is one character

?- get_char(C).
   peeks("ab"), C = a.

% inputs/1 is characters, not a DCG body: an answer says exactly what
% was read, and a nonterminal may stand for more than one string

?- get_char(C).
   inputs(seq([a])), C = a.

% a query is run once against one input

?- get_char(C).
   inputs("a"), inputs("b"), C = a.

% a peek leaves the character, so every later read returns it and the
% query can never be left waiting

?- peek_char(C).
   peeks("c"), waits.

% 'unexpected' negates the whole answer, input claims included. These
% two describe the same run, one of them negated, so exactly one of
% them holds whatever read/1 does. Trealla's read/1 leaves the layout
% character after the end token unread, so it is the second.

?- read(X).
   inputs("1."), peeks("\n"), X = 1.

?- read(X).
   inputs("1."), peeks("\n"), X = 1, unexpected.

?- read(X).
   inputs("1.\n"), X = 1.

?- read(X).
   inputs("1.\n"), X = 1, unexpected.

% An ill-formed byte is no character at all, so the parser meets the
% sentinel the way a character-level read does: with a representation
% error rather than a syntax error, which is what lets read/1 be
% described as waiting.

?- read(_).
   waits.

% What a peek that leaves no trace cannot show is that it happened -
% except that it can, per UWN: put the sentinel in the position to be
% peeked. read/1 has to look past the end token to confirm it (6.4.8),
% so it meets the sentinel there and the run is one that waits. A
% reader that does not peek would answer X = 1 instead, which is what
% the second of these denies.

?- read(X).
   inputs("1."), waits.

?- read(X).
   inputs("1."), X = 1, unexpected.

% and the sentinel is still there to be found after read/1 has taken
% the line it sits on into the parser's buffer

?- read(X).
   inputs("1."), peeks(" "), X = 1.

?- read(X).
   inputs("1. "), peeks(" "), X = 1, unexpected.

% Stage 3 of #1099: encode notes from UWN's ISO syntax conformity suite
% (https://www.complang.tuwien.ac.at/ulrich/iso-prolog/conformity_testing)
% as quads. Row numbers below are that page's own numbering ("s#" in
% the issue). Trealla passes all six.

% s#1: a conforming reader must peek the character past the end token
% to confirm it, not merely recognise the '.'.

?- read(G_0), G_0.
   inputs("writeq('\\n')."), peeks("\n"), outputs("'\\n'"), G_0 = writeq('\n').

% s#2: an unterminated quoted atom is a syntax error, not a wait.

?- catch(read(_), error(E,_), true).
   inputs("'\n"), E = syntax_error(unterminated_quoted_atom).

% s#3: ')' alone is not a complete term, so the reader asks for more.

?- read(_).
   inputs(")\n"), waits.

% s#4: '.' alone has no term for it to terminate.

?- catch(read(_), error(E,_), true).
   inputs(".\n"), E = syntax_error(incomplete_statement).

% s#270/s#271: the one character read/1 peeks past the end token must
% not skip ahead through a following comment. s#270 has a space before
% the comment - that space is what's peeked, and the next get_char
% consumes it, landing on '%'. s#271 has no space: '.' is immediately
% followed by '%', which is itself an end-token trigger (ISO 6.4.8),
% and '%' is what get_char then sees - the comment's own text ('a')
% is left untouched either way.

?- read(G_0), G_0.
   inputs("get_char(C). "), peeks("%"), G_0 = get_char(' ').

?- read(G_0), G_0.
   inputs("get_char(C).%"), peeks("a"), G_0 = get_char('%').

% run_quads names the file as it was consulted, so the report would
% otherwise depend on the path this was invoked with. Keep the base
% name only.

strip_dirs(Cs, Out) :- strip_dirs(Cs, [], Out).

strip_dirs([], W, Out) :- reverse(W, Out).
strip_dirs([C|Cs], W, Out) :-
	(	C == (/)
	->	strip_dirs(Cs, [], Out)
	;	C == ' '
	->	reverse([C|W], Pre), append(Pre, Out0, Out), strip_dirs(Cs, [], Out0)
	;	C == '\n'
	->	reverse([C|W], Pre), append(Pre, Out0, Out), strip_dirs(Cs, [], Out0)
	;	strip_dirs(Cs, [C|W], Out)
	).

main :-
	use_module(library(quads)),
	with_output_to(chars(Cs), run_quads),
	strip_dirs(Cs, Out),
	atom_chars(A, Out),
	write(A).
