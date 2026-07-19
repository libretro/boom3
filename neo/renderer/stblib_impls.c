// this source file includes the implementation of stb_image_write
// having it in a separate source file allows optimizing it in debug builds (for faster load times)
// without hurting the debugability of the source files stb_image_write is used in
//
// stb_image (the reader) is no longer built: JPEG decoding moved to
// libretro-common's rjpeg via image_transfer, and libretro-common has no image
// writer, so only stb_image_write remains here.


#include <encodings/deflate.h>

/* PNG's IDAT payload is a zlib-wrapped deflate stream, so window_bits is
   positive here (negative would select raw deflate). Previously this used
   miniz's compress2(); it now goes through libretro-common's deflate so the
   core has a single compression implementation. */
static unsigned char* compress_for_stbiw(unsigned char* data, int data_len, int* out_len, int quality)
{
	void *st;
	size_t bufSize;
	size_t total = 0;
	unsigned char* buf;

	/* worst case for stored/incompressible input, plus zlib header/adler and
	   per-block overhead - matches compressBound()'s intent */
	bufSize = (size_t)data_len + (data_len >> 12) + (data_len >> 14)
	          + (data_len >> 25) + 13 + 6;

	/* note that buf will be free'd by stb_image_write.h
	   with STBIW_FREE() (plain free() by default) */
	buf = (unsigned char*)malloc(bufSize);
	if (buf == NULL)
		return NULL;

	st = rdeflate_new(quality, 15);
	if (!st)
	{
		free(buf);
		return NULL;
	}

	rdeflate_set_in(st, data, (size_t)data_len);
	rdeflate_set_out(st, buf, bufSize);
	rdeflate_finish(st);

	for (;;)
	{
		size_t read = 0, wrote = 0;
		int ret = rdeflate_process(st, &read, &wrote);
		total += wrote;
		if (ret == RDEFLATE_PROCESS_END)
			break;
		if (ret == RDEFLATE_PROCESS_ERROR || (read == 0 && wrote == 0))
		{
			rdeflate_free(st);
			free(buf);
			return NULL;
		}
	}
	rdeflate_free(st);

	*out_len = (int)total;

	return buf;
}

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ZLIB_COMPRESS compress_for_stbiw
#include "stb_image_write.h"

