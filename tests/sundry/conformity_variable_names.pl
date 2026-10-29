:- initialization(main).
:- use_module(library(freeze)).

% UWN's variable_names/1 conformity table, the Cor.3 (Codex) column, from
% https://www.complang.tuwien.ac.at/ulrich/iso-prolog/variable_names
% A row starting /**/ stands for its own goals followed by the last row
% before it without one; for a read the text after /**/ is the input.
% The page's file f is conformity_variable_names.tmp here, and the one
% open/4 that succeeds closes its stream.

var_name([C|Cs]) --> [C], { C == '_' }, name_chars(Cs).

name_chars([C|Cs]) --> [C], { char_code(C, X), ( X >= 0'0, X =< 0'9 ; X >= 0'A, X =< 0'Z ; X >= 0'a, X =< 0'z ; X =:= 0'_ ) }, !, name_chars(Cs).
name_chars([]) --> [].

a_var --> var_name(_).
f_var --> "f(", var_name(_), ")".

1 ?- write_term(T,[quoted(true),variable_names([N=T])]).
   instantiation_error.

2 ?- N = 'X', write_term(T,[quoted(true),variable_names([N=T])]).
   outputs("X"), N = 'X'.

3 ?- N = T, write_term(T,[quoted(true),variable_names([N=T])]).
   instantiation_error.

4 ?- N = '_', write_term(T,[quoted(true),variable_names([N=T])]).
   outputs("_"), N = '_'.

65 ?- N = '_/*.*/', write_term(T,[quoted(true),variable_names([N=T])]).
   outputs("_/*.*/"), N = '_/*.*/'.

5 ?- N = x, write_term(T,[quoted(true),variable_names([N=T])]).
   outputs("x"), N = x.

6 ?- N = 'x+y', write_term(T,[quoted(true),variable_names([N=T])]).
   outputs("x+y"), N = 'x+y'.

50 ?- N = '))', write_term(T,[quoted(true),variable_names([N=T])]).
   outputs("))"), N = '))'.

7 ?- N = 7, write_term(T,[quoted(true),variable_names([N=T])]).
   domain_error(write_option,variable_names(...)).

8 ?- N = 1+2, write_term(T,[quoted(true),variable_names([N=T])]).
   domain_error(write_option,variable_names(...)).

9 ?- N = '$VAR'(9), write_term(T,[quoted(true),variable_names([N=T])]).
   domain_error(write_option,variable_names(...)).

10 ?- T = a, write_term(T,[quoted(true),variable_names([N=T])]).
   instantiation_error.

11 ?- T = a, N = 'Any', write_term(T,[quoted(true),variable_names([N=T])]).
   outputs("a"), T = a, N = 'Any'.

12 ?- T = '$VAR'(9), N = '_', write_term(T,[quoted(true),variable_names([N=T])]).
   outputs("'$VAR'(9)"), T = '$VAR'(9), N = '_'.

74 ?- T = f(_), N = 'Bad', write_term(T,[quoted(true),variable_names([N=T])]).
   outputs(f_var), T = f(_A), N = 'Bad'.

28 ?- freeze(T,throw(g(T))),N = 'X', write_term(T,[quoted(true),variable_names([N=T])]).
   outputs("X"), N = 'X', maybe.

13 ?- write_term(T,[quoted(true),variable_names(['X'=X,'Y'=Y,'Z'=Z])]).
   outputs(a_var), true.

14 ?- T=(X,Y,Z), write_term(T,[quoted(true),variable_names(['X'=X,'Y'=Y,'Z'=Z])]).
   outputs("X,Y,Z"), T = (X,Y,Z).

15 ?- Z=Y, T=(X,Y,Z), write_term(T,[quoted(true),variable_names(['X'=X,'Y'=Y,'Z'=Z])]).
   outputs("X,Y,Y"), Z = Y, T = (X,Y,Y).

16 ?- Z=Y, Y=X, T=(X,Y,Z), write_term(T,[quoted(true),variable_names(['X'=X,'Y'=Y,'Z'=Z])]).
   outputs("X,X,X"), Z = X, Y = X, T = (X,X,X).

17 ?- T=(Y,Z), write_term(T,[quoted(true),variable_names(['X'=X,'Y'=Y,'Z'=Z])]).
   outputs("Y,Z"), T = (Y,Z).

18 ?- T=(Z,Y), write_term(T,[quoted(true),variable_names(['X'=X,'Y'=Y,'Z'=Z])]).
   outputs("Z,Y"), T = (Z,Y).

19 ?- write_term(T,[quoted(true),variable_names(['Z'=Z,'Y'=Y,'X'=X])]).
   outputs(a_var), true.

20 ?- T=(X,Y,Z), write_term(T,[quoted(true),variable_names(['Z'=Z,'Y'=Y,'X'=X])]).
   outputs("X,Y,Z"), T = (X,Y,Z).

21 ?- Z=Y, T=(X,Y,Z), write_term(T,[quoted(true),variable_names(['Z'=Z,'Y'=Y,'X'=X])]).
   outputs("X,Z,Z"), Z = Y, T = (X,Y,Y).

22 ?- Z=Y, Y=X, T=(X,Y,Z), write_term(T,[quoted(true),variable_names(['Z'=Z,'Y'=Y,'X'=X])]).
   outputs("Z,Z,Z"), Z = X, Y = X, T = (X,X,X).

23 ?- T=(Y,Z), write_term(T,[quoted(true),variable_names(['Z'=Z,'Y'=Y,'X'=X])]).
   outputs("Y,Z"), T = (Y,Z).

24 ?- T=(Z,Y), write_term(T,[quoted(true),variable_names(['Z'=Z,'Y'=Y,'X'=X])]).
   outputs("Z,Y"), T = (Z,Y).

25 ?- write_term(T,[quoted(true),variable_names(['X'=Z,'X'=Y,'X'=X])]).
   outputs(a_var), true.

26 ?- T=(X,Y,Z), write_term(T,[quoted(true),variable_names(['X'=Z,'X'=Y,'X'=X])]).
   outputs("X,X,X"), T = (X,Y,Z).

27 ?- T=(1,2,3), T=(X,Y,Z), write_term(T,[quoted(true),variable_names(['X'=Z,'X'=Y,'X'=X])]).
   outputs("1,2,3"), T = (1,2,3), X = 1, Y = 2, Z = 3.

32 ?- read_term(T,[variable_names(VN_list)]),VN_list=[_=1,_=2,_=3],writeq(VN_list).
   waits.

29 ?- read_term(T,[variable_names(VN_list)]),VN_list=[_=1,_=2,_=3],writeq(VN_list).
   inputs("B+C+A+B+C+A."), peeks("\n"), outputs("['B'=1,'C'=2,'A'=3]"), T = 1+2+3+1+2+3, VN_list = ['B'=1,'C'=2,'A'=3].

30 ?- write_term(T,[variable_names(VN_list)]).
   instantiation_error.

31 ?- VN_list = 1, write_term(T,[variable_names(VN_list)]).
   domain_error(write_option,variable_names(...)).

33 ?- VN_list = [[]], write_term(T,[variable_names(VN_list)]).
   domain_error(write_option,variable_names(...)).

34 ?- VN_list = non_list, write_term(T,[variable_names(VN_list)]).
   domain_error(write_option,variable_names(...)).

35 ?- VN_list = [T='T'|non_list], write_term(T,[variable_names(VN_list)]).
   domain_error(write_option,variable_names(...))
|  instantiation_error.

52 ?- VN_list = ['T'=T|_], write_term(T,[variable_names(VN_list)]).
   instantiation_error.

51 ?- VN_list = ['T'=T|non_list], write_term(T,[variable_names(VN_list)]).
   domain_error(write_option,variable_names(...)).

36 ?- VN_list = [T-'T'], write_term(T,[variable_names(VN_list)]).
   domain_error(write_option,variable_names(...)).

63 ?- VN_list = [_,a], write_term(T,[variable_names(VN_list)]).
   domain_error(write_option,variable_names(...))
|  instantiation_error.

64 ?- VN_list = [a,_], write_term(T,[variable_names(VN_list)]).
   domain_error(write_option,variable_names(...))
|  instantiation_error.

66 ?- VN_list = [a|_], write_term(T,[variable_names(VN_list)]).
   domain_error(write_option,variable_names(...))
|  instantiation_error.

67 ?- VN_list = [i=i,7=i], write_term(T,[variable_names(VN_list)]).
   domain_error(write_option,variable_names([i=i,7=i])).

68 ?- VN_list = [_,_], write_term(T,[variable_names(VN_list)]).
   instantiation_error.

43 ?- write_term(-X^2,[variable_names(['X'=X])]).
   outputs("- (X^2)"), true.

44 ?- X=1, write_term(-X^2,[variable_names(['X'=X])]).
   outputs("- (1^2)"), X = 1.

37 ?- open('conformity_variable_names.tmp',write,_,[O]).
   instantiation_error.

38 ?- O = 1, open('conformity_variable_names.tmp',write,_,[O]).
   domain_error(stream_option,1).

56 ?- O = typex(_), open('conformity_variable_names.tmp',write,_,[O]).
   domain_error(stream_option,typex(_)).

57 ?- O = typex(1), open('conformity_variable_names.tmp',write,_,[O]).
   domain_error(stream_option,typex(1)).

62 ?- O = typex(s(_)), open('conformity_variable_names.tmp',write,_,[O]).
   domain_error(stream_option,typex(s(_))).

39 ?- O = type(text), open('conformity_variable_names.tmp',write,S,[O]), close(S).
   O = type(text), S = ... .

40 ?- O = type(1), open('conformity_variable_names.tmp',write,_,[O]).
   domain_error(stream_option,type(1)).

41 ?- O = type(_), open('conformity_variable_names.tmp',write,_,[O]).
   instantiation_error.

60 ?- O = alias(_), open('conformity_variable_names.tmp',write,_,[O]).
   instantiation_error.

42 ?- O = type(nontype), open('conformity_variable_names.tmp',write,_,[O]).
   domain_error(stream_option,type(nontype)).

61 ?- O = alias(1), open('conformity_variable_names.tmp',write,_,[O]).
   domain_error(stream_option,alias(1)).

45 ?- read_term(T,[variable_names(VN_list)]).
   waits.

46 ?- read_term(T,[variable_names(VN_list)]).
   inputs("a."), peeks("\n"), T = a, VN_list = [].

47 ?- VN_list = 42, read_term(T,[variable_names(VN_list)]).
   waits.

48 ?- VN_list = 42, read_term(T,[variable_names(VN_list)]).
   inputs("a."), peeks("\n"), false.

49 ?- VN_list = 42, read_term(T,[variable_names(VN_list)]).
   inputs("a b."), peeks("\n"), syntax_error(...).

53 ?- write_term(S,[quoted(true),variable_names([N=T])]).
   instantiation_error.

54 ?- S=1+T,N='/*r*/V', write_term(S,[quoted(true),variable_names([N=T])]).
   outputs("1+/*r*/V"), S = 1+T, N = '/*r*/V'
|  outputs("1+ /*r*/V"), S = 1+T, N = '/*r*/V'.

55 ?- S=1+T,N=' /*r*/V', write_term(S,[quoted(true),variable_names([N=T])]).
   outputs("1+ /*r*/V"), S = 1+T, N = ' /*r*/V'.

58 ?- S=1+T,N=(+), write_term(S,[quoted(true),variable_names([N=T])]).
   outputs("1++"), S = 1+T, N = (+)
|  outputs("1+ +"), S = 1+T, N = (+).

59 ?- S=T+1,N=(+), write_term(S,[quoted(true),variable_names([N=T])]).
   outputs("++1"), S = T+1, N = (+)
|  outputs("+ +1"), S = T+1, N = (+).

73 ?- S=(1 is T),N='X', write_term(S,[quoted(true),variable_names([N=T])]).
   outputs("1 is X"), S = (1 is T), N = 'X'.

69 ?- read_term(T,[singletons(1)]).
   waits.

70 ?- read_term(T,[singletons(1)]).
   inputs("a."), peeks("\n"), false.

71 ?- write_term(T,[ variable_names(['Bad'=T]), variable_names(['Good'=T])]).
   outputs("Good"), true.

72 ?- read_term(T,[singletons([])]).
   waits.

main :-
	use_module(library(quads)),
	run_quads,
	catch(delete_file('conformity_variable_names.tmp'), _, true).
