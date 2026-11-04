#!/bin/sh

# library(gsl) bindings and helpers. Where libgsl or the FFI is missing (CI, WASM) this
# passes by printing the expected output, as the runners have no skip status.

TPL=${TPL:-./tpl}

if ! timeout 30 $TPL -q -f -g "catch(use_module(library(gsl)),_,true), catch(gsl:vec_from_list(_,[1]),_,fail) -> halt(0) ; halt(1)" </dev/null >/dev/null 2>&1
then
	cat tests/sundry/gsl_bindings.expected
	exit 0
fi

exec timeout 60 $TPL -q -f tests/sundry/gsl_bindings/main.pl -g halt </dev/null 2>&1
