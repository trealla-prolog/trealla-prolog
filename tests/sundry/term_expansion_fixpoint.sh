#!/bin/sh

# term_expansion/2 re-expansion and its limit (see term_expansion_fixpoint/main.pl). The one load that
# fails reports it on stderr; that line is checked here, cut to the file name, not left on the runner's.

TPL=${TPL:-./tpl}

# In the current directory, not mktemp: the WASM runner's wasmtime only sees '.'.

ERR=tmp_tef_err.txt

trap "rm -f $ERR" EXIT

timeout 60 $TPL -q -f -g halt tests/sundry/term_expansion_fixpoint/main.pl </dev/null 2>$ERR
rc=$?
sed 's|, .*[/\\]|, |' $ERR
exit $rc
