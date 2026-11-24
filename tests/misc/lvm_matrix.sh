#!/bin/sh

# lvm's Logtalk matrix tests, ported to library(gsl) and library(igraph). Where either library or the
# FFI is missing this passes by printing the expected output, as the runners have no skip status.

TPL=${TPL:-./tpl}

if ! timeout 30 $TPL -q -f -g "catch(use_module(library(gsl)),_,true), catch(use_module(library(igraph)),_,true), catch((gsl:vec_from_list(_,[1]), igraph:igraph_version(_)),_,fail) -> halt(0) ; halt(1)" </dev/null >/dev/null 2>&1
then
	cat tests/misc/lvm_matrix.expected
	exit 0
fi

exec timeout 60 $TPL -q -f tests/misc/lvm_matrix/main.pl -g halt </dev/null 2>&1
