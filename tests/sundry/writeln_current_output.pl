% writeln/1 writes to the current output, as write/1 and SWI's writeln do,
% so tell/1 and set_output/1 redirect it. It wrote to stdout regardless.

:- initialization(main).

main :-
	tell('tmp_writeln_tell.txt'), writeln(a), write(b), nl, told,
	file_text('tmp_writeln_tell.txt', T1), writeq(T1), nl,
	open('tmp_writeln_set.txt', write, S), current_output(Old), set_output(S),
	writeln(c), writeln(S, d), set_output(Old), close(S),
	file_text('tmp_writeln_set.txt', T2), writeq(T2), nl,
	writeln(e),
	delete_file('tmp_writeln_tell.txt'), delete_file('tmp_writeln_set.txt').

file_text(F, A) :-
	open(F, read, S), get_all(S, Cs), close(S), atom_codes(A, Cs).

get_all(S, Cs) :-
	get_code(S, C),
	(   C == -1 -> Cs = [] ; Cs = [C|R], get_all(S, R) ).
