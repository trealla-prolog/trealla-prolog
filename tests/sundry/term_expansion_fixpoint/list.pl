% A list holding the input never settles: the load fails with an error, where this used to crash.

te_before.		% not checked: what a failed consult keeps is not at issue here

term_expansion(te_list(X), [te_list(X), te_more(X)]).

te_list(1).

te_after.
