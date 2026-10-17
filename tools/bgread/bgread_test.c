/* Regression test for the background reader's read, bgread_read: the
 * reader must leave a read within one chunk of a cancel or of its exit
 * flag, never after the whole entry, and a read it left on exit must
 * resume to the exact bytes. Reads a stored and a deflated pk4 entry and
 * a plain file of several chunks each.
 *
 * build: make -C tools/bgread
 * run:   make -C tools/bgread check */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "framework/bgread.h"

#define ENTRY_SIZE	( 5 * BGREAD_CHUNK + 12345 )

static int fail;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		fail = 1;
}

static unsigned char *pattern(int n, unsigned seed)
{
	int i;
	unsigned char *p = (unsigned char *)malloc(n);
	for (i = 0; i < n; i++)
	{
		seed = seed * 1103515245u + 12345u;
		/* runs of a byte so the deflated entry actually compresses */
		p[i] = (unsigned char)((seed >> 20) & 0x0f) + (i & 0x300 ? 'a' : 'A');
	}
	return p;
}

/* One source: osFile or zipFile, len bytes that must equal ref. */
static void run_source(const char *name, RFILE *os, rzip_file_t *zf,
		int len, const unsigned char *ref)
{
	unsigned char *buf = (unsigned char *)malloc(len);
	retro_atomic_int_t cancel, stop;
	int bytes;
	char what[128];

	retro_atomic_int_init(&cancel, 0);
	retro_atomic_int_init(&stop, 0);

	/* A whole read, no flags set */
	memset(buf, 0, len);
	bytes = 0;
	bgread_read(os, zf, buf, len, &bytes, &cancel, &stop);
	sprintf(what, "%s: whole read (%d of %d)", name, bytes, len);
	check(bytes == len && !memcmp(buf, ref, len), what);

	/* Cancelled before it starts: nothing read, -1 */
	if (os) filestream_seek(os, 0, RETRO_VFS_SEEK_POSITION_START);
	else rzip_file_rewind(zf);
	retro_atomic_store_release_int(&cancel, 1);
	bytes = 0;
	bgread_read(os, zf, buf, len, &bytes, &cancel, &stop);
	sprintf(what, "%s: cancelled before start -> -1 (%d)", name, bytes);
	check(bytes == -1, what);
	retro_atomic_store_release_int(&cancel, 0);

	/* Stopped mid-way: the reader exits within one chunk; what is read
	 * so far is exact; the resumed read completes it exactly. The stop is
	 * set after a partial read, as the main thread would set it while
	 * the reader is between chunks. */
	if (os) filestream_seek(os, 0, RETRO_VFS_SEEK_POSITION_START);
	else rzip_file_rewind(zf);
	memset(buf, 0, len);
	bytes = 0;
	retro_atomic_store_release_int(&stop, 1);
	bgread_read(os, zf, buf, len, &bytes, &cancel, &stop);
	sprintf(what, "%s: stop seen before the first chunk (%d)", name, bytes);
	check(bytes == 0, what);
	retro_atomic_store_release_int(&stop, 0);

	/* Read one chunk's worth by bounding len, then stop */
	bytes = 0;
	bgread_read(os, zf, buf, BGREAD_CHUNK, &bytes, &cancel, &stop);
	sprintf(what, "%s: first chunk exact (%d)", name, bytes);
	check(bytes == BGREAD_CHUNK && !memcmp(buf, ref, BGREAD_CHUNK), what);
	retro_atomic_store_release_int(&stop, 1);
	bgread_read(os, zf, buf, len, &bytes, &cancel, &stop);
	sprintf(what, "%s: stop between chunks leaves the read where it was (%d)", name, bytes);
	check(bytes == BGREAD_CHUNK, what);
	retro_atomic_store_release_int(&stop, 0);
	bgread_read(os, zf, buf, len, &bytes, &cancel, NULL);
	sprintf(what, "%s: resumed read exact (%d of %d)", name, bytes, len);
	check(bytes == len && !memcmp(buf, ref, len), what);

	/* Cancel between chunks: -1, and no more than one further chunk was
	 * read (the old whole-file read ignored a cancel once started) */
	if (os) filestream_seek(os, 0, RETRO_VFS_SEEK_POSITION_START);
	else rzip_file_rewind(zf);
	bytes = 0;
	bgread_read(os, zf, buf, 2 * BGREAD_CHUNK, &bytes, &cancel, &stop);
	retro_atomic_store_release_int(&cancel, 1);
	bgread_read(os, zf, buf, len, &bytes, &cancel, &stop);
	sprintf(what, "%s: cancel between chunks -> -1 (%d)", name, bytes);
	check(bytes == -1, what);
	{
		/* the source position says how much was really read */
		long pos = os ? (long)filestream_tell(os) : (long)rzip_file_tell(zf);
		sprintf(what, "%s: cancel cost at most one chunk (pos %ld)", name, pos);
		check(pos <= 3 * BGREAD_CHUNK, what);
	}
	retro_atomic_store_release_int(&cancel, 0);

	free(buf);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	char path[1024];
	unsigned char *refS = pattern(ENTRY_SIZE, 1);
	unsigned char *refD = pattern(ENTRY_SIZE, 2);
	unsigned char *refF = pattern(ENTRY_SIZE, 3);
	FILE *o;
	rzip_t *z;
	int i;

	/* the plain file */
	sprintf(path, "%s/bgread_plain.bin", dir);
	o = fopen(path, "wb");
	fwrite(refF, 1, ENTRY_SIZE, o);
	fclose(o);
	{
		RFILE *os = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ,
				RETRO_VFS_FILE_ACCESS_HINT_NONE);
		check(os != NULL, "plain file opens");
		if (os)
		{
			run_source("plain", os, NULL, ENTRY_SIZE, refF);
			filestream_close(os);
		}
	}

	/* the pk4: one stored and one deflated entry, made by zip(1) */
	sprintf(path, "%s/stored.bin", dir);
	o = fopen(path, "wb"); fwrite(refS, 1, ENTRY_SIZE, o); fclose(o);
	sprintf(path, "%s/deflated.bin", dir);
	o = fopen(path, "wb"); fwrite(refD, 1, ENTRY_SIZE, o); fclose(o);
	sprintf(path, "cd '%s' && rm -f bgread.pk4 && zip -q -0 bgread.pk4 stored.bin && zip -q -9 bgread.pk4 deflated.bin", dir);
	check(system(path) == 0, "zip makes the pk4");
	sprintf(path, "%s/bgread.pk4", dir);
	z = rzip_open(path);
	check(z != NULL, "pk4 opens");
	if (z)
	{
		for (i = 0; i < rzip_num_entries(z); i++)
		{
			const rzip_entry_t *e = rzip_entry_at(z, i);
			rzip_file_t *f = rzip_file_open(z, i);
			check(f != NULL, e->name);
			if (!f)
				continue;
			run_source(e->name, NULL, f, ENTRY_SIZE,
					e->method == 0 ? refS : refD);
			rzip_file_close(f);
		}
		rzip_close(z);
	}

	free(refS);
	free(refD);
	free(refF);
	printf(fail ? "RESULT: FAIL\n" : "RESULT: ALL OK\n");
	return fail;
}
