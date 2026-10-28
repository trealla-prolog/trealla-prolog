#!/bin/sh

# With no locale in the environment, tpl takes a UTF-8 LC_CTYPE, so accented
# letters still change case and an unquoted accented atom still reads, as does
# LANG=C. An LC_ALL the user set is left alone; what C gives there depends on
# the C library, so it is not checked.

TPL=${TPL:-./tpl}

# In the current directory, not mktemp: the WASM runner's wasmtime only sees '.'.
# The accented text is in the file, not on the command line: with no locale set,
# MSYS2 hands a Windows program its arguments in the ANSI code page, not UTF-8.

SRC=tmp_utf8_without_locale.pl

trap "rm -f $SRC" EXIT

printf 'w(\303\261and\303\272).\nmain :- atom_upper(\047\303\221and\303\272\047, U), atom_lower(\047\303\221AND\303\232\047, L), (catch(w(X), _, fail) -> true ; X = none), writeq([U, L, X]), nl.\n' > $SRC

env -u LC_ALL -u LANG -u LC_CTYPE $TPL -q $SRC -g main,halt 2>&1
echo "--- LANG=C"
env -u LC_ALL -u LC_CTYPE LANG=C $TPL -q $SRC -g main,halt 2>&1
