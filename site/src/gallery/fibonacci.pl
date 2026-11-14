% Doubly recursive Fibonacci, made linear by tabling: each fib(N)
% is computed once, then looked up. Integers grow as needed.
:- use_module(library(tabling)).
:- table fib/2.

fib(0, 0).
fib(1, 1).
fib(N, F) :-
	N > 1,
	A is N - 1, B is N - 2,
	fib(A, FA), fib(B, FB),
	F is FA + FB.
%?- fib(100, F).
%?- fib(1000, F).
