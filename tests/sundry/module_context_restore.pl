% M:Goal runs Goal in M, then the caller's own module is back: call/1,N, once/1, ignore/1
% and current_predicate(M:PI) left M in place, so a later assertz/1 put its clause in lists.

:- initialization(main).

where(N) :-
	findall(M, (member(M, [user,lists]), catch(predicate_property(M:N, number_of_clauses(C)), _, fail), C > 0), Ms),
	writeq(N-Ms), nl.

main :-
	current_predicate(lists:append/3), assertz(f1), where(f1),
	call_cleanup((call(lists:member(_, [a,b])), assertz(f2)), true), where(f2),
	findall(x, (call(lists:member(_, [a])), assertz(f3)), _), where(f3),
	findall(x, (once(lists:member(_, [a])), assertz(f4)), _), where(f4),
	findall(x, (ignore(lists:member(_, [a])), assertz(f5)), _), where(f5),
	findall(x, (call(lists:member(a), [a]), assertz(f6)), _), where(f6),
	G = (lists:member(_, [a,b]), assertz(f7)), once(G), where(f7),
	findall(N, (current_predicate(lists:N/3), N == append), _), assertz(f8), where(f8).
