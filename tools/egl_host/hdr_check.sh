#!/bin/sh
# hdr_check.sh: every HDR shader the core builds must compile, for every
# tone curve, in both tone-mapped colour formats, on every renderer the
# core has.  Each combination runs the main menu for a few frames under
# egl_host and fails on any composite, bloom or ARB program the core
# reports as not compiling.
#
#   ./hdr_check.sh <core.so> <content> [renderer ...]
#
# <content> is a Doom 3 .pk4 in a base/ directory.  The renderers default
# to the desktop build's (arb2 es2 es3 es31); for a GLES build, which has
# no renderer option, pass gles.

set -u
here=$(cd "$(dirname "$0")" && pwd)
host="$here/egl_host"
core=$1
content=$2
shift 2
renderers=${*:-"arb2 es2 es3 es31"}
curves="reinhard rplain rext jodie devlin aces acesfit aces2 aces2full hejl
filmicalu filmiclog hable hable2017 lottes gt unreal neutral agx drago expo
tumblin ward schlick"
log=$(mktemp)
fail=0
runs=0

for r in $renderers; do
	for fmt in 30bit-hdr 24bit-tonemapped; do
		for c in $curves; do
			opt="doom_renderer=$r"
			[ "$r" = gles ] && opt=
			"$host" "$core" "$content" 40 doom_resolution=320x240 $opt \
				doom_color_format=$fmt doom_hdr_rolloff=$c > "$log" 2>&1
			runs=$((runs + 1))
			if grep -q "did not compile\|did not load\|disabling the HDR pass" "$log"; then
				echo "FAIL $r $fmt $c:"
				grep "HDR" "$log" | sed 's/^/    /'
				fail=$((fail + 1))
			fi
		done
	done
done
rm -f "$log"
echo "$runs runs, $fail failed"
[ $fail -eq 0 ]
