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
	float	gainLF;				// 0..1, output low band below lfReference
	float	decayTime;			// 0.1..20 s
	float	decayHFRatio;		// 0.1..2
	float	decayLFRatio;		// 0.1..2, LF decay via in-loop low-shelf
	float	reflectionsGain;	// 0..3.16
	float	reflectionsDelay;	// 0..0.3 s
	float	lateReverbGain;		// 0..10
	float	lateReverbDelay;	// 0..0.1 s
	float	echoTime;			// 0.075..0.25 s, parallel comb period
	float	echoDepth;			// 0..1, comb output blend
	float	modulationTime;		// 0.04..4 s, read-tap LFO period
	float	modulationDepth;	// 0..1, LFO excursion
	float	airAbsorptionGainHF;// 0.892..1, send loss per meter (gain-only approx)
	float	hfReference;		// Hz
	float	lfReference;		// Hz, low band split point
	float	roomRolloffFactor;	// wet-path inverse-distance factor
	int		decayHFLimit;		// 0/1, caps HF decay at the air-absorption limit

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
/*
   Echo comb length: EAXREVERB echo time tops out at 0.25 s, which is 24000
   samples at 96kHz; power of two for mask wrapping. 256 KB across both
   format twins on the single instance.
*/
#define REVERB_ECHO_LEN			32768
// density read-tap slew: 1/64 sample per sample. 1.56% instantaneous pitch
// deviation on the wet tail while an area transition retargets the taps,
// reaching a worst-case small-room retarget in ~1.5s; the artifact rides
// under the simultaneous gain/decay crossfade of the same area change
#define REVERB_TAP_SLEW_Q16		1024

/*
   Q15 quarter-wave-symmetric sine for the modulation LFO, 64 entries per
   quarter. A static literal table, not computed at Init: identical bytes on
   every compiler and architecture, so the fixed path stays bit-exact.
   Generated once with round-half-away from sin(pi/2 * i/64)*32767.
*/
static const short snd_reverbSinQ15[65] = {
	    0,   804,  1608,  2410,  3212,  4011,  4808,  5602,  6393,  7179,
	 7962,  8739,  9512, 10278, 11039, 11793, 12539, 13279, 14010, 14732,
	15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403,
	22005, 22594, 23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
	27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956, 30273, 30571,
	30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521,
	32609, 32678, 32728, 32757, 32767
};
// full-wave sine, phase in [0,256) Q8, output Q15
static inline int Snd_ReverbSinQ15( unsigned ph ) {
	unsigned q = ( ph >> 6 ) & 3, i = ph & 63;
	switch ( q ) {
	case 0: return  snd_reverbSinQ15[i];
	case 1: return  snd_reverbSinQ15[64 - i];
	case 2: return -snd_reverbSinQ15[i];
	default: return -snd_reverbSinQ15[64 - i];
	}
}
#if defined(__SSE2__) && !defined(REVERB_FORCE_SCALAR)
#include <emmintrin.h>
/*
   Branch-free 4-lane QMUL, bit-identical to the scalar macro for every
   input the loop can produce: fold the sign out (s = x>>31, ax = (x^s)-s),
   widen-multiply the magnitude by the non-negative gain, shift, fold the
   sign back in ((p^s)-s). Toward-zero truncation of |x|*g is plain
   unsigned >>, so the equivalence is exact; the only excluded input is
   x = INT_MIN, which the contracting loop cannot reach (states stay
   orders of magnitude below it). pmuludq multiplies the even 32-bit
   lanes, so the odd lanes go through a second multiply and the two
   results interleave back with two shuffles.
*/
static inline __m128i Snd_ReverbQmul4( __m128i x, __m128i g, int shift ) {
	__m128i s  = _mm_srai_epi32( x, 31 );
	__m128i ax = _mm_sub_epi32( _mm_xor_si128( x, s ), s );
	__m128i pe = _mm_srli_epi64( _mm_mul_epu32( ax, g ), shift );
	__m128i po = _mm_srli_epi64( _mm_mul_epu32( _mm_srli_epi64( ax, 32 ),
	                                            _mm_srli_epi64( g, 32 ) ), shift );
	__m128i r  = _mm_castps_si128( _mm_shuffle_ps( _mm_castsi128_ps( pe ),
	                                               _mm_castsi128_ps( po ),
	                                               _MM_SHUFFLE( 2, 0, 2, 0 ) ) );
	r = _mm_shuffle_epi32( r, _MM_SHUFFLE( 3, 1, 2, 0 ) );
	return _mm_sub_epi32( _mm_xor_si128( r, s ), s );
}
#endif

// sine with 16-bit phase (Q8 index + Q8 fraction linearly interpolated):
// 256 raw LFO steps would jump the read tap by whole samples at once and
// zipper; the interpolation keeps successive tap deltas fractional
static inline int Snd_ReverbSinQ15i( unsigned ph16 ) {
	int a = Snd_ReverbSinQ15( ( ph16 >> 8 ) & 255u );
	int b = Snd_ReverbSinQ15( ( ( ph16 >> 8 ) + 1u ) & 255u );
	int f = (int)( ph16 & 255u );
	return a + ( ( ( b - a ) * f ) >> 8 );
}

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
	// resolved preset for the send side (air absorption / room rolloff are
	// per-source distance terms, applied where the send gain is computed)
	const sndReverbParams_t &Params( void ) const { return target; }

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

	// LF band: per-line low-shelf in the loop (decayLFRatio) and an output
	// band split (gainLF / mid / gainHF). adjLF is the shelf's LP-component
	// gain delta; the DC loop gain fbGain*(1+adjLF) is clamped below 1 in
	// StepBlockParams, which is what keeps the decay proof intact.
	float	lfLoopCoef, lfAdj;
	short	lfLoopCoefQ, lfAdjQ;		// |lfAdj| in Q15: the only signed gain
	int		lfAdjNeg;					// its sign, applied after the multiply
	float	osLowCoef, osHighCoef, osGainLF, osGainHF;
	short	osLowCoefQ, osHighCoefQ, osGainLFQ, osGainHFQ;

	// echo: parallel feedback comb at echoTime, RT60-derived feedback,
	// output blended by echoDepth (depth 0 skips the whole stage)
	int		echoLen;
	float	echoFbGain, echoOutGain;
	short	echoFbGainQ, echoOutGainQ;

	// modulation + density: both act on the FDN read tap. densityTapQ16 is
	// the slewed per-line base tap (Q16 samples, == lineLen<<16 at density
	// 1, which is the exact legacy read); modDepthQ16 is the LFO excursion,
	// subtractive only, so the tap never exceeds the line's history.
	unsigned modPhase, modPhaseInc;		// Q32 phase accumulator
	int		modDepthQ16;
	unsigned densityTapQ16[REVERB_LINES], densityTargetQ16[REVERB_LINES];

	// --- state: float path ---
	float	preF[REVERB_PREDELAY_LEN];
	float	apF[2][512];
	float	lineF[REVERB_LINES][REVERB_MAX_LINE];
	float	dampF[REVERB_LINES];
	float	lfLoopF[REVERB_LINES];		// per-line low-shelf state
	float	echoF[REVERB_ECHO_LEN];
	float	osLowF[2], osHighF[2];		// output split states, L/R (1st pole)
	float	osLow2F[2], osHigh2F[2];	// 2nd pole: 12dB/oct band split
	// --- state: fixed path (int16-scale samples in int32) ---
	int		preI[REVERB_PREDELAY_LEN];
	int		apI[2][512];
	int		lineI[REVERB_LINES][REVERB_MAX_LINE];
	int		dampI[REVERB_LINES];
	int		lfLoopI[REVERB_LINES];
	int		echoI[REVERB_ECHO_LEN];
	int		osLowI[2], osHighI[2];
	int		osLow2I[2], osHigh2I[2];
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
	/*
	   Round rather than truncate. (short)( v * 32768.0f ) truncates toward
	   zero, which averages 0.50 LSB of error against 0.25 for round-to-
	   nearest and peaks at 1.0 LSB against 0.5 - measured over 200k
	   coefficients. Scale by 32767 so the rounded result cannot reach 32768,
	   which does not fit in a short and would wrap to -32768.

	   This is below the 16-bit noise floor on its own (0.5 LSB of 32768 is
	   -96 dBFS), but reverb feedback gains are applied once per pass around
	   the FDN loop, so the error compounds with decay time: about -0.0065 dB
	   over a 3 second RT60. Still inaudible; it is fixed because a defined
	   choke point costs nothing to get right.
	*/
	if ( v <= -0.999969f ) return -32767;
	if ( v >=  0.999969f ) return  32767;
	int q = (int)( v * 32767.0f + ( v >= 0.0f ? 0.5f : -0.5f ) );
	if ( q >  32767 ) q =  32767;
	if ( q < -32767 ) q = -32767;
	return (short)q;
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
	static const int lens32[REVERB_LINES] = { 1187, 1399, 1637, 1973, 2309, 2591, 2903, 3119 };
	static const int lens44[REVERB_LINES] = { 1637, 1913, 2251, 2707, 3169, 3571, 4001, 4297 };
	static const int lens48[REVERB_LINES] = { 1783, 2083, 2459, 2953, 3449, 3889, 4357, 4679 };
	static const int lens96[REVERB_LINES] = { 3571, 4177, 4903, 5897, 6899, 7789, 8713, 9371 };
	static const int apl32[2] = { 241, 331 };
	static const int apl44[2] = { 331, 449 };
	static const int apl48[2] = { 359, 487 };
	static const int apl96[2] = { 719, 977 };
	static const int eofs32[REVERB_EARLY_TAPS] = { 257, 659, 1031, 1361, 1693, 2237 };
	static const int eofs44[REVERB_EARLY_TAPS] = { 353, 907, 1409, 1861, 2311, 3079 };
	static const int eofs48[REVERB_EARLY_TAPS] = { 383, 991, 1531, 2027, 2521, 3347 };
	static const int eofs96[REVERB_EARLY_TAPS] = { 769, 1979, 3067, 4051, 5039, 6703 };

	const int *lens = lens44, *apl = apl44, *eofs = eofs44;
	if ( snd_SampleRate() == 96000 ) {
		lens = lens96; apl = apl96; eofs = eofs96;
	} else if ( snd_SampleRate() == 48000 ) {
		lens = lens48; apl = apl48; eofs = eofs48;
	} else if ( snd_SampleRate() == 32000 ) {
		lens = lens32; apl = apl32; eofs = eofs32;
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
	for ( int i = 0; i < REVERB_LINES; i++ ) {
		densityTapQ16[i] = densityTargetQ16[i] = (unsigned)lineLen[i] << 16;
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
		RVB_LERP(gainLF);
		RVB_LERP(decayTime); RVB_LERP(decayHFRatio); RVB_LERP(decayLFRatio);
		RVB_LERP(reflectionsGain); RVB_LERP(reflectionsDelay);
		RVB_LERP(lateReverbGain); RVB_LERP(lateReverbDelay);
		RVB_LERP(echoTime); RVB_LERP(echoDepth);
		RVB_LERP(modulationTime); RVB_LERP(modulationDepth);
		RVB_LERP(airAbsorptionGainHF);
		RVB_LERP(hfReference); RVB_LERP(lfReference);
		RVB_LERP(roomRolloffFactor);
		#undef RVB_LERP
		cur.decayHFLimit = target.decayHFLimit;	// 0/1 flag: snap, no lerp
		xfadeBlocksLeft--;
	}

	// clamp inputs defensively (efx files are data)
	float decay = cur.decayTime   < 0.1f ? 0.1f : ( cur.decayTime > 20.0f ? 20.0f : cur.decayTime );
	float hfr   = cur.decayHFRatio< 0.1f ? 0.1f : ( cur.decayHFRatio > 2.0f ? 2.0f : cur.decayHFRatio );
	float lfr   = cur.decayLFRatio< 0.1f ? 0.1f : ( cur.decayLFRatio > 2.0f ? 2.0f : cur.decayLFRatio );

	/*
	   decayHFLimit: when set (and air absorption is actually absorbing),
	   cap the HF decay so it never exceeds what propagation through the
	   room's air would allow: solving the decay equation for the distance
	   at which air absorption alone gives -60 dB and converting through
	   the speed of sound, limitRatio = -3 / (c * log10(g) * T60). The
	   line length cancels, so one clamp covers all lines. Same
	   relationship the EAX spec defines and OpenAL Soft implements.
	*/
	if ( cur.decayHFLimit && cur.airAbsorptionGainHF < 0.9999f ) {
		float lg = log10f( cur.airAbsorptionGainHF < 0.892f ? 0.892f : cur.airAbsorptionGainHF );
		float limitRatio = -3.0f / ( 343.3f * lg * decay );
		if ( limitRatio < hfr ) {
			hfr = limitRatio < 0.1f ? 0.1f : limitRatio;
		}
	}

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

	/*
	   LF decay band: a first-order low-shelf inside each feedback line.
	   The shelf passes the sample and adds lfAdj times its low-passed
	   component, so the per-line loop gain is fbGain at HF and
	   fbGain*(1+lfAdj) at DC. lfAdj is derived from the ratio of the LF
	   target gain to the mid gain for a representative line and clamped so
	   the worst-case DC loop gain stays at or below the same 0.9995 the
	   mid band is held to - that inequality is the entire stability proof
	   for the integer path, so it is enforced here, not assumed.
	*/
	{
		float gMidRep = powf( 10.0f, -3.0f * lineLen[REVERB_LINES/2] / ( decay * REVERB_FS ) );
		float gLFRep  = powf( 10.0f, -3.0f * lineLen[REVERB_LINES/2] / ( decay * lfr * REVERB_FS ) );
		float adj = ( gMidRep > 0.0001f ) ? ( gLFRep / gMidRep - 1.0f ) : 0.0f;
		float worstMid = fbGain[REVERB_LINES-1] > fbGain[0] ? fbGain[REVERB_LINES-1] : fbGain[0];
		if ( adj > 0.0f && worstMid * ( 1.0f + adj ) > 0.9995f ) {
			adj = 0.9995f / worstMid - 1.0f;
			if ( adj < 0.0f ) adj = 0.0f;
		}
		if ( adj < -0.95f ) adj = -0.95f;
		lfAdj  = adj;
		/*
		   Quantize the magnitude and carry the sign separately. QMUL's
		   toward-zero identity only holds for non-negative gains - with a
		   negative coefficient the scalar macro rounds toward -inf for
		   positive samples and +inf for negative ones, and the vector
		   kernel's unsigned widening multiply is simply wrong. lfAdj is
		   the one signed gain in the topology (decayLFRatio < 1), so it
		   pays the split; every other gain is non-negative by
		   construction.
		*/
		{
			float am = adj < 0.0f ? -adj : adj;
			lfAdjQ   = Snd_ReverbCoefQ15( am > 0.999f ? 0.999f : am );
			lfAdjNeg = adj < 0.0f;
		}
		// one-pole coefficient at lfReference (see the damping mapping above
		// for the same first-order approximation style)
		float lref = cur.lfReference < 20.0f ? 20.0f : ( cur.lfReference > 1000.0f ? 1000.0f : cur.lfReference );
		// exact one-pole design: c = exp(-2*pi*fc/fs). The small-angle
		// form 1 - 2*pi*fc/fs drifts sharply above ~1kHz (a 5kHz corner
		// lands near 8.7kHz), which mattered for the output split below.
		float c = expf( -6.2831853f * lref / REVERB_FS );
		if ( c < 0.0f ) c = 0.0f;
		if ( c > 0.9995f ) c = 0.9995f;
		lfLoopCoef  = c;
		lfLoopCoefQ = Snd_ReverbCoefQ15( c );
	}

	/*
	   Output band split for gainLF / gainHF: low = one-pole LP at
	   lfReference, high = signal minus one-pole LP at hfReference, mid the
	   remainder; out = gainLF*low + mid + gainHF*high. This replaces the
	   old broadband gainHF fold into earlyGain, which attenuated the whole
	   wet signal instead of its high band.
	*/
	{
		float lref = cur.lfReference < 20.0f ? 20.0f : ( cur.lfReference > 1000.0f ? 1000.0f : cur.lfReference );
		float href = cur.hfReference < 1000.0f ? 1000.0f : ( cur.hfReference > 20000.0f ? 20000.0f : cur.hfReference );
		float cl = expf( -6.2831853f * lref / REVERB_FS );
		float ch = expf( -6.2831853f * href / REVERB_FS );
		if ( cl < 0.0f ) cl = 0.0f; if ( cl > 0.9995f ) cl = 0.9995f;
		if ( ch < 0.0f ) ch = 0.0f; if ( ch > 0.9995f ) ch = 0.9995f;
		osLowCoef = cl; osHighCoef = ch;
		osGainLF = cur.gainLF < 0.0f ? 0.0f : ( cur.gainLF > 1.0f ? 1.0f : cur.gainLF );
		osGainHF = cur.gainHF < 0.0f ? 0.0f : ( cur.gainHF > 1.0f ? 1.0f : cur.gainHF );
		osLowCoefQ = Snd_ReverbCoefQ15( cl ); osHighCoefQ = Snd_ReverbCoefQ15( ch );
		osGainLFQ = Snd_ReverbCoefQ15( osGainLF ); osGainHFQ = Snd_ReverbCoefQ15( osGainHF );
	}

	/*
	   Echo: parallel feedback comb at echoTime, feedback set from the same
	   RT60 relationship as the FDN lines so the pulses die with the room,
	   output weighted by echoDepth. Depth 0 skips the stage entirely in
	   the process loops.
	*/
	{
		float et = cur.echoTime < 0.075f ? 0.075f : ( cur.echoTime > 0.25f ? 0.25f : cur.echoTime );
		echoLen = (int)( et * REVERB_FS );
		if ( echoLen > REVERB_ECHO_LEN - 2 ) echoLen = REVERB_ECHO_LEN - 2;
		if ( echoLen < 64 ) echoLen = 64;
		float g = powf( 10.0f, -3.0f * echoLen / ( decay * REVERB_FS ) );
		if ( g > 0.9995f ) g = 0.9995f;
		echoFbGain  = g;
		echoFbGainQ = Snd_ReverbCoefQ15( g );
		float d = cur.echoDepth < 0.0f ? 0.0f : ( cur.echoDepth > 1.0f ? 1.0f : cur.echoDepth );
		echoOutGain  = d;
		echoOutGainQ = Snd_ReverbCoefQ15( d > 0.999f ? 0.999f : d );
	}

	/*
	   Modulation: LFO on the FDN read taps. Depth in samples follows the
	   EAXREVERB scale (0.05/4 * modulationTime * depth * fs, the OpenAL
	   Soft relationship); the excursion is subtractive only so the tap
	   never asks for more history than the line holds. Phase advances by a
	   Q32 integer increment - a pure function of the parameters and the
	   frame index, so the fixed path stays bit-exact.

	   Density scales the same read taps: cbrt(density) of the line length
	   (delay-length multiplier per EFX, applied downward within our fixed
	   prime buffers rather than upward with reallocation), slewed at
	   REVERB_TAP_SLEW_Q16 per sample. density 1 with modulation off gives
	   a whole-sample tap equal to the line length: the exact legacy read,
	   bit-identical to the pre-feature reverb.
	*/
	{
		float mt = cur.modulationTime < 0.04f ? 0.04f : ( cur.modulationTime > 4.0f ? 4.0f : cur.modulationTime );
		float md = cur.modulationDepth < 0.0f ? 0.0f : ( cur.modulationDepth > 1.0f ? 1.0f : cur.modulationDepth );
		modPhaseInc = (unsigned)( 4294967296.0 / ( (double)mt * REVERB_FS ) );
		modDepthQ16 = (int)( ( 0.05f / 4.0f ) * mt * md * REVERB_FS * 65536.0f );
		float dens = cur.density < 0.001f ? 0.001f : ( cur.density > 1.0f ? 1.0f : cur.density );
		float mult = cbrtf( dens );
		if ( mult < 0.35f ) mult = 0.35f;
		for ( int i = 0; i < REVERB_LINES; i++ ) {
			unsigned t = (unsigned)( (double)lineLen[i] * mult * 65536.0 );
			// keep the tap inside [16 samples, lineLen], and leave room for
			// the modulation excursion plus the interpolation neighbour
			int maxT = ( lineLen[i] << 16 );
			int minT = ( 16 << 16 ) + modDepthQ16;
			if ( (int)t > maxT ) t = (unsigned)maxT;
			if ( (int)t < minT ) t = (unsigned)( minT < maxT ? minT : maxT );
			densityTargetQ16[i] = t;
		}
	}

	// gainHF no longer folded in broadband: the output band split applies
	// it to the high band only, which is what the parameter means
	earlyGain  = cur.gain * cur.reflectionsGain;
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

	const bool echoOn = echoOutGain > 0.0001f;
	const bool modOn  = modDepthQ16 > 0;

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

		// FDN: read line outputs at the (slewed, possibly modulated)
		// fractional tap, low-shelf for the LF decay band, one-pole damp
		// for the HF band, Householder feedback
		float out[REVERB_LINES]; float sum = 0.0f;
		for ( int l = 0; l < REVERB_LINES; l++ ) {
			unsigned tap = densityTapQ16[l];
			if ( tap != densityTargetQ16[l] ) {
				if ( tap < densityTargetQ16[l] ) {
					tap += REVERB_TAP_SLEW_Q16;
					if ( tap > densityTargetQ16[l] ) tap = densityTargetQ16[l];
				} else {
					tap -= REVERB_TAP_SLEW_Q16;
					if ( tap < densityTargetQ16[l] ) tap = densityTargetQ16[l];
				}
				densityTapQ16[l] = tap;
			}
			if ( modOn ) {
				// per-line phase offset decorrelates the lines; excursion
				// is (1+sin)/2 * depth, subtractive only
				unsigned ph = ( ( modPhase >> 16 ) + (unsigned)l * 8192u ) & 65535u;
				int sq = Snd_ReverbSinQ15i( ph );
				tap -= (unsigned)( ( (long long)modDepthQ16 * ( 32768 + sq ) ) >> 16 );
			}
			int intD = (int)( tap >> 16 );
			unsigned frac = tap & 0xffffu;
			unsigned base = linePos[l] + (unsigned)lineLen[l];
			float v;
			float a0 = lineF[l][( base - (unsigned)intD ) % (unsigned)lineLen[l]];
			if ( frac == 0 ) {
				v = a0;		// whole-sample tap: the exact legacy read
			} else {
				float b0 = lineF[l][( base - (unsigned)intD - 1u ) % (unsigned)lineLen[l]];
				v = a0 + ( b0 - a0 ) * ( frac * ( 1.0f / 65536.0f ) );
			}
			// LF decay shelf: pass v, add lfAdj times its low band
			lfLoopF[l] += ( 1.0f - lfLoopCoef ) * ( v - lfLoopF[l] );
			v += lfAdj * lfLoopF[l];
			// one-pole LP in the loop; alpha = 1-dampCoef so dampCoef 0
			// means transparent (out follows v exactly), not silent. The
			// original used dampCoef as the alpha directly, which inverted
			// the "higher = darker" mapping and, at the clamp value 0
			// (any decayHFRatio >= 1), froze the state at zero and gated
			// the whole late reverb - unexercised until efx content ran.
			dampF[l] += ( 1.0f - dampCoef ) * ( v - dampF[l] );
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

		if ( echoOn ) {
			unsigned p = (unsigned)( prePos % (unsigned)echoLen );
			float e = echoF[p];
			echoF[p] = x + echoFbGain * e;
			lL += e * echoOutGain;
			lR += e * echoOutGain;
		}

		float wL = eL * eG + lL * lG;
		float wR = eR * eG + lR * lG;

		// output band split, two cascaded one-poles per side (12 dB/oct):
		// a single pole leaks so much of a 10kHz tone into the mid band
		// that gainHF could only ever cut it by half
		osLowF[0]   += ( 1.0f - osLowCoef )  * ( wL - osLowF[0] );
		osLowF[1]   += ( 1.0f - osLowCoef )  * ( wR - osLowF[1] );
		osLow2F[0]  += ( 1.0f - osLowCoef )  * ( osLowF[0] - osLow2F[0] );
		osLow2F[1]  += ( 1.0f - osLowCoef )  * ( osLowF[1] - osLow2F[1] );
		osHighF[0]  += ( 1.0f - osHighCoef ) * ( wL - osHighF[0] );
		osHighF[1]  += ( 1.0f - osHighCoef ) * ( wR - osHighF[1] );
		osHigh2F[0] += ( 1.0f - osHighCoef ) * ( osHighF[0] - osHigh2F[0] );
		osHigh2F[1] += ( 1.0f - osHighCoef ) * ( osHighF[1] - osHigh2F[1] );
		float loL = osLow2F[0], loR = osLow2F[1];
		float hiL = wL - osHigh2F[0], hiR = wR - osHigh2F[1];
		float midL = wL - loL - hiL, midR = wR - loR - hiR;

		dest[i*2+0] += osGainLF * loL + midL + osGainHF * hiL;
		dest[i*2+1] += osGainLF * loR + midR + osGainHF * hiR;
		prePos++;
		apPos[0]++; apPos[1]++;
		modPhase += modPhaseInc;
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

	const bool echoOn = echoOutGainQ > 0;
	const bool modOn  = modDepthQ16 > 0;

#if defined(__SSE2__) && !defined(REVERB_FORCE_SCALAR)
	/*
	   The line stage vectorized 4 lanes at a time. Measured against this
	   compiler's own -O3 output first: the scalar QMUL's sign conditional
	   keeps the whole stage scalar (2032 insns, one packed op, 193
	   branches in the loop body), so unlike the mix kernels this is not a
	   fight with the autovectorizer. Gathers, taps and the LFO stay
	   scalar; the shelf/damp/feedback arithmetic runs in 2x4 int32 lanes
	   with the branch-free QMUL above, and the state vectors are hoisted
	   across the frame loop. Bit-exactness against the scalar path is
	   asserted by the bench, not assumed.
	*/
	__m128i lfv0   = _mm_loadu_si128( (const __m128i *)&lfLoopI[0] );
	__m128i lfv1   = _mm_loadu_si128( (const __m128i *)&lfLoopI[4] );
	__m128i dampv0 = _mm_loadu_si128( (const __m128i *)&dampI[0] );
	__m128i dampv1 = _mm_loadu_si128( (const __m128i *)&dampI[4] );
	__m128i fbg0   = _mm_set_epi32( fbGainQ[3], fbGainQ[2], fbGainQ[1], fbGainQ[0] );
	__m128i fbg1   = _mm_set_epi32( fbGainQ[7], fbGainQ[6], fbGainQ[5], fbGainQ[4] );
	const __m128i lfAlpha   = _mm_set1_epi32( 32767 - lfLoopCoefQ );
	const __m128i lfAdjV    = _mm_set1_epi32( lfAdjQ );
	const __m128i lfAdjSgn  = _mm_set1_epi32( lfAdjNeg ? -1 : 0 );
	const __m128i dampAlpha = _mm_set1_epi32( 32767 - dampCoefQ );
#endif

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
#if defined(__SSE2__) && !defined(REVERB_FORCE_SCALAR)
		int a0A[REVERB_LINES], dA[REVERB_LINES], fracA[REVERB_LINES];
		for ( int l = 0; l < REVERB_LINES; l++ ) {
			unsigned tap = densityTapQ16[l];
			if ( tap != densityTargetQ16[l] ) {
				if ( tap < densityTargetQ16[l] ) {
					tap += REVERB_TAP_SLEW_Q16;
					if ( tap > densityTargetQ16[l] ) tap = densityTargetQ16[l];
				} else {
					tap -= REVERB_TAP_SLEW_Q16;
					if ( tap < densityTargetQ16[l] ) tap = densityTargetQ16[l];
				}
				densityTapQ16[l] = tap;
			}
			if ( modOn ) {
				unsigned ph = ( ( modPhase >> 16 ) + (unsigned)l * 8192u ) & 65535u;
				int sq = Snd_ReverbSinQ15i( ph );
				tap -= (unsigned)( ( (long long)modDepthQ16 * ( 32768 + sq ) ) >> 16 );
			}
			int intD = (int)( tap >> 16 );
			unsigned base = linePos[l] + (unsigned)lineLen[l];
			/*
			   b0 is gathered unconditionally: at frac == 0 (the density-1,
			   modulation-off whole-sample tap) the scalar path skips this
			   load, but the wrapped index still lands inside the buffer
			   and the interpolation multiplies the delta by zero, so the
			   result is exactly a0 either way.
			*/
			int a0 = lineI[l][( base - (unsigned)intD ) % (unsigned)lineLen[l]];
			int b0 = lineI[l][( base - (unsigned)intD - 1u ) % (unsigned)lineLen[l]];
			a0A[l] = a0;
			dA[l]  = b0 - a0;
			fracA[l] = (int)( tap & 0xffffu );
		}
		__m128i a0v0 = _mm_loadu_si128( (const __m128i *)&a0A[0] );
		__m128i a0v1 = _mm_loadu_si128( (const __m128i *)&a0A[4] );
		__m128i dv0  = _mm_loadu_si128( (const __m128i *)&dA[0] );
		__m128i dv1  = _mm_loadu_si128( (const __m128i *)&dA[4] );
		__m128i frv0 = _mm_loadu_si128( (const __m128i *)&fracA[0] );
		__m128i frv1 = _mm_loadu_si128( (const __m128i *)&fracA[4] );
		// v = a0 + toward-zero( d * frac >> 16 ): the interp step
		__m128i vv0 = _mm_add_epi32( a0v0, Snd_ReverbQmul4( dv0, frv0, 16 ) );
		__m128i vv1 = _mm_add_epi32( a0v1, Snd_ReverbQmul4( dv1, frv1, 16 ) );
		// LF decay shelf (loop gain bound enforced in StepBlockParams)
		lfv0 = _mm_add_epi32( lfv0, Snd_ReverbQmul4( _mm_sub_epi32( vv0, lfv0 ), lfAlpha, 15 ) );
		lfv1 = _mm_add_epi32( lfv1, Snd_ReverbQmul4( _mm_sub_epi32( vv1, lfv1 ), lfAlpha, 15 ) );
		{
			__m128i t0 = Snd_ReverbQmul4( lfv0, lfAdjV, 15 );
			__m128i t1 = Snd_ReverbQmul4( lfv1, lfAdjV, 15 );
			t0 = _mm_sub_epi32( _mm_xor_si128( t0, lfAdjSgn ), lfAdjSgn );
			t1 = _mm_sub_epi32( _mm_xor_si128( t1, lfAdjSgn ), lfAdjSgn );
			vv0 = _mm_add_epi32( vv0, t0 );
			vv1 = _mm_add_epi32( vv1, t1 );
		}
		// damping one-pole
		dampv0 = _mm_add_epi32( dampv0, Snd_ReverbQmul4( _mm_sub_epi32( vv0, dampv0 ), dampAlpha, 15 ) );
		dampv1 = _mm_add_epi32( dampv1, Snd_ReverbQmul4( _mm_sub_epi32( vv1, dampv1 ), dampAlpha, 15 ) );
		_mm_storeu_si128( (__m128i *)&out[0], dampv0 );
		_mm_storeu_si128( (__m128i *)&out[4], dampv1 );
		for ( int l = 0; l < REVERB_LINES; l++ ) {
			sum += out[l];	// int64 accumulate: identical to the scalar path
		}
		int sumQ = (int)( sum >> 2 );	// 2/8 exactly
		__m128i sumv = _mm_set1_epi32( sumQ );
		__m128i xv   = _mm_set1_epi32( x );
		__m128i fb0  = _mm_add_epi32( xv, Snd_ReverbQmul4( _mm_sub_epi32( dampv0, sumv ), fbg0, 15 ) );
		__m128i fb1  = _mm_add_epi32( xv, Snd_ReverbQmul4( _mm_sub_epi32( dampv1, sumv ), fbg1, 15 ) );
		int fbA[REVERB_LINES];
		_mm_storeu_si128( (__m128i *)&fbA[0], fb0 );
		_mm_storeu_si128( (__m128i *)&fbA[4], fb1 );
		for ( int l = 0; l < REVERB_LINES; l++ ) {
			unsigned p = linePos[l] % (unsigned)lineLen[l];
			lineI[l][p] = fbA[l];
			linePos[l]++;
		}
#else
		for ( int l = 0; l < REVERB_LINES; l++ ) {
			unsigned tap = densityTapQ16[l];
			if ( tap != densityTargetQ16[l] ) {
				if ( tap < densityTargetQ16[l] ) {
					tap += REVERB_TAP_SLEW_Q16;
					if ( tap > densityTargetQ16[l] ) tap = densityTargetQ16[l];
				} else {
					tap -= REVERB_TAP_SLEW_Q16;
					if ( tap < densityTargetQ16[l] ) tap = densityTargetQ16[l];
				}
				densityTapQ16[l] = tap;
			}
			if ( modOn ) {
				unsigned ph = ( ( modPhase >> 16 ) + (unsigned)l * 8192u ) & 65535u;
				int sq = Snd_ReverbSinQ15i( ph );
				tap -= (unsigned)( ( (long long)modDepthQ16 * ( 32768 + sq ) ) >> 16 );
			}
			int intD = (int)( tap >> 16 );
			unsigned frac = tap & 0xffffu;
			unsigned base = linePos[l] + (unsigned)lineLen[l];
			int a0 = lineI[l][( base - (unsigned)intD ) % (unsigned)lineLen[l]];
			int v;
			if ( frac == 0 ) {
				v = a0;		// whole-sample tap: the exact legacy read
			} else {
				/*
				   Linear interpolation between two history samples is a
				   convex combination: |v| <= max(|a0|,|b0|) regardless of
				   the delta rounding, so the read cannot expand the loop -
				   the contraction proof needs no new argument here. The
				   delta product truncates toward zero, matching QMUL.
				*/
				int b0 = lineI[l][( base - (unsigned)intD - 1u ) % (unsigned)lineLen[l]];
				int d = b0 - a0;
				int step = d >= 0 ? (int)( ( (long long)d * frac ) >> 16 )
				                  : -(int)( ( -(long long)d * frac ) >> 16 );
				v = a0 + step;
			}
			// LF decay shelf: one-pole low band, added at lfAdj. Loop gain
			// at DC = fbGain*(1+lfAdj) <= 0.9995 (enforced in
			// StepBlockParams), QMUL truncation keeps the contraction.
			lfLoopI[l] += QMUL( v - lfLoopI[l], (short)( 32767 - lfLoopCoefQ ) );
			{
				int t = QMUL( lfLoopI[l], lfAdjQ );
				v += lfAdjNeg ? -t : t;
			}
			dampI[l] += QMUL( v - dampI[l], (short)( 32767 - dampCoefQ ) );
			out[l] = dampI[l];
			sum += out[l];
		}
		int sumQ = (int)( sum >> 2 );	// 2/8 exactly
		for ( int l = 0; l < REVERB_LINES; l++ ) {
			unsigned p = linePos[l] % (unsigned)lineLen[l];
			lineI[l][p] = x + QMUL( out[l] - sumQ, fbGainQ[l] );
			linePos[l]++;
		}
#endif

		int lL = out[0] + out[2] + out[4] + out[6];
		int lR = out[1] + out[3] + out[5] + out[7];

		if ( echoOn ) {
			unsigned p = (unsigned)( prePos % (unsigned)echoLen );
			int e = echoI[p];
			echoI[p] = x + QMUL( e, echoFbGainQ );	// toward-zero: contracts
			lL += QMUL( e, echoOutGainQ );
			lR += QMUL( e, echoOutGainQ );
		}

		int wL = QMUL( eL, earlyGainQ ) + QMUL( lL, lateGainQ );
		int wR = QMUL( eR, earlyGainQ ) + QMUL( lR, lateGainQ );

		// output band split, two cascaded one-poles per side (12 dB/oct);
		// feed-forward only, no stability interaction
		osLowI[0]   += QMUL( wL - osLowI[0],  (short)( 32767 - osLowCoefQ ) );
		osLowI[1]   += QMUL( wR - osLowI[1],  (short)( 32767 - osLowCoefQ ) );
		osLow2I[0]  += QMUL( osLowI[0] - osLow2I[0],  (short)( 32767 - osLowCoefQ ) );
		osLow2I[1]  += QMUL( osLowI[1] - osLow2I[1],  (short)( 32767 - osLowCoefQ ) );
		osHighI[0]  += QMUL( wL - osHighI[0], (short)( 32767 - osHighCoefQ ) );
		osHighI[1]  += QMUL( wR - osHighI[1], (short)( 32767 - osHighCoefQ ) );
		osHigh2I[0] += QMUL( osHighI[0] - osHigh2I[0], (short)( 32767 - osHighCoefQ ) );
		osHigh2I[1] += QMUL( osHighI[1] - osHigh2I[1], (short)( 32767 - osHighCoefQ ) );
		int loL = osLow2I[0], loR = osLow2I[1];
		int hiL = wL - osHigh2I[0], hiR = wR - osHigh2I[1];
		int midL = wL - loL - hiL, midR = wR - loR - hiR;

		int oL = QMUL( loL, osGainLFQ ) + midL + QMUL( hiL, osGainHFQ );
		int oR = QMUL( loR, osGainLFQ ) + midR + QMUL( hiR, osGainHFQ );

		destAccum[i*2+0] += QMUL( oL, wetQ );
		destAccum[i*2+1] += QMUL( oR, wetQ );
		prePos++;
		apPos[0]++; apPos[1]++;
		modPhase += modPhaseInc;
	}
#if defined(__SSE2__) && !defined(REVERB_FORCE_SCALAR)
	_mm_storeu_si128( (__m128i *)&lfLoopI[0], lfv0 );
	_mm_storeu_si128( (__m128i *)&lfLoopI[4], lfv1 );
	_mm_storeu_si128( (__m128i *)&dampI[0],   dampv0 );
	_mm_storeu_si128( (__m128i *)&dampI[4],   dampv1 );
#endif
	#undef QMUL
}

#endif /* !__SND_REVERB_H__ */
