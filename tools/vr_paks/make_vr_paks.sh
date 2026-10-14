#!/bin/sh
# Builds the VR replacement packs from vr/ in this repository, laid out the
# way boom3 mounts them with the "Enable VR" core option:
#
#   <out>/base_vr/pak399.pk4     over base
#   <out>/d3xp_vr/pak399roe.pk4  over d3xp (Resurrection of Evil)
#
# Copy both directories next to base/ (and d3xp/) in the game data.
#
# usage: make_vr_paks.sh <out>

set -e

if [ $# -ne 1 ]; then
	echo "usage: $0 <out>" >&2
	exit 1
fi

src=$(cd "$(dirname "$0")/../../vr" && pwd)
out="$1"

mkdir -p "$out/base_vr" "$out/d3xp_vr"
out=$(cd "$out" && pwd)

# a pk4 is a zip of the tree, paths relative to its root
rm -f "$out/base_vr/pak399.pk4" "$out/d3xp_vr/pak399roe.pk4"
(cd "$src/pak399" && zip -qr "$out/base_vr/pak399.pk4" .)
(cd "$src/pak399roe" && zip -qr "$out/d3xp_vr/pak399roe.pk4" .)

echo "built $out/base_vr/pak399.pk4 and $out/d3xp_vr/pak399roe.pk4"
