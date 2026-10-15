/*
 * range_alloc_test: randomized check of renderer/range_alloc.c against a
 * byte map of the same region.  Every allocation must land inside the
 * region, at its alignment, on bytes nothing else holds; every free must
 * make those bytes available again, with the free list kept sorted,
 * merged and summing to what is not handed out.
 *
 * build: cc -std=c89 -pedantic -Wall -O1 -g -fsanitize=address,undefined \
 *        -I../../neo/renderer -o range_alloc_test range_alloc_test.c \
 *        ../../neo/renderer/range_alloc.c
 * run:   ./range_alloc_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "range_alloc.h"

#define REGION   (1u << 20)
#define LIVE_MAX 4096
#define STEPS    400000

typedef struct live_s {
   unsigned int offset;
   unsigned int size;
} live_t;

static unsigned char owner[REGION];
static live_t live[LIVE_MAX];
static int live_count;
static unsigned long rng = 12345;

static unsigned int rnd(unsigned int n)
{
   rng = rng * 1103515245ul + 12345ul;
   return (unsigned int)((rng >> 8) % n);
}

static int check_list(const range_alloc_t *ra)
{
   unsigned int free_bytes = 0;
   int i;
   for (i = 0; i < ra->count; i++)
   {
      const range_span_t *s = &ra->spans[i];
      unsigned int b;
      if (!s->size || s->offset + s->size > ra->capacity)
         return 0;
      if (i > 0 && ra->spans[i - 1].offset + ra->spans[i - 1].size >= s->offset)
         return 0; /* unsorted, overlapping or touching (unmerged) */
      for (b = s->offset; b < s->offset + s->size; b++)
         if (owner[b])
            return 0; /* a free span over held bytes */
      free_bytes += s->size;
   }
   return free_bytes + ra->used == ra->capacity;
}

int main(void)
{
   static const unsigned int aligns[] = { 1, 2, 4, 16, 60, 240, 256 };
   range_alloc_t ra;
   int step;
   int allocs = 0;
   int fails  = 0;

   if (!range_alloc_init(&ra, REGION))
   {
      printf("FAIL init\n");
      return 1;
   }
   for (step = 0; step < STEPS; step++)
   {
      if (live_count < LIVE_MAX && (live_count == 0 || rnd(100) < 55))
      {
         unsigned int align = aligns[rnd(sizeof(aligns) / sizeof(aligns[0]))];
         unsigned int size  = 1 + rnd(rnd(8) == 0 ? 65536 : 2048);
         unsigned int off;
         if (range_alloc_alloc(&ra, size, align, &off))
         {
            unsigned int b;
            if (off % align || off + size > REGION)
            {
               printf("FAIL step %d: offset %u size %u align %u\n", step, off, size, align);
               return 1;
            }
            for (b = off; b < off + size; b++)
            {
               if (owner[b])
               {
                  printf("FAIL step %d: overlap at %u\n", step, b);
                  return 1;
               }
               owner[b] = 1;
            }
            live[live_count].offset = off;
            live[live_count].size   = size;
            live_count++;
            allocs++;
         }
         else
            fails++;
      }
      else
      {
         int i = (int)rnd((unsigned int)live_count);
         memset(owner + live[i].offset, 0, live[i].size);
         range_alloc_free(&ra, live[i].offset, live[i].size);
         live[i] = live[--live_count];
      }
      if ((step & 1023) == 0 && !check_list(&ra))
      {
         printf("FAIL step %d: free list inconsistent\n", step);
         return 1;
      }
   }
   while (live_count)
   {
      live_count--;
      memset(owner + live[live_count].offset, 0, live[live_count].size);
      range_alloc_free(&ra, live[live_count].offset, live[live_count].size);
   }
   if (!check_list(&ra) || ra.count != 1 || ra.used != 0)
   {
      printf("FAIL: region not whole again (%d spans, %u used)\n", ra.count, ra.used);
      return 1;
   }
   range_alloc_deinit(&ra);
   printf("PASS %d allocations, %d refused for space\n", allocs, fails);
   return 0;
}
