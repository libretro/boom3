#!/bin/sh
# capture_check.sh: the objective screenshots the game captures mid-play
# (idObjective::Event_CamShot -> CaptureRenderToFile) come out of every
# renderer as a picture, and the same picture.  Runs the demo's first map
# on each renderer to just past the objectives, then compares the TGAs
# the engine wrote beside the content: each must have light in it, and
# each must be within a few code values of the first renderer's.  Then
# each renderer in 30-bit HDR with a float scene supersampled 2x2, whose
# shots on the second map are 512 square and whose target the capture
# converts: each shot must be written whole and have light in it.
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

# the large shots: a content directory of the script's own, taking the
# second map, whose 256 square crop the supersampling makes 512
big="$out/big/demo"
mkdir -p "$big"
ln -s "$(cd "$(dirname "$content")" && pwd)/$(basename "$content")" "$big/demo00.pk4"
printf 'wait 400\nmap game/demo_mc_underground\n' > "$big/autoexec.cfg"
bigshots="reacholdcomm seccheckpoint"
bigdir="$out/big/base/maps/game/demo_mc_underground/textures"
for r in $renderers; do
	h=$host
	opt="doom_renderer=$r"
	[ "$r" = vulkan ] && h=$vkhost
	[ "$r" = vulkan ] && opt=
	rm -rf "/tmp/$(basename "$h")_sys" "$out/big/base"
	HOST_SURFACES=hdr10 "$h" "$core" "$big/demo00.pk4" 650 doom_resolution=960x540 \
		doom_color_format=30bit-hdr doom_hdr_precision=fp16 doom_hdr_ssaa=2x2 $opt \
		> "$out/big_$r.log" 2>&1
	rc=$?
	if [ $rc -ne 0 ] && [ $rc -ne 3 ]; then
		echo "FAIL 30-bit $r: host exited $rc"
		tail -3 "$out/big_$r.log" | sed 's/^/    /'
		fail=1
		continue
	fi
	for s in $bigshots; do
		if [ ! -f "$bigdir/$s.tga" ]; then
			echo "FAIL 30-bit $r $s: not written"
			fail=1
			continue
		fi
		# 18 header bytes and 512 x 512 BGRA
		size=$(wc -c < "$bigdir/$s.tga")
		if [ "$size" -ne 1048594 ]; then
			echo "FAIL 30-bit $r $s: $size bytes, not a whole 512 square"
			fail=1
			continue
		fi
		level=$("$diff" "$bigdir/$s.tga" "$bigdir/$s.tga" | sed 's/.*first //')
		if [ "$(printf '%s\n' "$level" | awk '{ print ($1 > 1.0) ? 1 : 0 }')" != 1 ]; then
			echo "FAIL 30-bit $r $s: black (mean level $level)"
			fail=1
			continue
		fi
		echo "pass 30-bit $r $s: 512 square, mean level $level"
	done
done
rm -rf "$out"
[ $fail -eq 0 ] && echo "all shots pass" || echo "FAIL"
exit $fail
