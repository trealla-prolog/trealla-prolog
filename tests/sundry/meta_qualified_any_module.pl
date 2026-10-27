% A : meta argument that is already M:G is left alone whatever M is, as in SWI
% and Scryer, both compiled in a clause and at the call.

:- initialization(main).

:- meta_predicate w(:).
w(X) :- \+ \+ (numbervars(X, 0, _), writeq(X), nl).

main :-
	w(_:foo), w(f(x):foo), w(1:foo), w(_:_:foo), w(a:foo),
	G1 = _:foo, w(G1),
	G2 = f(x):foo, w(G2),
	G3 = 1:foo, w(G3).
