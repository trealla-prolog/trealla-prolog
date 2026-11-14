% A DCG that parses arithmetic, with precedence and parentheses,
% into a term that is/2 then evaluates.
expr(E) --> term(T), expr_rest(T, E).

expr_rest(A, E) --> ws, "+", !, term(B), expr_rest(A+B, E).
expr_rest(A, E) --> ws, "-", !, term(B), expr_rest(A-B, E).
expr_rest(E, E) --> [].

term(T) --> factor(F), term_rest(F, T).

term_rest(A, T) --> ws, "*", !, factor(B), term_rest(A*B, T).
term_rest(A, T) --> ws, "/", !, factor(B), term_rest(A/B, T).
term_rest(T, T) --> [].

factor(E) --> ws, "(", !, expr(E), ws, ")".
factor(N) --> ws, digits(Ds), { number_chars(N, Ds) }.

digits([D|Ds]) --> digit(D), digits(Ds).
digits([D]) --> digit(D).

digit(D) --> [D], { member(D, "0123456789") }.

ws --> [C], { member(C, " \t") }, !, ws.
ws --> [].

calc(Text, Tree, Value) :-
	phrase((expr(Tree), ws), Text), !,
	Value is Tree.
%?- calc("2 * (3 + 4) - 10 / 4", Tree, Value).
%?- calc("((1+2)*(3+4))*(5+6)", _, Value).
