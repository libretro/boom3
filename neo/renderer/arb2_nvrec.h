/*
 * arb2_nvrec: the interaction parameter record the NV program tiers of
 * the ARB2 renderer read from program.buffer[0].
 *
 * One record holds every per-draw interaction parameter as a vec4, in
 * the order the NV interaction programs index it.  The record is the
 * CPU-side copy of the parameter buffer's contents: setting a vec4
 * compares it bitwise with what the buffer already holds and widens the
 * dirty span only when it differs, so one upload of the span replaces a
 * call per parameter and an unchanged parameter costs a compare.
 *
 * Bitwise, not float equality: -0.0f == 0.0f but they are different
 * values to hand a program, and a NaN compares unequal to itself.
 *
 * C89.
 */
#ifndef BOOM3_ARB2_NVREC_H
#define BOOM3_ARB2_NVREC_H

#include <string.h>

/* vec4 indices into program.buffer[0]; the NV interaction programs
 * hardcode these */
enum {
	ARB2_NVREC_LIGHT_ORIGIN,
	ARB2_NVREC_VIEW_ORIGIN,
	ARB2_NVREC_PROJECT_S,
	ARB2_NVREC_PROJECT_T,
	ARB2_NVREC_PROJECT_Q,
	ARB2_NVREC_FALLOFF_S,
	ARB2_NVREC_BUMP_S,
	ARB2_NVREC_BUMP_T,
	ARB2_NVREC_DIFFUSE_S,
	ARB2_NVREC_DIFFUSE_T,
	ARB2_NVREC_SPECULAR_S,
	ARB2_NVREC_SPECULAR_T,
	ARB2_NVREC_COLOR_MODULATE,
	ARB2_NVREC_COLOR_ADD,
	ARB2_NVREC_DIFFUSE_COLOR,
	ARB2_NVREC_SPECULAR_COLOR,
	ARB2_NVREC_VECS
};

typedef struct arb2_nvrec_s {
	float v[ARB2_NVREC_VECS][4];  /* what the buffer holds, once flushed */
	int   lo;                     /* dirty span [lo, hi]; empty when lo > hi */
	int   hi;
} arb2_nvrec_t;

/* Forget what the buffer holds: the whole record is dirty, so the next
 * flush uploads all of it. */
#define ARB2_NVREC_RESET(r) \
	do { \
		memset((r)->v, 0, sizeof((r)->v)); \
		(r)->lo = 0; \
		(r)->hi = ARB2_NVREC_VECS - 1; \
	} while (0)

/* Store vec4 src at index i, widening the dirty span when it changed. */
#define ARB2_NVREC_SET(r, i, src) \
	do { \
		if (memcmp((r)->v[(i)], (src), 4 * sizeof(float))) { \
			memcpy((r)->v[(i)], (src), 4 * sizeof(float)); \
			if ((i) < (r)->lo) \
				(r)->lo = (i); \
			if ((i) > (r)->hi) \
				(r)->hi = (i); \
		} \
	} while (0)

/* The dirty span as words for ProgramBufferParametersfvNV: first word
 * ARB2_NVREC_FIRST_WORD(r), ARB2_NVREC_WORDS(r) words from
 * (r)->v[(r)->lo].  Only meaningful when ARB2_NVREC_DIRTY(r). */
#define ARB2_NVREC_DIRTY(r)      ((r)->lo <= (r)->hi)
#define ARB2_NVREC_FIRST_WORD(r) ((r)->lo * 4)
#define ARB2_NVREC_WORDS(r)      (((r)->hi - (r)->lo + 1) * 4)

/* The span has been uploaded. */
#define ARB2_NVREC_CLEAN(r) \
	do { \
		(r)->lo = ARB2_NVREC_VECS; \
		(r)->hi = -1; \
	} while (0)

#endif
