/*
 * arb2_nvrec_check: the interaction parameter record of the ARB2
 * renderer's NV tiers (neo/renderer/arb2_nvrec.h).
 *
 * The record decides which words of the parameter buffer are uploaded
 * per draw, so a span that is too narrow leaves a program reading a
 * stale parameter and one that is too wide only costs bandwidth.  This
 * checks the span against a reference that tracks every vec4 on its own:
 * after a reset everything is dirty, an unchanged store leaves the
 * record clean, a change widens the span to cover exactly the changed
 * vec4s and nothing outside them, and the comparison is bitwise (-0.0
 * against 0.0 is a change, a NaN stored again is not).  A random walk
 * of stores, flushed at random, must keep the record's copy equal to the
 * reference buffer that only ever receives the uploaded spans.
 *
 *   make check
 *
 * Builds with any C89 compiler; no GL.
 *
 * C89.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arb2_nvrec.h"

static int failures;

#define CHECK(cond, what) \
	do { \
		if (!(cond)) { \
			printf("FAIL %s (line %d)\n", (what), __LINE__); \
			failures++; \
		} \
	} while (0)

/* what the GPU-side buffer holds: only uploaded spans reach it */
static float gpu[ARB2_NVREC_VECS][4];

static void flush(arb2_nvrec_t *r)
{
	if (ARB2_NVREC_DIRTY(r)) {
		memcpy(&gpu[0][0] + ARB2_NVREC_FIRST_WORD(r), r->v[r->lo],
			ARB2_NVREC_WORDS(r) * sizeof(float));
		ARB2_NVREC_CLEAN(r);
	}
}

static unsigned rng_state = 12345u;

static unsigned rng(void)
{
	rng_state = rng_state * 1103515245u + 12345u;
	return (rng_state >> 8) & 0xffffffu;
}

int main(void)
{
	arb2_nvrec_t r;
	float v[4];
	float negZero[4];
	float nan[4];
	unsigned bits;
	int i, step;

	/* a reset makes the whole record dirty */
	ARB2_NVREC_RESET(&r);
	CHECK(ARB2_NVREC_DIRTY(&r), "reset is dirty");
	CHECK(ARB2_NVREC_FIRST_WORD(&r) == 0, "reset starts at word 0");
	CHECK(ARB2_NVREC_WORDS(&r) == ARB2_NVREC_VECS * 4, "reset covers every word");
	flush(&r);
	CHECK(!ARB2_NVREC_DIRTY(&r), "flushed record is clean");

	/* storing what is already there leaves it clean */
	v[0] = v[1] = v[2] = v[3] = 0.0f;
	for (i = 0; i < ARB2_NVREC_VECS; i++)
		ARB2_NVREC_SET(&r, i, v);
	CHECK(!ARB2_NVREC_DIRTY(&r), "unchanged stores stay clean");

	/* two changes give the span between them, inclusive */
	v[0] = 1.0f;
	ARB2_NVREC_SET(&r, 7, v);
	ARB2_NVREC_SET(&r, 3, v);
	CHECK(ARB2_NVREC_DIRTY(&r), "a change is dirty");
	CHECK(r.lo == 3 && r.hi == 7, "span covers both changes");
	CHECK(ARB2_NVREC_FIRST_WORD(&r) == 12, "span first word");
	CHECK(ARB2_NVREC_WORDS(&r) == 20, "span word count");
	flush(&r);

	/* a single change at either end */
	v[1] = 2.0f;
	ARB2_NVREC_SET(&r, ARB2_NVREC_VECS - 1, v);
	CHECK(r.lo == ARB2_NVREC_VECS - 1 && r.hi == ARB2_NVREC_VECS - 1, "last vec4 alone");
	flush(&r);
	ARB2_NVREC_SET(&r, 0, v);
	CHECK(r.lo == 0 && r.hi == 0, "first vec4 alone");
	flush(&r);

	/* bitwise: -0.0 is a different value to hand a program */
	negZero[0] = negZero[1] = negZero[2] = 0.0f;
	negZero[3] = -0.0f;
	ARB2_NVREC_SET(&r, 5, negZero);   /* 5 holds 0,0,0,0 */
	CHECK(ARB2_NVREC_DIRTY(&r), "-0.0 against 0.0 is a change");
	flush(&r);

	/* and a NaN stored again is not a change */
	bits = 0x7fc00001u;
	memcpy(&nan[0], &bits, sizeof(bits));
	nan[1] = nan[2] = nan[3] = nan[0];
	ARB2_NVREC_SET(&r, 9, nan);
	flush(&r);
	ARB2_NVREC_SET(&r, 9, nan);
	CHECK(!ARB2_NVREC_DIRTY(&r), "the same NaN again stays clean");

	/* random stores and flushes: the uploaded buffer must always equal
	 * the record once flushed */
	for (step = 0; step < 200000; step++) {
		i = (int)(rng() % ARB2_NVREC_VECS);
		v[0] = (float)(rng() % 4);
		v[1] = (float)(rng() % 2);
		v[2] = 0.5f;
		v[3] = (rng() & 1) ? 0.0f : -0.0f;
		ARB2_NVREC_SET(&r, i, v);
		if (ARB2_NVREC_DIRTY(&r)) {
			CHECK(r.lo >= 0 && r.hi < ARB2_NVREC_VECS, "span in range");
		}
		if ((rng() & 7) == 0) {
			flush(&r);
			if (memcmp(gpu, r.v, sizeof(gpu))) {
				printf("FAIL buffer diverged from the record at step %d\n", step);
				failures++;
				break;
			}
		}
	}

	/* the check can fail: a span one vec4 short loses a parameter */
	ARB2_NVREC_RESET(&r);
	flush(&r);
	v[0] = 42.0f;
	ARB2_NVREC_SET(&r, 4, v);
	ARB2_NVREC_SET(&r, 6, v);
	r.hi--;	/* a deliberately short span */
	flush(&r);
	CHECK(memcmp(gpu, r.v, sizeof(gpu)) != 0, "a short span is detected");

	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("arb2_nvrec: all checks passed\n");
	return 0;
}
