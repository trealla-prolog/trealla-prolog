#!/bin/sh

# abort/0 may be caught, but Recovery then rethrows an uncatchable abort,
# and that must stop the query. The run loop resumed after any throw it
# was handed and only stopped on q->error, which an abort leaves clear -
# so a bare abort, one caught by an uncompiled catch/3, or one under
# setup_call_cleanup carried on with the goals after it. None of these may
# print NOT_HERE or reach the halt, while an ordinary error still does.

TPL=${TPL:-./tpl}

# In the current directory, not mktemp: the WASM runner's wasmtime only sees '.'.

TMPPL=tmp_abort_stops.pl

trap "rm -f $TMPPL" EXIT

cat > $TMPPL <<'EOF'
a_bare :- abort, write('NOT_HERE'), nl.
a_compiled :- catch(abort, E, (write(caught(E)), nl)), write('NOT_HERE'), nl.
a_var_goal :- G = abort, catch(G, E, (write(caught(E)), nl)), write('NOT_HERE'), nl.
a_call :- G = (catch(abort, E, (write(caught(E)), nl)), write('NOT_HERE'), nl), call(G).
a_nested :- catch(catch(abort, E, (write(inner(E)), nl)), E2, (write(outer(E2)), nl)), write('NOT_HERE'), nl.
a_in_recovery :- catch(throw(x), x, abort), write('NOT_HERE'), nl.
a_findall :- catch(findall(X, (member(X,[1,2]), abort), _), _, true), write('NOT_HERE'), nl.
a_cleanup :- setup_call_cleanup(true, catch(abort, _, true), true), write('NOT_HERE'), nl.
e_caught :- catch(atom_length(1, _), error(E, _), (write(caught(E)), nl)), write(continued), nl.
EOF

for g in a_bare a_compiled a_var_goal a_call a_nested a_in_recovery a_findall a_cleanup e_caught
do
	echo "--- $g"
	timeout 10 $TPL -q $TMPPL -g "$g,write(halted),nl,halt" </dev/null 2>&1
done
