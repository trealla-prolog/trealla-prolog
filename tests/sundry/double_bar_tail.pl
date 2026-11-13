:- initialization(main).

% A double bar list as a list tail was a syntax error: its cons cells ran past the tail to a stray [].

tail ?- term_to_atom(L, '[x|"ab"||T]').
   L = [x,a,b|_].

bracketed_tail ?- term_to_atom(L, '[x|("ab"||T)]').
   L = [x,a,b|_].

compound_tail ?- term_to_atom(L, '[x|"ab"||g(Y,Z)]').
   L = [x,a,b|g(_A,_B)].

element ?- term_to_atom(L, '["ab"||g(Y),1]').
   L = [[a,b|g(_)],1].

double_bar_list_tail ?- term_to_atom(L, '[x|"a"||[b,c]]').
   L = [x,a,b,c].

two_tails ?- term_to_atom(L, '[x|"ab"||T|U]').
   syntax_error(...).

codes_tail ?- set_prolog_flag(double_quotes, codes), term_to_atom(L, '[x|"ab"||T]'), set_prolog_flag(double_quotes, chars).
   L = [x,97,98|_].

% A long string before || grows the clause, and the cells after the tail move up past it.
% Its token outgrows the tokenizer's buffer, which leaked when the next token began.

long_lhs(N) :-
	length(Cs, N), maplist(=(a), Cs), atom_chars(A, Cs),
	atomic_list_concat(['g("', A, '"||t,z)'], Src),
	term_to_atom(g(L, Z), Src), append(Cs, t, L), Z == z.

long_lhs ?- long_lhs(1500).
   true.

copied ?- L = [x|"ab"||g(Y)], copy_term(L, L2), L2 = [_,_,_|g(1)].
   L = [x,a,b|g(Y)], L2 = [x,a,b|g(1)].

asserted ?- assertz(dbt([x|"ab"||g(Y)], Y)), dbt(L, 1), retract(dbt(_, _)).
   L = [x,a,b|g(1)].

% The tail's cells were shared, not moved, so a bigint or long string in it leaked on every parse (tests/run_leaks.sh)

bigint_tail ?- term_to_atom(L, '"ab"||g(123456789012345678901234567890)').
   L = [a,b|g(123456789012345678901234567890)].

string_tail ?- term_to_atom(L, '"ab"||g("a string long enough not to be stored inline in the cell")').
   L = [a,b|g("a string long enough not to be stored inline in the cell")].

% The writer printed the tail after || in frame 0, so a clause's variable showed another variable's value.

r(f("ab"||g(Y), Y)).

other_frame ?- A = zzz, r(Z), Z = f(_, 8), write_term(Z, [quoted(true),double_quotes(true)]).
   outputs("f(\"ab\"||g(8),8)"), A = zzz, Z = f([a,b|g(8)],8).

shared_var ?- r(Z), Z = f(_, Y), write_term(Z, [quoted(true),double_quotes(true),variable_names(['Y'=Y])]).
   outputs("f(\"ab\"||g(Y),Y)"), Z = f([a,b|g(Y)],Y).

% The tail has priority 0, so an operator term or atom there is bracketed however it was built

functional_minus ?- write_term([a,b|-(1)], [quoted(true),double_quotes(true)]).
   outputs("\"ab\"||(- (1))"), true.

functional_prefix ?- write_term([a,b|-(a)], [quoted(true),double_quotes(true)]).
   outputs("\"ab\"||(-a)"), true.

built_infix ?- functor(F, +, 2), arg(1, F, 1), arg(2, F, 2), write_term([a,b|F], [quoted(true),double_quotes(true)]).
   outputs("\"ab\"||(1+2)"), F = 1+2.

op_atom ?- write_term([a,b|dynamic], [quoted(true),double_quotes(true)]).
   outputs("\"ab\"||(dynamic)"), true.

negative_number ?- write_term([a,b|-1], [quoted(true),double_quotes(true)]).
   outputs("\"ab\"||-1"), true.

plain_compound ?- write_term([a,b|f(-)], [quoted(true),double_quotes(true)]).
   outputs("\"ab\"||f(-)"), true.

% A cycle back through the tail was never cut, so write_term/2 ran on to the depth limit

cyclic_tail ?- \+ \+ (X = "ab"||f(X), write_term(X, [double_quotes(true)])).
   sto, outputs("\"ab\"||f(...)"), true.

cyclic_inner_tail ?- \+ \+ (X = [a|T], T = [b|f(T)], write_term(X, [double_quotes(true)])).
   sto, outputs("\"ab\"||f(\"b\"||...)"), true.

cyclic_through_arg ?- \+ \+ (X = "ab"||f(Y), Y = g(X), write_term(X, [double_quotes(true)])).
   sto, outputs("\"ab\"||f(g(...))"), true.

main :- use_module(library(quads)), run_quads.
