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


/*
   Everything from the AP0 input to the tone-mapped J, verified.

   The three fetches this leaves for the rest of the transform - the
   packed hue table in L, the cusp pair, the reach and the exponent -
   are already read; what remains to write is the chroma compression and
   the gamut compression on top.

   Checked the same way as the GLSL: assembled on a driver, run over 300
   colours, compared against the CPU reference.  Worst difference
   0.00196, which is half an 8-bit readback step.

   Two transposes were found doing it, and they point in opposite
   directions, which is why both had to be found separately.  DP3 against
   an env register holding a matrix ROW computes matrix-times-column.
   The appearance matrices are applied on the CPU as row-vector-times-
   matrix, so their COLUMNS go into the registers.  The AP0 to AP1 pair
   are applied the other way round on the CPU, so their rows do.  Getting
   the first right and the second wrong left an error of 0.116 that looked
   exactly like a numerical problem rather than a bookkeeping one.

   Cost so far: 99 ALU and 24 temporaries.  The temporaries are worth
   watching - ARB guarantees only 16, and while this driver reports it
   under native limits and NVIDIA allows far more, the full program still
   has two stages to go.  If it does not fit, the fix is to stop naming a
   temporary per intermediate and reuse them, which costs nothing but
   readability.
*/

/*
   Everything from the AP0 input through the chroma compression.
   Replaces the tone-mapped-J macro this file carried before; that was a
   prefix of this and there is no reason to keep both.

   Checked the same way: assembled on a driver, 300 colours, against the
   CPU reference.  The compressed M matches to 0.00196 - the 8-bit
   readback floor again - and the tone-mapped J it is built on still
   matches to the same figure with the chroma stage running after it.

   The bug that cost this stage its afternoon was mine and it was a
   probe, not the transform.  To read an intermediate out through a byte
   framebuffer it has to be scaled into 0..1, and the scaling line for an
   earlier probe was left in the program.  Every value downstream of it
   was then a hundredth of what it should be, so the chroma stage looked
   broken and each term I checked inside it looked broken too - which is
   how a measurement fault imitates a deep bug.  Four terms were
   verified individually before I thought to re-check the input they all
   shared.  Cheap lesson: when several independent things fail at once,
   suspect what they have in common.

   Still 24 temporaries; the gamut stage is what will settle whether
   that has to come down.
*/

#define ACES2_ARB_TO_CHROMA_COMPRESSED \
	"PARAM inA=program.env[0];\n" \
	"PARAM inB=program.env[1];\n" \
	"PARAM inC=program.env[2];\n" \
	"PARAM caA=program.env[3];\n" \
	"PARAM caB=program.env[4];\n" \
	"PARAM caC=program.env[5];\n" \
	"PARAM acA=program.env[6];\n" \
	"PARAM acB=program.env[7];\n" \
	"PARAM acC=program.env[8];\n" \
	"PARAM lrA=program.env[9];\n" \
	"PARAM lrB=program.env[10];\n" \
	"PARAM lrC=program.env[11];\n" \
	"PARAM inS=program.env[12];\n" \
	"PARAM limS=program.env[13];\n" \
	"PARAM ts=program.env[14];\n" \
	"PARAM cc=program.env[15];\n" \
	"PARAM cc2=program.env[16];\n" \
	"PARAM gm=program.env[17];\n" \
	"PARAM lsc=program.env[18];\n" \
	"PARAM p1A=program.env[19];\n" \
	"PARAM p1B=program.env[20];\n" \
	"PARAM p1C=program.env[21];\n" \
	"PARAM p0A=program.env[22];\n" \
	"PARAM p0B=program.env[23];\n" \
	"PARAM p0C=program.env[24];\n" \
	"PARAM src=program.env[25];\n" \
	"PARAM kA={0.99997726,-0.33262347,0.19354346,-0.11643287};\n" \
	"PARAM kB={0.05265332,-0.01172120,1.5707963268,3.1415926536};\n" \
	"PARAM kC={0.42,27.13,100.0,0.0027777778};\n" \
	"PARAM kH={11.34072,16.46899,7.88380,77.12896};\n" \
	"PARAM kH2={14.66441,-6.37224,9.19364,0.0};\n" \
	"PARAM kE={2.3809523809,57.2957795131,0.5,1.0};\n" \
	"TEMP a,b,c,d,e,f,g,h,mc,n,o,q,r,s,t,u,v,w,z,Jl,Mc,L,cu,ab;\n" \
	"DP3 a.x,src,p1A;\n" \
	"DP3 a.y,src,p1B;\n" \
	"DP3 a.z,src,p1C;\n" \
	"MAX a,a,0.0;\n" \
	"MIN a,a,gm.y;\n" \
	"DP3 b.x,a,p0A;\n" \
	"DP3 b.y,a,p0B;\n" \
	"DP3 b.z,a,p0C;\n" \
	"DP3 c.x,b,inA;\n" \
	"DP3 c.y,b,inB;\n" \
	"DP3 c.z,b,inC;\n" \
	"ABS d,c;\n" \
	"POW d.x,d.x,kC.x;\n" \
	"POW d.y,d.y,kC.x;\n" \
	"POW d.z,d.z,kC.x;\n" \
	"ADD e,d,kC.y;\n" \
	"RCP e.x,e.x;\n" \
	"RCP e.y,e.y;\n" \
	"RCP e.z,e.z;\n" \
	"MUL d,d,e;\n" \
	"SUB e,0.0,d;\n" \
	"CMP d,c,e,d;\n" \
	"DP3 f.x,d,caA;\n" \
	"DP3 f.y,d,caB;\n" \
	"DP3 f.z,d,caC;\n" \
	"MAX g.x,f.x,0.0000001;\n" \
	"POW g.x,g.x,inS.y;\n" \
	"MUL Jl.x,g.x,kC.z;\n" \
	"MUL g.y,f.y,f.y;\n" \
	"MAD g.y,f.z,f.z,g.y;\n" \
	"RSQ g.z,g.y;\n" \
	"RCP Mc.x,g.z;\n" \
	"MOV ab.x,f.y;\n" \
	"MOV ab.y,f.z;\n" \
	"ABS a,ab;\n" \
	"MAX b.x,a.x,a.y;\n" \
	"MIN b.y,a.x,a.y;\n" \
	"MAX b.x,b.x,0.00000001;\n" \
	"RCP b.x,b.x;\n" \
	"MUL z.x,b.y,b.x;\n" \
	"MUL z.y,z.x,z.x;\n" \
	"MAD r.x,z.y,kB.y,kB.x;\n" \
	"MAD r.x,r.x,z.y,kA.w;\n" \
	"MAD r.x,r.x,z.y,kA.z;\n" \
	"MAD r.x,r.x,z.y,kA.y;\n" \
	"MAD r.x,r.x,z.y,kA.x;\n" \
	"MUL r.x,r.x,z.x;\n" \
	"SUB t.x,a.x,a.y;\n" \
	"SUB t.y,kB.z,r.x;\n" \
	"CMP r.x,t.x,t.y,r.x;\n" \
	"SUB t.y,kB.w,r.x;\n" \
	"CMP r.x,ab.x,t.y,r.x;\n" \
	"SUB t.y,0.0,r.x;\n" \
	"CMP r.x,ab.y,t.y,r.x;\n" \
	"MUL h.x,r.x,kE.y;\n" \
	"ADD t.x,h.x,360.0;\n" \
	"CMP h.x,h.x,t.x,h.x;\n" \
	"ADD t.x,h.x,0.5;\n" \
	"MUL t.x,t.x,kC.w;\n" \
	"MOV t.y,0.5;\n" \
	"TEX L,t,texture[3],2D;\n" \
	"MUL L,L,lsc;\n" \
	"MUL t.x,Jl.x,0.01;\n" \
	"POW t.x,t.x,inS.z;\n" \
	"MUL t.x,t.x,inS.w;\n" \
	"MIN t.y,t.x,0.99;\n" \
	"SUB t.z,1.0,t.y;\n" \
	"MAX t.z,t.z,0.000001;\n" \
	"RCP t.z,t.z;\n" \
	"MUL t.y,t.y,kC.y;\n" \
	"MUL t.y,t.y,t.z;\n" \
	"POW t.y,t.y,kE.x;\n" \
	"RCP t.z,inS.x;\n" \
	"MUL t.y,t.y,t.z;\n" \
	"MUL t.y,t.y,0.01;\n" \
	"ADD t.z,t.y,ts.y;\n" \
	"MAX t.z,t.z,0.0000001;\n" \
	"RCP t.z,t.z;\n" \
	"MAX t.w,t.y,0.0;\n" \
	"MUL t.z,t.w,t.z;\n" \
	"POW t.z,t.z,ts.z;\n" \
	"MUL t.z,t.z,ts.x;\n" \
	"MUL t.w,t.z,t.z;\n" \
	"ADD u.x,t.z,ts.w;\n" \
	"MAX u.x,u.x,0.0000001;\n" \
	"RCP u.x,u.x;\n" \
	"MUL t.w,t.w,u.x;\n" \
	"MAX t.w,t.w,0.0;\n" \
	"MUL t.w,t.w,kC.z;\n" \
	"MUL u.y,t.w,inS.x;\n" \
	"POW u.y,u.y,kC.x;\n" \
	"ADD u.z,u.y,kC.y;\n" \
	"RCP u.z,u.z;\n" \
	"MUL u.y,u.y,u.z;\n" \
	"RCP u.z,inS.w;\n" \
	"MUL u.y,u.y,u.z;\n" \
	"POW u.y,u.y,inS.y;\n" \
	"MUL n.x,u.y,kC.z;\n" \
	"RCP q.x,cc.x;\n" \
	"MUL q.x,n.x,q.x;\n" \
	"SUB q.y,1.0,q.x;\n" \
	"MAX q.y,q.y,0.0;\n" \
	"MUL w.x,r.x,1.0;\n" \
	"COS w.y,w.x;\n" \
	"SIN w.z,w.x;\n" \
	"MUL v.x,w.y,w.y;\n" \
	"MAD v.x,-w.z,w.z,v.x;\n" \
	"MUL v.y,w.y,w.z;\n" \
	"ADD v.y,v.y,v.y;\n" \
	"MUL v.z,w.y,w.y;\n" \
	"MUL v.z,v.z,w.y;\n" \
	"MUL v.z,v.z,4.0;\n" \
	"MAD v.z,-3.0,w.y,v.z;\n" \
	"MUL v.w,w.z,w.z;\n" \
	"MUL v.w,v.w,w.z;\n" \
	"MUL v.w,v.w,-4.0;\n" \
	"MAD v.w,3.0,w.z,v.w;\n" \
	"MUL o.x,w.y,kH.x;\n" \
	"MAD o.x,v.x,kH.y,o.x;\n" \
	"MAD o.x,v.z,kH.z,o.x;\n" \
	"MAD o.x,w.z,kH2.x,o.x;\n" \
	"MAD o.x,v.y,kH2.y,o.x;\n" \
	"MAD o.x,v.w,kH2.z,o.x;\n" \
	"ADD o.x,o.x,kH.w;\n" \
	"MUL o.x,o.x,cc2.y;\n" \
	"MAX o.y,q.x,0.0000001;\n" \
	"POW o.y,o.y,cc.y;\n" \
	"MUL o.y,o.y,L.z;\n" \
	"MAX o.z,o.x,0.0000001;\n" \
	"RCP o.z,o.z;\n" \
	"MUL o.y,o.y,o.z;\n" \
	"MAX u.x,Jl.x,0.0000001;\n" \
	"RCP u.x,u.x;\n" \
	"MUL u.x,n.x,u.x;\n" \
	"MAX u.x,u.x,0.0000001;\n" \
	"POW u.x,u.x,cc.y;\n" \
	"MUL mc.x,Mc.x,u.x;\n" \
	"MUL mc.x,mc.x,o.z;\n" \
	"SUB s.x,o.y,mc.x;\n" \
	"SUB s.y,o.y,0.001;\n" \
	"MUL s.z,q.y,cc.z;\n" \
	"MUL s.w,q.x,q.x;\n" \
	"ADD s.w,s.w,cc.w;\n" \
	"RSQ s.w,s.w;\n" \
	"RCP s.w,s.w;\n" \
	"MAX u.y,s.w,0.001;\n" \
	"MUL u.z,s.z,s.z;\n" \
	"MAD u.z,u.y,u.y,u.z;\n" \
	"RSQ u.z,u.z;\n" \
	"RCP u.z,u.z;\n" \
	"ADD u.w,s.y,u.z;\n" \
	"ADD e.x,s.y,u.y;\n" \
	"MAX e.x,e.x,0.0000001;\n" \
	"RCP e.x,e.x;\n" \
	"MUL u.w,u.w,e.x;\n" \
	"MUL e.y,u.w,s.x;\n" \
	"SUB e.y,e.y,u.z;\n" \
	"MUL e.z,u.y,u.w;\n" \
	"MUL e.z,e.z,s.x;\n" \
	"MUL e.w,e.y,e.y;\n" \
	"MAD e.w,4.0,e.z,e.w;\n" \
	"MAX e.w,e.w,0.0;\n" \
	"RSQ e.w,e.w;\n" \
	"RCP e.w,e.w;\n" \
	"ADD e.w,e.y,e.w;\n" \
	"MUL e.w,e.w,0.5;\n" \
	"SUB t.x,s.x,s.y;\n" \
	"CMP e.w,t.x,e.w,s.x;\n" \
	"SUB mc.x,o.y,e.w;\n" \
	"MUL s.z,q.x,cc2.x;\n" \
	"MAX u.y,q.y,0.001;\n" \
	"MUL u.z,s.z,s.z;\n" \
	"MAD u.z,u.y,u.y,u.z;\n" \
	"RSQ u.z,u.z;\n" \
	"RCP u.z,u.z;\n" \
	"ADD u.w,o.y,u.z;\n" \
	"ADD e.x,o.y,u.y;\n" \
	"MAX e.x,e.x,0.0000001;\n" \
	"RCP e.x,e.x;\n" \
	"MUL u.w,u.w,e.x;\n" \
	"MUL e.y,u.w,mc.x;\n" \
	"SUB e.y,e.y,u.z;\n" \
	"MUL e.z,u.y,u.w;\n" \
	"MUL e.z,e.z,mc.x;\n" \
	"MUL e.w,e.y,e.y;\n" \
	"MAD e.w,4.0,e.z,e.w;\n" \
	"MAX e.w,e.w,0.0;\n" \
	"RSQ e.w,e.w;\n" \
	"RCP e.w,e.w;\n" \
	"ADD e.w,e.y,e.w;\n" \
	"MUL e.w,e.w,0.5;\n" \
	"SUB t.x,mc.x,o.y;\n" \
	"CMP mc.x,t.x,e.w,mc.x;\n" \
	"MUL mc.x,mc.x,o.x;\n" \
	"SUB t.x,0.0,Mc.x;\n" \
	"ABS t.x,Mc.x;\n" \
	"SUB t.x,0.0000001,t.x;\n" \
	"CMP mc.x,t.x,mc.x,mc.x;\n"

#endif /* !__ACES2_ARB_H__ */
