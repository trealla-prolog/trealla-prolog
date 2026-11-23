#!/bin/sh

# A signature's type name that is neither a C type nor a declared struct is reported, where labs([long], long) used
# to register and then fail every call; long, int and short are C's own spellings for slong, sint and sshort now.
# A library that will not open says why, the loader's own reason being platform text, so only its presence is
# checked. A foreign predicate or function behind a builtin of its kind, name and arity, never to be called, is
# reported too. Skipped where no libc opens.

TPL=${TPL:-./tpl}

# In the current directory, not mktemp: the WASM runner's wasmtime only sees '.'.

DRV=tmp_ffisig_drive.pl
OUT=tmp_ffisig_out.txt

trap "rm -f $DRV $OUT" EXIT

cat > $DRV <<'EOF'
:- initialization(main).

libc('libc.so.6').
libc('libc.so.7').
libc('libc.so').
libc('msvcrt.so').

main :-
	(	libc(L), catch('$dlopen'(L, 0, H), _, fail)
	->	use_foreign_module(L, [labs([long], long), abs([int], int), atoi([cstr], short)]),
		labs(-5, X), abs(-6, Y), atoi('-7', Z),
		write(aliases(X, Y, Z)), nl,
		use_foreign_module(L, [atol([cstrr], slong)]),
		use_foreign_module(L, [getenv([cstr], cstr)]),
		'$register_function'(H, abs, [sint], sint),
		( use_foreign_module('libtpl_no_such_lib.so', [f([sint], sint)]) -> true ; true )
	;	write(skip), nl
	),
	halt.
EOF

timeout 60 $TPL -q $DRV </dev/null >$OUT 2>&1

if grep -q '^skip$' $OUT; then
	echo 'aliases(5,6,-7)'
	echo 'Warning: foreign predicate atol: cstrr is no C type and no foreign_struct declared before it'
	echo 'Warning: foreign predicate getenv/2: the builtin of that name and arity is called instead'
	echo 'Warning: foreign function abs/1: the builtin of that name and arity is called instead'
	echo 'load failure for libtpl_no_such_lib.so gives a reason'
	exit 0
fi

grep '^aliases' $OUT
grep '^Warning: foreign predicate atol:' $OUT
grep '^Warning: foreign predicate getenv/2:' $OUT
grep '^Warning: foreign function abs/1:' $OUT
sed -n 's/^Error: foreign module creation failed: \(libtpl_no_such_lib\.so\), [^:]*: ..*$/load failure for \1 gives a reason/p' $OUT
