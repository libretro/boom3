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
static inline void Snd_MixTwoSpeakerMono( float *dest, const float *src, int numFrames, const float lastV[2], const float currentV[2] ) {
	if ( numFrames <= 0 )
		return;
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
static inline void Snd_MixTwoSpeakerStereo( float *dest, const float *src, int numFrames, const float lastV[2], const float currentV[2] ) {
	if ( numFrames <= 0 )
		return;
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

Clamp-and-round float samples (int16 scale) to int16.
<= -32768 saturates low, >= 32767 saturates high, otherwise round half
away from zero. numSamples counts individual samples (frames * channels).

Rounds rather than truncating: the only caller is the s16 mixer's decode
edge (Snd_FloatToS16), where PCM decode floats are exact integers - the
+-0.5 bias truncates back to the same integer, so WAV sources stay
bit-lossless - but Ogg blocks arrive as real-valued floats from the
vorbis decode and the sinc resampler. Truncation toward zero gave those a
signal-correlated error of up to 1 LSB folded around zero (crossover
distortion); rounding halves it to +-0.5 LSB and removes the fold.

Half-away (add +-0.5 by sign, then truncate) rather than half-even so all
three implementations are the same two exact float operations: the bias
add is IEEE nearest in every path and the conversion truncates in every
path (cvtt / vcvtq / C cast), keeping the result bit-identical across
scalar, SSE2, and NEON.
====================
*/
static inline void Snd_MixedSoundToSamples( short *out, const float *in, int numSamples ) {
	int i = 0;

#if SND_MIX_SSE2
	const __m128 lo   = _mm_set1_ps( -32768.0f );
	const __m128 hi   = _mm_set1_ps(  32767.0f );
	const __m128 half = _mm_set1_ps( 0.5f );
	const __m128 sgn  = _mm_castsi128_ps( _mm_set1_epi32( (int)0x80000000u ) );
	for ( ; i + 8 <= numSamples; i += 8 ) {
		__m128 a = _mm_loadu_ps( in + i );
		__m128 b = _mm_loadu_ps( in + i + 4 );
		a = _mm_min_ps( _mm_max_ps( a, lo ), hi );
		b = _mm_min_ps( _mm_max_ps( b, lo ), hi );
		// +-0.5 with the sample's sign, then truncate = round half away
		a = _mm_add_ps( a, _mm_or_ps( _mm_and_ps( a, sgn ), half ) );
		b = _mm_add_ps( b, _mm_or_ps( _mm_and_ps( b, sgn ), half ) );
		// inputs are pre-clamped so the INT_MIN indefinite case can't occur
		__m128i ia = _mm_cvttps_epi32( a );
		__m128i ib = _mm_cvttps_epi32( b );
		_mm_storeu_si128( (__m128i *)( out + i ), _mm_packs_epi32( ia, ib ) );
	}
#elif SND_MIX_NEON
	const float32x4_t lo   = vdupq_n_f32( -32768.0f );
	const float32x4_t hi   = vdupq_n_f32(  32767.0f );
	const float32x4_t half = vdupq_n_f32( 0.5f );
	const uint32x4_t  sgn  = vdupq_n_u32( 0x80000000u );
	for ( ; i + 8 <= numSamples; i += 8 ) {
		float32x4_t a = vld1q_f32( in + i );
		float32x4_t b = vld1q_f32( in + i + 4 );
		a = vminq_f32( vmaxq_f32( a, lo ), hi );
		b = vminq_f32( vmaxq_f32( b, lo ), hi );
		float32x4_t ba = vreinterpretq_f32_u32( vorrq_u32(
				vandq_u32( vreinterpretq_u32_f32( a ), sgn ),
				vreinterpretq_u32_f32( half ) ) );
		float32x4_t bb = vreinterpretq_f32_u32( vorrq_u32(
				vandq_u32( vreinterpretq_u32_f32( b ), sgn ),
				vreinterpretq_u32_f32( half ) ) );
		a = vaddq_f32( a, ba );
		b = vaddq_f32( b, bb );
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
			float v = *ip;
			if ( v <= -32768.0f ) {
				*op = -32768;
			} else if ( v >= 32767.0f ) {
				*op = 32767;
			} else {
				*op = (short)( v >= 0.0f ? v + 0.5f : v - 0.5f );
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
 - each contribution is (sample * gain + 0x4000) >> 15 - round half up
   at the Q15 requantizer rather than the floor of a bare arithmetic
   shift, whose error is one-sided in (-1, 0] LSB: with N active voices
   that floored a DC bias of about -N/2 LSB into the accumulator on top
   of signal-correlated truncation noise. The bias add is the same
   integer op in scalar, SSE2, and NEON, so lanes stay bit-identical.
   Accumulated in int32 (each term is <= 32767 in magnitude, so overflow
   would need ~65k simultaneous full-scale channels - impossible),
 - the final narrow saturates int32 to [-32768, 32767].
Every operation is integer with defined semantics, there are no
cross-lane reductions, and the gain ramp is closed-form per frame - so
the kernels are plain scalar loops and any auto-vectorization of them is
bit-identical by construction (hand-written SSE2 paths measured 2x
slower than what the compiler emits from these loops; see the kernels).
===========================================================================
*/

static inline short Snd_ClampGainQ15( float v ) {
	/*
	   Single, defined float->Q15 choke point for the s16 mixer.

	   Rounds rather than truncating: gains are non-negative, so truncation
	   was a one-sided error that attenuated every gain by an average of half
	   an LSB. Measured against the ideal v*32767, worst-case error drops from
	   0.999 to 0.501 LSB and the mean from 0.334 to 0.250.

	   The 32767 scale is deliberate. Rounding v*32768 overflows for v just
	   below 1.0 - lrintf( 0.99999f * 32768.0f ) is 32768, which does not fit
	   in a short and wraps to -32768, a full-scale sign flip on the loudest
	   gains. The old truncation happened to be safe from that because it
	   could never reach 32768; the rounded form has to be made safe on
	   purpose.
	*/
	if ( v <= 0.0f )   return 0;
	if ( v >= 1.0f )   return 32767;
	int q = (int)( v * 32767.0f + 0.5f );
	if ( q > 32767 ) q = 32767;
	return (short)q;
}

/*
====================
Snd_MixTwoSpeakerMonoS16
dest: int32 stereo accumulator, numFrames*2; src: mono s16
====================
*/
static inline void Snd_MixTwoSpeakerMonoS16( int *dest, const short *src, int numFrames, const short lastQ15[2], const short currentQ15[2] ) {
	if ( numFrames <= 0 )
		return;
	const int baseL = (int)lastQ15[0] << 8;
	const int baseR = (int)lastQ15[1] << 8;
	const int incL  = ( ( (int)currentQ15[0] - lastQ15[0] ) << 8 ) / numFrames;
	const int incR  = ( ( (int)currentQ15[1] - lastQ15[1] ) << 8 ) / numFrames;

	/*
	   Deliberately plain scalar. A hand-written SSE2 path lived here (pack
	   the 4 mono samples against zero partners, madd, unpack) and measured
	   0.72 ns/frame-sample; the compiler's auto-vectorization of this loop
	   measures 0.35 on the same machine - the pack dance wastes half of
	   every vector, and the closed-form gain g(i) = (base + inc*i) >> 8 is
	   exactly what makes the loop legal to vectorize. Bit-exactness does
	   not depend on how the compiler carves it into lanes: every operation
	   is defined integer arithmetic with no cross-lane reductions, so any
	   vectorization of this loop produces identical bytes.
	*/
	int *d = dest;
	for ( int i = 0; i < numFrames; i++, d += 2 ) {
		const int gL = ( baseL + incL * i ) >> 8;
		const int gR = ( baseR + incR * i ) >> 8;
		d[0] += ( src[i] * gL + 0x4000 ) >> 15;
		d[1] += ( src[i] * gR + 0x4000 ) >> 15;
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

	/* plain scalar on purpose - see the mono kernel above */
	int *d = dest;
	const short *sp = src;
	for ( int i = 0; i < numFrames; i++, d += 2, sp += 2 ) {
		const int gL = ( baseL + incL * i ) >> 8;
		const int gR = ( baseR + incR * i ) >> 8;
		d[0] += ( sp[0] * gL + 0x4000 ) >> 15;
		d[1] += ( sp[1] * gR + 0x4000 ) >> 15;
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


/*
====================
Snd_ClampFloatOutput

Saturate the float mix to the output range before handing it to the
frontend. The engine's mix is deliberately hot (sums of channels regularly
exceed full scale - that is by design, every previous pipeline saturated
at the final int16 conversion), so unclamped float output overloads the
frontend's audio chain and distorts. Range is [-1.0, 32767/32768] for
exact behavioral parity with the s16 pipeline's saturating narrow.
====================
*/
static inline void Snd_ClampFloatOutput( float *buf, int numSamples ) {
	const float loF = -1.0f;
	const float hiF = 32767.0f / 32768.0f;
	int i = 0;

#if SND_MIX_SSE2
	const __m128 lo = _mm_set1_ps( loF );
	const __m128 hi = _mm_set1_ps( hiF );
	for ( ; i + 8 <= numSamples; i += 8 ) {
		__m128 a = _mm_loadu_ps( buf + i );
		__m128 b = _mm_loadu_ps( buf + i + 4 );
		_mm_storeu_ps( buf + i,     _mm_min_ps( _mm_max_ps( a, lo ), hi ) );
		_mm_storeu_ps( buf + i + 4, _mm_min_ps( _mm_max_ps( b, lo ), hi ) );
	}
#elif SND_MIX_NEON
	const float32x4_t lo = vdupq_n_f32( loF );
	const float32x4_t hi = vdupq_n_f32( hiF );
	for ( ; i + 8 <= numSamples; i += 8 ) {
		float32x4_t a = vld1q_f32( buf + i );
		float32x4_t b = vld1q_f32( buf + i + 4 );
		vst1q_f32( buf + i,     vminq_f32( vmaxq_f32( a, lo ), hi ) );
		vst1q_f32( buf + i + 4, vminq_f32( vmaxq_f32( b, lo ), hi ) );
	}
#endif

	for ( ; i < numSamples; i++ ) {
		float v = buf[i];
		buf[i] = v < loF ? loF : ( v > hiF ? hiF : v );
	}
}

#endif /* !__SND_MIX_KERNELS_H__ */
