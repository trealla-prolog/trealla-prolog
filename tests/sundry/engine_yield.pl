% engine_yield/1 never suspended the engine, so a goal ran on past its yields, and
% engine_post/2 shared the yield's slot, so a posted term could come back as an answer.

:- initialization(main).

check(G) :-
	(	catch(G, E, (write(G), write(' THREW '), writeq(E), nl, fail))
	->	true
	;	write(G), write(' FAILED'), nl
	).

next_or_none(E, A) :-
	(	engine_next(E, A0)
	->	A = A0
	;	A = none
	).

in_order :-
	engine_create(x, (engine_yield(one), write(after_one), nl, engine_yield(two), write(after_two), nl), E),
	next_or_none(E, A), write(first(A)), nl,
	next_or_none(E, B), write(second(B)), nl,
	next_or_none(E, C), write(third(C)), nl,
	next_or_none(E, D), write(fourth(D)), nl,
	engine_destroy(E).

pong :-
	engine_fetch(X),
	engine_yield(got(X)),
	pong.

ping_pong :-
	engine_create(_, pong, E),
	engine_post(E, a, R1),
	engine_post(E, b, R2),
	engine_destroy(E),
	write(ping_pong(R1, R2)), nl.

backtracking :-
	engine_create(done, ((between(1, 3, X), engine_yield(y(X)), fail) ; true), E),
	findall(A, (between(1, 4, _), engine_next(E, A)), As),
	engine_destroy(E),
	write(backtracking(As)), nl.

inside_control :-
	engine_create(r(C, L, I, N),
		(	catch((engine_yield(in_catch), C = caught_ok), _, C = threw),
			findall(X, (member(X, [1,2]), engine_yield(in_findall(X))), L),
			( engine_yield(in_if) -> I = then ; I = else ),
			( \+ (engine_yield(in_not), fail) -> N = negated ; N = not_negated )
		), E),
	findall(A, (between(1, 6, _), engine_next(E, A)), As),
	engine_destroy(E),
	write(inside_control(As)), nl.

unfetched_post :-
	engine_create(x, true, E),
	engine_post(E, junk),
	engine_next(E, R),
	engine_destroy(E),
	write(unfetched_post(R)), nl.

post_while_suspended :-
	engine_create(x, (engine_yield(first), engine_fetch(P), engine_yield(fetched(P))), E),
	engine_next(E, A1),
	engine_post(E, hello),
	engine_next(E, A2),
	engine_next(E, A3),
	engine_destroy(E),
	write(post_while_suspended(A1, A2, A3)), nl.

main :-
	check(in_order),
	check(ping_pong),
	check(backtracking),
	check(inside_control),
	check(unfetched_post),
	check(post_while_suspended).
