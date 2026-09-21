:- initialization(main).

:- use_module(library(pio)).
:- use_module(library(dcgs)).
:- use_module(library(lists)).

% seq//1 answers a whole compact string in one step: same list as the walk
% described, and its own characters, close/1 having unmapped the file's.

write_bytes([], _).
write_bytes([B|Bs], S) :- put_byte(S, B), write_bytes(Bs, S).

make_file(File, Bytes) :-
	open(File, write, S, [type(binary)]),
	write_bytes(Bytes, S),
	close(S).

codes_of([], []).
codes_of([C|Cs], [N|Ns]) :- char_code(C, N), codes_of(Cs, Ns).

show(Name, G) :-
	(	catch(call(G), E, (write(Name-threw(E)), nl, fail))
	->	true
	;	write(Name-failed), nl
	).

% Read after the close: a stale answer is wrong characters, or a crash.
read_whole(File, Opts, Ns) :-
	phrase_from_file(seq(Cs), File, Opts),
	codes_of(Cs, Ns).

main :-
	File = 'tmp.seq',
	make_file(File, [0'c, 0xc3, 0xa9]),		% one character as text, two as bytes

	show(text, (read_whole(File, [], T), write(text(T)), nl)),
	show(binary, (read_whole(File, [type(binary)], B), write(binary(B)), nl)),

	% a slice starting partway in
	show(suffix, (phrase_from_file(("c", seq(S)), File), codes_of(S, Sn), write(suffix(Sn)), nl)),

	% with something left over, the walk describes it
	show(rest, (phrase_from_file((seq(M), "\xe9\"), File), codes_of(M, Mn), write(rest(Mn)), nl)),

	% two reads agree, the first still intact
	show(twice, (read_whole(File, [], A), read_whole(File, [], C),
		( A == C -> write(twice(eq)) ; write(twice(ne)) ), nl)),

	% an empty file is [], not ''
	make_file(File, []),
	show(empty, (phrase_from_file(seq(E), File), ( E == [] -> write(empty(nil)) ; write(empty(E)) ), nl)),

	% and the other modes enumerate as before
	show(rests, (findall(X-Y, phrase(seq(X), "ab", Y), L1), write(rests(L1)), nl)),
	show(cons, (findall(X, phrase(seq(X), [a,b], []), L2), write(cons(L2)), nl)),
	show(gen, (findall(Y, phrase(seq("ab"), Y, []), L3), write(gen(L3)), nl)),

	( catch(delete_file(File), _, true) -> true ; true ).
