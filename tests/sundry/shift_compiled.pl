% A control construct compiled inline, coming after a shift/1 in the same
% clause, must run whole in the continuation. Its instructions only work
% in place - jump offsets, a skip to the else or recovery code - but the
% continuation was built by copying them one at a time, following the
% jump over the else or recovery code: an if-then-else taking its else
% branch threw a type error, \+ in a condition lost its answer, and so did
% a catch/3 that caught something. Now the construct goes in as its
% source term. Results match SWI.

:- initialization(main).

k(C) :- ( C = cont(K) -> call(K) ; call(C) ).
answers(P, L) :- findall(Y, (reset(call(P, Y), _, C), k(C)), L).

p_ite_else(Y) :- X = 2, shift(a), ( X == 1 -> Y = then ; Y = else ).
p_ite_then(Y) :- X = 1, shift(a), ( X == 1 -> Y = then ; Y = else ).
p_ite_chain(Y) :- X = 3, shift(a), ( X == 1 -> Y = one ; X == 2 -> Y = two ; Y = other ).
p_ite_cond_choices(Y) :- shift(a), ( member(Z, [1,2]), Z > 1 -> Y = z(Z) ; Y = none ).
p_disj(Y) :- shift(a), ( Y = left ; Y = right ).
p_if_then(Y) :- shift(a), ( true -> Y = yes ).
p_soft(Y) :- shift(a), ( member(Z, [1,2]) *-> Y = got(Z) ; Y = none ).
p_soft_else(Y) :- shift(a), ( fail *-> Y = got ; Y = none ).
p_soft_then(Y) :- shift(a), ( member(Y, [s1,s2]) *-> true ).
p_if3(Y) :- shift(a), if(fail, Y = then, Y = else).
p_if3_choices(Y) :- shift(a), if(member(Z, [1,2]), Y = z(Z), Y = none).
p_not(Y) :- shift(a), ( \+ fail -> Y = yes ; Y = no ).
p_not_true(Y) :- shift(a), ( \+ true -> Y = yes ; Y = no ).
p_notunify(Y) :- shift(a), ( a \= b -> Y = differ ; Y = same ).
p_ignore(Y) :- shift(a), ignore(fail), Y = ignored.
p_call(Y) :- shift(a), call(member(Y, [c1,c2])).
p_calln(Y) :- shift(a), call(member, Y, [n1,n2]).
p_once(Y) :- shift(a), once(member(Y, [o1,o2])).
p_catch_throw(Y) :- shift(a), catch(throw(oops), E, Y = caught(E)).
p_catch_ok(Y) :- shift(a), catch(member(Y, [k1,k2]), _, true).
p_catch_nested(Y) :- X = 2, shift(a), catch(( X == 1 -> throw(one) ; throw(two) ), E, Y = caught(E)).
p_twice(Y) :- call(shift(a)), X = 1, catch(shift(b), _, true), Y = X-2.

w_twice(Y-B) :- reset(p_twice(Y), _, C1), reset(k(C1), B, C2), k(C2).

main :-
	forall(
		member(P, [p_ite_else, p_ite_then, p_ite_chain, p_ite_cond_choices, p_disj, p_if_then,
			p_soft, p_soft_else, p_soft_then, p_if3, p_if3_choices, p_not, p_not_true, p_notunify,
			p_ignore, p_call, p_calln, p_once, p_catch_throw, p_catch_ok, p_catch_nested]),
		(	( catch(answers(P, L), E, L = uncaught(E)) -> true ; L = failed ),
			write(P), write(': '), writeq(L), nl
		)
	),
	(	catch(w_twice(R), E, R = uncaught(E)) -> true ; R = failed ),
	write(w_twice), write(': '), writeq(R), nl.
