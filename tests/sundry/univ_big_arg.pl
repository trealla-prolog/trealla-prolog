% =../2 costs the same whatever the size of a compound in the arguments it builds or takes apart: a big one
% goes in as a variable bound to it, not a copy, and a variable in it stays the same variable.

:- initialization(main).

t(Name, Goal) :- ( catch(Goal, E, (print_message(error, E), fail)) -> R = yes ; R = no ), format("~w: ~w~n", [Name, R]).

walk(0, _) :- !.
walk(N, T) :- T =.. [F, A], T2 =.. [F, A], N1 is N - 1, walk(N1, T2).

main :-
	numlist(1, 200000, L0),
	copy_term(f(L0), T),
	copy_term([f, L0], Lw),
	forall(between(1, 100000, _), _ =.. [f, L0]),
	forall(between(1, 100000, _), _ =.. Lw),
	forall(between(1, 100000, _), T =.. _),
	numlist(1, 20, Big),
	t(compose_var, (X =.. [g, a, L0, b], X = g(_, L1, _), L1 == L0)),
	t(compose_written, (Y =.. Lw, Y = f(B), B == L0)),
	t(decompose, (T =.. [F|As], F == f, As = [A], A == L0)),
	t(roundtrip, (T =.. Xs, T2 =.. Xs, T2 == T)),
	t(compose_shares_vars, (length(Vs, 20), copy_term(Vs-[h, Vs], Ws-Lh), W =.. Lh, Ws = [1|_], arg(1, W, [E1|_]), E1 == 1)),
	t(decompose_shares_vars, (length(Us, 20), copy_term(k(Us), K), K =.. [k, KA], KA = [x|_], arg(1, K, [E2|_]), E2 == x)),
	t(backtrack, findall(N, (between(1, 3, N), Z =.. [f, Big], Z = f(Big)), [1, 2, 3])),
	Nm =.. [g|Big],
	t(name_error, catch(_ =.. [Nm, a], error(type_error(atom, Nm), _), true)),
	copy_term(f(Big), TB),
	t(walk, walk(100000, TB)).
