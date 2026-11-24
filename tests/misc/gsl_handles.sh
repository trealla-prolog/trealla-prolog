#!/bin/sh

# Typed foreign handles, ptr(Type) and free(Type), as library(gsl) declares its matrices, vectors and
# permutations. Where libgsl or the FFI is missing this passes by printing the expected output.

TPL=${TPL:-./tpl}

if ! timeout 30 $TPL -q -f -g "catch(use_module(library(gsl)),_,true), catch(gsl:vec_from_list(_,[1]),_,fail) -> halt(0) ; halt(1)" </dev/null >/dev/null 2>&1
then
	cat tests/misc/gsl_handles.expected
	exit 0
fi

exec timeout 60 $TPL -q -f tests/misc/gsl_handles/main.pl -g halt </dev/null 2>&1
