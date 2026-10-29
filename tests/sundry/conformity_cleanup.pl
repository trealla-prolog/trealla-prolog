:- initialization(main).

% The examples of UWN's setup_call_cleanup/3 draft, 7.8.11.4, from
% https://www.complang.tuwien.ac.at/ulrich/iso-prolog/cleanup
% Where the page allows either outcome, Trealla's is encoded. A query
% with more than one answer is wrapped in findall/3, as the harness
% cuts each answer off and that runs the cleanup there.

var_name([C|Cs]) --> [C], { C == '_' }, name_chars(Cs).

name_chars([C|Cs]) --> [C], { char_code(C, X), ( X >= 0'0, X =< 0'9 ; X >= 0'A, X =< 0'Z ; X >= 0'a, X =< 0'z ; X =:= 0'_ ) }, !, name_chars(Cs).
name_chars([]) --> [].

out_14 --> "1+2>", var_name(_), "+", var_name(_).
out_15 --> "1+", var_name(_).
out_18 --> "1+2>", var_name(_).
out_21 --> "1+", var_name(_), ">", var_name(_).

1 ?- setup_call_cleanup(fail, _, _).
   false.

2 ?- setup_call_cleanup(throw(ex), _, _).
   throw(ex).

3 ?- setup_call_cleanup(true, throw(unthrown),_).
   instantiation_error.

4 ?- setup_call_cleanup(true, true, ( true ; throw(x) )).
   true.

5 ?- setup_call_cleanup(true, X = 1, X = 2).
   X = 1.

6 ?- setup_call_cleanup(true, true, X = 2).
   X = 2.

7 ?- setup_call_cleanup(true, X=true,X).
   instantiation_error.

8 ?- setup_call_cleanup(X=throw(ex), true, X).
   throw(ex).

9 ?- setup_call_cleanup(true, true, fail).
   true.

10 ?- setup_call_cleanup(S=1, G=2, C=3).
   S = 1, G = 2, C = 3.

11 ?- setup_call_cleanup((S=1;S=2), G=3, C=4).
   S = 1, G = 3, C = 4.

12 ?- setup_call_cleanup(S=1,G=2,write(S+G)).
   outputs("1+2"), S = 1, G = 2.

13 ?- findall(S-G, setup_call_cleanup(S=1,(G=2;G=3),write(S+G)), L).
   outputs("1+3"), L = [1-2,1-3].

14 ?- setup_call_cleanup(S=1,G=2,write(S+G>A+B)), A=3, B=4.
   outputs(out_14), S = 1, G = 2, A = 3, B = 4.

15 ?- findall(S-G, setup_call_cleanup(S=1,(G=2;G=3,throw(x)),write(S+G)), L).
   outputs(out_15), throw(x).

% The page's open(f,read,S) example, on a file main/0 writes

16 ?- setup_call_cleanup(open('conformity_cleanup.tmp',read,S),read(S,X),close(S)).
   S = ..., X = f(x).

% Events in the continuation

17 ?- setup_call_cleanup(S=1,(G=2;G=3),write(S+G>B)), B=4, !.
   outputs("1+2>4"), S = 1, G = 2, B = 4.

18 ?- setup_call_cleanup(S=1,G=2,write(S+G>B)),B=3,!.
   outputs(out_18), S = 1, G = 2, B = 3.

19 ?- setup_call_cleanup(S=1,(G=2;fail),write(S+G>B)), B=3, !.
   outputs("1+2>3"), S = 1, G = 2, B = 3.

20 ?- setup_call_cleanup(S=1,(G=2;S=2),write(S+G>B)), B=3, !.
   outputs("1+2>3"), S = 1, G = 2, B = 3.

21 ?- setup_call_cleanup(S=1,(G=2;G=3), write(S+G>B)), B=4, throw(x).
   outputs(out_21), throw(x).

22 ?- setup_call_cleanup(S=1,(G=2;G=3), write(S+G>B)), B=4, !, throw(x).
   outputs("1+2>4"), throw(x).

23 ?- setup_call_cleanup(true, (X=1;X=2), write(a)),
      setup_call_cleanup(true,(Y=1;Y=2),write(b)), throw(x).
   outputs("ba"), throw(x).

24 ?- setup_call_cleanup(true, (X=1;X=2), write(a)),
      setup_call_cleanup(true,(Y=1;Y=2),write(b)), !.
   outputs("ba"), X = 1, Y = 1.

% Multiple exceptions

25 ?- catch(setup_call_cleanup(true,throw(goal),throw(cl)), Pat, true).
   Pat = goal.

26 ?- catch(( setup_call_cleanup(true,(G=1;G=2),throw(cl)), throw(cont)), Pat, true).
   Pat = cont.

27 ?- catch(
   setup_call_cleanup(true, throw(a),
      setup_call_cleanup(true,fail,throw(b))
      ),
   Pat, true).
   Pat = a.

main :-
	F = 'conformity_cleanup.tmp',
	open(F, write, S),
	write(S, 'f(x).\n'),
	close(S),
	use_module(library(quads)),
	run_quads,
	delete_file(F).
