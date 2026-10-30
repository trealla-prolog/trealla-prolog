% A chain of nested SCCs, each escaping to its parent: r(1,N) calls
% r(2,N) fresh, and so on down to r(N,N), which calls r(1,N) back. Every
% '$tbl_pop_scc' merges into the parent. Merging re-tagged the child's
% tables and re-added its dependencies through a linear scan, and
% completion copied the SCC's dependencies to every incremental table:
% cubic, then quadratic. 20000 levels took far longer than run.sh's 60 s
% timeout; it is well under a second now.

:- use_module(library(tabling)).

:- initialization(main).

:- table r/2.

r(A, N) :- step(A, B, N), r(B, N).
r(N, N).

step(A, B, N) :- A < N, B is A + 1.
step(N, 1, N).

% The same chain over an incremental predicate, so dependencies are
% recorded, merged and handed to the tables; then an update that must
% still invalidate them.

:- dynamic(lim/1).
:- incremental(lim/1).
:- table ri/2 as incremental.

ri(A, N) :- istep(A, B, N), ri(B, N).
ri(N, N).

istep(A, B, N) :- lim(L), A < L, A < N, B is A + 1.
istep(N, 1, N).

lim(1000000).

main :-
	( r(1, 20000) -> write(plain_chain_ok) ; write(plain_chain_FAILED) ), nl,
	( ri(1, 20000) -> write(incremental_chain_ok) ; write(incremental_chain_FAILED) ), nl,
	retract(lim(_)),
	assertz(lim(10)),
	( ri(1, 20000) -> write(invalidation_FAILED) ; write(invalidation_ok) ), nl.
