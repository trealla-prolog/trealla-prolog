:- initialization(main).

:- use_module(library(pio)).
:- use_module(library(dcgs)).
:- use_module(library(lists)).
:- use_module(library(charsio)).

% phrase_from_file/3 mmap()s the file and hands the mapping over as a
% string, which is walked as UTF-8. type(binary) marks that mapping so
% it is walked a byte at a time instead, without copying it: the octets
% of a multi-byte character stay apart, and a file that is not UTF-8 at
% all can still be read.

write_bytes([], _).
write_bytes([B|Bs], S) :- put_byte(S, B), write_bytes(Bs, S).

make_file(File, Bytes) :-
	open(File, write, S, [type(binary)]),
	write_bytes(Bytes, S),
	close(S).

codes_of([], []).
codes_of([C|Cs], [N|Ns]) :- char_code(C, N), codes_of(Cs, Ns).

probe(File, Opts) :-
	(	catch(phrase_from_file(seq(Cs), File, Opts), E, (write(Opts-threw(E)), nl, fail))
	->	codes_of(Cs, Ns),
		write(Opts-Ns), nl
	;	write(Opts-failed), nl
	).

probe_all(File, Bytes) :-
	make_file(File, Bytes),
	probe(File, []),
	probe(File, [type(binary)]),
	nl.

% A byte string and a text string may hold the same bytes and still be
% different lists, so neither may be compared by its backing store.

same(File) :-
	phrase_from_file(seq(B), File, [type(binary)]),
	phrase_from_file(seq(T), File, []),
	(	B = T -> write(unify(yes)) ; write(unify(no)) ), nl,
	compare(O, B, T), write(compare(O)), nl,
	msort([B,T], M), length(M, N), write(msort(N)), nl,
	sort([B,T], S), length(S, D), write(sort(D)), nl,
	nl.

% get_n_chars/3 reads a stream rather than a mapping, but must agree.

chars(File, Type) :-
	open(File, read, S, [type(Type)]),
	get_n_chars(S, 99, Cs),
	close(S),
	codes_of(Cs, Ns),
	write(Type-Ns), nl.

main :-
	File = 'tmp.pff',
	probe_all(File, [0'a, 0'b]),			% ASCII, the two agree
	probe_all(File, [0'c, 0xc3, 0xa9]),		% one character, two octets
	probe_all(File, []),					% empty file maps to []
	make_file(File, [0'a, 0'b]), same(File),	% equal as lists
	make_file(File, [0'c, 0xc3, 0xa9]), same(File),	% differ as lists
	make_file(File, [0'c, 0xc3, 0xa9]),
	chars(File, text),
	chars(File, binary),
	( catch(delete_file(File), _, true) -> true ; true ).
