/*
===========================================================================
snd_reverb.h - environmental reverb for the libretro dual-format mixer.

Replaces the OpenAL EFX (EAXREVERB) effect that was removed with OpenAL.
Same data drives it: the game's efxs/<map>.efx files parsed by idEFXFile
into sndReverbParams_t (the EAXREVERB parameter set), selected by listener
portal area exactly like the old code (area number -> area name ->
"default").

Topology (one instance per sound world, mono send in, stereo wet out):

  send -> predelay line -+-> early reflection taps (reflections gain/delay)
                         +-> 2 series allpasses (diffusion)
                             -> 8-line FDN, Householder feedback,
                                one-pole damping per line (decay hf ratio)
                                (late reverb gain/delay)

Dual format, one topology:
 - float path: float lines/coefficients, wet output added to the [-1,1]
   normalized mix (the 1/32768 normalization is folded into the wet gains,
   same trick as the channel gains);
 - fixed path: int32 lines at int16 scale, all coefficients quantized to
   Q15 through one defined choke point, products (x*g)>>15 via int64
   intermediates, Householder 2/8 as an exact >>2. Every operation is
   integer with defined semantics: bit-exact across compilers and
   architectures, like the s16 mixer it feeds.

Deliberately scalar: the whole effect is a few dozen ops per sample
(<1% of a core at 44.1kHz), and scalar integer code is trivially
deterministic. Parameter changes (area transitions) crossfade over
REVERB_XFADE_BLOCKS mix blocks to avoid zipper noise; derived
coefficients are recomputed once per block, never per sample.

State is just delay lines and indices: serialization for savestates is a
straight memcpy of the instance (flagged for the retro_serialize work).
===========================================================================
*/

#ifndef __SND_REVERB_H__
#define __SND_REVERB_H__

#include <string.h>
#include <math.h>

// EAXREVERB parameter set as parsed from efxs/*.efx (linear gains, seconds)
typedef struct sndReverbParams_s {
	float	density;			// 0..1
	float	diffusion;			// 0..1
	float	gain;				// master wet gain 0..1
	float	gainHF;				// 0..1
	float	gainLF;				// 0..1  (unused by this topology)
	float	decayTime;			// 0.1..20 s
	float	decayHFRatio;		// 0.1..2
	float	decayLFRatio;		// (unused)
	float	reflectionsGain;	// 0..3.16
	float	reflectionsDelay;	// 0..0.3 s
	float	lateReverbGain;		// 0..10
	float	lateReverbDelay;	// 0..0.1 s
	float	echoTime;			// (unused)
	float	echoDepth;			// (unused)
	float	modulationTime;		// (unused)
	float	modulationDepth;	// (unused)
	float	airAbsorptionGainHF;// (unused)
	float	hfReference;		// Hz
	float	lfReference;		// (unused)
	float	roomRolloffFactor;	// (unused)
	int		decayHFLimit;		// 0/1

	void SetDefaults( void ) {	// EFX_REVERB_PRESET_GENERIC
		density = 1.0f; diffusion = 1.0f;
		gain = 0.3162f; gainHF = 0.8913f; gainLF = 1.0f;
		decayTime = 1.49f; decayHFRatio = 0.83f; decayLFRatio = 1.0f;
		reflectionsGain = 0.05f; reflectionsDelay = 0.007f;
		lateReverbGain = 1.2589f; lateReverbDelay = 0.011f;
		echoTime = 0.25f; echoDepth = 0.0f;
		modulationTime = 0.25f; modulationDepth = 0.0f;
		airAbsorptionGainHF = 0.9943f;
		hfReference = 5000.0f; lfReference = 250.0f;
		roomRolloffFactor = 0.0f; decayHFLimit = 1;
	}
} sndReverbParams_t;

/*
   The reverb runs at the output rate. REVERB_FS was a compile-time 44100;
   it is now the live rate, and the FDN delay lines are re-derived whenever it
   changes so the reverb keeps the same delay times in milliseconds.

   Declared here rather than relying on snd_local.h: this header is included
   from it, so the accessor is not visible yet at this point.
*/
extern int snd_sampleRate;
static ID_INLINE int snd_SampleRate( void ) { return snd_sampleRate; }

#define REVERB_FS				( snd_SampleRate() )
#define REVERB_LINES			8
#define REVERB_PREDELAY_LEN		16384	// ~371 ms, power of two (wrap by mask)
/*
   Per-FDN-line max, power of two. Sized for the longest line at the highest
   supported rate: the ~97ms line is 4297 samples at 44.1kHz but 9371 at
   96kHz, so 8192 is not enough there. Costs 0.5 MB more on the single
   idSoundReverb instance and nothing at all at 44.1/48kHz, where the lines
   are unchanged in length.
*/
#define REVERB_MAX_LINE			16384
#define REVERB_XFADE_BLOCKS		24		// parameter crossfade span (~0.4 s at 60fps)
#define REVERB_EARLY_TAPS		6

class idSoundReverb {
public:
	void	Init( void );
	// set a new target parameter set; crossfades over REVERB_XFADE_BLOCKS
	void	SetParams( const sndReverbParams_t &p );
	// mono send at int16 scale -> stereo wet ADDED to dest.
	// float dest is the [-1,1] normalized mix buffer; wetScale is the master
	// (s_reverbGain cvar), applied identically in both paths.
	void	ProcessFloat( const float *send, float *dest, int numFrames, float wetScale );
	void	ProcessS16( const int *send, int *destAccum, int numFrames, float wetScale );
	bool	IsActive( void ) const { return active; }
	void	Deactivate( void ) { active = false; }

private:
	void	StepBlockParams( void );	// advance crossfade, derive coefficients

	// --- parameters ---
	sndReverbParams_t	cur, target;
	int					xfadeBlocksLeft;
	bool				active;

	// --- derived per-block coefficients (float masters) ---
	float	fbGain[REVERB_LINES];		// per-line loop gain (RT60)
	float	dampCoef;					// one-pole damping coefficient
	float	apGain;						// allpass (diffusion) gain
	float	earlyGain, lateGainL, lateGainR;
	int		preDelaySamps, lateDelaySamps;
	// Q15 twins, quantized once per block through one choke point
	short	fbGainQ[REVERB_LINES], dampCoefQ, apGainQ;
	short	earlyGainQ, lateGainQ;

	// --- state: float path ---
	float	preF[REVERB_PREDELAY_LEN];
	float	apF[2][512];
	float	lineF[REVERB_LINES][REVERB_MAX_LINE];
	float	dampF[REVERB_LINES];
	// --- state: fixed path (int16-scale samples in int32) ---
	int		preI[REVERB_PREDELAY_LEN];
	int		apI[2][512];
	int		lineI[REVERB_LINES][REVERB_MAX_LINE];
	int		dampI[REVERB_LINES];
	// --- shared indices/lengths ---
	unsigned prePos;
	unsigned apPos[2];
	unsigned linePos[REVERB_LINES];
	int		lineLen[REVERB_LINES];
	int		apLen[2];
	int		earlyOfs[REVERB_EARLY_TAPS];
	float	earlyTapGain[REVERB_EARLY_TAPS];
	short	earlyTapGainQ[REVERB_EARLY_TAPS];
};

// single defined float->Q15 choke point for all reverb coefficients
static inline short Snd_ReverbCoefQ15( float v ) {
	if ( v <= -0.999969f ) return -32767;
	if ( v >=  0.999969f ) return  32767;
	return (short)( v * 32768.0f );
}

ID_INLINE void idSoundReverb::Init( void ) {
	memset( this, 0, sizeof( *this ) );
	/*
	   Mutually coprime line lengths, ~37..97 ms, all < REVERB_MAX_LINE.

	   These are primes, which makes them pairwise coprime by construction -
	   an FDN needs that for diffusion, and simply scaling the 44.1kHz set by
	   the rate ratio does not preserve it (scaling to 96kHz yields 18
	   non-coprime pairs). The tables below are the next prime at or above
	   each scaled length, so the delay times match to within 0.2 ms across
	   all three rates.
	*/
	static const int lens44[REVERB_LINES] = { 1637, 1913, 2251, 2707, 3169, 3571, 4001, 4297 };
	static const int lens48[REVERB_LINES] = { 1783, 2083, 2459, 2953, 3449, 3889, 4357, 4679 };
	static const int lens96[REVERB_LINES] = { 3571, 4177, 4903, 5897, 6899, 7789, 8713, 9371 };
	static const int apl44[2] = { 331, 449 };
	static const int apl48[2] = { 359, 487 };
	static const int apl96[2] = { 719, 977 };
	static const int eofs44[REVERB_EARLY_TAPS] = { 353, 907, 1409, 1861, 2311, 3079 };
	static const int eofs48[REVERB_EARLY_TAPS] = { 383, 991, 1531, 2027, 2521, 3347 };
	static const int eofs96[REVERB_EARLY_TAPS] = { 769, 1979, 3067, 4051, 5039, 6703 };

	const int *lens = lens44, *apl = apl44, *eofs = eofs44;
	if ( snd_SampleRate() == 96000 ) {
		lens = lens96; apl = apl96; eofs = eofs96;
	} else if ( snd_SampleRate() == 48000 ) {
		lens = lens48; apl = apl48; eofs = eofs48;
	}

	for ( int i = 0; i < REVERB_LINES; i++ ) {
		lineLen[i] = lens[i];
	}
	apLen[0] = apl[0]; apLen[1] = apl[1];
	// early reflection tap offsets (~8..70 ms), alternate L/R at use site
	for ( int i = 0; i < REVERB_EARLY_TAPS; i++ ) {
		earlyOfs[i] = eofs[i];
		earlyTapGain[i]  = 1.0f - (float)i / ( REVERB_EARLY_TAPS + 1 );
		earlyTapGainQ[i] = Snd_ReverbCoefQ15( earlyTapGain[i] );
	}
	cur.SetDefaults();
	target = cur;
	xfadeBlocksLeft = 0;
	active = false;
	StepBlockParams();
}

ID_INLINE void idSoundReverb::SetParams( const sndReverbParams_t &p ) {
	target = p;
	xfadeBlocksLeft = REVERB_XFADE_BLOCKS;
	active = true;
}

ID_INLINE void idSoundReverb::StepBlockParams( void ) {
	if ( xfadeBlocksLeft > 0 ) {
		const float a = 1.0f / xfadeBlocksLeft;
		#define RVB_LERP(f) cur.f += ( target.f - cur.f ) * a
		RVB_LERP(density); RVB_LERP(diffusion); RVB_LERP(gain); RVB_LERP(gainHF);
		RVB_LERP(decayTime); RVB_LERP(decayHFRatio);
		RVB_LERP(reflectionsGain); RVB_LERP(reflectionsDelay);
		RVB_LERP(lateReverbGain); RVB_LERP(lateReverbDelay);
		RVB_LERP(hfReference);
		#undef RVB_LERP
		xfadeBlocksLeft--;
	}

	// clamp inputs defensively (efx files are data)
	float decay = cur.decayTime   < 0.1f ? 0.1f : ( cur.decayTime > 20.0f ? 20.0f : cur.decayTime );
	float hfr   = cur.decayHFRatio< 0.1f ? 0.1f : ( cur.decayHFRatio > 2.0f ? 2.0f : cur.decayHFRatio );

	// per-line loop gain from RT60: g = 10^(-3*L/(T60*fs))
	for ( int i = 0; i < REVERB_LINES; i++ ) {
		float g = powf( 10.0f, -3.0f * lineLen[i] / ( decay * REVERB_FS ) );
		if ( g > 0.9995f ) g = 0.9995f;
		fbGain[i]  = g;
		fbGainQ[i] = Snd_ReverbCoefQ15( g );
	}
	// damping one-pole: choose coefficient so HF decays hfr times faster.
	// approximation: lp mix c in [0,1), higher = darker. Map from hfr and
	// hfReference: c = clamp( (1-hfr) * (0.35 + 0.4*(5000/hfRef clamped)), 0, 0.95 )
	{
		float refBias = cur.hfReference > 100.0f ? 5000.0f / cur.hfReference : 1.0f;
		if ( refBias > 2.0f ) refBias = 2.0f;
		float c = ( 1.0f - hfr ) * ( 0.35f + 0.4f * refBias );
		if ( c < 0.0f ) c = 0.0f;
		if ( c > 0.95f ) c = 0.95f;
		dampCoef  = c;
		dampCoefQ = Snd_ReverbCoefQ15( c );
	}
	// diffusion -> allpass gain
	apGain  = 0.15f + 0.55f * ( cur.diffusion < 0.0f ? 0.0f : ( cur.diffusion > 1.0f ? 1.0f : cur.diffusion ) );
	apGainQ = Snd_ReverbCoefQ15( apGain );

	earlyGain  = cur.gain * cur.gainHF * cur.reflectionsGain;
	if ( earlyGain > 1.0f ) earlyGain = 1.0f;
	float lg   = cur.gain * cur.lateReverbGain * 0.25f;	// FDN sum headroom
	if ( lg > 1.0f ) lg = 1.0f;
	lateGainL = lateGainR = lg;
	earlyGainQ = Snd_ReverbCoefQ15( earlyGain );
	lateGainQ  = Snd_ReverbCoefQ15( lg );

	preDelaySamps  = (int)( cur.reflectionsDelay * REVERB_FS );
	lateDelaySamps = preDelaySamps + (int)( cur.lateReverbDelay * REVERB_FS );
	if ( preDelaySamps  < 1 ) preDelaySamps = 1;
	if ( preDelaySamps  > REVERB_PREDELAY_LEN - 8 )  preDelaySamps  = REVERB_PREDELAY_LEN - 8;
	if ( lateDelaySamps < 1 ) lateDelaySamps = 1;
	if ( lateDelaySamps > REVERB_PREDELAY_LEN - 8 )  lateDelaySamps = REVERB_PREDELAY_LEN - 8;
}

/*
====================
idSoundReverb::ProcessFloat
====================
*/
ID_INLINE void idSoundReverb::ProcessFloat( const float *send, float *dest, int numFrames, float wetScale ) {
	StepBlockParams();

	const float norm  = wetScale / 32768.0f;	// wet is mixed into the normalized buffer
	const float eG    = earlyGain * norm;
	const float lG    = lateGainL * norm;
	const unsigned preMask = REVERB_PREDELAY_LEN - 1;

	for ( int i = 0; i < numFrames; i++ ) {
		preF[prePos & preMask] = send[i];

		// early reflections (taps off predelay), alternate L/R
		float eL = 0.0f, eR = 0.0f;
		for ( int t = 0; t < REVERB_EARLY_TAPS; t++ ) {
			float v = preF[( prePos - (unsigned)( preDelaySamps + earlyOfs[t] ) ) & preMask] * earlyTapGain[t];
			if ( t & 1 ) eR += v; else eL += v;
		}

		// late input: predelayed, diffused through 2 series allpasses
		float x = preF[( prePos - (unsigned)lateDelaySamps ) & preMask];
		for ( int a = 0; a < 2; a++ ) {
			unsigned p = apPos[a] % (unsigned)apLen[a];
			float d = apF[a][p];
			float y = d - apGain * x;
			apF[a][p] = x + apGain * y;
			x = y;
		}

		// FDN: read line outputs, damp, Householder feedback
		float out[REVERB_LINES]; float sum = 0.0f;
		for ( int l = 0; l < REVERB_LINES; l++ ) {
			unsigned p = linePos[l] % (unsigned)lineLen[l];
			float v = lineF[l][p];
			dampF[l] += dampCoef * ( v - dampF[l] );	// one-pole LP in the loop
			out[l] = dampF[l];
			sum += out[l];
		}
		sum *= 0.25f;	// 2/N, N=8
		for ( int l = 0; l < REVERB_LINES; l++ ) {
			unsigned p = linePos[l] % (unsigned)lineLen[l];
			lineF[l][p] = x + fbGain[l] * ( out[l] - sum );
			linePos[l]++;
		}

		float lL = out[0] + out[2] + out[4] + out[6];
		float lR = out[1] + out[3] + out[5] + out[7];

		dest[i*2+0] += eL * eG + lL * lG;
		dest[i*2+1] += eR * eG + lR * lG;
		prePos++;
		apPos[0]++; apPos[1]++;
	}
}

/*
====================
idSoundReverb::ProcessS16

Identical topology in pure integer math. Samples are int16-scale in int32,
coefficients Q15, products via int64 then >>15 (arithmetic). Bit-exact
across compilers and architectures.
====================
*/
ID_INLINE void idSoundReverb::ProcessS16( const int *send, int *destAccum, int numFrames, float wetScale ) {
	StepBlockParams();

	const short wetQ  = Snd_ReverbCoefQ15( wetScale > 0.999f ? 0.999f : ( wetScale < 0.0f ? 0.0f : wetScale ) );
	const unsigned preMask = REVERB_PREDELAY_LEN - 1;
	// toward-ZERO truncation, not arithmetic >>15 (toward -inf): with g < 1
	// this guarantees |QMUL(x,g)| <= |x|*g < |x|, i.e. strict contraction in
	// every feedback path, so the tail provably decays to exactly 0. Plain
	// arithmetic shift biases negative values toward -inf and the FDN
	// sustains a permanent quantization-noise limit cycle (caught by the
	// impulse test: 15% of energy past RT60 instead of ~0).
	#define QMUL(x,g) ( (x) >= 0 ? (int)( ( (long long)(x) * (g) ) >> 15 ) \
	                             : -(int)( ( -(long long)(x) * (g) ) >> 15 ) )

	for ( int i = 0; i < numFrames; i++ ) {
		preI[prePos & preMask] = send[i];

		int eL = 0, eR = 0;
		for ( int t = 0; t < REVERB_EARLY_TAPS; t++ ) {
			int v = QMUL( preI[( prePos - (unsigned)( preDelaySamps + earlyOfs[t] ) ) & preMask], earlyTapGainQ[t] );
			if ( t & 1 ) eR += v; else eL += v;
		}

		int x = preI[( prePos - (unsigned)lateDelaySamps ) & preMask];
		for ( int a = 0; a < 2; a++ ) {
			unsigned p = apPos[a] % (unsigned)apLen[a];
			int d = apI[a][p];
			int y = d - QMUL( x, apGainQ );
			apI[a][p] = x + QMUL( y, apGainQ );
			x = y;
		}

		int out[REVERB_LINES]; long long sum = 0;
		for ( int l = 0; l < REVERB_LINES; l++ ) {
			unsigned p = linePos[l] % (unsigned)lineLen[l];
			int v = lineI[l][p];
			dampI[l] += QMUL( v - dampI[l], dampCoefQ );
			out[l] = dampI[l];
			sum += out[l];
		}
		int sumQ = (int)( sum >> 2 );	// 2/8 exactly
		for ( int l = 0; l < REVERB_LINES; l++ ) {
			unsigned p = linePos[l] % (unsigned)lineLen[l];
			lineI[l][p] = x + QMUL( out[l] - sumQ, fbGainQ[l] );
			linePos[l]++;
		}

		int lL = out[0] + out[2] + out[4] + out[6];
		int lR = out[1] + out[3] + out[5] + out[7];

		destAccum[i*2+0] += QMUL( QMUL( eL, earlyGainQ ) + QMUL( lL, lateGainQ ), wetQ );
		destAccum[i*2+1] += QMUL( QMUL( eR, earlyGainQ ) + QMUL( lR, lateGainQ ), wetQ );
		prePos++;
		apPos[0]++; apPos[1]++;
	}
	#undef QMUL
}

#endif /* !__SND_REVERB_H__ */
