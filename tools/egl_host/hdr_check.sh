#!/bin/sh
# hdr_check.sh: every HDR shader the core builds must compile, for every
# tone curve, in the three colour formats of the scene pipeline, on every renderer the
# core has.  Each combination runs the main menu for a few frames under
# egl_host and fails on any composite, bloom or ARB program the core
# reports as not compiling.  Where the renderers include arb2, each
# other renderer's menu frame is also compared with ARB2's by ppmdiff,
# per curve and format: the GLSL composite has to draw what the ARB one
# draws.
#
#   ./hdr_check.sh <core.so> <content> [renderer ...]
#
# <content> is a Doom 3 .pk4 in a base/ directory.  The renderers default
# to the desktop build's (arb2 es2 es3 es31, and vulkan when vk_host is
# built - on the device the loader picks, lavapipe with
# VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json); for a GLES
# build, which has no renderer option, pass gles.

set -u
here=$(cd "$(dirname "$0")" && pwd)
host="$here/egl_host"
vkhost="$here/../vk_host/vk_host"
diff="$here/ppmdiff"
core=$1
content=$2
shift 2
default="arb2 es2 es3 es31"
[ -x "$vkhost" ] && default="$default vulkan"
renderers=${*:-$default}
curves="reinhard rplain rext jodie devlin aces acesfit aces2 aces2full hejl
filmicalu filmiclog hable hable2017 lottes gt unreal neutral agx drago expo
tumblin ward schlick"
log=$(mktemp)
shots=$(mktemp -d)
fail=0
runs=0
reference=
case " $renderers " in
	*" arb2 "*) reference=arb2 ;;
esac
# the menu has to be up by the frame compared, or every comparison is of
# two black frames
if [ -n "$reference" ]; then
	"$host" "$core" "$content" 60 doom_resolution=320x240 doom_renderer=$reference \
		ppm="$shots/up" shots=59 > "$log" 2>&1
	case "$("$diff" "$shots/up_59.ppm" "$shots/up_59.ppm")" in
		*"first 0.000"|"")
			echo "FAIL: no picture at frame 59 on $reference"
			rm -rf "$log" "$shots"
			exit 1 ;;
	esac
fi

for r in $renderers; do
	for fmt in 30bit-hdr 24bit-tonemapped 16bit-scrgb; do
		for c in $curves; do
			opt="doom_renderer=$r"
			[ "$r" = gles ] && opt=
			h=$host
			[ "$r" = vulkan ] && h=$vkhost
			"$h" "$core" "$content" 60 doom_resolution=320x240 $opt \
				doom_color_format=$fmt doom_hdr_rolloff=$c \
				ppm="$shots/${r}_${fmt}_$c" shots=59 > "$log" 2>&1
			runs=$((runs + 1))
			if grep -q "did not compile\|did not load\|disabling the HDR pass" "$log"; then
				echo "FAIL $r $fmt $c:"
				grep "HDR" "$log" | sed 's/^/    /'
				fail=$((fail + 1))
			elif [ -n "$reference" ] && [ "$r" != "$reference" ]; then
				# the same frame as the ARB composite, within a code value
				if ! res=$("$diff" "$shots/${reference}_${fmt}_${c}_59.ppm" \
						"$shots/${r}_${fmt}_${c}_59.ppm" 1.0); then
					echo "FAIL $r $fmt $c against $reference: $res"
					fail=$((fail + 1))
				fi
			fi
		done
	done
done
rm -rf "$log" "$shots"
echo "$runs runs, $fail failed"
[ $fail -eq 0 ]
