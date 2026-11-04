% The same, through a hook that only checks its input.

term_expansion(T, T) :- T = te_cond(_).

te_cond(1).
