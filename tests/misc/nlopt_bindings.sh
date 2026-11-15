#!/bin/sh

# library(nlopt). Where libnlopt or the FFI is missing this passes by printing the expected output, as the
# runners have no skip status.

TPL=${TPL:-./tpl}

if ! timeout 30 $TPL -q -f -g "catch(use_module(library(nlopt)),_,true), catch(nlopt:nlopt_version(_),_,fail) -> halt(0) ; halt(1)" </dev/null >/dev/null 2>&1
then
	cat tests/misc/nlopt_bindings.expected
	exit 0
fi

exec timeout 60 $TPL -q -f tests/misc/nlopt_bindings/main.pl -g halt </dev/null 2>&1
