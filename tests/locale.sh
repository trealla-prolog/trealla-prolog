# Sourced by the test runners, which all diff output against a recorded file.
#
# Trealla documents a UTF-8 locale as a requirement, and tests across
# tests/tests, tests/issues, tests/sundry and tests/misc print accented text.
# Under the C locale those differ and are reported as failures - failures that
# read exactly like real ones and say nothing about the code. So the locale is
# chosen here rather than left to whatever the caller happens to have
# exported, because the cost of getting it wrong is an afternoon spent on a
# bug that was never there.
#
# The ladder matches the one in .github/workflows/build.yaml: macOS has
# en_US.UTF-8 and no C.UTF-8, and most Linux images have it the other way
# round.

if locale -a 2>/dev/null | grep -qx 'C.UTF-8'; then
	LC_ALL=C.UTF-8
elif locale -a 2>/dev/null | grep -qx 'en_US.UTF-8'; then
	LC_ALL=en_US.UTF-8
else
	LC_ALL=C
	echo "WARNING: no UTF-8 locale here; tests on accented text will differ"
fi

export LC_ALL
