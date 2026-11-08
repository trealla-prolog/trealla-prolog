% A cyclic term was written by unrolling it to the writer's depth limit: 24KB for X = a(X), and with two
% references back, X = f(X,X), doubling at every level so that write/1, writeq/1, print/1, format/2,
% term_to_atom/2 and portray_clause/1 never returned. Now ... closes each cycle where it starts again,
% as Scryer writes it, unless a max_depth is given. write_canonical/1 also lost the ancestors inside a
% list and missed a spine looping back past its first cell, and portray_clause/1 wrote a looping chars
% list as a partial string.

:- initialization(main).

t(Label, G) :-
	(	catch(with_output_to(string(S), G), E, (S = "", print_message(error, E)))
	->	format("~w: ~s~n", [Label, S])
	;	format("~w: failed~n", [Label])
	).

main :-
	t(self, (X1 = a(X1), writeq(X1))),
	t(branching, (X2 = f(X2, X2), writeq(X2))),
	t(branching_write, (X3 = f(X3, X3), write(X3))),
	t(via_another, (X4 = f(Y4), Y4 = g(X4, X4), writeq(X4))),
	t(shared_var, (X5 = f(X5, Y5, Y5), portray_clause(f(X5, Y5)))),
	t(conjunction, (X6 = (X6, X6), writeq(X6))),
	t(prefix_op, (X7 = -(X7), writeq(X7))),
	t(infix_ops, (X8 = - X8 - X8, writeq(X8))),
	t(curly, (X9 = {X9}, writeq(X9))),
	t(list_heads, (X10 = [X10|X10], writeq(X10))),
	t(clause, (X11 = (h(X11) :- b(X11)), portray_clause(X11))),
	t(clause_ops, (X12 = (X12 :- X12), portray_clause(X12))),
	t(chars_loop, (X13 = [a, b|X13], portray_clause(X13))),
	t(chars_partial, (X14 = [a, b|_], portray_clause(X14))),
	t(canonical_in_list, (X15 = f([X15, X15]), write_canonical(X15))),
	t(canonical_lasso, (A16 = [a, b|B16], B16 = [c|C16], C16 = [d|B16], write_canonical(A16))),
	t(canonical_two_lists, (A17 = [c|D17], D17 = [D17|A17], write_canonical(A17))),
	t(term_to_atom, (X18 = f(X18, X18), term_to_atom(X18, A18), write(A18))),
	t(format_raises, (X19 = f(X19, X19), catch(format("~w", [X19]), error(E19, _), write(E19)))),
	t(max_depth_unrolls, (X20 = f(X20, X20), write_term(X20, [max_depth(3)]))).
