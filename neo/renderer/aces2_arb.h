/*
===========================================================================

ACES 2.0 output transform, ARB fragment program - building blocks.

The desktop composite is ARB, so this is the backend that decides
whether the transform is usable where it matters.  The GLSL version is
already transcribed and checked; this file exists because ARB is missing
one thing GLSL has, and it is worth having that solved and measured
before the rest is written on top of it.

What ARB does have: SIN, COS, POW, LG2, EX2, RCP, RSQ, and CMP for
branchless selection.  Every conditional in the transform - the early
outs, the two branches of the intersection solve, the toe's limit test,
the remap's threshold test - is a CMP.

What it does not have is atan2, and the hue angle is what indexes the
three tables.  Two ways round it: index the tables by direction with a
two-dimensional texture and never form the angle, or compute the angle.
The direction-indexed table is tempting because the shader already holds
cos and sin of the hue as the normalised Aab pair, and everything else
that uses the hue - the chroma compression's harmonics, the conversion
back - can be built from those two by angle-doubling rather than from
the angle itself.  It was rejected on resolution: a 256 square table
gives about 0.45 degrees around the unit circle against the hue table's
1 degree, but it costs 512 KB where the hue table costs 2.8 KB, and the
whole point of the packed table was that one fetch answers a hue.

So the angle is computed.  The polynomial below is odd, fitted on the
ratio of the smaller to the larger magnitude so its argument never
leaves [0, 1], with the octant and quadrant folded back in by three
CMPs.

Measured twice, because the driver run is limited by what a byte
framebuffer can show: in double precision the worst error against libm
over 200000 angles is 1.7e-06 radians, which is 0.0001 degrees, or a
ten-thousandth of one hue table entry.  Assembled and run on a driver
the worst is 0.11 degrees, and that is half of one 8-bit step in the
readback rather than the program - the same measurement floor the GLSL
check ran into.

===========================================================================
*/

#ifndef __ACES2_ARB_H__
#define __ACES2_ARB_H__

/* Takes the Aab pair in src.xy - x is a, y is b - and leaves the hue
 * angle in radians in r.x.  Clobbers a, b, z, z2, t. */
#define ACES2_ARB_ATAN2 \
	"PARAM kAtanP = { 0.99997726, -0.33262347, 0.19354346, -0.11643287 };\n" \
	"PARAM kAtanQ = { 0.05265332, -0.01172120, 1.5707963268, 3.1415926536 };\n" \
	"ABS a, src;\n" \
	"MAX b.x, a.x, a.y;\n" \
	"MIN b.y, a.x, a.y;\n" \
	"MAX b.x, b.x, 0.00000001;\n" \
	"RCP b.x, b.x;\n" \
	"MUL z.x, b.y, b.x;\n" \
	"MUL z2.x, z.x, z.x;\n" \
	"MAD r.x, z2.x, kAtanQ.y, kAtanQ.x;\n" \
	"MAD r.x, r.x, z2.x, kAtanP.w;\n" \
	"MAD r.x, r.x, z2.x, kAtanP.z;\n" \
	"MAD r.x, r.x, z2.x, kAtanP.y;\n" \
	"MAD r.x, r.x, z2.x, kAtanP.x;\n" \
	"MUL r.x, r.x, z.x;\n" \
	/* |y| > |x| folds the octant */ \
	"SUB t.x, a.x, a.y;\n" \
	"SUB t.y, kAtanQ.z, r.x;\n" \
	"CMP r.x, t.x, t.y, r.x;\n" \
	/* x < 0 folds the half plane */ \
	"SUB t.y, kAtanQ.w, r.x;\n" \
	"CMP r.x, src.x, t.y, r.x;\n" \
	/* y < 0 mirrors it */ \
	"SUB t.y, 0.0, r.x;\n" \
	"CMP r.x, src.y, t.y, r.x;\n"

#endif /* !__ACES2_ARB_H__ */
