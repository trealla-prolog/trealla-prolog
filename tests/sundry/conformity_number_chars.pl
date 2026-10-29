:- initialization(main).

% UWN's number_chars/2 conformity table, in its page order, from
% https://www.complang.tuwien.ac.at/ulrich/iso-prolog/number_chars_cont

1 ?- number_chars(1.2,"1.2").
   true.

55 ?- number_chars(1.2,"1.20").
   true.

2 ?- number_chars(1.0e9,"1.0E9").
   true.

56 ?- number_chars(1.0e9,"1.0e9").
   true.

64 ?- number_chars(0.0,"-0.0").
   true.

3 ?- number_chars(1,"01").
   true.

65 ?- number_chars(10,"010").
   true.

66 ?- number_chars(N,"010").
   N=10.

67 ?- number_chars(N,"08").
   N=8.

68 ?- number_chars(N,"0b11").
   N=3.

69 ?- number_chars(N,"0o11").
   N=9.

70 ?- number_chars(N,"0x11").
   N=17.

4 ?- number_chars(1,"a").
   syntax_error(...).

5 ?- number_chars(1,[]).
   syntax_error(...).

6 ?- number_chars(1,[[]]).
   type_error(character,[]).

7 ?- number_chars(1,[' ',[]]).
   type_error(character,[]).

8 ?- number_chars(1,[0]).
   type_error(character,0).

9 ?- number_chars(1,[_,[]]).
   type_error(character,[]).

10 ?- number_chars(N,[X]).
   instantiation_error.

11 ?- number_chars(N,['0'|_]).
   instantiation_error.

12 ?- number_chars(N,'1').
   type_error(list,'1').

13 ?- number_chars(N,[a|a]).
   type_error(list,[a|a]).

14 ?- number_chars(N,[49]).
   type_error(character,49).

15 ?- number_chars(N,[]).
   syntax_error(...).

16 ?- number_chars(N,"3 ").
   syntax_error(...).

17 ?- number_chars(N,"3.").
   syntax_error(...).

18 ?- number_chars(N," 1").
   N = 1.

19 ?- number_chars(N,"\n1").
   N = 1.

20 ?- number_chars(N," 0'a").
   N = 0'a. N = 97.

58 ?- number_chars(N,"0'").
   syntax_error(...).

59 ?- number_chars(N,"0'\n").
   syntax_error(...).

60 ?- number_chars(N,"0'\\n").
   N=0'\n. N=10.

61 ?- number_chars(N,"0'\\7\\").
   N=7.

71 ?- number_chars(N,"0'\\7").
   syntax_error(...).

89 ?- number_chars(N,"0'\\0").
   syntax_error(...).

90 ?- number_chars(N,"0'\\o0\\").
   syntax_error(...).

62 ?- number_chars(N,"0'.").
   N=0'. . N = 46.

21 ?- number_chars(N,"- 1").
   N = -1.

91 ?- number_chars(- 1,"- 1").
   true.

54 ?- number_chars(N,"'-'1").
   N = -1.

22 ?- number_chars(N,"/**/1").
   N = 1.

23 ?- number_chars(N,"%\n1").
   N = 1.

74 ?- number_chars(N,"-%\n0").
   N = 0.

88 ?- number_chars(N,"0''").
   syntax_error(...).

75 ?- number_chars(N,"0'''").
   N = 39.

76 ?- number_chars(N,"0'\\'").
   N = 39.

77 ?- number_chars(N,"0' ").
   N = 32.

57 ?- number_chars(N,"- /**/1").
   N = -1.

24 ?- number_chars(N,"-/**/1").
   syntax_error(...).

63 ?- number_chars(N,"'\\\n-' 3").
   N= -3.

25 ?- number_chars(N,"1e1").
   syntax_error(...).

26 ?- number_chars(N,"1.0e").
   syntax_error(...).

27 ?- number_chars(N,"1.0ee").
   syntax_error(...).

28 ?- number_chars(N,"0x1").
   N = 1.

29 ?- number_chars(N,"0X1").
   syntax_error(...).

30 ?- number_chars(N,"1E1").
   syntax_error(...).

73 ?- number_chars(N,"(0)").
   syntax_error(...).

84 ?- number_chars(N,"0%0'").
   syntax_error(...).

78 ?- number_chars(N,"1.0e-8"),number_chars(N,L),number_chars(N,L).
   N= ...,L= ... .

47 ?- number_chars(1,['.'|_]).
   false.

48 ?- number_chars(N,"+1").
   syntax_error(...).

49 ?- number_chars(N,"+ 1").
   syntax_error(...).

50 ?- number_chars(N,"'+'1").
   syntax_error(...).

51 ?- number_chars(N,['11']).
   type_error(character,'11').

52 ?- number_chars(N,['1.1']).
   type_error(character,'1.1').

53 ?- number_chars(1+1,"2").
   type_error(number,1+1).

31 ?- number_chars(1,[C]).
   C = '1'.

32 ?- number_chars(1,[C,D]).
   false.

33 ?- number_chars(1,[C,C]).
   false.

34 ?- number_chars(0,[C,C]).
   false.

35 ?- number_chars(10,[C,D]).
   C = '1', D = '0'.

36 ?- number_chars(100,[C,D]).
   false.

37 ?- number_chars(N,[X|2]).
   type_error(list,[_|2])
|          instantiation_error.

38 ?- number_chars(N,[1|_]).
   instantiation_error
|          type_error(character,1).

39 ?- number_chars(V,[1|2]).
   type_error(list,[1|2])
|          type_error(character,1).

40 ?- number_chars([],1).
   type_error(number,[])
|          type_error(list,1).

41 ?- number_chars(1,1).
   type_error(list,1).

42 ?- number_chars(1,[a|2]).
   type_error(list,[a|2]).

43 ?- number_chars(1,[_|2]).
   type_error(list,[_|2]).

44 ?- number_chars(1,[[]|_]).
   type_error(character,[]).

45 ?- number_chars(1,[[]|2]).
   type_error(character,[])
|          type_error(list,[[]|2]).

72 ?- number_chars(V,[1,[],X|2]).
   type_error(list,[1,[],_A|2])
|  type_error(character,1)
|  type_error(character,[])
|  instantiation_error.

83 ?- number_chars(N,[1,2,3]).
   type_error(character,1)
| type_error(character,2)
| type_error(character,3).

79 ?- N is 0.1*10** -999.
   N = 0.0
| evaluation_error(underflow).

80 ?- number_chars(N,"0.1e-999").
   N = 0.0
| representation_error(number).

81 ?- N is 9.9*10**999.
   evaluation_error(float_overflow).

82 ?- number_chars(N,"9.9e999").
   representation_error(max_float)
| representation_error(float)
| representation_error(number).
   syntax_error(...), unexpected.

85 ?- char_code(N,0),char_code(N,Null),Null==0.
   representation_error(character_code)
| N = ..., Null=0. % '\0\'

86 ?- number_chars(N,"0'\\0\\").
   representation_error(character_code)
| N = 0.
   syntax_error(...), unexpected.

87 ?- number_chars(1,[C|C]).
   false.

46 ?- L=['1'|L], number_chars(N,L). % * is default
   sto, ... ; ... .
   sto, false % occurs-check
| sto, representation_error(term)
| sto, loops % rational trees
| sto, type_error(list,['1'|...])
| sto, resource_error(...)
| sto, instantiation_error. % literal substitutions

main :-
	use_module(library(quads)),
	run_quads.
