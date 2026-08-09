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
	"PARAM a2AtanP = { 0.99997726, -0.33262347, 0.19354346, -0.11643287 };\n" \
	"PARAM a2AtanQ = { 0.05265332, -0.01172120, 1.5707963268, 3.1415926536 };\n" \
	"ABS a, src;\n" \
	"MAX b.x, a.x, a.y;\n" \
	"MIN b.y, a.x, a.y;\n" \
	"MAX b.x, b.x, 0.00000001;\n" \
	"RCP b.x, b.x;\n" \
	"MUL z.x, b.y, b.x;\n" \
	"MUL z2.x, z.x, z.x;\n" \
	"MAD r.x, z2.x, a2AtanQ.y, a2AtanQ.x;\n" \
	"MAD r.x, r.x, z2.x, a2AtanP.w;\n" \
	"MAD r.x, r.x, z2.x, a2AtanP.z;\n" \
	"MAD r.x, r.x, z2.x, a2AtanP.y;\n" \
	"MAD r.x, r.x, z2.x, a2AtanP.x;\n" \
	"MUL r.x, r.x, z.x;\n" \
	/* |y| > |x| folds the octant */ \
	"SUB t.x, a.x, a.y;\n" \
	"SUB t.y, a2AtanQ.z, r.x;\n" \
	"CMP r.x, t.x, t.y, r.x;\n" \
	/* x < 0 folds the half plane */ \
	"SUB t.y, a2AtanQ.w, r.x;\n" \
	"CMP r.x, src.x, t.y, r.x;\n" \
	/* y < 0 mirrors it */ \
	"SUB t.y, 0.0, r.x;\n" \
	"CMP r.x, src.y, t.y, r.x;\n"


/*
   Notes from building this, kept because each cost a debugging session
   and none of the mistakes looked like mistakes on screen.

   The stages were written and checked one at a time - tone-mapped J,
   then chroma-compressed M, then the gamut geometry - each assembled on
   a driver, run over 300 colours and compared against the CPU
   reference, each landing at 0.00196, which is half an 8-bit readback
   step rather than the program.

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
   The whole transform: the scene colour in "lin", display RGB in "v",
   unclamped, ready for the composite's existing encode.

   The env registers start at 10 rather than 0.  The composite already
   uses 0 through 9 for its own parameters, and a program that quietly
   overwrote them would misbehave in the bloom and expansion rather than
   in the transform, which is the worst place for it to show.  The input
   colour is not an env register at all here - it is the composite's own
   "lin" temporary, which is what makes this splice in as a curve rather
   than sit beside one.

   Verified end to end: assembled on a driver, 300 colours, against the
   CPU reference with the reference clamped the way a display encode
   clamps.  Worst channel difference 0.00196, the 8-bit readback floor.
   Re-verified unchanged after the renumbering, which is the point of
   checking a mechanical edit rather than trusting it.

   432 ALU, 25 temporaries.  Above ARB's guaranteed 16, so this needs
   the native-limits query at load and a fallback when it fails.
*/

/* The transform's own registers, kept apart from its body.  The
   composite declares its own, and a program that declares a name twice
   is rejected outright - so these are spliced in only for the mode that
   needs them, rather than charged to every other curve's temporary
   budget. */
#define ACES2_ARB_DECLS \
	"PARAM inA=program.env[10];\n" \
	"PARAM inB=program.env[11];\n" \
	"PARAM inC=program.env[12];\n" \
	"PARAM caA=program.env[13];\n" \
	"PARAM caB=program.env[14];\n" \
	"PARAM caC=program.env[15];\n" \
	"PARAM acA=program.env[16];\n" \
	"PARAM acB=program.env[17];\n" \
	"PARAM acC=program.env[18];\n" \
	"PARAM lrA=program.env[19];\n" \
	"PARAM lrB=program.env[20];\n" \
	"PARAM lrC=program.env[21];\n" \
	"PARAM inS=program.env[22];\n" \
	"PARAM limS=program.env[23];\n" \
	"PARAM ts=program.env[24];\n" \
	"PARAM cc=program.env[25];\n" \
	"PARAM cc2=program.env[26];\n" \
	"PARAM gm=program.env[27];\n" \
	"PARAM lsc=program.env[28];\n" \
	"PARAM p1A=program.env[29];\n" \
	"PARAM p1B=program.env[30];\n" \
	"PARAM p1C=program.env[31];\n" \
	"PARAM p0A=program.env[32];\n" \
	"PARAM p0B=program.env[33];\n" \
	"PARAM p0C=program.env[34];\n" \
	"PARAM a2A={0.99997726,-0.33262347,0.19354346,-0.11643287};\n" \
	"PARAM a2B={0.05265332,-0.01172120,1.5707963268,3.1415926536};\n" \
	"PARAM a2C={0.42,27.13,100.0,0.0027777778};\n" \
	"PARAM a2H={11.34072,16.46899,7.88380,77.12896};\n" \
	"PARAM a2H2={14.66441,-6.37224,9.19364,0.0};\n" \
	"PARAM a2E={2.3809523809,57.2957795131,0.5,1.0};\n" \
	"TEMP b,c,f,mc,o,q,w,z,Jl,Mc,L,cu,ab;\n"

/* The transform itself.  Consumes the composite's "lin" and leaves
   finished display RGB in "v".  Declarations are in ACES2_ARB_DECLS
   above; this is instructions only. */
#define ACES2_ARB_TRANSFORM_BODY \
	"DP3 a.x,lin,p1A;\n" \
	"DP3 a.y,lin,p1B;\n" \
	"DP3 a.z,lin,p1C;\n" \
	"MAX a,a,0.0;\n" \
	"MIN a,a,gm.y;\n" \
	"DP3 b.x,a,p0A;\n" \
	"DP3 b.y,a,p0B;\n" \
	"DP3 b.z,a,p0C;\n" \
	"DP3 c.x,b,inA;\n" \
	"DP3 c.y,b,inB;\n" \
	"DP3 c.z,b,inC;\n" \
	"ABS d,c;\n" \
	"POW d.x,d.x,a2C.x;\n" \
	"POW d.y,d.y,a2C.x;\n" \
	"POW d.z,d.z,a2C.x;\n" \
	"ADD e,d,a2C.y;\n" \
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
	"MUL Jl.x,g.x,a2C.z;\n" \
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
	"MAD r.x,z.y,a2B.y,a2B.x;\n" \
	"MAD r.x,r.x,z.y,a2A.w;\n" \
	"MAD r.x,r.x,z.y,a2A.z;\n" \
	"MAD r.x,r.x,z.y,a2A.y;\n" \
	"MAD r.x,r.x,z.y,a2A.x;\n" \
	"MUL r.x,r.x,z.x;\n" \
	"SUB t.x,a.x,a.y;\n" \
	"SUB t.y,a2B.z,r.x;\n" \
	"CMP r.x,t.x,t.y,r.x;\n" \
	"SUB t.y,a2B.w,r.x;\n" \
	"CMP r.x,ab.x,t.y,r.x;\n" \
	"SUB t.y,0.0,r.x;\n" \
	"CMP r.x,ab.y,t.y,r.x;\n" \
	"MUL h.x,r.x,a2E.y;\n" \
	"ADD t.x,h.x,360.0;\n" \
	"CMP h.x,h.x,t.x,h.x;\n" \
	/* texel centre: hue/360 plus the half-texel in gm.z.  The width is \
	   passed in because the packing picks it - a shader that assumed \
	   360 would sample between entries. */ \
	"MUL t.x,h.x,a2C.w;\n" \
	"ADD t.x,t.x,gm.z;\n" \
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
	"MUL t.y,t.y,a2C.y;\n" \
	"MUL t.y,t.y,t.z;\n" \
	"POW t.y,t.y,a2E.x;\n" \
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
	"MUL t.w,t.w,a2C.z;\n" \
	"MUL u.y,t.w,inS.x;\n" \
	"POW u.y,u.y,a2C.x;\n" \
	"ADD u.z,u.y,a2C.y;\n" \
	"RCP u.z,u.z;\n" \
	"MUL u.y,u.y,u.z;\n" \
	"RCP u.z,inS.w;\n" \
	"MUL u.y,u.y,u.z;\n" \
	"POW u.y,u.y,inS.y;\n" \
	"MUL n.x,u.y,a2C.z;\n" \
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
	"MUL o.x,w.y,a2H.x;\n" \
	"MAD o.x,v.x,a2H.y,o.x;\n" \
	"MAD o.x,v.z,a2H.z,o.x;\n" \
	"MAD o.x,w.z,a2H2.x,o.x;\n" \
	"MAD o.x,v.y,a2H2.y,o.x;\n" \
	"MAD o.x,v.w,a2H2.z,o.x;\n" \
	"ADD o.x,o.x,a2H.w;\n" \
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
	"CMP mc.x,t.x,mc.x,mc.x;\n" \
	"MOV cu.x,L.x;\n" \
	"MOV cu.y,L.y;\n" \
	"RCP v.x,cc.x;\n" \
	"MUL v.x,cu.x,v.x;\n" \
	"SUB v.x,1.3,v.x;\n" \
	"MIN v.x,v.x,1.0;\n" \
	"SUB v.y,cc2.z,cu.x;\n" \
	"MAD v.y,v.y,v.x,cu.x;\n" \
	"SUB v.z,cc.x,cu.x;\n" \
	"MAD v.z,v.z,0.3,cu.x;\n" \
	"SUB s.x,cc.x,v.z;\n" \
	"SUB s.y,cc.x,n.x;\n" \
	"MAX s.y,s.y,0.0001;\n" \
	"RCP s.y,s.y;\n" \
	"MUL s.x,s.x,s.y;\n" \
	"MAX s.x,s.x,0.0000001;\n" \
	"LG2 s.x,s.x;\n" \
	"MUL s.x,s.x,0.3010299957;\n" \
	"MUL s.x,s.x,s.x;\n" \
	"ADD s.x,s.x,1.0;\n" \
	"MUL s.z,cc.x,cc2.w;\n" \
	"MUL s.w,s.z,s.x;\n" \
	"SUB t.x,n.x,v.z;\n" \
	"CMP s.w,t.x,s.z,s.w;\n" \
	"RCP u.x,s.w;\n" \
	"MUL u.y,mc.x,u.x;\n" \
	"RCP u.z,v.y;\n" \
	"MUL u.z,u.y,u.z;\n" \
	"SUB e.x,1.0,u.y;\n" \
	"SUB e.y,0.0,n.x;\n" \
	"MUL e.z,e.x,e.x;\n" \
	"MUL e.w,u.z,e.y;\n" \
	"MAD e.z,-4.0,e.w,e.z;\n" \
	"MAX e.z,e.z,0.0;\n" \
	"RSQ e.z,e.z;\n" \
	"RCP e.z,e.z;\n" \
	"ADD e.w,e.x,e.z;\n" \
	"MAX e.w,e.w,0.0000001;\n" \
	"RCP e.w,e.w;\n" \
	"MUL e.w,e.w,e.y;\n" \
	"MUL q.z,e.w,-2.0;\n" \
	"MUL w.x,cc.x,u.z;\n" \
	"ADD w.x,w.x,u.y;\n" \
	"ADD w.x,w.x,1.0;\n" \
	"SUB w.x,0.0,w.x;\n" \
	"MUL w.y,cc.x,u.y;\n" \
	"ADD w.y,w.y,n.x;\n" \
	"MUL w.z,w.x,w.x;\n" \
	"MUL w.w,u.z,w.y;\n" \
	"MAD w.z,-4.0,w.w,w.z;\n" \
	"MAX w.z,w.z,0.0;\n" \
	"RSQ w.z,w.z;\n" \
	"RCP w.z,w.z;\n" \
	"SUB w.w,w.x,w.z;\n" \
	"RCP w.w,w.w;\n" \
	"MUL w.w,w.w,w.y;\n" \
	"MUL w.w,w.w,-2.0;\n" \
	"SUB t.x,n.x,v.y;\n" \
	"CMP q.z,t.x,q.z,w.w;\n" \
	"SUB t.y,cc.x,q.z;\n" \
	"SUB t.z,q.z,v.y;\n" \
	"CMP t.w,t.z,q.z,t.y;\n" \
	"MUL t.w,t.w,t.z;\n" \
	"RCP t.x,v.y;\n" \
	"MUL t.w,t.w,t.x;\n" \
	"RCP t.x,s.w;\n" \
	"MUL t.w,t.w,t.x;\n" \
	"MUL u.y,cu.y,u.x;\n" \
	"MUL u.z,u.y,e.x;\n" \
	"RCP z.x,v.y;\n" \
	"MUL u.z,u.y,z.x;\n" \
	"SUB e.x,1.0,u.y;\n" \
	"SUB e.y,0.0,cu.x;\n" \
	"MUL e.z,e.x,e.x;\n" \
	"MUL e.w,u.z,e.y;\n" \
	"MAD e.z,-4.0,e.w,e.z;\n" \
	"MAX e.z,e.z,0.0;\n" \
	"RSQ e.z,e.z;\n" \
	"RCP e.z,e.z;\n" \
	"ADD e.w,e.x,e.z;\n" \
	"MAX e.w,e.w,0.0000001;\n" \
	"RCP e.w,e.w;\n" \
	"MUL e.w,e.w,e.y;\n" \
	"MUL z.y,e.w,-2.0;\n" \
	"MUL w.x,cc.x,u.z;\n" \
	"ADD w.x,w.x,u.y;\n" \
	"ADD w.x,w.x,1.0;\n" \
	"SUB w.x,0.0,w.x;\n" \
	"MUL w.y,cc.x,u.y;\n" \
	"ADD w.y,w.y,cu.x;\n" \
	"MUL w.z,w.x,w.x;\n" \
	"MUL w.w,u.z,w.y;\n" \
	"MAD w.z,-4.0,w.w,w.z;\n" \
	"MAX w.z,w.z,0.0;\n" \
	"RSQ w.z,w.z;\n" \
	"RCP w.z,w.z;\n" \
	"SUB w.w,w.x,w.z;\n" \
	"RCP w.w,w.w;\n" \
	"MUL w.w,w.w,w.y;\n" \
	"MUL w.w,w.w,-2.0;\n" \
	"SUB t.x,cu.x,v.y;\n" \
	"CMP z.y,t.x,z.y,w.w;\n" \
	"MAX z.z,z.y,0.0000001;\n" \
	"RCP z.z,z.z;\n" \
	"MUL z.z,q.z,z.z;\n" \
	"MAX z.z,z.z,0.0000001;\n" \
	"POW z.z,z.z,gm.x;\n" \
	"MUL z.z,z.z,z.y;\n" \
	"MUL s.x,t.w,cu.y;\n" \
	"SUB s.x,cu.x,s.x;\n" \
	"MAX s.x,s.x,0.0000001;\n" \
	"RCP s.x,s.x;\n" \
	"MUL z.z,z.z,cu.y;\n" \
	"MUL z.z,z.z,s.x;\n" \
	"SUB s.y,cc.x,z.y;\n" \
	"SUB s.z,cc.x,q.z;\n" \
	"SUB s.w,cc.x,cu.x;\n" \
	"MAX z.w,s.y,0.0000001;\n" \
	"RCP z.w,z.w;\n" \
	"MUL z.w,s.z,z.w;\n" \
	"MAX z.w,z.w,0.0000001;\n" \
	"POW z.w,z.w,L.w;\n" \
	"MUL z.w,z.w,s.y;\n" \
	"MUL v.w,t.w,cu.y;\n" \
	"ADD v.w,s.w,v.w;\n" \
	"MAX v.w,v.w,0.0000001;\n" \
	"RCP v.w,v.w;\n" \
	"MUL z.w,z.w,cu.y;\n" \
	"MUL z.w,z.w,v.w;\n" \
	"MUL q.w,cu.y,0.12;\n" \
	"SUB c.x,z.z,z.w;\n" \
	"ABS c.x,c.x;\n" \
	"SUB c.x,q.w,c.x;\n" \
	"MAX c.x,c.x,0.0;\n" \
	"MAX c.y,q.w,0.0000001;\n" \
	"RCP c.y,c.y;\n" \
	"MUL c.x,c.x,c.y;\n" \
	"MUL c.z,c.x,c.x;\n" \
	"MUL c.z,c.z,c.x;\n" \
	"MUL c.z,c.z,q.w;\n" \
	"MUL c.z,c.z,0.1666666667;\n" \
	"MIN c.w,z.z,z.w;\n" \
	"SUB c.w,c.w,c.z;\n" \
	"MAX d.x,q.z,0.0000001;\n" \
	"RCP d.y,cc.x;\n" \
	"MUL d.x,d.x,d.y;\n" \
	"MAX d.x,d.x,0.0000001;\n" \
	"POW d.x,d.x,cc.y;\n" \
	"MUL d.x,d.x,cc.x;\n" \
	"MUL d.z,t.w,L.z;\n" \
	"SUB d.z,cc.x,d.z;\n" \
	"MAX d.z,d.z,0.0000001;\n" \
	"RCP d.z,d.z;\n" \
	"MUL d.x,d.x,L.z;\n" \
	"MUL d.x,d.x,d.z;\n" \
	"MAX d.w,d.x,0.0000001;\n" \
	"RCP d.w,d.w;\n" \
	"MUL d.w,c.w,d.w;\n" \
	"MAX d.w,d.w,0.75;\n" \
	"MUL f.x,d.w,c.w;\n" \
	"SUB f.y,mc.x,f.x;\n" \
	"SUB f.z,c.w,f.x;\n" \
	"SUB f.w,d.x,f.x;\n" \
	"MAX g.x,f.z,0.0000001;\n" \
	"RCP g.x,g.x;\n" \
	"MUL g.x,f.w,g.x;\n" \
	"SUB g.x,g.x,1.0;\n" \
	"MAX g.y,g.x,0.0000001;\n" \
	"RCP g.y,g.y;\n" \
	"MUL g.y,f.w,g.y;\n" \
	"MAX g.z,g.y,0.0000001;\n" \
	"RCP g.z,g.z;\n" \
	"MUL g.z,f.y,g.z;\n" \
	"ADD g.w,g.z,1.0;\n" \
	"MAX g.w,g.w,0.0000001;\n" \
	"RCP g.w,g.w;\n" \
	"MUL g.w,g.z,g.w;\n" \
	"MUL g.w,g.w,g.y;\n" \
	"ADD g.w,g.w,f.x;\n" \
	"SUB t.x,mc.x,f.x;\n" \
	"CMP g.w,t.x,mc.x,g.w;\n" \
	"SUB t.x,d.w,1.0;\n" \
	"CMP g.w,t.x,g.w,mc.x;\n" \
	"MAD h.y,g.w,t.w,q.z;\n" \
	"CMP h.y,n.x,0.0,h.y;\n" \
	"CMP g.w,n.x,0.0,g.w;\n" \
	"SUB t.x,cc.x,n.x;\n" \
	"CMP g.w,t.x,0.0,g.w;\n" \
	"MUL c.x,h.y,0.01;\n" \
	"MAX c.x,c.x,0.0000001;\n" \
	"POW c.x,c.x,limS.z;\n" \
	"MUL c.y,r.x,1.0;\n" \
	"COS c.z,c.y;\n" \
	"SIN c.w,c.y;\n" \
	"MUL d.x,g.w,c.z;\n" \
	"MUL d.y,g.w,c.w;\n" \
	"MOV f.x,c.x;\n" \
	"MOV f.y,d.x;\n" \
	"MOV f.z,d.y;\n" \
	"DP3 g.x,f,acA;\n" \
	"DP3 g.y,f,acB;\n" \
	"DP3 g.z,f,acC;\n" \
	"ABS mc.x,g.x;\n" \
	"ABS mc.y,g.y;\n" \
	"ABS mc.z,g.z;\n" \
	"MIN mc,mc,0.99;\n" \
	"SUB n.y,1.0,mc.x;\n" \
	"SUB n.z,1.0,mc.y;\n" \
	"SUB n.w,1.0,mc.z;\n" \
	"MAX n.y,n.y,0.000001;\n" \
	"MAX n.z,n.z,0.000001;\n" \
	"MAX n.w,n.w,0.000001;\n" \
	"RCP n.y,n.y;\n" \
	"RCP n.z,n.z;\n" \
	"RCP n.w,n.w;\n" \
	"MUL mc.x,mc.x,a2C.y;\n" \
	"MUL mc.x,mc.x,n.y;\n" \
	"MUL mc.y,mc.y,a2C.y;\n" \
	"MUL mc.y,mc.y,n.z;\n" \
	"MUL mc.z,mc.z,a2C.y;\n" \
	"MUL mc.z,mc.z,n.w;\n" \
	"POW mc.x,mc.x,a2E.x;\n" \
	"POW mc.y,mc.y,a2E.x;\n" \
	"POW mc.z,mc.z,a2E.x;\n" \
	"SUB o.x,0.0,mc.x;\n" \
	"CMP mc.x,g.x,o.x,mc.x;\n" \
	"SUB o.x,0.0,mc.y;\n" \
	"CMP mc.y,g.y,o.x,mc.y;\n" \
	"SUB o.x,0.0,mc.z;\n" \
	"CMP mc.z,g.z,o.x,mc.z;\n" \
	"DP3 v.x,mc,lrA;\n" \
	"DP3 v.y,mc,lrB;\n" \
	"DP3 v.z,mc,lrC;\n"

#endif /* !__ACES2_ARB_H__ *//*
   Assembled inside the real composite rather than alone: 511 ALU and 29
   temporaries, under this driver's native limits.  Two collisions had to
   be resolved to get there and neither is subtle once seen.

   The composite already defines a PARAM called kE - Euler's number, for
   the GT curve - and this file had its own kE holding something else
   entirely.  ARB rejects a duplicate name outright, which is the good
   case; the bad case would have been silent reuse.  Everything here now
   carries an a2 prefix.

   The temporaries overlapped too.  The composite declares sixteen and
   this declared twenty-four with most of the single letters in common,
   so only the thirteen it does not already have are declared now.  That
   is also why the declarations sit in their own macro: they are spliced
   in for this mode only, rather than charging every other curve for
   registers it will never touch.
*/


