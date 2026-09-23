# Sourced by the test runners, which all diff output against a recorded file.
#
# The README asks for a UTF-8 locale to run the tests, and tests across
# tests/tests, tests/issues, tests/sundry and tests/misc print accented text.
# Under the C locale those differ and are reported as failures - failures that
# read exactly like real ones and say nothing about the code. So the locale is
# chosen here rather than left to whatever the caller happens to have
# exported, because the cost of getting it wrong is an afternoon spent on a
# bug that was never there.
#
# A caller who already asked for UTF-8 keeps what they asked for: the s390x
# job hands one in through the container's environment, and replacing it with
# a guess of our own is how this file first broke the runners.
#
# Names are matched as `locale -a` prints them, which is not how they are
# written: glibc says `C.utf8` where macOS says `C.UTF-8`. Matching the
# spelling rather than the locale is what made every Linux runner fall through
# to C.

tpl_utf8_locale() {
	locale -a 2>/dev/null | grep -iE "^$1$" | head -n 1
}

case "${LC_ALL:-${LANG:-}}" in
*[Uu][Tt][Ff]8 | *[Uu][Tt][Ff]-8)
	LC_ALL=${LC_ALL:-${LANG:-}}		# a caller who set only LANG still gets one
	export LC_ALL
	;;
*)
	tpl_utf8=$(tpl_utf8_locale 'C\.utf-?8')

	if [ -z "$tpl_utf8" ]; then
		tpl_utf8=$(tpl_utf8_locale 'en_US\.utf-?8')
	fi

	if [ -n "$tpl_utf8" ]; then
		LC_ALL=$tpl_utf8
	else
		LC_ALL=C
		echo "WARNING: no UTF-8 locale here; tests on accented text will differ"
	fi

	export LC_ALL
	;;
esac
