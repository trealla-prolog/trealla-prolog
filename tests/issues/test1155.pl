% Issue #1155: call_nth/2 type-checked the goal before looking at Nth,
% so call_nth(1, 0) and call_nth(_, 0) raised errors instead of failing.

:- initialization(main).

zero_nongoal ?- call_nth(1, 0).
   false.

zero_var ?- call_nth(_, 0).
   false.

negative ?- call_nth(1, -1).
   domain_error(not_less_than_zero,-1).

nth_nongoal ?- call_nth(1, 1).
   type_error(callable,1).

nth_var ?- call_nth(_, 1).
   instantiation_error.

count_nongoal ?- call_nth(1, _).
   type_error(callable,1).

second ?- call_nth(member(X, [a,b,c]), 2).
   X = b.

main :-
	use_module(library(quads)),
	run_quads.
