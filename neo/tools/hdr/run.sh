#!/bin/sh
# Runs the core's own HDR composite under OSMesa and checks the frame.
#
# This drives the shipped functions rather than a copy of them.  The
# trick is that they are static and get inlined at -O3, so libretro.cpp
# is rebuilt once at -O1 -fno-inline, its statics are globalised with
# objcopy, and the harness links against that object plus every other
# object the core already built.
#
#   cd neo && make -j4 && tools/hdr/run.sh
#
# Exit status is the result: 0 means the frame rendered with its regions
# correctly ordered, non-zero means it did not.
set -e
cd "$(dirname "$0")/../.."

CMD=$(make -B -n sys/libretro/libretro.o 2>/dev/null | grep -m1 'libretro\.cpp')
CMD=$(echo "$CMD" | sed 's/-O3/-O1 -fno-inline/; s#-o sys/libretro/libretro.o#-o /tmp/hdr_ni.o#')
eval "$CMD"

objcopy \
	--globalize-symbol=_ZL17hdr_ensure_targetii \
	--globalize-symbol=_ZL14hdr_bind_scenev \
	--globalize-symbol=_ZL11hdr_presentj \
	/tmp/hdr_ni.o /tmp/hdr_glob.o

OBJS=$(find . -name '*.o' | grep -v 'sys/libretro/libretro\.o' | tr '\n' ' ')
g++ -O1 -o /tmp/hdr_core_harness tools/hdr/core_harness.cpp /tmp/hdr_glob.o \
	$OBJS -lOSMesa -lGL -lm -lpthread -ldl

exec /tmp/hdr_core_harness
