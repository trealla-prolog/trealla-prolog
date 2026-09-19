#!/bin/sh

# See tests/run.sh: a test passes only if its output matches AND it
# exited cleanly.
#
# Here "cleanly" also covers valgrind itself: --error-exitcode makes a
# leak or an invalid access fail the test rather than scroll past in the
# log. VG_RC is distinct from any status tpl produces, so the two cases
# stay tellable apart.

TPL=./tpl
#TPL=swipl

VG_RC=99

TMP=tmp.out

trap "rm -f $TMP" EXIT

. tests/locale.sh

failed_count=0
succeeded_count=0
failed_list=

describe_rc() {
	case "$1" in
		$VG_RC) echo "valgrind reported errors" ;;
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

	echo "Running $source ..."
	valgrind --leak-check=full --error-exitcode=$VG_RC -q $cmd "$source" >$TMP
	run_rc=$?

	diff "${source%.*}.expected" $TMP
	diff_rc=$?

	if [ $diff_rc -ne 0 ]
	then
		echo "*** $source: output differs"
		failed_count=$(expr $failed_count + 1)
		failed_list="$failed_list $source"
	elif [ $run_rc -ne 0 ]
	then
		echo "*** $source: output matched but $(describe_rc $run_rc)"
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
