% Sudoku as constraints: every row, column and 3×3 box holds
% the digits 1 to 9 once each.
:- use_module(library(clpz)).

sudoku(Rows) :-
	length(Rows, 9),
	maplist(same_length(Rows), Rows),
	append(Rows, Cells),
	Cells ins 1..9,
	maplist(all_distinct, Rows),
	transpose(Rows, Columns),
	maplist(all_distinct, Columns),
	Rows = [A, B, C, D, E, F, G, H, I],
	boxes(A, B, C), boxes(D, E, F), boxes(G, H, I).

boxes([], [], []).
boxes([A,B,C|R1], [D,E,F|R2], [G,H,I|R3]) :-
	all_distinct([A,B,C,D,E,F,G,H,I]),
	boxes(R1, R2, R3).

puzzle([[5,3,_, _,7,_, _,_,_],
        [6,_,_, 1,9,5, _,_,_],
        [_,9,8, _,_,_, _,6,_],
        [8,_,_, _,6,_, _,_,3],
        [4,_,_, 8,_,3, _,_,1],
        [7,_,_, _,2,_, _,_,6],
        [_,6,_, _,_,_, 2,8,_],
        [_,_,_, 4,1,9, _,_,5],
        [_,_,_, _,8,_, _,7,9]]).

solve :-
	puzzle(Rows),
	sudoku(Rows),
	maplist(label, Rows),
	maplist(portray_clause, Rows).
%?- solve.
