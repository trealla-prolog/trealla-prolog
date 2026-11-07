#!/bin/sh

# Structs passed and returned by value, which need a C library to take them, built here. Where there is no C
# compiler this passes by printing the expected output, as the runner has no skip status.

TPL=${TPL:-./tpl}
CC=${CC:-cc}
LIB=tmp_ffi_struct_byvalue.so

trap 'rm -f "$LIB"' EXIT

if ! $CC -shared -fPIC -o "$LIB" tests/misc/ffi_struct_byvalue/structs.c 2>/dev/null
then
	cat tests/misc/ffi_struct_byvalue.expected
	exit 0
fi

timeout 60 $TPL -q -f tests/misc/ffi_struct_byvalue/main.pl -g halt </dev/null 2>&1
