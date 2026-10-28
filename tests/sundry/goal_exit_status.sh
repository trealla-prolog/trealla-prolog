#!/bin/sh

# A -g goal that raises an exception, or does not parse, exits 2; one that fails
# exits 1, without running later goals or the toplevel; halt/1 keeps its code; as
# in SWI.

TPL=${TPL:-./tpl}

run() {
	$TPL -q "$@" < /dev/null > /dev/null 2>&1
	echo "$*: $?"
}

run -g true
run -g fail
run -g 'throw(oops)'
run -g 'atom_length(_, _)'
run -g 'catch(throw(x), _, true)'
run -g 'halt(3)'
run -g 'foo('
run -g fail -g 'halt(5)'
run -g true -g 'halt(6)'
