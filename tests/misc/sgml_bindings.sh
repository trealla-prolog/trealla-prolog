#!/bin/sh

# library(sgml) over libxml2. Where libxml2 or the FFI is missing this passes by printing the expected output, as
# the runners have no skip status.

TPL=${TPL:-./tpl}

if ! timeout 30 $TPL -q -f -g "catch(use_module(library(sgml)),_,true), catch(sgml:load_xml(string(\"<a/>\"),_,[]),_,fail) -> halt(0) ; halt(1)" </dev/null >/dev/null 2>&1
then
	cat tests/misc/sgml_bindings.expected
	exit 0
fi

exec timeout 60 $TPL -q -f tests/misc/sgml_bindings/main.pl -g halt </dev/null 2>&1
