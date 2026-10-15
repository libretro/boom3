/*
 * gles_pack: GLES textures in fewer bytes than RGBA where the image
 * allows it.
 *
 * An RGBA image whose channels repeat - grey, or a mask over black or
 * white - samples back to exactly its RGBA texels from a one- or two-byte
 * format: luminance, alpha, luminance-alpha, or (GLES 3) one red byte read
 * through a swizzle.  Filtering works channel by channel, so the filtered
 * values are the same too, and mip levels made by filtering every channel
 * alike keep the relation the base level has.
 *
 * C89.
 */
#ifndef BOOM3_GLES_PACK_H
#define BOOM3_GLES_PACK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
   GLES_PACK_RGBA = 0,
   GLES_PACK_L,         /* r = g = b, a = 255 */
   GLES_PACK_I,         /* r = g = b = a */
   GLES_PACK_LA,        /* r = g = b */
   GLES_PACK_A0,        /* r = g = b = 0 */
   GLES_PACK_A1         /* r = g = b = 255 */
} gles_pack_t;

/* How count RGBA texels can be held. */
gles_pack_t gles_pack_classify(const unsigned char *rgba, int count);

/* Bytes a texel of the packing takes: 1 or 2, 4 for RGBA.  swizzle is
 * non-zero where texture swizzles exist (GLES 3), which hold the
 * intensity and white-mask packings in one byte; without them they take
 * luminance-alpha. */
int gles_pack_bytes(gles_pack_t pack, int swizzle);

/* The texels in the packing's layout, gles_pack_bytes(pack, swizzle) bytes
 * each, into out. */
void gles_pack_texels(gles_pack_t pack, int swizzle, const unsigned char *rgba,
      int count, unsigned char *out);

#ifdef __cplusplus
}
#endif

#endif
