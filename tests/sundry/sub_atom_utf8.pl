% sub_atom/5 over atoms holding multibyte characters.
%
% The scan in do_sub_atom() works in bytes and used to report those
% bytes as positions: it stepped one byte per match, so an empty
% sub-atom matched inside a character, and it counted Length and After
% in bytes as well. sub_atom('abé',B,L,F,ab) answered After=2.

:- initialization(main).

utf8_atom('éab').
utf8_atom('abé').
utf8_atom('éàü').
utf8_atom('aébéc').
utf8_atom('日本語').
utf8_atom(abc).
utf8_atom('').

main :-
	check(empty_sub_atom, (findall([B,L,F], sub_atom('éab',B,L,F,''), X1), X1 == [[0,0,3],[1,0,2],[2,0,1],[3,0,0]])),
	check(after_in_chars, (findall([B,L,F], sub_atom('abé',B,L,F,ab), X2), X2 == [[0,2,1]])),
	check(before_in_chars, (findall([B,L,F], sub_atom('éab',B,L,F,ab), X3), X3 == [[1,2,0]])),
	check(repeated_multibyte, (findall([B,L,F], sub_atom('aébéc',B,L,F,'é'), X4), X4 == [[1,1,3],[3,1,1]])),
	check(three_byte_chars, (findall([B,L,F], sub_atom('日本語',B,L,F,本), X5), X5 == [[1,1,1]])),
	check(all_substrings, (findall(S, sub_atom('éab',_,_,_,S), X6), X6 == ['','é','éa','éab','',a,ab,'',b,''])),
	check(consistent, forall((utf8_atom(A), sub_atom(A,B7,L7,F7,S7)), consistent(A,B7,L7,F7,S7))),
	check(consistent_scanning, forall((utf8_atom(A2), sub_atom(A2,_,_,_,S8), sub_atom(A2,B8,L8,F8,S8)), consistent(A2,B8,L8,F8,S8))),
	check(sub_string_utf8, (findall([B,L,F], sub_string("abé",B,L,F,"ab"), X8), X8 == [[0,2,1]])).

% Every answer has to add up and to hold when asked back.

consistent(A, Before, Len, After, Sub) :-
	atom_length(A, N),
	N =:= Before + Len + After,
	atom_length(Sub, Len),
	sub_atom(A, Before, Len, After, Sub).

check(Name, Goal) :-
	(   catch(Goal, E, (format("~w threw ~q~n", [Name,E]), fail))
	->  format("~w ok~n", [Name])
	;   format("~w FAILED~n", [Name])
	).
