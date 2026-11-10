/*
 * ppmdiff: the mean and the largest absolute difference between two binary
 * PPM (P6) images of the same size, and the first image's mean level so a
 * caller can tell two black frames from two equal pictures:
 * An uncompressed 32-bit TGA, the engine's capture, reads too, by its
 * .tga extension.
 *
 *   mean <m> max <x> over <n> pixels, first <l>
 *
 * The exit status is 0 when the mean difference is at or under the
 * threshold given as the third argument (default 0), else 1; 2 for a file
 * that cannot be read or sizes that differ.
 *
 *   ppmdiff a.ppm b.ppm [threshold]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_ppm(const char *path, int *w, int *h)
{
   FILE *f = fopen(path, "rb");
   char magic[3];
   int maxval;
   unsigned char *data;
   size_t n;

   if (!f)
      return NULL;
   if (fscanf(f, "%2s %d %d %d", magic, w, h, &maxval) != 4 || strcmp(magic, "P6") != 0
         || *w <= 0 || *h <= 0 || maxval != 255)
   {
      fclose(f);
      return NULL;
   }
   fgetc(f);
   n = (size_t)*w * (size_t)*h * 3;
   data = (unsigned char *)malloc(n);
   if (!data || fread(data, 1, n, f) != n)
   {
      free(data);
      fclose(f);
      return NULL;
   }
   fclose(f);
   return data;
}

/* an uncompressed true-colour TGA of 32 bits, as the engine writes it,
 * to RGB rows from the top */
static unsigned char *read_tga(const char *path, int *w, int *h)
{
   FILE *f = fopen(path, "rb");
   unsigned char header[18];
   unsigned char *data, *rgb;
   int x, y, flip;
   size_t n;

   if (!f)
      return NULL;
   if (fread(header, 1, 18, f) != 18 || header[2] != 2 || header[16] != 32)
   {
      fclose(f);
      return NULL;
   }
   *w = header[12] | (header[13] << 8);
   *h = header[14] | (header[15] << 8);
   flip = (header[17] & 0x20) != 0;   /* set: rows from the top */
   if (*w <= 0 || *h <= 0)
   {
      fclose(f);
      return NULL;
   }
   n = (size_t)*w * (size_t)*h * 4;
   data = (unsigned char *)malloc(n);
   rgb  = (unsigned char *)malloc((size_t)*w * (size_t)*h * 3);
   if (!data || !rgb || fread(data, 1, n, f) != n)
   {
      free(data);
      free(rgb);
      fclose(f);
      return NULL;
   }
   fclose(f);
   for (y = 0; y < *h; y++)
   {
      const unsigned char *src = data + (size_t)(flip ? y : *h - 1 - y) * *w * 4;
      unsigned char *dst = rgb + (size_t)y * *w * 3;
      for (x = 0; x < *w; x++)
      {
         dst[x * 3]     = src[x * 4 + 2];
         dst[x * 3 + 1] = src[x * 4 + 1];
         dst[x * 3 + 2] = src[x * 4];
      }
   }
   free(data);
   return rgb;
}

static unsigned char *read_image(const char *path, int *w, int *h)
{
   size_t len = strlen(path);
   if (len > 4 && strcmp(path + len - 4, ".tga") == 0)
      return read_tga(path, w, h);
   return read_ppm(path, w, h);
}

int main(int argc, char **argv)
{
   unsigned char *a, *b;
   int aw, ah, bw, bh, i, n, max = 0;
   double sum = 0.0, level = 0.0, mean, threshold = 0.0;

   if (argc < 3)
   {
      fprintf(stderr, "usage: ppmdiff a.ppm|a.tga b.ppm|b.tga [threshold]\n");
      return 2;
   }
   if (argc > 3)
      threshold = atof(argv[3]);
   a = read_image(argv[1], &aw, &ah);
   b = read_image(argv[2], &bw, &bh);
   if (!a || !b || aw != bw || ah != bh)
   {
      fprintf(stderr, "ppmdiff: cannot compare %s and %s\n", argv[1], argv[2]);
      return 2;
   }
   n = aw * ah * 3;
   for (i = 0; i < n; i++)
   {
      int d = (int)a[i] - (int)b[i];
      if (d < 0)
         d = -d;
      sum += d;
      level += a[i];
      if (d > max)
         max = d;
   }
   mean = sum / n;
   printf("mean %.3f max %d over %d pixels, first %.3f\n", mean, max, aw * ah, level / n);
   free(a);
   free(b);
   return mean <= threshold ? 0 : 1;
}
