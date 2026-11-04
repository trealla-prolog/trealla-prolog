% A result the hook applies to again is expanded again, as in Scryer (SWI stops at te_chain(2)).

term_expansion(te_chain(X), te_chain(Y)) :- Y is X + 1, Y < 5.

te_chain(1).
