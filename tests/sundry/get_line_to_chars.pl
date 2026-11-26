% get_line_to_chars/3 called partial_string/3, removed in 2025; lines keep their newline, end of file gives Cs0 = Cs.

:- initialization(main).
:- use_module(library(charsio)).

main :-
	F = 'tmp_get_line_to_chars.txt',
	open(F, write, W), write(W, 'ab\ncd'), close(W),
	open(F, read, S),
	get_line_to_chars(S, A, []),
	get_line_to_chars(S, B, tail),
	get_line_to_chars(S, C, []),
	close(S),
	delete_file(F),
	write_canonical([A,B,C]), nl.
