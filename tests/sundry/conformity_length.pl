:- initialization(main).
:- use_module(library(freeze)).

% UWN's length/2 conformity quads, from
% https://www.complang.tuwien.ac.at/ulrich/iso-prolog/length_quad.pl

% ids refer to http://www.complang.tuwien.ac.at/ulrich/iso-prolog/length#1

a1 ?- atom_length(A,N).
      instantiation_error.
a2 ?- atom_length(a,a).
      type_error(integer,a).
a3 ?- atom_length(a,1.1).
      type_error(integer,1.1).
a4 ?- atom_length(a,-1).
      domain_error(not_less_than_zero,-1).
a5 ?- atom_length(1,N).
       type_error(atom,1).
1  ?- length(L,N).
      L = [], N = 0
   ;  L = [_A], N = 1
   ;  L = [_A,_B], N = 2
   ;  ... .
2  ?- length(L,0).
      L = [].
3  ?- length([_|L],0).
      false.
4  ?- length(2,0).
      false.
5  ?- length([_|2],0).
      false.
6  ?- length([_|2],N).
      false.
7  ?- length([_|2],2).
      false.
8  ?- length(L,-1).
      domain_error(not_less_than_zero,-1).
9  ?- length([],-1).
      domain_error(not_less_than_zero,-1).
10 ?- length(a,-1).
      domain_error(not_less_than_zero,-1).
11 ?- length([],-0.1).
      type_error(integer,-0.1).
12 ?- length(L,-0.1).
      type_error(integer,-0.1).
13 ?- length([a],1.0).
      type_error(integer,1.0).
14 ?- length(L,1.0).
      type_error(integer,1.0).
15 ?- length(L,1.1).
      type_error(integer,1.1).
16 ?- length(L,1.0e99).
      type_error(integer,1.0e99).
17 ?- N is 2^52, length([], N).
      false.
18 ?- length([],0+0).
      type_error(integer,0+0).
19 ?- length([],-_).
      type_error(integer,-_).
20 ?- length([a],-_).
      type_error(integer,-_).
21 ?- length([a,b|X],X).
      resource_error(finite_memory)
   |  resource_error(...)
   |  loops.
22 ?- length(L,L).
      resource_error(finite_memory)
   |  resource_error(...)
   |  loops.
23 ?- L = [_|_], length(L,L).
      type_error(integer,[_|_]).
24 ?- L = [_], length(L,L).
      type_error(integer,[_]).
25 ?- L = [1], length(L,L).
      type_error(integer,[1]).
26 ?- L = [a|L], length(L,N).
      sto, false % current_prolog_flag(occurs_check, true)
   |  sto, resource_error(finite_memory)
   |  sto, resource_error(...)
   |  sto, loops
   |  sto, L = [a,a], N = 2
   ;  L = [a,a,_A], N = 3
   ;  ... . % dag representation, literal substitution, Tau
27 ?- L = [a|L], length(L,0).
      sto, false.
28 ?- L = [a|L], length(L,7).
      sto, false
   |  sto, L = [a,a,_A,_B,_C,_D,_E]. % tau
29 ?- freeze(L,L=[]), length(L,L).
      false.
30 ?- freeze(L,L=[_|L]), length(L,N).
      sto, loops
   |  sto, resource_error(...)
   |  sto, false.
31 ?- freeze(L,L=[_|L]), N is 2^64, length(L,N).
      sto, false.
32 ?- length([a,b|L], N).
      L = [], N = 2
   ;  L = [_A], N = 3
   ;  L = [_A,_B], N = 4
   ;  ... .

main :-
	use_module(library(quads)),
	run_quads.
