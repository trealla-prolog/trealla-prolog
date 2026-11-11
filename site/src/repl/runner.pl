% Runs the query in query.pl against program.pl, printing answers the way the toplevel does.

web_repl_main :-
	open('query.pl', read, S),
	catch(read_term(S, Goal, [variable_names(Vs)]), E, true),
	close(S),
	(   nonvar(E) -> web_repl_error(E)
	;   Goal == end_of_file -> true
	;   catch(web_repl_answers(Goal, Vs), E2, web_repl_error(E2))
	),
	flush_output,
	halt.

web_repl_answers(Goal, Vs) :-
	N = n(0),
	(   call_cleanup(user:Goal, Det = true),
		arg(1, N, N0), N1 is N0 + 1, nb_setarg(1, N, N1),
		( N1 =:= 1 -> write('   ') ; write(';  ') ),
		web_repl_bindings(Vs),
		(   Det == true -> write('.'), nl
		;   N1 >= 25 -> nl, write('   ... stopped after 25 answers.'), nl
		;   nl, flush_output, fail
		)
	->  true
	;   ( arg(1, N, 0) -> write('   ') ; write(';  ') ),
		write('false.'), nl
	).

web_repl_bindings(Vs0) :-
	copy_term(Vs0, Vs, Gs0),
	exclude(var, Gs0, Gs),
	web_repl_pairs(Vs, Vs, Bs),
	web_repl_fresh(Bs-Gs, Vs, Names),
	(   Bs == [], Gs == [] -> write(true)
	;   web_repl_list(Bs, Names, first, Sep),
		web_repl_goals(Gs, Names, Sep)
	).

% Names the remaining unbound variables _A, _B, ... as the toplevel does.
web_repl_fresh(T, Vs, Names) :-
	term_variables(T, All),
	exclude(web_repl_named(Vs), All, Fresh),
	web_repl_name(Fresh, 0, Vs, Names).

web_repl_named(Vs, V) :- member(_=W, Vs), W == V, !.

% Prepends to the query's names rather than appending: an append/3-built
% variable_names list can make write_term/2 hang after term_variables/2.
web_repl_name([], _, Taken, Taken).
web_repl_name([V|Vs], I, Taken, [N=V|Ns]) :-
	web_repl_gen(I, Taken, I1, N),
	web_repl_name(Vs, I1, Taken, Ns).

web_repl_gen(I, Taken, I1, N) :-
	L is 0'A + I mod 26, K is I // 26,
	( K =:= 0 -> atom_codes(C, [0'_, L]) ; number_codes(K, Ks), atom_codes(C, [0'_, L|Ks]) ),
	I2 is I + 1,
	( memberchk(C=_, Taken) -> web_repl_gen(I2, Taken, I1, N) ; I1 = I2, N = C ).

% Drops _-prefixed names and lone unbound variables; a variable shared by
% several names comes out once as Name = First.
web_repl_pairs([], _, []).
web_repl_pairs([Name=V|T], Vs, Bs) :-
	(   sub_atom(Name, 0, 1, _, '_') -> Bs = Bs1
	;   nonvar(V) -> Bs = [Name=V|Bs1]
	;   once((member(First=W, Vs), W == V)), First \== Name -> Bs = [alias(First, Name)|Bs1]
	;   Bs = Bs1
	),
	web_repl_pairs(T, Vs, Bs1).

web_repl_list([], _, Sep, Sep).
web_repl_list([B|Bs], Vs, Sep0, Sep) :-
	( Sep0 == first -> true ; write(', ') ),
	(   B = alias(First, Name) -> write(Name), write(' = '), write(First)
	;   B = (Name=V), write(Name), write(' = '),
		write_term(V, [quoted(true), variable_names(Vs), max_depth(0)])
	),
	web_repl_list(Bs, Vs, rest, Sep).

web_repl_goals([], _, _).
web_repl_goals([G|Gs], Vs, Sep) :-
	( Sep == first -> true ; write(', ') ),
	write_term(G, [quoted(true), variable_names(Vs), max_depth(0)]),
	web_repl_goals(Gs, Vs, rest).

web_repl_error(E) :-
	write('   '), write_term(E, [quoted(true), max_depth(20)]), write('.'), nl.
