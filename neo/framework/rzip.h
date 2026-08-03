/*
===========================================================================
rzip - the pak (zip) reader, replacing minizip.

Doom 3 needs a fraction of what minizip carries: parse a pak's central
directory once, look entries up by a stable id, and read each entry
sequentially (with rewind for backward seeks). This is that fraction,
built directly on the libretro-common pieces the tree already vendors:

 - file access through filestream with the FREQUENT_ACCESS hint: where
   the platform VFS can map (HAVE_MMAP), the whole pak is one mapping
   and every read is pointer arithmetic - a DEFLATED entry's compressed
   span is handed to the inflater in one zero-copy set_in, a STORED
   entry is memcpy from the map. Where mapping is unavailable, each
   opened entry holds its own buffered RFILE, so concurrent reads from
   one pak need no locking in either mode (the parsed directory is
   immutable after open).
 - inflation through rinflate (encodings/deflate.h), raw-DEFLATE mode,
   one persistent stream per opened entry, rinflate_reset on rewind.
 - integrity through encoding_crc32 over the decompressed bytes, with
   a loud warning on mismatch at end of stream - the check minizip did
   silently and this engine never looked at.

Scope is stated rather than implied: methods 0 (stored) and 8
(deflate); classic end-of-central-directory only. A pak with ZIP64
markers (0xFFFFFFFF sentinels, or the ZIP64 EOCD locator) is refused
loudly at open - Doom 3 paks are nowhere near the limits, and a wrong
guess would be silent corruption. Encrypted and split archives are
refused by the same principle.
===========================================================================
*/

#ifndef __RZIP_H__
#define __RZIP_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rzip_s rzip_t;
typedef struct rzip_file_s rzip_file_t;

typedef struct rzip_entry_s {
	const char *name;             /* into rzip-owned storage, forward slashes as stored */
	uint64_t    uncompressedSize;
	uint64_t    compressedSize;
	uint64_t    localHeaderOfs;   /* absolute offset of the local header */
	uint32_t    crc32;
	uint16_t    method;           /* 0 stored, 8 deflate */
} rzip_entry_t;

/* open/parse; NULL on any refusal (message via warnFn if set) */
rzip_t *            rzip_open( const char *path );
void                rzip_close( rzip_t *z );

int                 rzip_num_entries( const rzip_t *z );
const rzip_entry_t *rzip_entry_at( const rzip_t *z, int index );

/* sequential reader over one entry; independent instances are safe
   concurrently on the same rzip_t */
rzip_file_t *       rzip_file_open( rzip_t *z, int index );
int                 rzip_file_read( rzip_file_t *f, void *buf, int len );
int                 rzip_file_rewind( rzip_file_t *f );   /* back to entry start; 0 on success */
int64_t             rzip_file_tell( const rzip_file_t *f );  /* uncompressed bytes handed out */
void                rzip_file_close( rzip_file_t *f );

/* Zero-copy borrow: for a STORED entry in a MAPPED pak, the entry's
   bytes ARE the file at a stable address for the pak's lifetime -
   return that pointer (and the length) instead of copying. NULL for
   deflated entries, unmapped paks, or out-of-range indices; the caller
   falls back to a copying read. Borrowed bytes skip the CRC check by
   design: verifying would touch every page up front, defeating the
   laziness that is the point, and the mapping is the file. */
const uint8_t *     rzip_entry_borrow( rzip_t *z, int index, uint64_t *len );
/* same borrow resolved through an already-open handle */
const uint8_t *     rzip_file_borrow( rzip_file_t *f, uint64_t *len );

/* diagnostics sink (Warning-level); set once at startup */
typedef void ( *rzip_warn_fn )( const char *fmt, ... );
void                rzip_set_warn( rzip_warn_fn fn );

#ifdef __cplusplus
}
#endif

#endif /* !__RZIP_H__ */
