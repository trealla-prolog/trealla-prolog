% A catch/3 in a clause body is compiled inline. On an exception the
% handler resumes at a landing just before Recovery, so Recovery's first
% goal runs exactly once however the ball was raised. Resuming at Recovery
% itself with noskip set ran that goal twice on paths that execute the
% instruction before stepping past it - a compiled if-then-else in it then
% found its choice var already bound (seen in Logtalk's call/1 tests).

:- initialization(main).

b(X) :- Y = (true, X), call(Y).

rec(R) :- ( true -> R = ok ; R = no ).
rec_e(E, R) :- ( var(E) -> R = unbound ; E = error(F, _), R = bound(F) ).
count(N) :- bb_get(cnt, C), N is C+1, bb_put(cnt, N).

cut_after(X) :- catch(member(X, [1,2,3]), _, true), !.
cut_after(9).

nondet_rec(X) :- catch(throw(a), a, member(X, [1,2,3])).

into_goal(R) :-
	catch((member(X, [1,2]), (X == 2 -> throw(in) ; true)), E, R = inner(E)),
	( var(R) -> R = x(X) ; true ).

t(var_goal_ite) :- catch(b(_), E, (E = error(F,_) -> write(F) ; write(other))).
t(var_goal_call) :- catch(b(_), _, rec(R)), write(R).
t(throw_call) :- catch(throw(x), _, rec(R)), write(R).
t(ball_arg) :- catch(b(_), E, rec_e(E, R)), write(R).
t(in_condition) :- ( catch(b(_), E, rec_e(E, R)) -> write(outer(R)) ; write(no) ).
t(count_var_goal) :- bb_put(cnt, 0), catch(b(_), _, count(_)), bb_get(cnt, N), write(N).
t(count_builtin) :- bb_put(cnt, 0), catch(atom_length(_, _), _, count(_)), bb_get(cnt, N), write(N).
t(count_call_var) :- bb_put(cnt, 0), catch(call(_), _, count(_)), bb_get(cnt, N), write(N).
t(cut_after_nondet) :- findall(X, cut_after(X), L), write(L).
t(nondet_recovery) :- findall(X, nondet_rec(X), L), write(L).
t(backtrack_into_goal) :- findall(R, into_goal(R), L), write(L).

main :-
	forall(
		member(Name, [var_goal_ite, var_goal_call, throw_call, ball_arg,
			in_condition, count_var_goal, count_builtin, count_call_var,
			cut_after_nondet, nondet_recovery, backtrack_into_goal]),
		(	write(Name), write(': '),
			(	catch(t(Name), E, (write(uncaught), write(' '), writeq(E)))
			->	true
			;	write(failed)
			),
			nl
		)
	).
