:- initialization(main).

% A stream opened for update is readable, so every input predicate has
% to accept it. get_char/2 and get_byte/2 always did, but read/2,
% get_code/2, unget_code/2 and the three peek predicates only accepted
% mode(read) - which is how they came to reject every socket, those
% being opened for update.

show(Label, Goal) :-
	(	catch(Goal, error(E, _), (write(Label-threw(E)), nl, fail))
	->	true
	;	write(Label-failed), nl
	).

make_file(File, Text) :-
	open(File, write, S),
	write(S, Text),
	close(S).

text(File) :-
	make_file(File, 'hello.'),
	open(File, update, S),
	show(peek_char, (peek_char(S, A), write(peek_char(A)), nl)),
	show(get_char,  (get_char(S, B),  write(get_char(B)),  nl)),
	show(peek_code, (peek_code(S, C), write(peek_code(C)), nl)),
	show(get_code,  (get_code(S, D),  write(get_code(D)),  nl)),
	show(unget_code, (unget_code(S, D), get_code(S, E), write(reget(E)), nl)),
	close(S),
	nl.

term(File) :-
	make_file(File, 'foo(bar). '),
	open(File, update, S),
	show(read, (read(S, T), write(read(T)), nl)),
	close(S),
	nl.

binary(File) :-
	open(File, write, W, [type(binary)]),
	put_byte(W, 0'a), put_byte(W, 0xff),
	close(W),
	open(File, update, S, [type(binary)]),
	show(peek_byte, (peek_byte(S, A), write(peek_byte(A)), nl)),
	show(get_byte,  (get_byte(S, B),  write(get_byte(B)),  nl)),
	show(get_byte_2, (get_byte(S, C), write(get_byte(C)), nl)),
	close(S),
	nl.

main :-
	File = 'tmp.upd',
	text(File),
	term(File),
	binary(File),
	( catch(delete_file(File), _, true) -> true ; true ).
