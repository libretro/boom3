/*
===========================================================================
Built-in HRTF renderer for the libretro core (s_HRTF).

Replaces the two-speaker pan law for spatialized mono channels with a
binaural HRIR convolution pair from the baked MIT KEMAR compact set
(snd_hrir_kemar.h). Full-phase responses: the interaural time
difference is embedded in the taps, so per-channel state is nothing but
FIR input history - no delay lines, nothing fractional.

Pipeline split follows the mixer's:
 - s16 path: Q15 coefficients, per-tap products accumulated in int64,
   requantized once (round half up, matching the mix kernels). Bit
   deterministic across compilers and architectures.
 - float path: float coefficients, plain float MACs; same-binary
   deterministic like the rest of the float pipeline.

Direction handling: bilinear interpolation over the measurement grid -
the two bracketing elevation rows, and on each row the two bracketing
azimuths of its full-circle ring (mirrored entries swap ears via the
baked flag). Weights are Q15 and forced to sum to exactly 32768, and
coefficient blending happens ONCE PER BLOCK per channel, like the
mixer's per-block gain ramps: a moving source steps its filter at block
rate. The gain ramp itself (distance/fade/master volume) still ramps
per sample inside the fused kernels below.

Azimuth convention, verified from the data (measured az 90 is
right-ear dominant by 24x with a 27-tap earlier onset): azimuth
increases clockwise viewed from above - to the listener's RIGHT. With
Doom 3's listener space (x forward, y left, z up) that is
  az = atan2( -y, x ),  el = asin( z )   (degrees).

Sample-rate support: the baked set is 44100 Hz; Init() resamples each
response once to the resolved output rate with a self-contained
windowed-sinc (Kaiser beta 8, 64-tap window, cutoff scaled for
downsampling), evaluated in double and quantized once. One-shot, a few
tens of ms at startup, and self-contained so the bench can drive every
rate without the engine.

Accumulator bounds for the s16 path, worked at the extremes: a
convolved sample is bounded by SND_HRIR_MAX_SUMABS_Q15/32768 = 1.93x
full scale, i.e. |conv| <= 63288 after requantization (interpolation is
convex so blending cannot exceed the per-direction bound). The volume
gain (<= 65534, the mixer's 2.0 ceiling) is applied with a 64-bit
multiply: 63288 * 65534 does not fit int32 and is not asked to.
===========================================================================
*/

#ifndef __SND_HRTF_H__
#define __SND_HRTF_H__

#include <math.h>
#include <string.h>
#include "snd_hrir_kemar.h"

/* self-sufficient outside the engine (bench) */
#ifndef ID_INLINE
#define ID_INLINE inline
#endif

/* 96000/44100 * 128 = 278.6 -> 279 taps at the highest supported rate */
#define SND_HRTF_MAX_TAPS 288
/* per-channel FIR input history */
#define SND_HRTF_HIST     ( SND_HRTF_MAX_TAPS - 1 )

class idSoundHRTF {
public:
	void	Init( int outputRate );
	bool	IsLoaded( void ) const { return loaded; }
	int		Taps( void ) const { return taps; }

	/*
	   Blend the four bracketing measured responses for (azDeg, elDeg)
	   into working per-ear filters, Q15 and float. Weights sum to
	   exactly 32768 (remainder assigned to the largest, deterministic),
	   and the same weights drive both output formats.
	*/
	void	BlendDirection( float azDeg, float elDeg,
	                        short *hL_q, short *hR_q,
	                        float *hL_f, float *hR_f ) const;

private:
	bool	loaded;
	int		taps;
	int		rate;
	short	coefQ[SND_HRIR_DIRS][SND_HRTF_MAX_TAPS][2];
	float	coefF[SND_HRIR_DIRS][SND_HRTF_MAX_TAPS][2];
};

/*
====================
idSoundHRTF::Init
====================
*/
ID_INLINE void idSoundHRTF::Init( int outputRate ) {
	loaded = false;
	rate = outputRate;

	if ( outputRate == SND_HRIR_RATE ) {
		taps = SND_HRIR_TAPS;
		for ( int d = 0; d < SND_HRIR_DIRS; d++ ) {
			for ( int t = 0; t < SND_HRTF_MAX_TAPS; t++ ) {
				for ( int e = 0; e < 2; e++ ) {
					short v = ( t < SND_HRIR_TAPS ) ? sndHrirData[d][t][e] : 0;
					coefQ[d][t][e] = v;
					/* float masters are unit-scale: the float pipeline's
					   norm-folded gains expect filters in [-1,1] */
					coefF[d][t][e] = (float)v * ( 1.0f / 32768.0f );
				}
			}
		}
		loaded = true;
		return;
	}

	/*
	   One-shot windowed-sinc resample, evaluated directly per output
	   tap: h_out[n] = sum_m h_in[m] * w(n*Fin/Fout - m), Kaiser beta 8
	   over a 64-tap support, cutoff scaled by min(1, Fout/Fin) * 0.92
	   to keep the transition band inside Nyquist when downsampling.
	   Double evaluation, single quantization at the end. Gain is
	   preserved by the Fout/Fin amplitude scale for downsampling.
	*/
	const double ratio = (double)outputRate / (double)SND_HRIR_RATE;
	int newTaps = (int)ceil( SND_HRIR_TAPS * ratio );
	if ( newTaps > SND_HRTF_MAX_TAPS ) newTaps = SND_HRTF_MAX_TAPS;
	taps = newTaps;

	const double cutoff = ( ratio < 1.0 ? ratio : 1.0 ) * 0.92;
	const double beta = 8.0;
	const int half = 32;
	/* I0(beta) for the Kaiser normalization */
	double i0b = 1.0, term = 1.0;
	for ( int k = 1; k < 32; k++ ) {
		term *= ( beta * 0.5 / k ) * ( beta * 0.5 / k );
		i0b += term;
	}
	const double amp = ( ratio < 1.0 ? ratio : 1.0 );

	for ( int d = 0; d < SND_HRIR_DIRS; d++ ) {
		for ( int n = 0; n < SND_HRTF_MAX_TAPS; n++ ) {
			double accL = 0.0, accR = 0.0;
			if ( n < newTaps ) {
				const double center = (double)n / ratio;
				int m0 = (int)floor( center ) - half + 1;
				int m1 = (int)floor( center ) + half;
				if ( m0 < 0 ) m0 = 0;
				if ( m1 >= SND_HRIR_TAPS ) m1 = SND_HRIR_TAPS - 1;
				for ( int m = m0; m <= m1; m++ ) {
					const double x = center - (double)m;
					double s;
					if ( x == 0.0 ) {
						s = cutoff;
					} else {
						s = sin( 3.14159265358979323846 * cutoff * x )
						    / ( 3.14159265358979323846 * x );
					}
					const double u = x / (double)half;
					double win = 0.0;
					if ( u > -1.0 && u < 1.0 ) {
						/* I0( beta*sqrt(1-u^2) ) / I0( beta ) */
						const double arg = beta * sqrt( 1.0 - u * u );
						double i0 = 1.0, tm = 1.0;
						for ( int k = 1; k < 32; k++ ) {
							tm *= ( arg * 0.5 / k ) * ( arg * 0.5 / k );
							i0 += tm;
						}
						win = i0 / i0b;
					}
					const double c = s * win;
					accL += c * (double)sndHrirData[d][m][0];
					accR += c * (double)sndHrirData[d][m][1];
				}
				accL *= amp / cutoff;   /* the s(0)=cutoff form bakes cutoff into gain */
				accR *= amp / cutoff;
			}
			double qL = accL >= 0.0 ? accL + 0.5 : accL - 0.5;
			double qR = accR >= 0.0 ? accR + 0.5 : accR - 0.5;
			if ( qL > 32767.0 ) qL = 32767.0; else if ( qL < -32768.0 ) qL = -32768.0;
			if ( qR > 32767.0 ) qR = 32767.0; else if ( qR < -32768.0 ) qR = -32768.0;
			coefQ[d][n][0] = (short)(int)qL;
			coefQ[d][n][1] = (short)(int)qR;
			coefF[d][n][0] = (float)(int)qL * ( 1.0f / 32768.0f );
			coefF[d][n][1] = (float)(int)qR * ( 1.0f / 32768.0f );
		}
	}
	loaded = true;
}

/*
====================
idSoundHRTF::BlendDirection
====================
*/
ID_INLINE void idSoundHRTF::BlendDirection( float azDeg, float elDeg,
		short *hL_q, short *hR_q, float *hL_f, float *hR_f ) const {
	/* normalize az to [0, 360) */
	float az = azDeg - 360.0f * floorf( azDeg / 360.0f );
	float el = elDeg;
	const float elMax = (float)( SND_HRIR_ELEV_MIN + ( SND_HRIR_ELEVS - 1 ) * SND_HRIR_ELEV_STEP );
	if ( el < (float)SND_HRIR_ELEV_MIN ) el = (float)SND_HRIR_ELEV_MIN;
	if ( el > elMax ) el = elMax;

	/* bracketing elevation rows */
	float er = ( el - (float)SND_HRIR_ELEV_MIN ) / (float)SND_HRIR_ELEV_STEP;
	int e0 = (int)er;
	if ( e0 > SND_HRIR_ELEVS - 2 ) e0 = SND_HRIR_ELEVS - 2;
	if ( e0 < 0 ) e0 = 0;
	const int e1 = e0 + 1;
	float we1f = er - (float)e0;
	if ( we1f < 0.0f ) we1f = 0.0f;
	if ( we1f > 1.0f ) we1f = 1.0f;

	int   idx[4];
	int   swp[4];
	int   wq[4];

	for ( int row = 0; row < 2; row++ ) {
		const int e = row ? e1 : e0;
		const sndHrirRingEntry_t *ring = sndHrirRings[e];
		const int len = sndHrirRingLen[e];
		/* bracketing ring entries, circular */
		int a0 = len - 1;
		for ( int i = 0; i < len; i++ ) {
			if ( (float)ring[i].azDeg > az ) { a0 = i - 1; break; }
		}
		int a1;
		float azW;      /* weight of the a1 entry */
		if ( a0 < 0 ) {                     /* below first entry: wraps from last */
			a0 = len - 1; a1 = 0;
			const float span = 360.0f - (float)ring[a0].azDeg + (float)ring[a1].azDeg;
			azW = span > 0.0f ? ( az + 360.0f - (float)ring[a0].azDeg ) / span : 0.0f;
		} else if ( a0 == len - 1 ) {       /* above last entry: wraps to first */
			a1 = 0;
			const float span = 360.0f - (float)ring[a0].azDeg + (float)ring[a1].azDeg;
			azW = span > 0.0f ? ( az - (float)ring[a0].azDeg ) / span : 0.0f;
		} else {
			a1 = a0 + 1;
			const float span = (float)ring[a1].azDeg - (float)ring[a0].azDeg;
			azW = span > 0.0f ? ( az - (float)ring[a0].azDeg ) / span : 0.0f;
		}
		if ( azW < 0.0f ) azW = 0.0f;
		if ( azW > 1.0f ) azW = 1.0f;

		const float wRow = row ? we1f : ( 1.0f - we1f );
		idx[row * 2 + 0] = ring[a0].dirIndex;
		swp[row * 2 + 0] = ring[a0].swap;
		wq [row * 2 + 0] = (int)( wRow * ( 1.0f - azW ) * 32768.0f + 0.5f );
		idx[row * 2 + 1] = ring[a1].dirIndex;
		swp[row * 2 + 1] = ring[a1].swap;
		wq [row * 2 + 1] = (int)( wRow * azW * 32768.0f + 0.5f );
	}

	/* force the Q15 weights to sum to exactly 32768: hand the rounding
	   remainder to the largest weight. Deterministic, and keeps the
	   blend convex so the SND_HRIR_MAX_SUMABS bound survives it. */
	{
		int sum = wq[0] + wq[1] + wq[2] + wq[3];
		int big = 0;
		for ( int i = 1; i < 4; i++ ) if ( wq[i] > wq[big] ) big = i;
		wq[big] += 32768 - sum;
	}

	for ( int t = 0; t < taps; t++ ) {
		int accL = 0, accR = 0;
		float afL = 0.0f, afR = 0.0f;
		for ( int i = 0; i < 4; i++ ) {
			const int eL = swp[i] ? 1 : 0;
			const int eR = swp[i] ? 0 : 1;
			accL += wq[i] * coefQ[idx[i]][t][eL];
			accR += wq[i] * coefQ[idx[i]][t][eR];
			afL  += (float)wq[i] * coefF[idx[i]][t][eL];
			afR  += (float)wq[i] * coefF[idx[i]][t][eR];
		}
		/* round half up at the Q15 requantizer, matching the mixer */
		hL_q[t] = (short)( ( accL + 0x4000 ) >> 15 );
		hR_q[t] = (short)( ( accR + 0x4000 ) >> 15 );
		hL_f[t] = afL * ( 1.0f / 32768.0f );
		hR_f[t] = afR * ( 1.0f / 32768.0f );
	}
}

/*
====================
Snd_HrtfConvolveMixS16

Fused binaural convolution + per-sample volume ramp + accumulate for
the s16 pipeline. src points at a contiguous [history | block] buffer:
src[0..taps-2] is the previous input tail, src[taps-1 ..] the current
block, so x[i-k] = src[taps-1 + i - k] with no wrap logic.

Per-tap products are short*short in int (<= 2^30), summed over <= 288
taps in int64 (<= 2^39), requantized round-half-up to an int sample
bounded by SND_HRIR_MAX_SUMABS: |conv| <= 63288 (blend is convex). The
ramped volume gain (Q15, <= 65534) then applies via a 64-bit multiply -
63288*65534 = 4.15e9 exceeds int32 and is not asked to fit. The final
per-frame term entering the int32 accumulator is <= 126576.

Plain scalar on purpose: the inner dot product is exactly the shape
compilers auto-vectorize, and every operation is defined integer
arithmetic, so any vectorization is bit-identical by construction -
the same argument as the mix kernels.
====================
*/
static inline void Snd_HrtfConvolveMixS16( int *dest, const short *src, int numFrames,
		const short *hL, const short *hR, int taps,
		const int lastQ15[2], const int currentQ15[2] ) {
	if ( numFrames <= 0 )
		return;
	const int baseL = lastQ15[0] << 8;
	const int baseR = lastQ15[1] << 8;
	const int incL  = ( ( currentQ15[0] - lastQ15[0] ) << 8 ) / numFrames;
	const int incR  = ( ( currentQ15[1] - lastQ15[1] ) << 8 ) / numFrames;

	int *d = dest;
	for ( int i = 0; i < numFrames; i++, d += 2 ) {
		const short *x = src + i;      /* x[k] = input sample (i - taps + 1 + k) */
		long long aL = 0, aR = 0;
		for ( int k = 0; k < taps; k++ ) {
			const int s = x[taps - 1 - k];
			aL += (long long)( s * (int)hL[k] );
			aR += (long long)( s * (int)hR[k] );
		}
		const int cL = (int)( ( aL + 0x4000 ) >> 15 );
		const int cR = (int)( ( aR + 0x4000 ) >> 15 );
		const int gL = ( baseL + incL * i ) >> 8;
		const int gR = ( baseR + incR * i ) >> 8;
		d[0] += (int)( ( (long long)cL * gL + 0x4000 ) >> 15 );
		d[1] += (int)( ( (long long)cR * gR + 0x4000 ) >> 15 );
	}
}

/*
====================
Snd_HrtfConvolveMixFloat

Float twin: src is the [history | block] float buffer at int16 scale,
gains are the [-1,1]-normalized per-block ramp endpoints (the caller
folds the 1/32768 output normalization in, exactly like the plain
float mix path).
====================
*/
static inline void Snd_HrtfConvolveMixFloat( float *dest, const float *src, int numFrames,
		const float *hL, const float *hR, int taps,
		const float lastV[2], const float currentV[2] ) {
	if ( numFrames <= 0 )
		return;
	const float incL = ( currentV[0] - lastV[0] ) / numFrames;
	const float incR = ( currentV[1] - lastV[1] ) / numFrames;

	float *d = dest;
	for ( int i = 0; i < numFrames; i++, d += 2 ) {
		const float *x = src + i;
		float aL = 0.0f, aR = 0.0f;
		for ( int k = 0; k < taps; k++ ) {
			const float s = x[taps - 1 - k];
			aL += s * hL[k];
			aR += s * hR[k];
		}
		d[0] += aL * ( lastV[0] + incL * i );
		d[1] += aR * ( lastV[1] + incR * i );
	}
}

#endif /* !__SND_HRTF_H__ */
