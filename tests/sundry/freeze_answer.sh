#!/bin/sh

# freeze/2 bound its variable to a fresh attributed one, so the toplevel
# answered T = _A, freeze:freeze(_B,true) instead of naming T.

TPL=${TPL:-./tpl}

printf '%s\n' 'freeze(T,true).' 'freeze(T,g(T)), freeze(T,h).' 'freeze(T,write(a)), T = 1.' 'freeze(1, write(now)).' | timeout 10 $TPL -q 2>&1
