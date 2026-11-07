#!/bin/sh

# dynamic, discontiguous or multifile for an ISO builtin set an error the loader
# took as the end of the file, so everything after it was dropped without a word,
# while consult/1 reported success. Each is now refused with a permission error,
# as in SWI, and the load goes on; a module exporting an ISO builtin it does not
# define, which made use_module/1 crash, is let be.
#
# The permission errors go to stderr, which the runner neither compares nor keeps
# out of its log, so they are caught here and checked after the program's own
# output, with the directory stripped by either separator for Windows.

TPL=${TPL:-./tpl}

# In the current directory, not mktemp: the WASM runner's wasmtime only sees '.'.

TMPERR=tmp_builtin_declarations.err

trap "rm -f $TMPERR" EXIT

timeout 30 $TPL -q -f tests/sundry/builtin_declarations/main.pl -g halt </dev/null 2>$TMPERR
rc=$?
sed -e 's#, .*[/\\]#, #' $TMPERR
exit $rc
