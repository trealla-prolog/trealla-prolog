/*  phrase_from_file/[2,3] by example.

    A DCG describes a list of characters. phrase_from_file/[2,3] applies
    one to the contents of a file, so the description stays declarative
    and the I/O stays at the edge. Run from the top of the source tree:

      $ tpl -g test,halt samples/test_phrase_from_file.pl
      $ scryer-prolog -g test,halt samples/test_phrase_from_file.pl
*/

:- use_module(library(pio)).
:- use_module(library(dcgs)).
:- use_module(library(lists)).
:- use_module(library(format)).

test :-
	ex_like,
	ex_lines,
	ex_grep,
	ex_wc,
	ex_csv,
	ex_config,
	ex_records,
	ex_roundtrip.

% Shared vocabulary --------------------------------------------------

% True at the end of the input.
eos([], []).

% One line, without its newline. The last line need not have one.
line([])     --> ( "\n" -> [] ; call(eos) ), !.
line([C|Cs]) --> [C], line(Cs).

% The whole file, split at newlines.
lines([])     --> call(eos), !.
lines([L|Ls]) --> line(L), lines(Ls).

% Zero or more spaces.
ws --> " ", !, ws.
ws --> [].

% (1) Anonymous context -----------------------------------------------
% ...//0 matches any sequence, so this pins down a prefix and lets the
% rest of the file be whatever it likes.

like(What) --> "I like ", seq(What), ".", ... .

ex_like :-
	heading("(1) what the file says it likes"),
	(	phrase_from_file(like(What), 'samples/like.txt'),
		format("  ~s~n", [What]),
		fail
	;	true
	).

% (2) A file as a list of lines ---------------------------------------

ex_lines :-
	heading("(2) numbered lines"),
	phrase_from_file(lines(Ls), 'samples/pio_words.txt'),
	(	nth1(N, Ls, L),
		format("  ~d: ~s~n", [N, L]),
		fail
	;	true
	).

% (3) Backtracking over every match -----------------------------------
% The leading ...//0 tries every starting position, so this enumerates
% one solution per occurrence of the needle rather than just the first.

after(Needle, Rest) --> ..., seq(Needle), line(Rest), ... .

ex_grep :-
	heading("(3) every \"the\", and what follows it on that line"),
	(	phrase_from_file(after("the", Rest), 'samples/pio_words.txt'),
		format("  the~s~n", [Rest]),
		fail
	;	true
	).

% (4) Counting in one pass --------------------------------------------
% wc(1), with the running totals threaded through as accumulators.

wc(L, W, C) --> wc(0, L, 0, W, 0, C).

wc(L0, L, W0, W, C0, C) --> call(eos), !, { L = L0, W = W0, C = C0 }.
wc(L0, L, W0, W, C0, C) --> "\n", !,
	{ L1 is L0+1, C1 is C0+1 },
	wc(L1, L, W0, W, C1, C).
wc(L0, L, W0, W, C0, C) --> " ", !,
	{ C1 is C0+1 },
	wc(L0, L, W0, W, C1, C).
wc(L0, L, W0, W, C0, C) --> word(N),
	{ W1 is W0+1, C1 is C0+N },
	wc(L0, L, W1, W, C1, C).

word(N)      --> [C], { \+ member(C, " \n") }, word_(1, N).
word_(N0, N) --> [C], { \+ member(C, " \n") }, !, { N1 is N0+1 }, word_(N1, N).
word_(N, N)  --> [].

ex_wc :-
	heading("(4) wc(1)"),
	phrase_from_file(wc(L, W, C), 'samples/pio_words.txt'),
	format("  ~d lines, ~d words, ~d characters~n", [L, W, C]).

% (5) A CSV file as a list of records ---------------------------------

csv([])     --> call(eos), !.
csv([R|Rs]) --> record(R), csv(Rs).

record([F|Fs])  --> field(F), fields(Fs).
fields([F|Fs])  --> ",", !, field(F), fields(Fs).
fields([])      --> ( "\n" -> [] ; call(eos) ), !.
field([C|Cs])   --> [C], { \+ member(C, ",\n") }, !, field(Cs).
field([])       --> [].

ex_csv :-
	heading("(5) a CSV file, and what the holdings are worth"),
	phrase_from_file(csv([_Header|Rows]), 'samples/pio_stock.csv'),
	(	member([Sym|_], Rows),
		format("  ~s~n", [Sym]),
		fail
	;	true
	),
	maplist(row_value, Rows, Values),
	sum_list(Values, Total),
	format("  total ~d~n", [Total]).

row_value([_, Shares, Price], V) :-
	number_chars(S, Shares),
	number_chars(P, Price),
	V is S*P.

% (6) Lines first, then each line on its own --------------------------
% phrase/2 applies the same kind of description to a list already in
% memory. Comments and blank lines simply fail to parse, so the
% failure-driven loop skips them without a special case.

setting(Key, Value) -->
	ws, key(Ks), ws, "=", ws, seq(Vs), call(eos),
	{ atom_chars(Key, Ks), value(Vs, Value) }.

key([C|Cs])  --> [C], { \+ member(C, " =") }, key_(Cs).
key_([C|Cs]) --> [C], { \+ member(C, " =") }, !, key_(Cs).
key_([])     --> [].

value(Cs, V) :- catch(number_chars(V, Cs), _, fail), !.
value(Cs, V) :- atom_chars(V, Cs).

ex_config :-
	heading("(6) settings, with numbers read as numbers"),
	phrase_from_file(lines(Ls), 'samples/pio_config.ini'),
	(	member(L, Ls),
		phrase(setting(Key, Value), L),
		format("  ~w = ~q~n", [Key, Value]),
		fail
	;	true
	).

% (7) phrase_from_file/3 with options ---------------------------------
% NUL-separated records, of the kind `find -print0` writes. Nothing
% line-based can read them, but a DCG does not care what the separator
% is. type(binary) asks for the file a byte at a time, which is also
% how to read one that is not text at all: a multi-byte character
% arrives as its separate octets rather than as one character.

nul_records([])     --> call(eos), !.
nul_records([R|Rs]) --> nul_record(R), nul_records(Rs).

nul_record([])     --> "\x0\", !.
nul_record([C|Cs]) --> [C], nul_record(Cs).

ex_records :-
	heading("(7) NUL-separated records, read with type(binary)"),
	phrase_from_file(nul_records(Rs), 'samples/pio_records.bin', [type(binary)]),
	(	member(R, Rs),
		format("  ~s~n", [R]),
		fail
	;	true
	).

% (8) The other direction ---------------------------------------------
% phrase_to_file/2 runs a DCG as a generator, so the same style of
% description that reads a file can also write one.

squares([])     --> [].
squares([N|Ns]) --> { S is N*N }, chars_of(N), " squared is ", chars_of(S), "\n", squares(Ns).

chars_of(N) --> { number_chars(N, Cs) }, seq(Cs).

ex_roundtrip :-
	heading("(8) written with phrase_to_file/2, read back with phrase_from_file/2"),
	File = 'samples/pio_squares.tmp',
	numlist(1, 4, Ns),
	phrase_to_file(squares(Ns), File),
	phrase_from_file(lines(Ls), File),
	(	member(L, Ls),
		format("  ~s~n", [L]),
		fail
	;	true
	),
	% delete_file/1 is not portable: Trealla wants an atom, Scryer a
	% char list from library(files), so this is best-effort.
	catch(delete_file(File), _, true).

heading(Cs) :- format("~n~s~n", [Cs]).
