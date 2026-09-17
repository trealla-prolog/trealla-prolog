% sub_atom/5 with a variable shared between two of its arguments.
%
% The before, length, after and sub-atom arguments were unified with
% cells dereferenced before any of the bindings, so a shared variable
% was bound twice and the later unify silently overwrote the earlier
% one: sub_atom(abcde,B,B,_,S) answered B=5, S=abcde, and sharing the
% sub-atom argument, as in sub_atom(a,B,_,_,B), crashed.

:- initialization(main).

main :-
	check(before_is_length, (findall(B-S, sub_atom(abcde,B,B,_,S), L1), L1 == [0-'',1-b,2-cd])),
	check(before_is_after, (findall(B-S, sub_atom(abcde,B,_,B,S), L2), L2 == [0-abcde,1-bcd,2-c])),
	check(length_is_after, (findall(L-S, sub_atom(abcde,_,L,L,S), L3), L3 == [2-bc,1-d,0-''])),
	check(all_three_shared, \+ sub_atom(abcde,X,X,X,_)),
	check(sub_atom_shared, \+ sub_atom(abc,B4,_,_,B4)),
	check(sub_atom_shared_empty, \+ sub_atom('',B5,_,_,B5)),
	check(bound_sub_atom_before_is_length, \+ sub_atom(abcab,B6,B6,_,ab)),
	check(bound_sub_atom_length_is_after, \+ sub_atom(abcab,_,L7,L7,ab)),
	check(bound_sub_atom_before_is_after, \+ sub_atom(abcab,B8,_,B8,ab)),
	check(bound_sub_atom_still_enumerates, (findall(B9, sub_atom(abcab,B9,_,_,ab), L9), L9 == [0,3])),
	check(sub_string_shared, (findall(B10, sub_string("abcde",B10,B10,_,_), L10), L10 == [0,1,2])).

check(Name, Goal) :-
	(   catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
