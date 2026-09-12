% A compiled call/1, call/N, *-> or if/3 whose goal leaves choices keeps
% its barrier for them, but a cut later in the clause must still reach
% the clause's own alternatives. drop_barrier() used to leave the frame
% in the barrier's cut generation, so these kept the clause's last
% alternative (9, 8) or, for if/3, fell into its else branch.

:- initialization(main).

p01(X) :- call(member(X,[1,2,3])), !.
p01(9).
p02(X) :- call(member, X, [1,2,3]), !.
p02(9).
p03(X) :- (member(X,[1,2,3]) *-> true), !.
p03(9).
p04(X) :- (member(X,[1,2,3]) *-> true ; true), !.
p04(9).
p05(X) :- if(member(X,[1,2,3]), true, true), !.
p05(9).
p06(X) :- call(lists:member(X,[1,2,3])), !.
p06(9).
p07(X) :- call(member(X,[1,2,3])), X >= 2, !.
p07(9).
p08(X) :- call(call(member(X,[1,2]))), !.
p08(9).
p09(X) :- call(member(X,[1,2,3])), !, X > 5.
p09(9).
p10(X) :- (member(X,[1,2,3]) *-> ! ; true).
p10(9).
p11(X) :- G = member(X,[1,2,3]), call(G), !.
p11(9).
p12(X-Y) :- call(member(X,[1,2])), call(member(Y,[a,b])), !.
p12(9-9).
p13(X) :- if(member(X,[1,2,3]), X > 1, true), !.
p13(9).
p14(X) :- call(member(X,[1,2,3])), !.
p14(X) :- X = 8.
p14(9).

% The goal's own choices and cuts are untouched.

q01(X) :- call(member(X,[1,2,3])), X > 1.
q02(X) :- ( call((member(X,[1,2,3]), !)) ; X = 4 ).
q03(X) :- call(member(Y,[1,2,3])), q03a(Y, X).
q03(9).
q03a(Y, X) :- call(member(X,[Y,Y])), !.

% An if/3 or *-> condition whose last choice fails, not just runs out.

r01(X) :- if((member(X,[1,2]) ; fail), true, X = none).
r02(X) :- ((member(X,[1,2]) ; fail) *-> true ; X = none).
r03(X) :- if(fail, true, X = none).

main :-
	forall(
		member(P, [p01,p02,p03,p04,p05,p06,p07,p08,p09,p10,p11,p12,p13,p14,q01,q02,q03,r01,r02,r03]),
		(	G =.. [P, X],
			findall(X, G, L),
			write(P), write(': '), writeq(L), nl
		)
	).
