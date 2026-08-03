/*
===========================================================================
Minizip I/O over libretro-common's VFS, with an mmap fast path.

The stock backend (fill_fopen64_filefunc) gives every pak its own
FILE*: every entry read is seek+fread into a bounce buffer, and Doom 3
hammers paks - map load is thousands of entry opens. This backend
opens the pak through filestream with the FREQUENT_ACCESS hint; where
the platform VFS can map (HAVE_MMAP builds), filestream_get_mapped_ptr
hands back the whole file and every ZREAD/ZSEEK below becomes pointer
arithmetic over the mapping - zero syscalls, no bounce buffer, the
page cache does the work for STORED and DEFLATED entries alike, and
unzip.cpp is untouched. Where mapping is unavailable (Windows VFS has
no mapping arm; consoles), reads go through the RFILE, which still
replaces raw fopen with the VFS's 64 KiB buffered I/O.

The fallback is per-open and transparent: a mapped pak and an unmapped
pak behave identically through the filefunc contract.
===========================================================================
*/

#include <stdlib.h>
#include <string.h>

#include <streams/file_stream.h>
#include <vfs/vfs.h>

#include "ioapi.h"
#include "ioapi_mapped.h"

typedef struct mapped_zip_stream {
	RFILE         *rf;      /* always valid while open */
	const uint8_t *base;    /* non-NULL when the VFS mapped the file */
	int64_t        len;
	int64_t        pos;     /* mapped-mode cursor */
} mapped_zip_stream_t;

static voidpf ZCALLBACK mapped_open64( voidpf opaque, const void *filename, int mode ) {
	mapped_zip_stream_t *s;
	RFILE *rf;

	(void)opaque;
	/* paks are read-only; refuse write modes so a misuse fails loudly */
	if ( mode & ( ZLIB_FILEFUNC_MODE_WRITE | ZLIB_FILEFUNC_MODE_CREATE ) )
		return NULL;

	rf = filestream_open( (const char *)filename,
			RETRO_VFS_FILE_ACCESS_READ,
			RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS );
	if ( !rf )
		return NULL;

	s = (mapped_zip_stream_t *)calloc( 1, sizeof( *s ) );
	if ( !s ) {
		filestream_close( rf );
		return NULL;
	}
	s->rf   = rf;
	s->base = filestream_get_mapped_ptr( rf, &s->len );
	s->pos  = 0;
	return (voidpf)s;
}

static uLong ZCALLBACK mapped_read( voidpf opaque, voidpf stream, void *buf, uLong size ) {
	mapped_zip_stream_t *s = (mapped_zip_stream_t *)stream;
	(void)opaque;
	if ( !s )
		return 0;
	if ( s->base ) {
		int64_t avail = s->len - s->pos;
		int64_t n = ( (int64_t)size < avail ) ? (int64_t)size : avail;
		if ( n <= 0 )
			return 0;
		memcpy( buf, s->base + s->pos, (size_t)n );
		s->pos += n;
		return (uLong)n;
	}
	{
		int64_t n = filestream_read( s->rf, buf, (int64_t)size );
		return ( n > 0 ) ? (uLong)n : 0;
	}
}

static uLong ZCALLBACK mapped_write( voidpf opaque, voidpf stream, const void *buf, uLong size ) {
	(void)opaque; (void)stream; (void)buf; (void)size;
	return 0;   /* read-only backend */
}

static uint64_t ZCALLBACK mapped_tell64( voidpf opaque, voidpf stream ) {
	mapped_zip_stream_t *s = (mapped_zip_stream_t *)stream;
	(void)opaque;
	if ( !s )
		return (uint64_t)-1;
	if ( s->base )
		return (uint64_t)s->pos;
	return (uint64_t)filestream_tell( s->rf );
}

static long ZCALLBACK mapped_seek64( voidpf opaque, voidpf stream, uint64_t offset, int origin ) {
	mapped_zip_stream_t *s = (mapped_zip_stream_t *)stream;
	(void)opaque;
	if ( !s )
		return -1;
	if ( s->base ) {
		int64_t target;
		switch ( origin ) {
			case ZLIB_FILEFUNC_SEEK_SET: target = (int64_t)offset; break;
			case ZLIB_FILEFUNC_SEEK_CUR: target = s->pos + (int64_t)offset; break;
			case ZLIB_FILEFUNC_SEEK_END: target = s->len + (int64_t)offset; break;
			default: return -1;
		}
		if ( target < 0 || target > s->len )
			return -1;
		s->pos = target;
		return 0;
	}
	{
		int whence;
		switch ( origin ) {
			case ZLIB_FILEFUNC_SEEK_SET: whence = RETRO_VFS_SEEK_POSITION_START; break;
			case ZLIB_FILEFUNC_SEEK_CUR: whence = RETRO_VFS_SEEK_POSITION_CURRENT; break;
			case ZLIB_FILEFUNC_SEEK_END: whence = RETRO_VFS_SEEK_POSITION_END; break;
			default: return -1;
		}
		return ( filestream_seek( s->rf, (int64_t)offset, whence ) < 0 ) ? -1 : 0;
	}
}

static int ZCALLBACK mapped_close( voidpf opaque, voidpf stream ) {
	mapped_zip_stream_t *s = (mapped_zip_stream_t *)stream;
	(void)opaque;
	if ( !s )
		return -1;
	filestream_close( s->rf );   /* unmaps too when mapped */
	free( s );
	return 0;
}

static int ZCALLBACK mapped_testerror( voidpf opaque, voidpf stream ) {
	(void)opaque; (void)stream;
	return 0;
}

void fill_mapped_filefunc64( zlib_filefunc64_def *pzlib_filefunc_def ) {
	pzlib_filefunc_def->zopen64_file = mapped_open64;
	pzlib_filefunc_def->zread_file   = mapped_read;
	pzlib_filefunc_def->zwrite_file  = mapped_write;
	pzlib_filefunc_def->ztell64_file = mapped_tell64;
	pzlib_filefunc_def->zseek64_file = mapped_seek64;
	pzlib_filefunc_def->zclose_file  = mapped_close;
	pzlib_filefunc_def->zerror_file  = mapped_testerror;
	pzlib_filefunc_def->opaque       = NULL;
}
