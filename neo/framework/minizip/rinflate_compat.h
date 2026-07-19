/*
   rinflate_compat.h

   A minimal z_stream-compatible inflate shim implemented on top of
   libretro-common's streaming inflate (encodings/deflate.h), so minizip's
   unzip.cpp can keep using the classic zlib calling convention while the
   actual decompression is done by libretro-common instead of miniz/zlib.

   Only the surface unzip.cpp needs is provided:

     types    z_stream, Bytef, uInt, uLong, voidpf, z_off_t
     calls    inflateInit2(), inflate(), inflateEnd(), crc32()
     codes    Z_OK, Z_STREAM_END, Z_DATA_ERROR, Z_BUF_ERROR, Z_MEM_ERROR,
              Z_STREAM_ERROR, Z_NO_FLUSH, Z_SYNC_FLUSH, Z_FINISH,
              Z_DEFLATED, MAX_WBITS

   Semantics deliberately match zlib as unzip.cpp relies on them:
     - next_in/avail_in and next_out/avail_out are advanced in place by the
       amount consumed/produced,
     - total_in/total_out accumulate across calls,
     - the return is Z_STREAM_END once the deflate stream is finished,
       Z_OK while progressing, and a negative code on error,
     - msg stays NULL unless an error occurred (unzip.cpp turns a non-NULL
       msg into Z_DATA_ERROR even on a non-negative return).

   window_bits follows the zlib convention and is passed straight through:
   negative selects raw deflate, which is what zip entries use
   (inflateInit2(&stream, -MAX_WBITS)).
*/

#ifndef RINFLATE_COMPAT_H
#define RINFLATE_COMPAT_H

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

#include <retro_inline.h>
#include <encodings/deflate.h>
#include <encodings/crc32.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char  Byte;
typedef unsigned char  Bytef;
typedef unsigned int   uInt;
typedef unsigned long  uLong;
typedef unsigned long  uLongf;
typedef void          *voidp;
typedef void          *voidpf;
typedef const void    *voidpc;
typedef uint32_t       z_crc_t;
typedef long           z_off_t;

#define Z_OK             0
#define Z_STREAM_END     1
#define Z_ERRNO         (-1)
#define Z_STREAM_ERROR  (-2)
#define Z_DATA_ERROR    (-3)
#define Z_MEM_ERROR     (-4)
#define Z_BUF_ERROR     (-5)

#define Z_NO_FLUSH       0
#define Z_SYNC_FLUSH     2
#define Z_FINISH         4

#define Z_DEFLATED       8
#define MAX_WBITS        15

typedef voidpf (*alloc_func)(voidpf opaque, uInt items, uInt size);
typedef void   (*free_func)(voidpf opaque, voidpf address);

typedef struct z_stream_s
{
   const Bytef *next_in;   /* next input byte                       */
   uInt         avail_in;  /* number of bytes available at next_in  */
   uLong        total_in;  /* total number of input bytes read      */

   Bytef       *next_out;  /* next output byte                      */
   uInt         avail_out; /* remaining free space at next_out      */
   uLong        total_out; /* total number of bytes output          */

   const char  *msg;       /* last error message, NULL if no error  */
   void        *state;     /* rinflate handle                       */

   /* zalloc/zfree/opaque are accepted and ignored: rinflate does its own
      allocation. They exist so callers that assign them still compile. */
   alloc_func   zalloc;
   free_func    zfree;
   voidpf       opaque;
   int          data_type;
   uLong        adler;
} z_stream;

typedef z_stream *z_streamp;

static INLINE int inflateInit2(z_stream *strm, int window_bits)
{
   if (!strm)
      return Z_STREAM_ERROR;

   strm->msg       = NULL;
   strm->total_in  = 0;
   strm->total_out = 0;
   strm->state     = rinflate_new(window_bits);

   if (!strm->state)
      return Z_MEM_ERROR;
   return Z_OK;
}

static INLINE int inflateEnd(z_stream *strm)
{
   if (!strm)
      return Z_STREAM_ERROR;
   if (strm->state)
   {
      rinflate_free(strm->state);
      strm->state = NULL;
   }
   return Z_OK;
}

static INLINE int inflate(z_stream *strm, int flush)
{
   size_t read  = 0;
   size_t wrote = 0;
   int    ret;

   (void)flush;   /* rinflate consumes as much as it can every call */

   if (!strm || !strm->state)
      return Z_STREAM_ERROR;

   rinflate_set_in(strm->state, strm->next_in, strm->avail_in);
   rinflate_set_out(strm->state, strm->next_out, strm->avail_out);

   ret = rinflate_process(strm->state, &read, &wrote);

   /* advance the caller's cursors exactly like zlib does */
   strm->next_in   += read;
   strm->avail_in  -= (uInt)read;
   strm->total_in  += (uLong)read;

   strm->next_out  += wrote;
   strm->avail_out -= (uInt)wrote;
   strm->total_out += (uLong)wrote;

   if (ret == RDEFLATE_PROCESS_ERROR)
   {
      strm->msg = "inflate error";
      return Z_DATA_ERROR;
   }
   if (ret == RDEFLATE_PROCESS_END)
      return Z_STREAM_END;

   /* Made no progress and cannot: report the zlib-equivalent condition so
      the caller stops rather than spinning. */
   if (read == 0 && wrote == 0 && strm->avail_out != 0)
      return Z_BUF_ERROR;

   return Z_OK;
}

static INLINE uLong crc32(uLong crc, const Bytef *buf, uInt len)
{
   if (!buf)
      return 0;
   return (uLong)encoding_crc32((uint32_t)crc, (const uint8_t*)buf, (size_t)len);
}

#ifdef __cplusplus
}
#endif

#endif /* RINFLATE_COMPAT_H */
