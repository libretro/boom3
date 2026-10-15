/*
 * range_alloc: offsets inside one fixed-size region - a GPU buffer the
 * vertex cache packs many surfaces into.  First fit over a sorted list of
 * free spans; freeing merges a span with its neighbours.  Alignments need
 * not be powers of two (the vertex cache aligns to a multiple of every
 * vertex stride it draws with).
 *
 * C89.
 */
#ifndef BOOM3_RANGE_ALLOC_H
#define BOOM3_RANGE_ALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct range_span_s {
   unsigned int offset;
   unsigned int size;
} range_span_t;

typedef struct range_alloc_s {
   range_span_t *spans;     /* free spans, by offset, none touching */
   unsigned int  capacity;  /* bytes in the region */
   unsigned int  used;      /* bytes handed out */
   int           count;
   int           max;
} range_alloc_t;

/* An empty region of capacity bytes, all free.  0 when out of memory. */
int  range_alloc_init(range_alloc_t *ra, unsigned int capacity);
void range_alloc_deinit(range_alloc_t *ra);

/* size bytes at a multiple of align (any non-zero value), the first that
 * fits; 0 when no free span can hold them. */
int  range_alloc_alloc(range_alloc_t *ra, unsigned int size, unsigned int align,
      unsigned int *offset);

/* Gives back what range_alloc_alloc handed out, exactly. */
void range_alloc_free(range_alloc_t *ra, unsigned int offset, unsigned int size);

#ifdef __cplusplus
}
#endif

#endif
