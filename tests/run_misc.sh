#!/bin/sh

# See tests/run.sh: a test passes only if its output matches AND it
# exited cleanly, so a crash after correct output cannot score as a pass.

TPL=./tpl

TMP=tmp.out

trap "rm -f $TMP" EXIT

. tests/locale.sh

failed_count=0
succeeded_count=0
failed_list=
start_time=$(date +%s)

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

for source in tests/misc/*

do
	case "$source" in
		*.pl)
			cmd="$TPL -q -f -g halt "
			;;
		*.sh)
			cmd="env TPL=$TPL bash"
			;;
		*)
			continue
	esac

	echo "Running $source ..."
	timeout 60 $cmd "$source" >$TMP
	run_rc=$?

	diff -a --strip-trailing-cr "${source%.*}.expected" $TMP
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

elapsed=$(expr $(date +%s) - $start_time)
mins=$(expr $elapsed / 60)
secs=$(expr $elapsed % 60)

cat <<EOF

============
TEST SUMMARY
============
Failed: $failed_count
Succeeded: $succeeded_count
Time: ${mins}m${secs}s
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
