#!/bin/sh
# determinism.sh: two runs of one build from the same state must draw the
# same frames.  Runs a host twice, wiping the state the engine leaves
# behind before each run (the host's save directory and the base/ the
# engine writes beside the content), and compares the frame hashes the
# host prints.  A frame that differs is drawn from something other than
# the frame count: the wall clock, a thread's timing, uninitialised
# memory.
#
#   ./determinism.sh <host> <core.so> <content> <frames> [key=value ...]
#
# <host> is egl_host or vk_host (lavapipe wants
# VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json in the
# environment); the rest is passed to it, from=0 added.  Exits 0 when the
# hashes agree, 3 when they differ, the host's status when a run fails.

set -u
host=$1
core=$2
content=$3
frames=$4
shift 4
statedir=/tmp/$(basename "$host")_sys
basedir=$(dirname "$content")/base
out=$(mktemp -d)
status=0

for run in 1 2; do
	rm -rf "$statedir" "$basedir"
	"$host" "$core" "$content" "$frames" from=0 "$@" > "$out/$run.log" 2>&1
	rc=$?
	if [ $rc -ne 0 ]; then
		echo "FAIL run $run: $host exited $rc"
		tail -5 "$out/$run.log" | sed 's/^/    /'
		status=$rc
	fi
	grep '^frame ' "$out/$run.log" > "$out/$run.frames"
done
if [ $status -eq 0 ]; then
	if cmp -s "$out/1.frames" "$out/2.frames"; then
		echo "pass: $(wc -l < "$out/1.frames") frames hash alike"
	else
		echo "FAIL: frames differ between runs:"
		diff "$out/1.frames" "$out/2.frames" | grep '^<' | awk '{ print $3 }' | sort -un | tr '\n' ' '
		echo
		status=3
	fi
fi
rm -rf "$out"
exit $status
