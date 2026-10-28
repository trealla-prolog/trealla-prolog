% format/2's ~N starts a new line unless the stream's last output ended one, whether that
% output came from this call, an earlier format/2, write/1 or put_char/1; a capture or an
% atom(_) sink starts at the beginning of a line, as in SWI.

:- initialization(main).

t(Name, G) :- with_output_to(atom(A), G), writeq(Name-A), nl.
main :-
	t(after_write, (write(x), format("~Ny~n", []))),
	t(after_nl, (write(x), nl, format("~Ny~n", []))),
	t(fresh, format("~Ny~n", [])),
	t(twice, (write(x), format("~N~Ny~n", []))),
	t(in_call, format("a~Nb~N~Nc", [])),
	t(after_put, (put_char(z), format("~Ny", []))),
	t(after_format, (format("p", []), format("~Nq", []))),
	t(after_format_nl, (format("p~n", []), format("~Nq", []))),
	format(atom(A1), "~Nfresh", []), writeq(atom_sink-A1), nl,
	t(stream_arg, (current_output(O), write(O, s), format(O, "~Nt", []))),
	t(nested, (write(outer), with_output_to(atom(S2), format("~Ninner", [])), write(S2))),
	open('tmp_tilde_n1.txt', write, W1), write(W1, x), close(W1),
	open('tmp_tilde_n2.txt', write, W2), format(W2, "~Nreused", []), close(W2),
	open('tmp_tilde_n2.txt', read, R2), get_char(R2, C2), close(R2), writeq(reused_slot-C2), nl,
	delete_file('tmp_tilde_n1.txt'), delete_file('tmp_tilde_n2.txt'),
	write(direct), format("~Nnext~n", []), format("~Nlast~n", []).
