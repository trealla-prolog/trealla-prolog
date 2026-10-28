#!/bin/sh

# With no locale in the environment, tpl takes a UTF-8 LC_CTYPE, so accented
# letters still change case and an unquoted accented atom still reads, as does
# LANG=C. An LC_ALL the user set is left alone; what C gives there depends on
# the C library, so it is not checked.

TPL=${TPL:-./tpl}

# In the current directory, not mktemp: the WASM runner's wasmtime only sees '.'.

SRC=tmp_utf8_without_locale.pl

trap "rm -f $SRC" EXIT

printf 'w(\303\261and\303\272).\n' > $SRC

GOAL=$(printf "atom_upper('\303\221and\303\272', U), atom_lower('\303\221AND\303\232', L), (catch(w(X), _, fail) -> true ; X = none), writeq([U, L, X]), nl, halt")

env -u LC_ALL -u LANG -u LC_CTYPE $TPL -q $SRC -g "$GOAL" 2>&1
echo "--- LANG=C"
env -u LC_ALL -u LC_CTYPE LANG=C $TPL -q $SRC -g "$GOAL" 2>&1
