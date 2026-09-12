:- initialization(main).

% write_term/2 took a list tail's variable name from variable_names only where it was not a tail, so [a|X] was written [a|_0].

tail ?- write_term([a|X], [variable_names(['X'=X])]).
   outputs("[a|X]"), true.

longer ?- write_term([a,b|X], [variable_names(['X'=X])]).
   outputs("[a,b|X]"), true.

% the same variable as an argument and as a tail

both ?- write_term(f(X,[a|X]), [variable_names(['X'=X]), quoted(true)]).
   outputs("f(X,[a|X])"), true.

partial_string ?- write_term("ab"||X, [variable_names(['X'=X])]).
   outputs("[a,b|X]"), true.

% a tail variable variable_names does not name is still numbered

unnamed ?- with_output_to(chars(Cs), write_term([a|_], [variable_names(['Y'=_])])).
   Cs = ['[',a,'|','_'|...].

main :- use_module(library(quads)), run_quads.
