% Option parsers read an option's argument without checking it had one, so a bare
% alias named the stream after whatever cell came next and alias(a, b) quietly used a.

:- initialization(main).

check(Name, Goal, Expected) :-
	catch((call(Goal) -> Got = succeeded ; Got = failed), error(Formal, _), Got = Formal),
	(	Got = Expected
	->	write(Name), write(' ok'), nl
	;	write(Name), write(' got '), writeq(Got), nl
	).

main :-
	F = option_arity_no_such_file,
	check(open_alias, open(F, read, _, [alias]), domain_error(stream_option, alias)),
	check(open_alias_two, open(F, read, _, [alias(a, b)]), domain_error(stream_option, alias(a, b))),
	check(open_mmap, open(F, read, _, [mmap]), domain_error(stream_option, mmap)),
	check(open_encoding, open(F, read, _, [encoding]), domain_error(stream_option, encoding)),
	check(open_type, open(F, read, _, [type]), domain_error(stream_option, type)),
	check(open_bom, open(F, read, _, [bom]), domain_error(stream_option, bom)),
	check(open_reposition, open(F, read, _, [reposition]), domain_error(stream_option, reposition)),
	check(open_eof_action, open(F, read, _, [eof_action]), domain_error(stream_option, eof_action)),
	check(set_stream_list_alias, set_stream(user_output, [alias]), domain_error(stream_property, alias)),
	check(set_stream_alias_two, set_stream(user_output, alias(a, b)), domain_error(stream_property, alias(a, b))),
	check(map_alias, map_create(_, [alias]), domain_error(stream_option, alias)),
	check(engine_alias, engine_create(x, true, _, [alias]), domain_error(engine_option, alias)),
	check(engine_alias_two, engine_create(x, true, _, [alias(a, b)]), domain_error(engine_option, alias(a, b))),
	check(engine_stack, engine_create(x, true, _, [stack]), domain_error(engine_option, stack)).
