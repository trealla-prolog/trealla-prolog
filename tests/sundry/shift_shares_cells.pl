% shift/1 copied its continuation onto the heap without taking a
% reference to each cell, but the heap releases what it trims, so a
% refcounted constant in the continuation (here a bigint) lost one per
% resumption and was freed while the clause still held it. Only ASan
% sees the use-after-free; the output just has to come out.

p :- shift(x), X is 123456789012345678901234567890 + 1, X > 0.

t :- between(1, 3, _), reset(p, _, C), C = cont(G), call(G), fail.
t.

:- initialization((t, p_again, write(done), nl)).

p_again :- reset(p, _, cont(G)), call(G).
