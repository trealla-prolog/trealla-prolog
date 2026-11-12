#!/bin/sh

# A cycle back through the || tail of an answer was never cut, so these ran on to the depth limit.

$TPL -q -f <<'EOF'
X = "ab"||f(X).
X = "ab"||f(Y), Y = g(X).
X = [a|T], T = [b|F], F = f(T).
X = "ab"||f(X,X).
EOF
