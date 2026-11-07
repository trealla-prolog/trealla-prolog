#!/bin/sh

# Reconsulting a file: its predicates report the properties a first consult would
# give, a predicate gone from the file reports none, and declarations survive as in
# Scryer (SWI drops discontiguous). A second file overwriting a static predicate
# mid-call must not free the clauses under the call.
#
# That overwrite is meant to warn, on stderr, which the runner neither compares nor
# keeps out of its log, so the warning is caught here and checked after the
# program's own output.

TPL=${TPL:-./tpl}

# In the current directory, not mktemp: the WASM runner's wasmtime only sees '.'.

TMPERR=tmp_reconsult_properties.err

trap "rm -f $TMPERR" EXIT

timeout 30 $TPL -q -f tests/sundry/reconsult_properties/main.pl -g halt </dev/null 2>$TMPERR
rc=$?
cat $TMPERR
exit $rc
