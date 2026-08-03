/* rzip.cpp - see rzip.h for scope and design. */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <streams/file_stream.h>
#include <vfs/vfs.h>
#include <encodings/deflate.h>
#include <encodings/crc32.h>

#ifdef HAVE_MMAP
#include <sys/mman.h>
#endif

#include "rzip.h"

#define RZIP_EOCD_SIG   0x06054b50u
#define RZIP_CD_SIG     0x02014b50u
#define RZIP_LOCAL_SIG  0x04034b50u
#define RZIP_Z64LOC_SIG 0x07064b50u
#define RZIP_IN_CHUNK   (32*1024)   /* unmapped-mode compressed refill */

static rzip_warn_fn rzip_warn = NULL;
void rzip_set_warn( rzip_warn_fn fn ) { rzip_warn = fn; }
#define WARN if ( rzip_warn ) rzip_warn

struct rzip_s {
	char          *path;
	RFILE         *rf;        /* held open for the pak's lifetime */
	const uint8_t *base;      /* non-NULL when the VFS mapped the pak */
	int64_t        len;
	int            numEntries;
	rzip_entry_t  *entries;
	char          *nameBlob;
};

struct rzip_file_s {
	rzip_t             *pak;
	const rzip_entry_t *e;
	uint64_t            dataOfs;   /* absolute start of entry data */
	uint64_t            outPos;    /* uncompressed bytes handed out */
	uint32_t            runCrc;
	int                 crcChecked;
	/* deflate state */
	void               *inf;       /* rinflate stream, deflate entries only */
	uint64_t            compPos;   /* compressed bytes fed (unmapped mode) */
	size_t              inAvail;   /* unconsumed bytes of the current span */
	/* unmapped mode */
	RFILE              *rf;        /* private handle; NULL when pak is mapped */
	uint8_t            *inBuf;     /* refill buffer, unmapped deflate only */
};

static uint16_t rd16( const uint8_t *p ) { return (uint16_t)( p[0] | ( p[1] << 8 ) ); }
static uint32_t rd32( const uint8_t *p ) {
	return (uint32_t)p[0] | ( (uint32_t)p[1] << 8 ) | ( (uint32_t)p[2] << 16 ) | ( (uint32_t)p[3] << 24 );
}

/* read [ofs, ofs+n) of the pak into dst, map or RFILE */
static int rzip_pread( rzip_t *z, uint64_t ofs, void *dst, size_t n ) {
	if ( ofs + n > (uint64_t)z->len )
		return -1;
	if ( z->base ) {
		memcpy( dst, z->base + ofs, n );
		return 0;
	}
	if ( filestream_seek( z->rf, (int64_t)ofs, RETRO_VFS_SEEK_POSITION_START ) < 0 )
		return -1;
	return ( filestream_read( z->rf, dst, (int64_t)n ) == (int64_t)n ) ? 0 : -1;
}

rzip_t *rzip_open( const char *path ) {
	rzip_t *z = (rzip_t *)calloc( 1, sizeof( *z ) );
	if ( !z )
		return NULL;

	z->rf = filestream_open( path, RETRO_VFS_FILE_ACCESS_READ,
			RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS );
	if ( !z->rf ) {
		free( z );
		return NULL;
	}
	z->len  = filestream_get_size( z->rf );
	z->base = filestream_get_mapped_ptr( z->rf, NULL );
	z->path = strdup( path );

	/*
	   EOCD: last 22 bytes when there is no archive comment; scan back
	   through the largest legal comment otherwise. 22 + 65535 caps the
	   tail read.
	*/
	{
		int64_t tailLen = z->len < ( 22 + 65535 ) ? z->len : ( 22 + 65535 );
		uint8_t *tail;
		int64_t  i, eocd = -1;
		uint32_t cdOfs, cdSize;
		uint16_t count, disk, cdDisk, diskCount;

		if ( tailLen < 22 )
			goto fail;
		tail = (uint8_t *)malloc( (size_t)tailLen );
		if ( !tail || rzip_pread( z, (uint64_t)( z->len - tailLen ), tail, (size_t)tailLen ) != 0 ) {
			free( tail );
			goto fail;
		}
		for ( i = tailLen - 22; i >= 0; i-- ) {
			if ( rd32( tail + i ) == RZIP_EOCD_SIG ) {
				eocd = i;
				break;
			}
		}
		if ( eocd < 0 ) {
			WARN( "rzip: '%s': no end-of-central-directory record", path );
			free( tail );
			goto fail;
		}
		disk      = rd16( tail + eocd + 4 );
		cdDisk    = rd16( tail + eocd + 6 );
		count     = rd16( tail + eocd + 10 );   /* total entries */
		diskCount = rd16( tail + eocd + 8 );
		cdSize    = rd32( tail + eocd + 12 );
		cdOfs     = rd32( tail + eocd + 16 );
		free( tail );

		if ( disk != 0 || cdDisk != 0 || diskCount != count ) {
			WARN( "rzip: '%s': split archives are not supported", path );
			goto fail;
		}
		/* ZIP64: sentinel fields, or a ZIP64 EOCD locator right before
		   the EOCD. Refuse loudly rather than guess. */
		if ( count == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOfs == 0xFFFFFFFFu ) {
			WARN( "rzip: '%s': ZIP64 archives are not supported", path );
			goto fail;
		}
		if ( ( z->len - tailLen + eocd ) >= 20 ) {
			uint8_t loc[4];
			if ( rzip_pread( z, (uint64_t)( z->len - tailLen + eocd - 20 ), loc, 4 ) == 0
					&& rd32( loc ) == RZIP_Z64LOC_SIG ) {
				WARN( "rzip: '%s': ZIP64 archives are not supported", path );
				goto fail;
			}
		}

		/* central directory: parsed in place over the mapping when we
		   have one (no transient the size of the directory), copied out
		   only on the unmapped fallback */
		{
			uint8_t *cdCopy = NULL;
			const uint8_t *cd;
			size_t   nameBytes = 0;
			uint32_t p = 0;
			int      n;

			if ( z->base ) {
				if ( (uint64_t)cdOfs + cdSize > (uint64_t)z->len )
					goto fail;
				cd = z->base + cdOfs;
			} else {
				cdCopy = (uint8_t *)malloc( cdSize ? cdSize : 1 );
				if ( !cdCopy || rzip_pread( z, cdOfs, cdCopy, cdSize ) != 0 ) {
					free( cdCopy );
					goto fail;
				}
				cd = cdCopy;
			}
			z->entries = (rzip_entry_t *)calloc( count ? count : 1, sizeof( rzip_entry_t ) );

			/* pass 1: validate records, sum name storage */
			for ( n = 0; n < count; n++ ) {
				uint16_t nameLen, extraLen, cmtLen, flags, method;
				if ( p + 46 > cdSize || rd32( cd + p ) != RZIP_CD_SIG ) {
					WARN( "rzip: '%s': corrupt central directory (entry %d)", path, n );
					free( cdCopy );
					goto fail;
				}
				flags    = rd16( cd + p + 8 );
				method   = rd16( cd + p + 10 );
				nameLen  = rd16( cd + p + 28 );
				extraLen = rd16( cd + p + 30 );
				cmtLen   = rd16( cd + p + 32 );
				if ( flags & 0x0001 ) {
					WARN( "rzip: '%s': encrypted entries are not supported", path );
					free( cdCopy );
					goto fail;
				}
				if ( method != 0 && method != 8 ) {
					WARN( "rzip: '%s': unsupported compression method %u (entry %d)", path, method, n );
					free( cdCopy );
					goto fail;
				}
				if ( rd32( cd + p + 20 ) == 0xFFFFFFFFu || rd32( cd + p + 24 ) == 0xFFFFFFFFu
						|| rd32( cd + p + 42 ) == 0xFFFFFFFFu ) {
					WARN( "rzip: '%s': ZIP64 entry fields are not supported", path );
					free( cdCopy );
					goto fail;
				}
				nameBytes += (size_t)nameLen + 1;
				p += 46u + nameLen + extraLen + cmtLen;
			}
			if ( p > cdSize ) {
				free( cdCopy );
				goto fail;
			}

			z->nameBlob = (char *)malloc( nameBytes ? nameBytes : 1 );
			if ( !z->nameBlob ) {
				free( cdCopy );
				goto fail;
			}

			/* pass 2: fill */
			{
				char *nb = z->nameBlob;
				p = 0;
				for ( n = 0; n < count; n++ ) {
					uint16_t nameLen  = rd16( cd + p + 28 );
					uint16_t extraLen = rd16( cd + p + 30 );
					uint16_t cmtLen   = rd16( cd + p + 32 );
					rzip_entry_t *e   = &z->entries[n];
					e->method            = rd16( cd + p + 10 );
					e->crc32             = rd32( cd + p + 16 );
					e->compressedSize    = rd32( cd + p + 20 );
					e->uncompressedSize  = rd32( cd + p + 24 );
					e->localHeaderOfs    = rd32( cd + p + 42 );
					memcpy( nb, cd + p + 46, nameLen );
					nb[nameLen] = '\0';
					e->name = nb;
					nb += nameLen + 1;
					p += 46u + nameLen + extraLen + cmtLen;
				}
			}
			free( cdCopy );
			z->numEntries = count;
		}
	}
	return z;

fail:
	rzip_close( z );
	return NULL;
}

void rzip_close( rzip_t *z ) {
	if ( !z )
		return;
	if ( z->rf )
		filestream_close( z->rf );   /* unmaps too */
	free( z->entries );
	free( z->nameBlob );
	free( z->path );
	free( z );
}

int rzip_num_entries( const rzip_t *z ) {
	return z ? z->numEntries : 0;
}

const rzip_entry_t *rzip_entry_at( const rzip_t *z, int index ) {
	if ( !z || index < 0 || index >= z->numEntries )
		return NULL;
	return &z->entries[index];
}

/* streaming-read hint over a mapped span: pulls the faults off the
   consumer's timeline for big sequential entries (roq, ogg). Hint
   only - failure is ignorable by contract. */
static void rzip_madvise_span( rzip_t *z, uint64_t ofs, uint64_t n ) {
#if defined(HAVE_MMAP) && defined(POSIX_MADV_SEQUENTIAL)
	if ( z->base && n >= 256 * 1024 ) {
		uintptr_t a = (uintptr_t)( z->base + ofs );
		uintptr_t pg = a & ~(uintptr_t)4095;
		posix_madvise( (void *)pg, (size_t)( n + ( a - pg ) ), POSIX_MADV_SEQUENTIAL );
	}
#else
	(void)z; (void)ofs; (void)n;
#endif
}

const uint8_t *rzip_entry_borrow( rzip_t *z, int index, uint64_t *len ) {
	const rzip_entry_t *e = rzip_entry_at( z, index );
	uint8_t lh[30];
	uint64_t dataOfs;

	if ( len )
		*len = 0;
	if ( !e || !z->base || e->method != 0 )
		return NULL;
	if ( rzip_pread( z, e->localHeaderOfs, lh, 30 ) != 0 || rd32( lh ) != RZIP_LOCAL_SIG )
		return NULL;
	dataOfs = e->localHeaderOfs + 30u + rd16( lh + 26 ) + rd16( lh + 28 );
	if ( dataOfs + e->uncompressedSize > (uint64_t)z->len )
		return NULL;
	if ( len )
		*len = e->uncompressedSize;
	rzip_madvise_span( z, dataOfs, e->uncompressedSize );
	return z->base + dataOfs;
}

const uint8_t *rzip_file_borrow( rzip_file_t *f, uint64_t *len ) {
	if ( len )
		*len = 0;
	if ( !f || !f->pak->base || f->e->method != 0 )
		return NULL;
	if ( len )
		*len = f->e->uncompressedSize;
	return f->pak->base + f->dataOfs;
}

rzip_file_t *rzip_file_open( rzip_t *z, int index ) {
	const rzip_entry_t *e = rzip_entry_at( z, index );
	rzip_file_t *f;
	uint8_t lh[30];

	if ( !e )
		return NULL;

	/* the local header's name/extra lengths can differ from the central
	   directory's; the data offset must come from the local header */
	if ( rzip_pread( z, e->localHeaderOfs, lh, 30 ) != 0 || rd32( lh ) != RZIP_LOCAL_SIG ) {
		WARN( "rzip: '%s': bad local header for '%s'", z->path, e->name );
		return NULL;
	}

	f = (rzip_file_t *)calloc( 1, sizeof( *f ) );
	if ( !f )
		return NULL;
	f->pak     = z;
	f->e       = e;
	f->dataOfs = e->localHeaderOfs + 30u + rd16( lh + 26 ) + rd16( lh + 28 );
	f->runCrc  = encoding_crc32( 0, NULL, 0 );

	rzip_madvise_span( z, f->dataOfs, e->compressedSize );

	if ( f->dataOfs + e->compressedSize > (uint64_t)z->len ) {
		WARN( "rzip: '%s': entry '%s' extends past the archive", z->path, e->name );
		free( f );
		return NULL;
	}

	if ( !z->base ) {
		/* private handle: sequential reads with no shared cursor */
		f->rf = filestream_open( z->path, RETRO_VFS_FILE_ACCESS_READ,
				RETRO_VFS_FILE_ACCESS_HINT_NONE );
		if ( !f->rf || filestream_seek( f->rf, (int64_t)f->dataOfs, RETRO_VFS_SEEK_POSITION_START ) < 0 ) {
			if ( f->rf )
				filestream_close( f->rf );
			free( f );
			return NULL;
		}
	}

	if ( e->method == 8 ) {
		f->inf = rinflate_new( -15 );   /* raw DEFLATE, per the zip format */
		if ( !f->inf ) {
			if ( f->rf )
				filestream_close( f->rf );
			free( f );
			return NULL;
		}
		if ( z->base ) {
			/* the whole compressed span, one zero-copy set_in */
			rinflate_set_in( f->inf, z->base + f->dataOfs, (size_t)e->compressedSize );
		} else {
			f->inBuf = (uint8_t *)malloc( RZIP_IN_CHUNK );
			if ( !f->inBuf ) {
				rinflate_free( f->inf );
				filestream_close( f->rf );
				free( f );
				return NULL;
			}
		}
	}
	return f;
}

/* completed exactly: verify the directory CRC against what was read */
static void rzip_file_check_crc( rzip_file_t *f ) {
	if ( f->crcChecked || f->outPos != f->e->uncompressedSize )
		return;
	f->crcChecked = 1;
	if ( f->runCrc != f->e->crc32 ) {
		WARN( "rzip: '%s': CRC mismatch on '%s' (directory %08x, data %08x)",
				f->pak->path, f->e->name, f->e->crc32, f->runCrc );
	}
}

int rzip_file_read( rzip_file_t *f, void *buf, int len ) {
	uint64_t remain;
	int done = 0;

	if ( !f || len <= 0 )
		return 0;
	remain = f->e->uncompressedSize - f->outPos;
	if ( (uint64_t)len > remain )
		len = (int)remain;
	if ( len == 0 )
		return 0;

	if ( f->e->method == 0 ) {
		/* stored */
		if ( f->pak->base ) {
			memcpy( buf, f->pak->base + f->dataOfs + f->outPos, (size_t)len );
			done = len;
		} else {
			int64_t n = filestream_read( f->rf, buf, len );
			done = ( n > 0 ) ? (int)n : 0;
		}
	} else {
		/* deflate */
		rinflate_set_out( f->inf, (uint8_t *)buf, (size_t)len );
		for ( ;; ) {
			size_t rd = 0, wr = 0;
			int ret;

			if ( !f->pak->base && f->inAvail == 0 && f->compPos < f->e->compressedSize ) {
				/* refill strictly when the previous span is drained:
				   set_in REPLACES the span, so an early refill would
				   discard unconsumed compressed bytes */
				uint64_t left = f->e->compressedSize - f->compPos;
				size_t n = ( left < RZIP_IN_CHUNK ) ? (size_t)left : RZIP_IN_CHUNK;
				int64_t got = filestream_read( f->rf, f->inBuf, (int64_t)n );
				if ( got <= 0 )
					break;
				rinflate_set_in( f->inf, f->inBuf, (size_t)got );
				f->compPos += (uint64_t)got;
				f->inAvail  = (size_t)got;
			}

			ret = rinflate_process( f->inf, &rd, &wr );
			if ( rd > f->inAvail ) rd = f->inAvail;   /* mapped mode: inAvail unused */
			if ( !f->pak->base ) f->inAvail -= rd;
			done += (int)wr;
			if ( ret == RDEFLATE_PROCESS_ERROR ) {
				WARN( "rzip: '%s': inflate error in '%s'", f->pak->path, f->e->name );
				break;
			}
			if ( done >= len || ret == RDEFLATE_PROCESS_END )
				break;
			if ( ret == RDEFLATE_PROCESS_NEXT && wr == 0 && rd == 0
					&& ( f->pak->base || f->compPos >= f->e->compressedSize ) )
				break;   /* wants input none of which remains: truncated */
			if ( done < len )
				rinflate_set_out( f->inf, (uint8_t *)buf + done, (size_t)( len - done ) );
		}
	}

	if ( done > 0 ) {
		f->runCrc = encoding_crc32( f->runCrc, (const uint8_t *)buf, (size_t)done );
		f->outPos += (uint64_t)done;
	}
	rzip_file_check_crc( f );
	return done;
}

int64_t rzip_file_tell( const rzip_file_t *f ) {
	return f ? (int64_t)f->outPos : 0;
}

int rzip_file_rewind( rzip_file_t *f ) {
	if ( !f )
		return -1;
	f->outPos     = 0;
	f->compPos    = 0;
	f->inAvail    = 0;
	f->runCrc     = encoding_crc32( 0, NULL, 0 );
	f->crcChecked = 0;
	if ( f->rf && filestream_seek( f->rf, (int64_t)f->dataOfs, RETRO_VFS_SEEK_POSITION_START ) < 0 )
		return -1;
	if ( f->inf ) {
		rinflate_reset( f->inf, -15 );
		if ( f->pak->base )
			rinflate_set_in( f->inf, f->pak->base + f->dataOfs, (size_t)f->e->compressedSize );
	}
	return 0;
}

void rzip_file_close( rzip_file_t *f ) {
	if ( !f )
		return;
	if ( f->inf )
		rinflate_free( f->inf );
	if ( f->rf )
		filestream_close( f->rf );
	free( f->inBuf );
	free( f );
}
