/*
===========================================================================
Point-cull kernels for idSIMD_Generic.

TracePointCull/DecalPointCull/OverlayPointCull evaluate a handful of plane
distances per vertex and reduce each to a sign bit. They are called once per
surface per light/decal/overlay, so they run over every visible vertex every
frame, and they were the slowest of the culling kernels in profiling.

GCC will not autovectorize them as written: idDrawVert is a 60-byte AoS
struct, so xyz is strided, and -fopt-info-vec-missed reports "no vectype for
stmt" on the idPlane member loads in idPlane::Distance. Hoisting the plane
coefficients into scalars does not help on its own (measured 1.02x) - the
stride is the real blocker. These kernels therefore do the AoS->SoA transpose
explicitly, four vertices at a time.

Bit-exactness with the scalar path is a hard requirement, since the output
selects which side of a plane a vertex falls on and feeds shadow volume and
decal generation:

 - the dot product keeps the scalar operand order, ((a*x + b*y) + c*z) + d,
   matching idPlane::Distance. Do not reassociate;
 - the sign test is a movemask/shift of the same float that the scalar code
   tests with FLOATSIGNBITSET, so it is exact by construction rather than by
   tolerance - including at d == 0 and for -0.0f;
 - the tail below a multiple of four runs the original scalar code verbatim.

Dispatch is at compile time, matching sound/snd_mix_kernels.h: SSE2 on x86
(baseline on x86_64, and forced on 32-bit x86 by the Makefile), NEON on ARM,
scalar C everywhere else. Compile-time dispatch keeps a given binary
deterministic - the same path runs on every machine that binary targets.
Define SIMD_POINTCULL_NO_SIMD to force the scalar path.
===========================================================================
*/

#ifndef __SIMD_POINTCULL_H__
#define __SIMD_POINTCULL_H__

#if !defined(SIMD_POINTCULL_NO_SIMD)
#  if defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#    define SIMD_POINTCULL_SSE2 1
#    include <emmintrin.h>
#  elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#    define SIMD_POINTCULL_NEON 1
#    include <arm_neon.h>
#  endif
#endif

/*
   Load the xyz of four consecutive idDrawVert and transpose to SoA.
   idDrawVert is 60 bytes, so a 4-float load at &xyz.x reads xyz plus the
   first st component; the transpose discards that fourth lane. The load is
   unaligned because the stride is not a multiple of 16.
*/
#if defined(SIMD_POINTCULL_SSE2)

#define SIMD_POINTCULL_LOAD4( verts, i, X, Y, Z ) \
	{ \
		__m128 r0 = _mm_loadu_ps( (const float *)&(verts)[(i)+0].xyz ); \
		__m128 r1 = _mm_loadu_ps( (const float *)&(verts)[(i)+1].xyz ); \
		__m128 r2 = _mm_loadu_ps( (const float *)&(verts)[(i)+2].xyz ); \
		__m128 r3 = _mm_loadu_ps( (const float *)&(verts)[(i)+3].xyz ); \
		__m128 t0 = _mm_unpacklo_ps( r0, r1 ); \
		__m128 t1 = _mm_unpacklo_ps( r2, r3 ); \
		__m128 t2 = _mm_unpackhi_ps( r0, r1 ); \
		__m128 t3 = _mm_unpackhi_ps( r2, r3 ); \
		(X) = _mm_movelh_ps( t0, t1 ); \
		(Y) = _mm_movehl_ps( t1, t0 ); \
		(Z) = _mm_movelh_ps( t2, t3 ); \
	}

/* ((a*x + b*y) + c*z) + d - scalar operand order, do not reassociate */
#define SIMD_POINTCULL_DIST( pa, pb, pc, pd, X, Y, Z ) \
	_mm_add_ps( _mm_add_ps( _mm_add_ps( \
		_mm_mul_ps( (pa), (X) ), _mm_mul_ps( (pb), (Y) ) ), \
		_mm_mul_ps( (pc), (Z) ) ), (pd) )

typedef __m128 simdPointCull_t;
#define SIMD_POINTCULL_SPLAT( f )		_mm_set1_ps( f )
#define SIMD_POINTCULL_SIGNMASK( v )	_mm_movemask_ps( v )
#define SIMD_POINTCULL_SUB( a, b )		_mm_sub_ps( (a), (b) )
#define SIMD_POINTCULL_ADD( a, b )		_mm_add_ps( (a), (b) )
#define SIMD_POINTCULL_STORE( p, v )	_mm_storeu_ps( (p), (v) )

#elif defined(SIMD_POINTCULL_NEON)

#define SIMD_POINTCULL_LOAD4( verts, i, X, Y, Z ) \
	{ \
		float32x4_t r0 = vld1q_f32( (const float *)&(verts)[(i)+0].xyz ); \
		float32x4_t r1 = vld1q_f32( (const float *)&(verts)[(i)+1].xyz ); \
		float32x4_t r2 = vld1q_f32( (const float *)&(verts)[(i)+2].xyz ); \
		float32x4_t r3 = vld1q_f32( (const float *)&(verts)[(i)+3].xyz ); \
		float32x4x2_t a = vtrnq_f32( r0, r1 ); \
		float32x4x2_t b = vtrnq_f32( r2, r3 ); \
		(X) = vcombine_f32( vget_low_f32( a.val[0] ), vget_low_f32( b.val[0] ) ); \
		(Y) = vcombine_f32( vget_low_f32( a.val[1] ), vget_low_f32( b.val[1] ) ); \
		(Z) = vcombine_f32( vget_high_f32( a.val[0] ), vget_high_f32( b.val[0] ) ); \
	}

/*
   Deliberately vmulq/vaddq rather than vmlaq: a fused multiply-add would
   keep the product at full precision and stop matching the scalar path.
   -ffp-contract=off (see the Makefile) stops the compiler re-fusing these.
*/
#define SIMD_POINTCULL_DIST( pa, pb, pc, pd, X, Y, Z ) \
	vaddq_f32( vaddq_f32( vaddq_f32( \
		vmulq_f32( (pa), (X) ), vmulq_f32( (pb), (Y) ) ), \
		vmulq_f32( (pc), (Z) ) ), (pd) )

typedef float32x4_t simdPointCull_t;
#define SIMD_POINTCULL_SPLAT( f )		vdupq_n_f32( f )
#define SIMD_POINTCULL_SUB( a, b )		vsubq_f32( (a), (b) )
#define SIMD_POINTCULL_ADD( a, b )		vaddq_f32( (a), (b) )
#define SIMD_POINTCULL_STORE( p, v )	vst1q_f32( (p), (v) )

/*
   NEON has no movemask. Shift each lane's sign bit down to bit 0, weight the
   lanes 1/2/4/8 and horizontally add, giving the same 4-bit mask layout
   _mm_movemask_ps produces.
*/
ID_INLINE int SIMD_POINTCULL_SIGNMASK( float32x4_t v ) {
	static const uint32_t weights[4] = { 1, 2, 4, 8 };
	uint32x4_t sign = vshrq_n_u32( vreinterpretq_u32_f32( v ), 31 );
	uint32x4_t w    = vmulq_u32( sign, vld1q_u32( weights ) );
#if defined(__aarch64__) || defined(_M_ARM64)
	return (int)vaddvq_u32( w );
#else
	uint32x2_t s = vadd_u32( vget_low_u32( w ), vget_high_u32( w ) );
	return (int)( vget_lane_u32( s, 0 ) + vget_lane_u32( s, 1 ) );
#endif
}

#endif

/*
   Helpers shared by DeriveTriPlanes.

   SIMD_TRIPLANE_RSQRT is a lane-wise transcription of idMath::RSqrt - the
   Quake inverse square root, one Newton step. It is only integer and float
   arithmetic, so it reproduces the scalar result exactly, including for
   x == 0, x == -0 and x < 0 (verified bit-identical over the float exponent
   range and on those special cases). Do not substitute rsqrtps/vrsqrteq:
   those are hardware approximations with different, and architecture
   dependent, results.

   SIMD_TRIPLANE_NEGATE must be an exclusive-or of the sign bit rather than a
   subtract from zero. idPlane::FitThroughPoint computes d = -( Normal() * p ),
   a unary negation, and 0.0f - 0.0f is +0.0f whereas -( 0.0f ) is -0.0f. A
   subtract-from-zero here silently turns every -0.0f plane distance into
   +0.0f, which is a bit difference that no visual test would catch.
*/
#if defined(SIMD_POINTCULL_SSE2)

ID_INLINE __m128 SIMD_TRIPLANE_RSQRT( __m128 x ) {
	__m128  y = _mm_mul_ps( x, _mm_set1_ps( 0.5f ) );
	__m128i i = _mm_sub_epi32( _mm_set1_epi32( 0x5f3759df ),
	                           _mm_srai_epi32( _mm_castps_si128( x ), 1 ) );
	__m128  r = _mm_castsi128_ps( i );
	return _mm_mul_ps( r, _mm_sub_ps( _mm_set1_ps( 1.5f ),
	                                  _mm_mul_ps( _mm_mul_ps( r, r ), y ) ) );
}

ID_INLINE __m128 SIMD_TRIPLANE_NEGATE( __m128 v ) {
	return _mm_xor_ps( v, _mm_castsi128_ps( _mm_set1_epi32( (int)0x80000000 ) ) );
}

#define SIMD_TRIPLANE_MUL( a, b )	_mm_mul_ps( (a), (b) )
#define SIMD_TRIPLANE_SUB( a, b )	_mm_sub_ps( (a), (b) )
#define SIMD_TRIPLANE_ADD( a, b )	_mm_add_ps( (a), (b) )
#define SIMD_TRIPLANE_LOAD( p )		_mm_loadu_ps( p )
#define SIMD_TRIPLANE_STORE( p, v )	_mm_storeu_ps( (p), (v) )
#define SIMD_TRIPLANE_SPLAT( f )	_mm_set1_ps( f )
typedef __m128 simdTriPlane_t;

/*
   Shadow cache helpers. A 4-float load at &xyz reads xyz plus st[0], so the
   fourth lane must be masked off before the w component is written - the
   masks below do that. No arithmetic is involved in
   CreateVertexProgramShadowCache, so it is exact by inspection.
*/
#define SIMD_SHADOW_MASKXYZ()	_mm_castsi128_ps( _mm_setr_epi32( -1, -1, -1, 0 ) )
#define SIMD_SHADOW_W1()		_mm_setr_ps( 0.0f, 0.0f, 0.0f, 1.0f )
#define SIMD_SHADOW_AND( a, b )	_mm_and_ps( (a), (b) )
#define SIMD_SHADOW_OR( a, b )	_mm_or_ps( (a), (b) )

#elif defined(SIMD_POINTCULL_NEON)

ID_INLINE float32x4_t SIMD_TRIPLANE_RSQRT( float32x4_t x ) {
	float32x4_t y = vmulq_f32( x, vdupq_n_f32( 0.5f ) );
	int32x4_t   i = vsubq_s32( vdupq_n_s32( 0x5f3759df ),
	                           vshrq_n_s32( vreinterpretq_s32_f32( x ), 1 ) );
	float32x4_t r = vreinterpretq_f32_s32( i );
	return vmulq_f32( r, vsubq_f32( vdupq_n_f32( 1.5f ),
	                                vmulq_f32( vmulq_f32( r, r ), y ) ) );
}

ID_INLINE float32x4_t SIMD_TRIPLANE_NEGATE( float32x4_t v ) {
	return vreinterpretq_f32_u32( veorq_u32( vreinterpretq_u32_f32( v ),
	                                         vdupq_n_u32( 0x80000000u ) ) );
}

#define SIMD_TRIPLANE_MUL( a, b )	vmulq_f32( (a), (b) )
#define SIMD_TRIPLANE_SUB( a, b )	vsubq_f32( (a), (b) )
#define SIMD_TRIPLANE_ADD( a, b )	vaddq_f32( (a), (b) )
#define SIMD_TRIPLANE_LOAD( p )		vld1q_f32( p )
#define SIMD_TRIPLANE_STORE( p, v )	vst1q_f32( (p), (v) )
#define SIMD_TRIPLANE_SPLAT( f )	vdupq_n_f32( f )
typedef float32x4_t simdTriPlane_t;

ID_INLINE float32x4_t SIMD_SHADOW_MASKXYZ( void ) {
	static const uint32_t m[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u };
	return vreinterpretq_f32_u32( vld1q_u32( m ) );
}
ID_INLINE float32x4_t SIMD_SHADOW_W1( void ) {
	static const float w[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	return vld1q_f32( w );
}
#define SIMD_SHADOW_AND( a, b )	vreinterpretq_f32_u32( vandq_u32( \
		vreinterpretq_u32_f32( a ), vreinterpretq_u32_f32( b ) ) )
#define SIMD_SHADOW_OR( a, b )	vreinterpretq_f32_u32( vorrq_u32( \
		vreinterpretq_u32_f32( a ), vreinterpretq_u32_f32( b ) ) )

#endif

/*
   ( ax*bx + ay*by ) + az*bz - the operand order idVec3::operator* uses.
   Shared by both instruction sets; do not reassociate.
*/
#if defined(SIMD_POINTCULL_SSE2) || defined(SIMD_POINTCULL_NEON)
#define SIMD_TRIPLANE_DOT3( ax, ay, az, bx, by, bz ) \
	SIMD_TRIPLANE_ADD( SIMD_TRIPLANE_ADD( \
		SIMD_TRIPLANE_MUL( (ax), SIMD_TRIPLANE_LOAD( bx ) ), \
		SIMD_TRIPLANE_MUL( (ay), SIMD_TRIPLANE_LOAD( by ) ) ), \
		SIMD_TRIPLANE_MUL( (az), SIMD_TRIPLANE_LOAD( bz ) ) )

#endif

#endif /* !__SIMD_POINTCULL_H__ */
