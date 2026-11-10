% A ball travels to its catcher as printed text, read back. Its strings were written as char lists, so a caught
% string came back a list: string/1 failed on it, though == still held. Now they are written in double quotes and
% read back as chars whatever the double_quotes flag.

:- initialization(main).

t(Label, Goal) :-
	(   catch(Goal, E, true)
	->  (var(E) -> R = ok ; R = E)
	;   R = failed
	),
	format("~w: ~q~n", [Label, R]).

str(S) :- format(string(S), "~s", [[a,b]]).

main :-
	t(bare, (str(S), catch(throw(S), Z, true), string(Z), Z == S)),
	t(nested, (str(S1), catch(throw(x(f(S1), [S1])), x(f(A1), [B1]), true), string(A1), string(B1))),
	t(literal, (catch(throw(x("ab")), x(Z2), true), string(Z2))),
	t(escapes, (X3 = "a\0\b\"q'\\\n", catch(throw(x(X3)), x(Z3), true), string(Z3), Z3 == X3)),
	t(empty, (catch(throw(x("")), x(Z4), true), Z4 == [])),
	t(partial_list, (catch(throw(x([a|"bc"], [a|_])), x(A5, B5), true), A5 == "abc", B5 = [a|T5], var(T5))),
	t(quoted_atoms, (catch(throw(x('a b', 'don''t')), x(A6, B6), true), A6 == 'a b', B6 == 'don''t')),
	t(culprit, (catch(atom_length(f("ab"), _), error(type_error(atom, f(Z7)), _), true), string(Z7))),
	t(cleanup, (catch(call_cleanup(throw(x("ab")), true), x(Z8), true), string(Z8))),
	t(engine, (engine_create(_, throw(x("ab")), En9), catch(engine_next(En9, _), x(Z9), true), string(Z9))),
	t(codes_flag, (str(S10), set_prolog_flag(double_quotes, codes), catch(throw(x(S10)), x(Z10), true),
		set_prolog_flag(double_quotes, chars), string(Z10))),
	t(codes_list, (set_prolog_flag(double_quotes, codes), atom_codes(ab, Cs11), catch(throw(x(Cs11)), x(Z11), true),
		set_prolog_flag(double_quotes, chars), Z11 == [97,98])),
	t(atom_flag, (str(S12), set_prolog_flag(double_quotes, atom), catch(throw(x(S12)), x(Z12), true),
		set_prolog_flag(double_quotes, chars), string(Z12))).
