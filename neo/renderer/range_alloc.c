/*
 * range_alloc: see range_alloc.h.
 *
 * C89.
 */
#include <stdlib.h>
#include <string.h>

#include "range_alloc.h"

int range_alloc_init(range_alloc_t *ra, unsigned int capacity)
{
   ra->max   = 16;
   ra->spans = (range_span_t *)malloc(ra->max * sizeof(range_span_t));
   if (!ra->spans)
   {
      ra->max   = 0;
      ra->count = 0;
      return 0;
   }
   ra->capacity        = capacity;
   ra->used            = 0;
   ra->count           = 1;
   ra->spans[0].offset = 0;
   ra->spans[0].size   = capacity;
   return 1;
}

void range_alloc_deinit(range_alloc_t *ra)
{
   free(ra->spans);
   ra->spans    = NULL;
   ra->count    = 0;
   ra->max      = 0;
   ra->capacity = 0;
   ra->used     = 0;
}

/* Room for one more span at index at, the ones from there moved up. */
static int range_alloc_insert(range_alloc_t *ra, int at)
{
   if (ra->count == ra->max)
   {
      int           max   = ra->max * 2;
      range_span_t *spans = (range_span_t *)realloc(ra->spans, max * sizeof(range_span_t));
      if (!spans)
         return 0;
      ra->spans = spans;
      ra->max   = max;
   }
   memmove(ra->spans + at + 1, ra->spans + at, (ra->count - at) * sizeof(range_span_t));
   ra->count++;
   return 1;
}

static void range_alloc_remove(range_alloc_t *ra, int at)
{
   memmove(ra->spans + at, ra->spans + at + 1, (ra->count - at - 1) * sizeof(range_span_t));
   ra->count--;
}

int range_alloc_alloc(range_alloc_t *ra, unsigned int size, unsigned int align,
      unsigned int *offset)
{
   int i;

   if (!size || !align)
      return 0;
   for (i = 0; i < ra->count; i++)
   {
      range_span_t *s       = &ra->spans[i];
      unsigned int  start   = ((s->offset + align - 1) / align) * align;
      unsigned int  pad     = start - s->offset;
      unsigned int  end     = s->offset + s->size;
      unsigned int  tail;

      if (start < s->offset || pad > s->size || s->size - pad < size)
         continue;
      tail = end - (start + size);
      if (pad && tail)
      {
         /* the span splits in two around the allocation */
         if (!range_alloc_insert(ra, i + 1))
            return 0;
         s                         = &ra->spans[i];
         ra->spans[i + 1].offset   = start + size;
         ra->spans[i + 1].size     = tail;
         s->size                   = pad;
      }
      else if (pad)
         s->size = pad;
      else if (tail)
      {
         s->offset = start + size;
         s->size   = tail;
      }
      else
         range_alloc_remove(ra, i);
      ra->used += size;
      *offset   = start;
      return 1;
   }
   return 0;
}

void range_alloc_free(range_alloc_t *ra, unsigned int offset, unsigned int size)
{
   int lo = 0;
   int hi = ra->count;
   int prev_touches;
   int next_touches;

   if (!size)
      return;
   /* the first span after offset */
   while (lo < hi)
   {
      int mid = (lo + hi) / 2;
      if (ra->spans[mid].offset < offset)
         lo = mid + 1;
      else
         hi = mid;
   }
   ra->used -= size;

   prev_touches = lo > 0
      && ra->spans[lo - 1].offset + ra->spans[lo - 1].size == offset;
   next_touches = lo < ra->count
      && offset + size == ra->spans[lo].offset;

   if (prev_touches && next_touches)
   {
      ra->spans[lo - 1].size += size + ra->spans[lo].size;
      range_alloc_remove(ra, lo);
   }
   else if (prev_touches)
      ra->spans[lo - 1].size += size;
   else if (next_touches)
   {
      ra->spans[lo].offset  = offset;
      ra->spans[lo].size   += size;
   }
   else if (range_alloc_insert(ra, lo))
   {
      ra->spans[lo].offset = offset;
      ra->spans[lo].size   = size;
   }
   /* else: out of memory for the list; the span stays lost to this
    * region rather than corrupting it */
}
