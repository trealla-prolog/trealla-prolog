#!/bin/sh

# macOS counterpart to run_valgrind.sh - see that file for the test-pass
# contract (output must match AND the run must exit cleanly).
#
# Unlike valgrind, `leaks --atExit` writes its own report into the SAME
# stdout stream as the child, interleaved after the child's own output -
# there is no clean-stdout/diagnostics-on-stderr split to rely on. So
# each .pl source runs TWICE: once plain, to capture output for the
# diff; once under `leaks --atExit`, purely to read its leak count off
# the "Process N: <count> leaks for ..." line. Prolog test sources are
# deterministic, so the doubled run costs time, not correctness.
#
# .sh sources are run plain only, no leak check. `leaks --atExit` hooks
# the exit of the one process it launches; it does not follow children.
# A .sh test typically forks a chain of its own (subshells, cc/clang/ld,
# background tpl instances) - wrapping that chain in leaks doesn't
# check tpl, it drowns the run in unrelated MallocStackLogging noise
# from every process in it and reports on whichever one happens to be
# in front when the wrapper exits. run_valgrind.sh has the identical
# gap (no --trace-children), just silently: valgrind's report on an
# idle sh wrapper is a harmless "0 errors," not noise or a false leak.

TPL=./tpl

TMP=tmp.out
LEAKTMP=tmp.leaks.out

trap "rm -f $TMP $LEAKTMP" EXIT

. tests/locale.sh

failed_count=0
succeeded_count=0
failed_list=

describe_rc() {
	case "$1" in
		124) echo "timed out" ;;
		134) echo "aborted (SIGABRT)" ;;
		135) echo "crashed (SIGBUS)" ;;
		139) echo "crashed (SIGSEGV)" ;;
		12[5-9]|1[3-9][0-9]) echo "killed by signal $(expr "$1" - 128)" ;;
		*) echo "exit status $1" ;;
	esac
}

for source in tests/tests/* tests/issues*/* tests/sundry/* tests/slow/* tests/misc/*

do
	case "$source" in
		*.pl)
			cmd="$TPL -q -f -g halt "
			;;
		*.sh)
			cmd="env TPL=$TPL sh"
			;;
		*)
			continue
	esac

	$cmd "$source" >$TMP
	run_rc=$?

	diff "${source%.*}.expected" $TMP >/dev/null
	diff_rc=$?

	leak_count=
	case "$source" in
		*.pl)
			leaks --atExit -- $cmd "$source" >$LEAKTMP 2>&1
			leak_line=$(grep -E '^Process [0-9]+: [0-9]+ leaks? for' $LEAKTMP)
			leak_count=$(echo "$leak_line" | sed -E 's/^Process [0-9]+: ([0-9]+) leaks? for.*/\1/')
			;;
	esac

	# Silent on success - a clean run of 300+ tests is not something to
	# scroll past looking for the three that matter. Only a failing
	# source gets printed at all, and only then with its diff/reason.
	if [ $diff_rc -ne 0 ]
	then
		echo "Running $source ..."
		diff "${source%.*}.expected" $TMP
		echo "*** $source: output differs"
		failed_count=$(expr $failed_count + 1)
		failed_list="$failed_list $source"
	elif [ $run_rc -ne 0 ]
	then
		echo "Running $source ..."
		echo "*** $source: output matched but $(describe_rc $run_rc)"
		failed_count=$(expr $failed_count + 1)
		failed_list="$failed_list $source"
	elif [ "$source" != "${source%.pl}" ] && [ -z "$leak_count" ]
	then
		echo "Running $source ..."
		echo "*** $source: leaks did not report a count (see $LEAKTMP)"
		failed_count=$(expr $failed_count + 1)
		failed_list="$failed_list $source"
	elif [ -n "$leak_count" ] && [ "$leak_count" -ne 0 ]
	then
		echo "Running $source ..."
		echo "*** $source: $leak_count leak(s) reported"
		failed_count=$(expr $failed_count + 1)
		failed_list="$failed_list $source"
	else
		succeeded_count=$(expr $succeeded_count + 1)
	fi
done

cat <<EOF

============
TEST SUMMARY
============
Failed: $failed_count
Succeeded: $succeeded_count
EOF

if [ $failed_count -ne 0 ]
then
	echo "Failed tests:"
	for t in $failed_list
	do
		echo "  $t"
	done
	exit 1
fi

exit 0
