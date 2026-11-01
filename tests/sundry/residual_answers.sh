#!/bin/sh

# The toplevel printed no residual goals at all when its dump met a variable whose attributes were gone or a stale entry.

TPL=${TPL:-./tpl}

# -f: a ~/.tplrc must not decide which libraries are loaded.

printf '%s\n' 'use_module(library(dif)).' 'use_module(library(freeze)).' 'use_module(library(clpz)).' 'use_module(library(clpb)).' \
	'dif(X,Y).' 'dif(X,Y), dif(Y,Z).' 'dif(X,Y), dif(X,Z), dif(Y,Z).' 'dif(X,Y), freeze(X,true).' 'dif(X,a), X = Y.' \
	'freeze(X,true), freeze(Y,true), X = Y.' 'X #> 3, X #< 10, Y #= X * 2.' 'sat(X+Y).' | timeout 30 $TPL -q -f 2>&1
