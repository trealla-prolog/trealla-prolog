% Five houses in a row, each with its own colour, owner, pet,
% drink and smoke. Who owns the zebra, and who drinks water?
% A house is h(Colour, Owner, Pet, Drink, Smoke).
right_of(A, B, [B,A|_]).
right_of(A, B, [_|Hs]) :- right_of(A, B, Hs).

next_to(A, B, Hs) :- right_of(A, B, Hs) ; right_of(B, A, Hs).

zebra(Owner, Drinker, Hs) :-
	Hs = [h(_,norwegian,_,_,_), _, h(_,_,_,milk,_), _, _],
	member(h(red,english,_,_,_), Hs),
	member(h(_,spanish,dog,_,_), Hs),
	member(h(green,_,_,coffee,_), Hs),
	member(h(_,ukrainian,_,tea,_), Hs),
	right_of(h(green,_,_,_,_), h(ivory,_,_,_,_), Hs),
	member(h(_,_,snails,_,old_gold), Hs),
	member(h(yellow,_,_,_,kools), Hs),
	next_to(h(_,_,_,_,chesterfield), h(_,_,fox,_,_), Hs),
	next_to(h(_,_,_,_,kools), h(_,_,horse,_,_), Hs),
	member(h(_,_,_,orange_juice,lucky_strike), Hs),
	member(h(_,japanese,_,_,parliament), Hs),
	next_to(h(_,norwegian,_,_,_), h(blue,_,_,_,_), Hs),
	member(h(_,Owner,zebra,_,_), Hs),
	member(h(_,Drinker,_,water,_), Hs).

houses :- once(zebra(_, _, Hs)), maplist(portray_clause, Hs).
%?- zebra(Owner, Drinker, _).
%?- houses.
