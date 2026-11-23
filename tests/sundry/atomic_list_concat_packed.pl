% atomic_list_concat/2,3 took only an ISO list, so the packed char list atom_chars/2 or a double-quoted string gives was a type error.

:- initialization(main).

atom_chars_3 ?- atom_chars(abc, Cs), atomic_list_concat(Cs, -, A).
   Cs = [a,b,c], A = 'a-b-c'.

atom_chars_2 ?- atom_chars(abc, Cs), atomic_list_concat(Cs, A).
   Cs = [a,b,c], A = abc.

string_3 ?- atomic_list_concat("abc", ',', A).
   A = 'a,b,c'.

single_char ?- atomic_list_concat("a", A).
   A = a.

plain_list ?- atomic_list_concat([a,b,c], -, A).
   A = 'a-b-c'.

split ?- atomic_list_concat(L, -, 'a-b').
   L = [a,b].

not_a_list ?- atomic_list_concat(foo, -, _).
   error(type_error(list,foo),atomic_list_concat/3).

partial ?- atomic_list_concat([a|_], _).
   error(instantiation_error,atomic_list_concat/2).

main :- use_module(library(quads)), run_quads.
