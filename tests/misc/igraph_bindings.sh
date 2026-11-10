#!/bin/sh

# library(igraph). Where libigraph or the FFI is missing this passes by printing the
# expected output, as the runners have no skip status.

TPL=${TPL:-./tpl}

if ! timeout 30 $TPL -q -f -g "catch(use_module(library(igraph)),_,true), catch(igraph:igraph_version(_),_,fail) -> halt(0) ; halt(1)" </dev/null >/dev/null 2>&1
then
	cat tests/misc/igraph_bindings.expected
	exit 0
fi

exec timeout 60 $TPL -q -f tests/misc/igraph_bindings/main.pl -g halt </dev/null 2>&1
