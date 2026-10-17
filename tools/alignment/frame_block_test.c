/* Regression test for the frame memory block layout: base must be 16
 * byte aligned on this pointer width, and R_FrameAlloc's rounding
 * keeps every allocation so. On 64-bit the old header put base at 20.
 *
 * build: make -C tools/alignment
 * run:   make -C tools/alignment check */
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include "renderer/frame_block.h"

int main(void)
{
	int fail = 0;
	size_t off = offsetof(frameMemoryBlock_t, base);
	size_t i, used = 0;
	printf("pointer %u bytes, base at %u, header %u\n",
		(unsigned)sizeof(void *), (unsigned)off,
		(unsigned)sizeof(frameMemoryBlock_t));
	if (off % 16)
	{
		printf("FAIL base misaligned by %u\n", (unsigned)(off % 16));
		fail = 1;
	}
	/* the rounding R_FrameAlloc applies to every request */
	for (i = 1; i < 4096; i++)
	{
		size_t bytes = (i + 16) & ~(size_t)15;
		if ((off + used) % 16)
		{
			printf("FAIL allocation %u lands at %u\n", (unsigned)i, (unsigned)(off + used));
			fail = 1;
			break;
		}
		used += bytes;
	}
	printf(fail ? "RESULT: FAIL\n" : "RESULT: ALL OK\n");
	return fail;
}
