:- initialization(main).
:- use_module(library(dif)).

% UWN's dif/2 conformity table, from
% https://www.complang.tuwien.ac.at/ulrich/iso-prolog/dif
% A dif/2 goal left in an answer is written maybe.

1 ?- dif(1,2).
   true.

2 ?- dif(1,Y), Y=1.
   false.

3 ?- dif(1,Y), Y=2.
   Y = 2.

4 ?- dif(X,-Y), X= -Y.
   false.

5 ?- dif(X,Y), X=Y.
   false.

6 ?- dif(X,Y), X=Y, X=1.
   false.

7 ?- dif(-X,-Y), X=Y.
   false.

8 ?- dif(-X,-Y), X=Y, X=1.
   false.

9 ?- dif(-X,X).
   true
|  maybe.

10 ?- dif(-X,Y), X=Y.
   X = Y
|  X = Y, maybe.

11 ?- X=Y, dif(X-Y,1-2).
   X = Y.

12 ?- dif(X-Y,1-2), X=Y.
   X = Y.

13 ?- X=Y, Y=1, dif(X-Y,1-2).
   X = 1, Y = 1.

14 ?- dif(X-Y,1-2), X=Y, Y=1.
   X = 1, Y = 1.

15 ?- dif(X-Y,1-2), X=Y, X=2.
   X = 2, Y = 2.

16 ?- dif(A-C,B-D), C-D=z-z, A-B=1-2.
   A = 1, C = z, B = 2, D = z.

17 ?- A-B=1-2, C-D=z-z, dif(A-C,B-D).
   A = 1, B = 2, C = z, D = z.

18 ?- dif(A,[C|B]), A=[[]|_], A=[B].
   A = [[]], B = [], maybe.

19 ?- dif([E],[/]), E=1.
   E = 1.

20 ?- dif([a],B), B=[_|_], B=[b].
   B = [b].

21 ?- dif([],A), A = [_].
   A = [_A].

22 ?- A = [_], dif([],A).
   A = [_A].

23 ?- dif(X,a), copy_term(X,a).
   maybe
|  false.

24 ?- findall(X,dif(X,a),[a]).
   true
|  false.

25 ?- setof(t,dif(X,a),_), X = a.
   X = a
|  false.

26 ?- setof(t,(dif(X,a);dif(X,b)),_), X = a.
   X = a.

% Rational tree unification

t1 ?- \+ \+ -X=X.
   sto, true.

t2 ?- -X=X, -Y=Y, X\=Y.
   sto, false.

t3 ?- -X=X, dif(X,1).
   sto, X = -X.

t4 ?- -X=X, -Y=Y, dif(X,Y).
   sto, false.

t5 ?- dif(X,Y), -X=X, -Y=Y.
   sto, false.

t6 ?- A=[[]|A],dif(A,B),B=[[]|A].
   sto, false.

t7 ?- dif(-X,X),-Y=Y,X=Y.
   sto, false.

t8 ?- -X=X, dif(X,Y),X=Y.
   sto, false.

% Occurs-check unification, with occurs_check set to true around each

o1 ?- setup_call_cleanup(set_prolog_flag(occurs_check,true), -X=X, set_prolog_flag(occurs_check,false)).
   sto, false.

o2 ?- setup_call_cleanup(set_prolog_flag(occurs_check,true), dif(-X,X), set_prolog_flag(occurs_check,false)).
   true.

o3 ?- setup_call_cleanup(set_prolog_flag(occurs_check,true), (dif(-X,Y), X=Y), set_prolog_flag(occurs_check,false)).
   Y = X.

main :-
	use_module(library(quads)),
	run_quads.
