#!/bin/sh
# capture_check.sh: the objective screenshots the game captures mid-play
# (idObjective::Event_CamShot -> CaptureRenderToFile) come out of every
# renderer as a picture, and the same picture.  Runs the demo's first map
# on each renderer to just past the objectives, then compares the TGAs
# the engine wrote beside the content: each must have light in it, and
# each must be within a few code values of the first renderer's.
#
#   ./capture_check.sh <core.so> <content> [renderer ...]
#
# <content> is the demo's .pk4; the renderers default to arb2 and, when
# vk_host is built, vulkan (lavapipe wants VK_ICD_FILENAMES).  Exits 0
# when every shot passes, 1 otherwise.

set -u
here=$(cd "$(dirname "$0")" && pwd)
host="$here/egl_host"
vkhost="$here/../vk_host/vk_host"
diff="$here/ppmdiff"
core=$1
content=$2
shift 2
default="arb2"
[ -x "$vkhost" ] && default="$default vulkan"
renderers=${*:-$default}
frames=600
shots="objective1 objective2"
# the engine's base/ is beside the game directory the content is in
basedir=$(dirname "$(dirname "$content")")/base
shotdir=$basedir/maps/game/demo_mars_city1/textures
out=$(mktemp -d)
fail=0
reference=

for r in $renderers; do
	h=$host
	opt="doom_renderer=$r"
	[ "$r" = vulkan ] && h=$vkhost
	[ "$r" = vulkan ] && opt=
	rm -rf "/tmp/$(basename "$h")_sys" "$basedir"
	"$h" "$core" "$content" $frames doom_resolution=640x480 $opt > "$out/$r.log" 2>&1
	rc=$?
	# 3 is the host's own gate (pipelines, allocations), checked elsewhere
	if [ $rc -ne 0 ] && [ $rc -ne 3 ]; then
		echo "FAIL $r: host exited $rc"
		tail -3 "$out/$r.log" | sed 's/^/    /'
		fail=1
		continue
	fi
	for s in $shots; do
		if [ ! -f "$shotdir/$s.tga" ]; then
			echo "FAIL $r $s: not written"
			fail=1
			continue
		fi
		cp "$shotdir/$s.tga" "$out/${r}_$s.tga"
		# light in it: the mean level ppmdiff reports for the first image
		level=$("$diff" "$out/${r}_$s.tga" "$out/${r}_$s.tga" | sed 's/.*first //')
		if [ "$(printf '%s\n' "$level" | awk '{ print ($1 > 1.0) ? 1 : 0 }')" != 1 ]; then
			echo "FAIL $r $s: black (mean level $level)"
			fail=1
			continue
		fi
		if [ -n "$reference" ]; then
			if res=$("$diff" "$out/${reference}_$s.tga" "$out/${r}_$s.tga" 8.0); then
				echo "pass $r $s against $reference: $res"
			else
				echo "FAIL $r $s against $reference: $res"
				fail=1
			fi
		else
			echo "pass $r $s: mean level $level"
		fi
	done
	[ -z "$reference" ] && reference=$r
done
rm -rf "$out"
[ $fail -eq 0 ] && echo "all shots pass" || echo "FAIL"
exit $fail
