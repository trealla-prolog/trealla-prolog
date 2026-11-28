#!/bin/sh

# Stopping a toplevel query runs every cleanup its choices hold, newest
# first. Once the first had run, noretry was cleared, so the next older
# choice was resumed as if ; had been typed and the query went on.
#
# Spaces after a redo's ; are squeezed to one: the WASI build echoes one
# more there.

TPL=${TPL:-./tpl}

printf '%s\n' \
	'setup_call_cleanup(true, member(X, [1,2]), writeln(c1)), member(Y, [a,b]), setup_call_cleanup(true, member(Z, [1,2]), writeln(c2)).' \
	| timeout 10 $TPL -q -f 2>&1 | sed 's/^;  */; /'

printf '%s\n' \
	'setup_call_cleanup(true, member(X, [1,2]), writeln(c1)), setup_call_cleanup(true, member(Y, [1,2]), (member(W, [p,q]), writeln(W), W == q)).' \
	| timeout 10 $TPL -q -f 2>&1 | sed 's/^;  */; /'
