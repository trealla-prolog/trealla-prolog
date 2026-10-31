#!/bin/sh

# A query whose last goal called a user predicate let that call reuse
# frame 0, which holds the query's own variables: the toplevel printed
# true. for X = 1, foo(a), or the callee's argument as X. Tail calls
# below frame 0 still reuse their frames, as the deep count shows.

TPL=${TPL:-./tpl}
F=tmp_toplevel_last_call.pl
M=tmp_toplevel_last_call_mod.pl

trap 'rm -f $F $M' EXIT

cat > $M <<'PL'
:- module(tmp_toplevel_last_call_mod, [m1/1]).
m1(G) :- m1_(G).
m1_(_).
PL

cat > $F <<'PL'
:- use_module(tmp_toplevel_last_call_mod).
foo(_).
bar(_) :- baz.
baz.
pick(G) :- ( G == q -> true ; fail ).
count(0) :- !.
count(N) :- N1 is N - 1, count(N1).
PL

printf '%s\n' 'X = 1, foo(a).' 'X = 1, bar(a).' 'X = 1, foo(Y).' \
	'X = 1, Y = f(Z), foo(a).' 'X = 1, pick(q).' 'X = 1, m1(q).' \
	'assertz(dyn(_)).' 'X = 1, dyn(a).' 'X = 1, count(1000000).' \
	| timeout 10 $TPL -q -f $F 2>&1
