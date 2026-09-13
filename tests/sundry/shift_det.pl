% A successful shift/1 resumes after its reset/3 but left reset's barrier
% behind and the frame in the barrier's cut generation. So a shifted
% reset/3 never exited deterministically, a later cut stopped at the
% barrier instead of pruning the clause, and a loop of them kept every
% frame and choice alive (0.9 GB for a million iterations). Now shift/1
% tidies up as reset/3's goal exiting does. Results match SWI and Scryer.

:- initialization(main).

det_status(G, R) :- setup_call_cleanup(true, G, Det = det), ( Det == det -> R = det ; R = nondet ).

d_shift(R) :- det_status(reset(shift(a), _, _), R).
d_no_shift(R) :- det_status(reset(true, _, _), R).
d_shift_after(R) :- det_status(reset((true, shift(a)), _, _), R).
d_goal_choices(R) :- det_status(reset((member(_,[1,2]), shift(a)), _, _), R).

c_cut(L) :- findall(B, c_cut_(B), L).
c_cut_(B) :- reset(shift(a), B, _), !.
c_cut_(z).
c_cut_choices(L) :- findall(X-B, c_cut_choices_(X, B), L).
c_cut_choices_(X, B) :- reset((member(X,[1,2,3]), shift(s)), B, _), !.
c_cut_choices_(9, z).
c_cut_later(L) :- findall(B-Y, c_cut_later_(B, Y), L).
c_cut_later_(B, Y) :- reset(shift(a), B, _), member(Y, [1,2]), !.
c_cut_later_(z, z).

b_after(L) :- findall(B-Y, (reset(shift(a), B, _), member(Y, [1,2])), L).
b_clause_alts(L) :- findall(B, b_clause_alts_(B), L).
b_clause_alts_(B) :- reset(shift(a), B, _).
b_clause_alts_(z).

loop(0) :- !.
loop(N) :- reset(shift(x), _, _), N1 is N-1, loop(N1).
l_loop(R) :- det_status(loop(1000), R).

main :-
	forall(
		member(T, [d_shift, d_no_shift, d_shift_after, d_goal_choices,
			c_cut, c_cut_choices, c_cut_later, b_after, b_clause_alts, l_loop]),
		(	G =.. [T, R],
			(	catch(G, E, R = uncaught(E)) -> true ; R = failed ),
			write(T), write(': '), writeq(R), nl
		)
	).
