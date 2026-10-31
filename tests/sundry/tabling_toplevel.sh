#!/bin/sh

# The toplevel shows an undefined answer as SWI does: its residual
# program first, then its delays where true would go, or just undefined
# when toplevel_list_wfs_residual_program is false.
#
# Spaces after a redo's ; are squeezed to one: the WASI build echoes one
# more there.

TPL=${TPL:-./tpl}
F=tmp_tabling_toplevel.pl

trap 'rm -f $F' EXIT

cat > $F <<'PL'
:- use_module(library(tabling)).
:- table win/1.
win(X) :- move(X, Y), tnot(win(Y)).
move(a,b). move(b,a). move(b,c). move(c,d). move(a,c).
PL

printf '%s\n' 'undefined.' 'win(a).' 'win(c).' 'win(d).' 'tnot(win(a)).' \
	'undefined, X = 1.' 'call_delays(win(a), D).' 'win(X).' ';;' \
	'current_prolog_flag(toplevel_list_wfs_residual_program, V).' \
	'set_prolog_flag(toplevel_list_wfs_residual_program, false).' \
	'win(a).' 'undefined, X = 1.' \
	'catch(set_prolog_flag(toplevel_list_wfs_residual_program, maybe), error(E, _), true).' \
	| timeout 10 $TPL -q -f $F 2>&1 | sed 's/^;  */; /'
