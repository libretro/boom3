#!/bin/sh
# Builds rzip_test mapped (HAVE_MMAP) and unmapped and runs both over a
# pak with a stored and a deflated entry, under ASan and UBSan.
#
#   tools/rzip/run.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
neo=$here/../../neo
lc=$neo/sys/libretro-common
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

python3 -I - <<'EOF'
import os, random, zipfile
random.seed(1)
e1 = bytes(random.randrange(256) for _ in range(300000))
e2 = b''.join(b'line %d of a text that deflates\n' % i for i in range(40000))
open('/tmp/e1.bin', 'wb').write(e1)
open('/tmp/e2.bin', 'wb').write(e2)
with zipfile.ZipFile('/tmp/test.pk4', 'w') as z:
    z.writestr(zipfile.ZipInfo('stored.bin'), e1, zipfile.ZIP_STORED)
    z.writestr(zipfile.ZipInfo('deflated.bin'), e2, zipfile.ZIP_DEFLATED)
EOF

srcs="$lc/streams/file_stream.c $lc/vfs/vfs_implementation.c $lc/vfs/vfs_hybrid.c
 $lc/encodings/encoding_crc32.c $lc/encodings/encoding_deflate.c
 $lc/encodings/encoding_utf.c $lc/file/file_path.c $lc/file/file_path_io.c
 $lc/file/retro_dirent.c $lc/compat/compat_strl.c $lc/compat/compat_strcasestr.c
 $lc/compat/compat_posix_string.c $lc/compat/fopen_utf8.c
 $lc/string/stdstring.c $lc/string/rstrtod.c $lc/time/rtime.c
 $lc/features/features_cpu.c"
flags="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -I$lc/include -I$neo/framework"

status=0
for mmap in 1 0; do
	def=
	[ $mmap = 1 ] && def=-DHAVE_MMAP
	for s in $srcs; do
		gcc -std=gnu99 $flags $def -c "$s" -o "$out/$(basename "$s" .c).o"
	done
	g++ $flags $def -c "$neo/framework/rzip.cpp" -o "$out/rzip.o"
	gcc -std=gnu99 $flags $def -c "$here/rzip_test.c" -o "$out/rzip_test.o"
	g++ -fsanitize=address,undefined -o "$out/rzip_test" "$out"/*.o
	echo "== HAVE_MMAP=$mmap"
	"$out/rzip_test" || status=1
	rm -f "$out"/*.o
done
rm -f /tmp/e1.bin /tmp/e2.bin /tmp/test.pk4 /tmp/bad.pk4
exit $status
