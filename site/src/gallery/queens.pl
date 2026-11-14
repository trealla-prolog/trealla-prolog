% Place N queens on an N×N board, no two sharing a row, column
% or diagonal. Qs gives each column's queen as its row number.
:- use_module(library(clpz)).
:- use_module(library(aggregate)).

n_queens(N, Qs) :-
	length(Qs, N),
	Qs ins 1..N,
	safe(Qs).

safe([]).
safe([Q|Qs]) :- no_attack(Q, Qs, 1), safe(Qs).

no_attack(_, [], _).
no_attack(Q, [Q1|Qs], D) :-
	Q #\= Q1,
	abs(Q - Q1) #\= D,
	D1 is D + 1,
	no_attack(Q, Qs, D1).

% Labeling first-fail tries the column with the fewest rows left.
queens(N, Qs) :- n_queens(N, Qs), labeling([ff], Qs).
%?- once(queens(8, Qs)).
%?- once(queens(30, Qs)).
%?- aggregate_all(count, queens(8, _), Solutions).
