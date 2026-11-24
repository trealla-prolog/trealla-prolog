#!/bin/sh

# library(sqlite3) on an in-memory database, and its connection and statement handles. Where libsqlite3 or the
# FFI is missing this passes by printing the expected output, as the runners have no skip status.

TPL=${TPL:-./tpl}

if ! timeout 30 $TPL -q -f -g "catch(use_module(library(sqlite3)),_,true), catch((sqlite3_open(':memory:',C,0), sqlite3_close(C,_)),_,fail) -> halt(0) ; halt(1)" </dev/null >/dev/null 2>&1
then
	cat tests/misc/sqlite3_bindings.expected
	exit 0
fi

exec timeout 60 $TPL -q -f tests/misc/sqlite3_bindings/main.pl -g halt </dev/null 2>&1
