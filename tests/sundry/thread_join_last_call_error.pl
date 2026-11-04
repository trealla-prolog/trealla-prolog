% A builtin's error as the last goal of a thread's clause joined as a bare exception(error):
% the frame was still resumed after the throw went unhandled, winding the heap back over the
% ball. Several such threads at once could crash.

:- initialization(main).

w1 :- atom_length(1, _).
w2 :- atom_length(1, _), true.
w3 :- functor(_, _, _).
w4 :- member(_, [a]), clause(atom_length(_, _), true).

has_threads :- catch(thread_create(true, T, []), _, fail), thread_join(T, _).

joined(G, S) :-
	(	has_threads -> thread_create(G, T, []), thread_join(T, S)
	;	catch(G, E, true), S = exception(E)			% no threads in this build
	).

crowd(Ss) :-
	(	has_threads
	->	findall(S, (between(1, 50, _), findall(T, (between(1, 4, _), thread_create(w4, T, [])), Ts), member(T, Ts), thread_join(T, S)), Ss)
	;	Ss = []
	).

main :-
	forall(member(G, [w1,w2,w3,w4]),
		(	catch(G, E, true), joined(G, S),
			(	S == exception(E) -> writeq(G-ok) ; writeq(G-S) ), nl
		)),
	crowd(Ss),
	(	member(S, Ss), \+ subsumes_term(exception(error(permission_error(_,_,_),_)), S)
	->	writeq(crowd-S)
	;	writeq(crowd-ok)
	),
	nl.
