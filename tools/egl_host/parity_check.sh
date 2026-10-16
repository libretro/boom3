#!/bin/sh
# parity_check.sh: the GLSL renderers against ARB2 on one frame.  The
# content's autoexec.cfg sets up the view (a map, a save or a demo); every
# renderer runs it for the given number of frames under egl_host and the
# last frame is compared with ARB2's by ppmdiff.  A renderer whose mean
# absolute difference is over the threshold fails.
#
#   ./parity_check.sh <core.so> <content> <frames> [threshold] [key=value ...]
#
# <content> is a Doom 3 .pk4 in a base/ directory, <frames> how many to run
# (the view must be up by then), the threshold a mean difference in 8-bit
# code values (default 1.5).  Further key=value pairs are core options for
# every run, say doom_color_format=24bit-tonemapped.  The build must be a
# desktop one with the renderer option.

set -u
here=$(cd "$(dirname "$0")" && pwd)
host="$here/egl_host"
diff="$here/ppmdiff"
core=$1
content=$2
frames=$3
threshold=${4:-1.5}
[ $# -ge 4 ] && shift 4 || shift $#
out=$(mktemp -d)
last=$((frames - 1))
fail=0

for r in arb2 es2 es3 es31; do
	"$host" "$core" "$content" "$frames" doom_resolution=640x480 doom_renderer=$r \
		ppm="$out/$r" shots=$last "$@" > "$out/$r.log" 2>&1
	if [ ! -f "$out/${r}_$last.ppm" ]; then
		echo "FAIL $r: no frame $last"
		tail -5 "$out/$r.log" | sed 's/^/    /'
		fail=$((fail + 1))
	fi
done
for r in es2 es3 es31; do
	[ -f "$out/${r}_$last.ppm" ] || continue
	if res=$("$diff" "$out/arb2_$last.ppm" "$out/${r}_$last.ppm" "$threshold"); then
		echo "pass $r: $res"
	else
		echo "FAIL $r: $res"
		fail=$((fail + 1))
	fi
done
rm -rf "$out"
echo "$fail failed"
[ $fail -eq 0 ]
