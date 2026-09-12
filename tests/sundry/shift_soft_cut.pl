% A soft-cut (*-> or if/3) whose condition succeeded marks its barrier so
% backtracking skips the else branch. It uses the choice's reset flag for
% that, which was also all shift/1 looked for to find its reset/3 - so a
% shift inside such a then-branch, while the condition still had choices,
% took the soft-cut barrier for the reset and returned a wrong ball.

:- initialization(main).

s_if(B) :- reset(if((member(X,[1,2]) ; fail), shift(X), true), B, _).
s_soft(B) :- reset(((member(X,[1,2]) ; fail) *-> shift(X) ; true), B, _).
s_soft_member(B) :- reset((member(X,[1,2]) *-> shift(X) ; true), B, _).
s_runtime_if(B) :- G = if((member(X,[1,2]) ; fail), shift(X), true), reset(G, B, _).
s_runtime_soft(B) :- G = ((member(X,[1,2]) ; fail) *-> shift(X) ; true), reset(G, B, _).
s_ite(B) :- reset(((member(X,[1,2]) ; fail) -> shift(X) ; true), B, _).
s_nested(B) :- ( member(Y,[a,b]) *-> reset((member(X,[1,2]) *-> shift(X-Y) ; true), B, _) ; true ).
s_cont(B-Z) :- reset((member(X,[1,2]) *-> shift(X), Z = done ; true), B, cont(K)), call(K).

% With no reset/3 at all, a soft-cut barrier must not stand in for one.

n_stray(R) :- ( member(_, [1,2]) *-> ( catch(shift(x), _, fail) -> R = wrongly_shifted ; R = no_reset ) ; true ).

% Soft-cuts still skip their else branch once the condition has succeeded.

e_soft(L) :- findall(X, ((member(X,[1,2]) ; fail) *-> true ; X = none), L).
e_if(L) :- findall(X, if((member(X,[1,2]) ; fail), true, X = none), L).
e_runtime_soft(L) :- G = ((member(X,[1,2]) ; fail) *-> true ; X = none), findall(X, G, L).
e_runtime_if(L) :- G = if((member(X,[1,2]) ; fail), true, X = none), findall(X, G, L).
e_else(L) :- findall(X, (fail *-> true ; X = none), L).

main :-
	forall(
		member(T, [s_if, s_soft, s_soft_member, s_runtime_if, s_runtime_soft, s_ite,
			s_nested, s_cont, n_stray, e_soft, e_if, e_runtime_soft, e_runtime_if, e_else]),
		(	G =.. [T, R],
			(	catch(G, E, R = uncaught(E)) -> true ; R = failed ),
			copy_term(R, R1), numbervars(R1, 0, _),
			write(T), write(': '), print(R1), nl
		)
	).
