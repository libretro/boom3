#include <cstddef>
/*
===========================================================================
Audio mix kernels for the libretro core.

Replaces the idSIMD Mix* sound kernels:
 - the old SSE versions were MSVC 32-bit __asm and therefore dead code on
   every libretro target (GCC/Clang, and all 64-bit builds) - every build
   was silently running the scalar generic path;
 - the old generic versions hardcoded the gain-ramp divisor and asserted
   numSamples == MIXBUFFER_SAMPLES (4096), which is incompatible with the
   per-retro_run variable-size mixing this core does now.

These kernels:
 - take a true numFrames (any count >= 0; SIMD paths vectorize the bulk
   and fall through to scalar for the tail, so no alignment or multiple
   requirements are imposed on callers);
 - ramp per-speaker gains linearly from lastV[] to currentV[] across the
   block, exactly like the originals (divisor is numFrames, as it must be);
 - mix in float at int16 full scale and convert to int16 only in
   Snd_MixedSoundToSamples, with clamp-then-truncate semantics identical
   to the old generic code (<= -32768 -> -32768, >= 32767 -> 32767,
   otherwise C float->short truncation toward zero);
 - dispatch at compile time: SSE2 on any x86 (baseline on x86_64,
   required on 32-bit x86 - every CPU since 2003 has it; build with
   SND_MIX_NO_SIMD to force scalar), NEON on ARM (__ARM_NEON, mandatory
   on aarch64), scalar C everywhere else. Compile-time dispatch keeps a
   given binary bit-deterministic: the same path runs on every machine
   that binary targets.

Note on cross-path determinism: the SIMD ramps are computed as
gain = start + i*inc per lane while the scalar ramp accumulates
gain += inc serially; the results can differ in the last ulp. Within one
binary only one path exists, so libretro determinism (same binary, same
inputs, same output) holds; bit-identity *across architectures* was never
possible for a float mixer and is not a goal.
===========================================================================
*/

#ifndef __SND_MIX_KERNELS_H__
#define __SND_MIX_KERNELS_H__

#if !defined(SND_MIX_NO_SIMD)
#  if defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#    define SND_MIX_SSE2 1
#    include <emmintrin.h>
#  elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#    define SND_MIX_NEON 1
#    include <arm_neon.h>
#  endif
#endif

/*
====================
Snd_MixTwoSpeakerMono

dest: interleaved stereo accumulation buffer, numFrames*2 floats
src:  mono samples, numFrames floats
Ramps lastV -> currentV linearly over the block.
====================
*/
static inline void Snd_MixTwoSpeakerMono( float *dest, const float *src, int numFrames,
                                          const float lastV[2], const float currentV[2] ) {
	if ( numFrames <= 0 ) {
		return;
	}
	const float incL = ( currentV[0] - lastV[0] ) / numFrames;
	const float incR = ( currentV[1] - lastV[1] ) / numFrames;
	float sL = lastV[0];
	float sR = lastV[1];
	int i = 0;

#if SND_MIX_SSE2
	// gains for 4 consecutive frames: s + inc*{0,1,2,3}; step both by 4*inc
	__m128 idx    = _mm_setr_ps( 0.0f, 1.0f, 2.0f, 3.0f );
	__m128 gL     = _mm_add_ps( _mm_set1_ps( sL ), _mm_mul_ps( _mm_set1_ps( incL ), idx ) );
	__m128 gR     = _mm_add_ps( _mm_set1_ps( sR ), _mm_mul_ps( _mm_set1_ps( incR ), idx ) );
	const __m128 stepL = _mm_set1_ps( incL * 4.0f );
	const __m128 stepR = _mm_set1_ps( incR * 4.0f );

	for ( ; i + 4 <= numFrames; i += 4 ) {
		__m128 s  = _mm_loadu_ps( src + i );
		__m128 pL = _mm_mul_ps( s, gL );          // L0 L1 L2 L3
		__m128 pR = _mm_mul_ps( s, gR );          // R0 R1 R2 R3
		__m128 lo = _mm_unpacklo_ps( pL, pR );    // L0 R0 L1 R1
		__m128 hi = _mm_unpackhi_ps( pL, pR );    // L2 R2 L3 R3
		_mm_storeu_ps( dest + i*2,     _mm_add_ps( _mm_loadu_ps( dest + i*2 ),     lo ) );
		_mm_storeu_ps( dest + i*2 + 4, _mm_add_ps( _mm_loadu_ps( dest + i*2 + 4 ), hi ) );
		gL = _mm_add_ps( gL, stepL );
		gR = _mm_add_ps( gR, stepR );
	}
	sL = lastV[0] + incL * i;
	sR = lastV[1] + incR * i;
#elif SND_MIX_NEON
	static const float idxv[4] = { 0.0f, 1.0f, 2.0f, 3.0f };
	float32x4_t idx   = vld1q_f32( idxv );
	float32x4_t gL    = vmlaq_n_f32( vdupq_n_f32( sL ), idx, incL );
	float32x4_t gR    = vmlaq_n_f32( vdupq_n_f32( sR ), idx, incR );
	const float32x4_t stepL = vdupq_n_f32( incL * 4.0f );
	const float32x4_t stepR = vdupq_n_f32( incR * 4.0f );

	for ( ; i + 4 <= numFrames; i += 4 ) {
		float32x4_t s  = vld1q_f32( src + i );
		float32x4_t pL = vmulq_f32( s, gL );
		float32x4_t pR = vmulq_f32( s, gR );
		float32x4x2_t z = vzipq_f32( pL, pR );    // z.val[0]=L0R0L1R1 z.val[1]=L2R2L3R3
		vst1q_f32( dest + i*2,     vaddq_f32( vld1q_f32( dest + i*2 ),     z.val[0] ) );
		vst1q_f32( dest + i*2 + 4, vaddq_f32( vld1q_f32( dest + i*2 + 4 ), z.val[1] ) );
		gL = vaddq_f32( gL, stepL );
		gR = vaddq_f32( gR, stepR );
	}
	sL = lastV[0] + incL * i;
	sR = lastV[1] + incR * i;
#endif

	{
		float *d = dest + (size_t)i*2;
		const float *sp = src + i, *se = src + numFrames;
		for ( ; sp < se; sp++, d += 2 ) {
			d[0] += *sp * sL;
			d[1] += *sp * sR;
			sL += incL;
			sR += incR;
		}
	}
}

/*
====================
Snd_MixTwoSpeakerStereo

dest: interleaved stereo accumulation buffer, numFrames*2 floats
src:  interleaved stereo samples, numFrames*2 floats
====================
*/
static inline void Snd_MixTwoSpeakerStereo( float *dest, const float *src, int numFrames,
                                            const float lastV[2], const float currentV[2] ) {
	if ( numFrames <= 0 ) {
		return;
	}
	const float incL = ( currentV[0] - lastV[0] ) / numFrames;
	const float incR = ( currentV[1] - lastV[1] ) / numFrames;
	float sL = lastV[0];
	float sR = lastV[1];
	int i = 0;

#if SND_MIX_SSE2
	// interleaved gain vector for 2 frames: [L0 R0 L1 R1], step 2 frames at a time
	__m128 g    = _mm_setr_ps( sL, sR, sL + incL, sR + incR );
	const __m128 step = _mm_setr_ps( incL * 2.0f, incR * 2.0f, incL * 2.0f, incR * 2.0f );

	for ( ; i + 2 <= numFrames; i += 2 ) {
		__m128 s = _mm_loadu_ps( src + i*2 );     // L0 R0 L1 R1
		_mm_storeu_ps( dest + i*2, _mm_add_ps( _mm_loadu_ps( dest + i*2 ), _mm_mul_ps( s, g ) ) );
		g = _mm_add_ps( g, step );
	}
	sL = lastV[0] + incL * i;
	sR = lastV[1] + incR * i;
#elif SND_MIX_NEON
	const float g0[4]   = { sL, sR, sL + incL, sR + incR };
	const float st[4]   = { incL * 2.0f, incR * 2.0f, incL * 2.0f, incR * 2.0f };
	float32x4_t g    = vld1q_f32( g0 );
	const float32x4_t step = vld1q_f32( st );

	for ( ; i + 2 <= numFrames; i += 2 ) {
		float32x4_t s = vld1q_f32( src + i*2 );
		vst1q_f32( dest + i*2, vmlaq_f32( vld1q_f32( dest + i*2 ), s, g ) );
		g = vaddq_f32( g, step );
	}
	sL = lastV[0] + incL * i;
	sR = lastV[1] + incR * i;
#endif

	{
		float *d = dest + (size_t)i*2;
		const float *sp = src + (size_t)i*2, *se = src + (size_t)numFrames*2;
		for ( ; sp < se; sp += 2, d += 2 ) {
			d[0] += sp[0] * sL;
			d[1] += sp[1] * sR;
			sL += incL;
			sR += incR;
		}
	}
}

/*
====================
Snd_MixedSoundToSamples

Clamp-and-truncate float mix buffer (int16 scale) to int16 output.
Semantics identical to the old idSIMD_Generic::MixedSoundToSamples:
<= -32768 saturates low, >= 32767 saturates high, otherwise C truncation
toward zero. numSamples counts individual samples (frames * channels).
====================
*/
static inline void Snd_MixedSoundToSamples( short *out, const float *in, int numSamples ) {
	int i = 0;

#if SND_MIX_SSE2
	const __m128 lo = _mm_set1_ps( -32768.0f );
	const __m128 hi = _mm_set1_ps(  32767.0f );
	for ( ; i + 8 <= numSamples; i += 8 ) {
		__m128 a = _mm_loadu_ps( in + i );
		__m128 b = _mm_loadu_ps( in + i + 4 );
		a = _mm_min_ps( _mm_max_ps( a, lo ), hi );
		b = _mm_min_ps( _mm_max_ps( b, lo ), hi );
		// cvtt = truncate toward zero, matching the old (short) cast;
		// inputs are pre-clamped so the INT_MIN indefinite case can't occur
		__m128i ia = _mm_cvttps_epi32( a );
		__m128i ib = _mm_cvttps_epi32( b );
		_mm_storeu_si128( (__m128i *)( out + i ), _mm_packs_epi32( ia, ib ) );
	}
#elif SND_MIX_NEON
	const float32x4_t lo = vdupq_n_f32( -32768.0f );
	const float32x4_t hi = vdupq_n_f32(  32767.0f );
	for ( ; i + 8 <= numSamples; i += 8 ) {
		float32x4_t a = vld1q_f32( in + i );
		float32x4_t b = vld1q_f32( in + i + 4 );
		a = vminq_f32( vmaxq_f32( a, lo ), hi );
		b = vminq_f32( vmaxq_f32( b, lo ), hi );
		int32x4_t ia = vcvtq_s32_f32( a );        // truncates toward zero
		int32x4_t ib = vcvtq_s32_f32( b );
		int16x8_t p  = vcombine_s16( vqmovn_s32( ia ), vqmovn_s32( ib ) );
		vst1q_s16( out + i, p );
	}
#endif

	{
		const float *ip = in + i, *ie = in + numSamples;
		short *op = out + i;
		for ( ; ip < ie; ip++, op++ ) {
			if ( *ip <= -32768.0f ) {
				*op = -32768;
			} else if ( *ip >= 32767.0f ) {
				*op = 32767;
			} else {
				*op = (short)*ip;
			}
		}
	}
}


/*
===========================================================================
Integer (all-s16) mixer kernels - used when the frontend does not support
RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT.

Design for bit-exact determinism across compilers AND architectures:
 - samples are int16, per-speaker gains are Q15 (0..32767; gains above
   unity are clamped to unity in this mode - s_clipVolumes clamps there
   anyway except for SSF_UNCLAMPED shaders, which saturate),
 - the gain ramp is the closed form g(i) = (last<<8 + inc_q23*i) >> 8
   with inc_q23 = ((cur-last)<<8)/numFrames (C integer division), so
   scalar and SIMD lanes compute identical per-frame gains,
 - each contribution is (sample * gain) >> 15 with arithmetic shift,
   accumulated in int32 (each term is <= 32767 in magnitude, so overflow
   would need ~65k simultaneous full-scale channels - impossible),
 - the final narrow saturates int32 to [-32768, 32767].
Every operation is integer with defined semantics; SSE2 uses
madd(unpack(s),unpack(g)) >> 15 and NEON uses vmull_s16 >> 15, which are
bit-identical to the scalar (s*g)>>15.
===========================================================================
*/

static inline short Snd_ClampGainQ15( float v ) {
	// single, defined float->Q15 choke point for the s16 mixer
	if ( v <= 0.0f )   return 0;
	if ( v >= 1.0f )   return 32767;
	return (short)( v * 32768.0f );   // truncation toward zero
}

/*
====================
Snd_MixTwoSpeakerMonoS16
dest: int32 stereo accumulator, numFrames*2; src: mono s16
====================
*/
static inline void Snd_MixTwoSpeakerMonoS16( int *dest, const short *src, int numFrames,
                                             const short lastQ15[2], const short currentQ15[2] ) {
	if ( numFrames <= 0 ) {
		return;
	}
	const int baseL = (int)lastQ15[0] << 8;
	const int baseR = (int)lastQ15[1] << 8;
	const int incL  = ( ( (int)currentQ15[0] - lastQ15[0] ) << 8 ) / numFrames;
	const int incR  = ( ( (int)currentQ15[1] - lastQ15[1] ) << 8 ) / numFrames;
	int i = 0;

#if SND_MIX_SSE2
	__m128i idx   = _mm_setr_epi32( 0, 1, 2, 3 );
	// per-lane q23 gain accumulators: base + inc*idx
	__m128i gLq   = _mm_add_epi32( _mm_set1_epi32( baseL ),
	                  _mm_setr_epi32( 0, incL, incL*2, incL*3 ) );
	__m128i gRq   = _mm_add_epi32( _mm_set1_epi32( baseR ),
	                  _mm_setr_epi32( 0, incR, incR*2, incR*3 ) );
	const __m128i stepL = _mm_set1_epi32( incL * 4 );
	const __m128i stepR = _mm_set1_epi32( incR * 4 );
	const __m128i zero  = _mm_setzero_si128();
	(void)idx;

	for ( ; i + 4 <= numFrames; i += 4 ) {
		// 4 mono samples, sign-extended to 32 then repacked as s16 lanes 0..3
		__m128i s16v = _mm_loadl_epi64( (const __m128i *)( src + i ) );  // s0..s3 in low 64
		__m128i s32  = _mm_srai_epi32( _mm_unpacklo_epi16( s16v, s16v ), 16 ); // sign-extend
		// gains q23 -> q15 (arithmetic shift preserves the closed form)
		__m128i gL15 = _mm_srai_epi32( gLq, 8 );
		__m128i gR15 = _mm_srai_epi32( gRq, 8 );
		// (s * g) >> 15 in 32-bit: products fit (|s|<=32768, |g|<=32767)
		// 32-bit lane multiply on SSE2 via madd of s16 pairs:
		// repack s and g to s16 lanes with zero partners, madd multiplies pairs
		__m128i sPk  = _mm_packs_epi32( s32,  zero );        // s16 lanes 0..3
		__m128i gLPk = _mm_packs_epi32( gL15, zero );
		__m128i gRPk = _mm_packs_epi32( gR15, zero );
		__m128i pL   = _mm_srai_epi32( _mm_madd_epi16( _mm_unpacklo_epi16( sPk, zero ),
		                                               _mm_unpacklo_epi16( gLPk, zero ) ), 15 );
		__m128i pR   = _mm_srai_epi32( _mm_madd_epi16( _mm_unpacklo_epi16( sPk, zero ),
		                                               _mm_unpacklo_epi16( gRPk, zero ) ), 15 );
		// interleave L/R and accumulate
		__m128i lo   = _mm_unpacklo_epi32( pL, pR );         // L0 R0 L1 R1
		__m128i hi   = _mm_unpackhi_epi32( pL, pR );         // L2 R2 L3 R3
		__m128i *d0  = (__m128i *)( dest + i*2 );
		__m128i *d1  = (__m128i *)( dest + i*2 + 4 );
		_mm_storeu_si128( d0, _mm_add_epi32( _mm_loadu_si128( d0 ), lo ) );
		_mm_storeu_si128( d1, _mm_add_epi32( _mm_loadu_si128( d1 ), hi ) );
		gLq = _mm_add_epi32( gLq, stepL );
		gRq = _mm_add_epi32( gRq, stepR );
	}
#elif SND_MIX_NEON
	const int32_t gl0[4] = { baseL, baseL + incL, baseL + incL*2, baseL + incL*3 };
	const int32_t gr0[4] = { baseR, baseR + incR, baseR + incR*2, baseR + incR*3 };
	int32x4_t gLq = vld1q_s32( gl0 );
	int32x4_t gRq = vld1q_s32( gr0 );
	const int32x4_t stepL = vdupq_n_s32( incL * 4 );
	const int32x4_t stepR = vdupq_n_s32( incR * 4 );

	for ( ; i + 4 <= numFrames; i += 4 ) {
		int16x4_t sv  = vld1_s16( src + i );
		int32x4_t gL  = vshrq_n_s32( gLq, 8 );
		int32x4_t gR  = vshrq_n_s32( gRq, 8 );
		int32x4_t pL  = vshrq_n_s32( vmulq_s32( vmovl_s16( sv ), gL ), 15 );
		int32x4_t pR  = vshrq_n_s32( vmulq_s32( vmovl_s16( sv ), gR ), 15 );
		int32x4x2_t z = vzipq_s32( pL, pR );
		vst1q_s32( dest + i*2,     vaddq_s32( vld1q_s32( dest + i*2 ),     z.val[0] ) );
		vst1q_s32( dest + i*2 + 4, vaddq_s32( vld1q_s32( dest + i*2 + 4 ), z.val[1] ) );
		gLq = vaddq_s32( gLq, stepL );
		gRq = vaddq_s32( gRq, stepR );
	}
#endif

	{
		int *d = dest + (size_t)i*2;
		for ( ; i < numFrames; i++, d += 2 ) {
			const int gL = ( baseL + incL * i ) >> 8;
			const int gR = ( baseR + incR * i ) >> 8;
			d[0] += ( src[i] * gL ) >> 15;
			d[1] += ( src[i] * gR ) >> 15;
		}
	}
}

/*
====================
Snd_MixTwoSpeakerStereoS16
dest: int32 stereo accumulator; src: interleaved stereo s16
====================
*/
static inline void Snd_MixTwoSpeakerStereoS16( int *dest, const short *src, int numFrames,
                                               const short lastQ15[2], const short currentQ15[2] ) {
	if ( numFrames <= 0 ) {
		return;
	}
	const int baseL = (int)lastQ15[0] << 8;
	const int baseR = (int)lastQ15[1] << 8;
	const int incL  = ( ( (int)currentQ15[0] - lastQ15[0] ) << 8 ) / numFrames;
	const int incR  = ( ( (int)currentQ15[1] - lastQ15[1] ) << 8 ) / numFrames;
	int i = 0;

	{
		int *d = dest + (size_t)i*2;
		const short *sp = src + (size_t)i*2;
		for ( ; i < numFrames; i++, d += 2, sp += 2 ) {
			const int gL = ( baseL + incL * i ) >> 8;
			const int gR = ( baseR + incR * i ) >> 8;
			d[0] += ( sp[0] * gL ) >> 15;
			d[1] += ( sp[1] * gR ) >> 15;
		}
	}
}

/*
====================
Snd_SumToS16
Saturating narrow of the int32 accumulator to int16 output.
====================
*/
static inline void Snd_SumToS16( short *out, const int *in, int numSamples ) {
	int i = 0;

#if SND_MIX_SSE2
	for ( ; i + 8 <= numSamples; i += 8 ) {
		__m128i a = _mm_loadu_si128( (const __m128i *)( in + i ) );
		__m128i b = _mm_loadu_si128( (const __m128i *)( in + i + 4 ) );
		_mm_storeu_si128( (__m128i *)( out + i ), _mm_packs_epi32( a, b ) );
	}
#elif SND_MIX_NEON
	for ( ; i + 8 <= numSamples; i += 8 ) {
		int32x4_t a = vld1q_s32( in + i );
		int32x4_t b = vld1q_s32( in + i + 4 );
		vst1q_s16( out + i, vcombine_s16( vqmovn_s32( a ), vqmovn_s32( b ) ) );
	}
#endif

	{
		const int *ip = in + i, *ie = in + numSamples;
		short *op = out + i;
		for ( ; ip < ie; ip++, op++ ) {
			int v = *ip;
			*op = (short)( v < -32768 ? -32768 : ( v > 32767 ? 32767 : v ) );
		}
	}
}

/*
====================
Snd_FloatToS16
Deterministic float -> s16 conversion at the s16 mixer's decode edge
(clamp then truncate toward zero - PCM decode floats are exact integers,
so this is lossless for WAV sources).
====================
*/
static inline void Snd_FloatToS16( short *out, const float *in, int numSamples ) {
	Snd_MixedSoundToSamples( out, in, numSamples );
}

#endif /* !__SND_MIX_KERNELS_H__ */
