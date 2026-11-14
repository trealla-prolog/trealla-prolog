% Digits of π from Machin's formula,
%   π = 16·arctan(1/5) − 4·arctan(1/239),
% in exact integer arithmetic scaled by a power of ten.
pi_digits(N, Pi) :-
	Unity is 10^(N + 10),
	arccot(5, Unity, A),
	arccot(239, Unity, B),
	Pi is (16*A - 4*B) // 10^10.

% arccot(X) = 1/X - 1/(3X³) + 1/(5X⁵) - ...
arccot(X, Unity, Sum) :-
	X2 is X * X,
	P is Unity // X,
	arccot(P, X2, 3, -1, P, Sum).

arccot(0, _, _, _, Sum, Sum) :- !.
arccot(P0, X2, N, Sign, Acc, Sum) :-
	P is P0 // X2,
	Acc1 is Acc + Sign * (P // N),
	N1 is N + 2,
	Sign1 is -Sign,
	arccot(P, X2, N1, Sign1, Acc1, Sum).

% The last K of the first N decimals.
pi_tail(N, K, Tail) :- pi_digits(N, Pi), Tail is Pi mod 10^K.
%?- pi_digits(100, Pi).
%?- pi_tail(2000, 20, Tail).
