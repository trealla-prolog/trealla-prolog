#!/bin/sh

# print_message/2 writes format(F, A) and ISO error terms to user_error with
# SWI's prefixes and wording; silent prints nothing; informational only prints
# when not quiet; an unknown term is reported as such.

TPL=${TPL:-./tpl}

# In the current directory, not mktemp: the WASM runner's wasmtime only sees '.'.

DRV=tmp_print_message.pl

trap "rm -f $DRV" EXIT

cat > $DRV <<'EOF'
main :-
	print_message(error, format("hello ~w", [world])),
	print_message(warning, format("careful ~w", [x])),
	print_message(silent, format("quiet", [])),
	print_message(informational, format("info ~w", [y])),
	print_message(help, format("helpful", [])),
	print_message(error, foo(bar)),
	print_message(error, error(type_error(integer, a), context(foo/2, _))),
	print_message(error, error(type_error(atom, f(x)), foo/2)),
	print_message(error, error(instantiation_error, _)),
	print_message(error, error(existence_error(procedure, nope/0), nope/0)),
	print_message(error, error(existence_error(stream, s), _)),
	print_message(error, error(domain_error(not_less_than_zero, -1), _)),
	print_message(error, error(permission_error(modify, static_procedure, foo/1), _)),
	print_message(error, error(evaluation_error(zero_divisor), (/)/2)),
	print_message(error, error(syntax_error(operator_expected), _)),
	print_message(warning, error(type_error(integer, a), context(_, 'some message'))),
	print_message(error, error(foo(bar), _)),
	catch(print_message(_, x), error(E, _), (write(user_error, E), nl(user_error))).
EOF

$TPL -q -f -g "main, halt" $DRV 2>&1
echo "--- not quiet"
$TPL -f -g "print_message(informational, format(\"info ~w\", [y])), halt" 2>&1 | tail -1
