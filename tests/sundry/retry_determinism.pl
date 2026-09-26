% A retried call keeps what find_key() learned of its bound arguments, so the last clause that can match still drops the choicepoint.

:- initialization(main).

p(chars, 0) :- fail.
p(chars, 1).
p(codes, 0).
p(codes, 1).

q(F, B) :- p(F, B).

w(chars, _, _, _, 0) :- fail.
w(chars, _, _, _, 1).
w(codes, _, _, _, 0).
w(codes, _, _, _, 1).

det(G, R) :- call_cleanup(G, Det = true), ( Det == true -> R = det ; R = nondet ), !.

main :-
	F = chars,
	det(p(F, B1), R1), write(bound_through_var(B1-R1)), nl,
	det(p(chars, B2), R2), write(bound_directly(B2-R2)), nl,
	G = p(F, B3), det(G, R3), write(via_call(B3-R3)), nl,
	det(q(F, B4), R4), write(via_caller(B4-R4)), nl,
	det(w(F, x, y, z, B5), R5), write(wide(B5-R5)), nl,
	findall(B6, p(F, B6), L6), write(all(L6)), nl.
