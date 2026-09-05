#!/bin/sh

# The issue #1121 rational terms must retain the name of the source
# variable that leads back into an already visited term.  This uses the
# interactive answer printer because write/1 does not exercise dump vars.

TPL=${TPL:-./tpl}

run() {
	printf '%s\n' "$1" \
		| "$TPL" -q -f -g 'use_module(library(dif))' --autofail
}

run 'dif(A,B),C=[[]|C],A=[C|D],D=[D|A],B=[C|A].'
run 'C=[[]|C],A=[C|D],D=[D|A],B=[C|A],dif(A,B).'
