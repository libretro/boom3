/*
 * gles_pack: see gles_pack.h.
 *
 * C89.
 */
#include "gles_pack.h"

gles_pack_t gles_pack_classify(const unsigned char *rgba, int count)
{
   int grey   = 1;
   int opaque = 1;
   int equal  = 1;
   int black  = 1;
   int white  = 1;
   int i;

   for (i = 0; i < count && (grey || black || white); i++, rgba += 4)
   {
      const int r = rgba[0];
      const int g = rgba[1];
      const int b = rgba[2];
      const int a = rgba[3];
      grey   &= r == g && g == b;
      opaque &= a == 255;
      equal  &= r == a;
      black  &= (r | g | b) == 0;
      white  &= (r & g & b) == 255;
   }
   if (grey && opaque)
      return GLES_PACK_L;
   if (grey && equal)
      return GLES_PACK_I;
   if (black)
      return GLES_PACK_A0;
   if (white)
      return GLES_PACK_A1;
   if (grey)
      return GLES_PACK_LA;
   return GLES_PACK_RGBA;
}

int gles_pack_bytes(gles_pack_t pack, int swizzle)
{
   switch (pack)
   {
      case GLES_PACK_L:
      case GLES_PACK_A0:
         return 1;
      case GLES_PACK_I:
      case GLES_PACK_A1:
         return swizzle ? 1 : 2;
      case GLES_PACK_LA:
         return 2;
      default:
         break;
   }
   return 4;
}

void gles_pack_texels(gles_pack_t pack, int swizzle, const unsigned char *rgba,
      int count, unsigned char *out)
{
   const int bytes = gles_pack_bytes(pack, swizzle);
   int i;

   for (i = 0; i < count; i++, rgba += 4)
   {
      if (bytes == 4)
      {
         out[0] = rgba[0];
         out[1] = rgba[1];
         out[2] = rgba[2];
         out[3] = rgba[3];
         out   += 4;
      }
      else if (bytes == 2)
      {
         /* luminance from red, alpha from alpha; the white mask's
          * luminance is 255 by construction */
         out[0] = rgba[0];
         out[1] = rgba[3];
         out   += 2;
      }
      else
         *out++ = pack == GLES_PACK_L ? rgba[0] : rgba[3];
   }
}
