#!/bin/sh

# Clauses for a non-ISO C builtin take it over, as in SWI and Scryer, with a
# warning for each one taken over.
#
# The warnings go to stderr, which the runner neither compares nor keeps out of its
# log, so they are caught here and checked after the program's own output.

TPL=${TPL:-./tpl}

# In the current directory, not mktemp: the WASM runner's wasmtime only sees '.'.

TMPERR=tmp_user_overrides_builtin.err

trap "rm -f $TMPERR" EXIT

timeout 30 $TPL -q -f tests/sundry/user_overrides_builtin/main.pl -g halt </dev/null 2>$TMPERR
rc=$?
cat $TMPERR
exit $rc
