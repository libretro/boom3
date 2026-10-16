/*
 * ppmdiff: the mean and the largest absolute difference between two binary
 * PPM (P6) images of the same size, and the first image's mean level so a
 * caller can tell two black frames from two equal pictures:
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

int main(int argc, char **argv)
{
   unsigned char *a, *b;
   int aw, ah, bw, bh, i, n, max = 0;
   double sum = 0.0, level = 0.0, mean, threshold = 0.0;

   if (argc < 3)
   {
      fprintf(stderr, "usage: ppmdiff a.ppm b.ppm [threshold]\n");
      return 2;
   }
   if (argc > 3)
      threshold = atof(argv[3]);
   a = read_ppm(argv[1], &aw, &ah);
   b = read_ppm(argv[2], &bw, &bh);
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
