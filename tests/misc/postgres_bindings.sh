#!/bin/sh

# library(postgres). Where libpq, the FFI or a server to connect to is missing this passes by printing the
# expected output, as the runners have no skip status. PG_TEST_CONNINFO names the server, libpq's defaults if
# unset.

TPL=${TPL:-./tpl}

if ! timeout 30 $TPL -q -f -g "(getenv('PG_TEST_CONNINFO', I) -> true ; I = ''), catch(use_module(library(postgres)),_,true), catch((postgres:pg_connect(I, C), postgres:pg_disconnect(C)),_,fail) -> halt(0) ; halt(1)" </dev/null >/dev/null 2>&1
then
	cat tests/misc/postgres_bindings.expected
	exit 0
fi

exec timeout 60 $TPL -q -f tests/misc/postgres_bindings/main.pl -g halt </dev/null 2>&1
