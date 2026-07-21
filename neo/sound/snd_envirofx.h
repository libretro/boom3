// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __SND_ENVIROFX_H__
#define __SND_ENVIROFX_H__

/*
   Enviro-suit muffling (the ROE suit sections): per stereo channel, a
   resonant one-stage biquad low-pass at s_enviroSuitCutoffFreq /
   s_enviroSuitCutoffQ followed by two feedback combs whose lengths come
   from s_reverbTime plus small per-channel offsets - the same chain the
   original DoEnviroSuit built out of SoundFX objects, reimplemented with
   the dual float / bit-exact-integer discipline of the rest of the mixer.

   Faithful-port notes, where the original's behavior is preserved on
   purpose:
   - the input volume scale (s_enviroSuitVolumeScale) applies at the input
     of every stage, so three stages scale by its cube - that compounding
     is what the shipped effect sounded like;
   - comb lengths are the cvar values in samples at 44.1kHz; they scale by
     the output rate so the comb pitch does not shift with the rate
     option.

   Original defects not preserved:
   - the continuity reseed was off by one sample per block (samples n-2
     and n-3 instead of n-1 and n-2), the same defect fixed in the
     slow-motion channel;
   - the state memsets cleared 10000 floats into buffers of 8196.
   Both are simply absent from this structure: state lives in members and
   persists across blocks, which is what the continuity dance was
   approximating.

   Integer path notes: the biquad's feedback coefficients exceed unity
   (b1 approaches -2 for a low cutoff), so they quantize at Q13 (+-4
   headroom) through one choke point, with the recursion in int64 and
   magnitude truncation (see the loop comment); the poles of the cvar-range filters sit well inside
   the unit circle and a +-1 LSB Q13 perturbation (1.2e-4) cannot move
   them out, and the states saturate defensively anyway. The combs use
   the reverb's toward-zero QMUL so their tails decay to exact zero.
*/

extern int snd_sampleRate;

#define ENVIRO_COMB_LEN		4096	// (1000+505 samples)*96k/44.1k = 3276 max
#define ENVIRO_CHANNELS		2

class idEnviroSuitFX {
public:
	void	Init( void );
	// derive coefficients from the cvars; call once per mix block
	void	SetParms( float cutoffHz, float cutoffQ, float volumeScale,
	                  float combTime, float combFeedback );
	// stereo interleaved, in place. Float: normalized mix buffer.
	void	ProcessFloat( float *samples, int numFrames );
	// stereo interleaved int16-scale int32 accumulator, in place.
	void	ProcessS16( int *samples, int numFrames );

private:
	// derived per block
	float	a1, a2, a3, b1, b2;		// biquad (feed a1..a3, back b1,b2)
	float	vol, combFb;
	int		combLen[ENVIRO_CHANNELS][2];
	// Q13 twins for the biquad (headroom for |b1| < 2), Q15 for the rest
	int		a1Q, a2Q, a3Q, b1Q, b2Q;	// Q13
	short	volQ, combFbQ;				// Q15

	// state, float path
	float	bqXF[ENVIRO_CHANNELS][2], bqYF[ENVIRO_CHANNELS][2];
	float	combF[ENVIRO_CHANNELS][2][ENVIRO_COMB_LEN];
	// state, fixed path
	int		bqXI[ENVIRO_CHANNELS][2], bqYI[ENVIRO_CHANNELS][2];
	int		combI[ENVIRO_CHANNELS][2][ENVIRO_COMB_LEN];
	unsigned combPos[ENVIRO_CHANNELS][2];
};

ID_INLINE void idEnviroSuitFX::Init( void ) {
	memset( this, 0, sizeof( *this ) );
	SetParms( 2000.0f, 2.0f, 0.9f, 1000.0f, 0.333f );
}

ID_INLINE void idEnviroSuitFX::SetParms( float cutoffHz, float cutoffQ,
		float volumeScale, float combTime, float combFeedback ) {
	// clamp cvar inputs to sane filter territory
	if ( cutoffHz < 100.0f ) cutoffHz = 100.0f;
	float ny = 0.45f * snd_sampleRate;
	if ( cutoffHz > ny ) cutoffHz = ny;
	if ( cutoffQ < 0.3f ) cutoffQ = 0.3f;
	if ( cutoffQ > 8.0f ) cutoffQ = 8.0f;

	// the original SoundFX_Lowpass form: c = 1/tan(pi*f/fs), resonance = Q
	float c = 1.0f / tanf( 3.14159265f * cutoffHz / snd_sampleRate );
	float inv = 1.0f / ( 1.0f + cutoffQ * c + c * c );
	a1 = inv;
	a2 = 2.0f * inv;
	a3 = inv;
	b1 = 2.0f * ( 1.0f - c * c ) * inv;
	b2 = ( 1.0f - cutoffQ * c + c * c ) * inv;

	vol = volumeScale < 0.0f ? 0.0f : ( volumeScale > 1.0f ? 1.0f : volumeScale );
	combFb = combFeedback < 0.0f ? 0.0f : ( combFeedback > 0.98f ? 0.98f : combFeedback );

	/*
	   Comb lengths: the cvar is samples at the original 44.1kHz; scale to
	   the output rate so the effect's pitch survives the rate option.
	   Per-channel offsets match the original fx construction: channel i
	   got combs at +i*100 and +i*100+5.
	*/
	for ( int ch = 0; ch < ENVIRO_CHANNELS; ch++ ) {
		for ( int k = 0; k < 2; k++ ) {
			float base = combTime + ch * 100.0f + ( k ? 5.0f : 0.0f );
			int len = (int)( base * snd_sampleRate / 44100.0f );
			if ( len < 32 ) len = 32;
			if ( len > ENVIRO_COMB_LEN - 1 ) len = ENVIRO_COMB_LEN - 1;
			combLen[ch][k] = len;
		}
	}

	// single choke points: Q13 for the biquad, Q15 for the gains
	#define EQ13(v) (int)( (v) * 8192.0f + ( (v) >= 0.0f ? 0.5f : -0.5f ) )
	a1Q = EQ13( a1 ); a2Q = EQ13( a2 ); a3Q = EQ13( a3 );
	b1Q = EQ13( b1 ); b2Q = EQ13( b2 );
	#undef EQ13
	volQ    = Snd_ReverbCoefQ15( vol );
	combFbQ = Snd_ReverbCoefQ15( combFb );
}

ID_INLINE void idEnviroSuitFX::ProcessFloat( float *samples, int numFrames ) {
	for ( int ch = 0; ch < ENVIRO_CHANNELS; ch++ ) {
		float x1 = bqXF[ch][0], x2 = bqXF[ch][1];
		float y1 = bqYF[ch][0], y2 = bqYF[ch][1];
		for ( int i = 0; i < numFrames; i++ ) {
			// stage 1: volume-scaled input through the resonant lowpass
			float x = samples[i * 2 + ch] * vol;
			float y = a1 * x + a2 * x1 + a3 * x2 - b1 * y1 - b2 * y2;
			x2 = x1; x1 = x;
			y2 = y1; y1 = y;
			// stages 2,3: the two combs, volume scale at each input
			for ( int k = 0; k < 2; k++ ) {
				float cx = y * vol;
				unsigned p = combPos[ch][k] % (unsigned)combLen[ch][k];
				float d = combF[ch][k][p];
				combF[ch][k][p] = d * combFb + cx;
				combPos[ch][k]++;
				y = d;
			}
			samples[i * 2 + ch] = y;
		}
		bqXF[ch][0] = x1; bqXF[ch][1] = x2;
		bqYF[ch][0] = y1; bqYF[ch][1] = y2;
	}
}

ID_INLINE void idEnviroSuitFX::ProcessS16( int *samples, int numFrames ) {
	// toward-zero, the reverb's comb discipline: tails decay to exact zero
	#define EQMUL(x,g) ( (x) >= 0 ? (int)( ( (long long)(x) * (g) ) >> 15 ) \
	                              : -(int)( ( -(long long)(x) * (g) ) >> 15 ) )
	for ( int ch = 0; ch < ENVIRO_CHANNELS; ch++ ) {
		int x1 = bqXI[ch][0], x2 = bqXI[ch][1];
		int y1 = bqYI[ch][0], y2 = bqYI[ch][1];
		for ( int i = 0; i < numFrames; i++ ) {
			int x = EQMUL( samples[i * 2 + ch], volQ );
			/*
			   Q13 biquad in int64, one symmetric round of the whole
			   accumulation; states saturated to int16-scale headroom so a
			   pathological cvar combination cannot wrap the recursion.
			*/
			long long acc = (long long)a1Q * x + (long long)a2Q * x1 + (long long)a3Q * x2
			              - (long long)b1Q * y1 - (long long)b2Q * y2;
			/*
			   Toward-zero, not round-half-away: symmetric rounding in a
			   resonant quantized biquad sustains a granular limit cycle
			   (measured ~26 LSB of everlasting tone after silence);
			   magnitude truncation biases every quantization step toward
			   zero, which starves the cycle and lets the tail die. Same
			   reasoning as the reverb's QMUL, one octave up in filter
			   order.
			*/
			int y = acc >= 0 ? (int)( acc >> 13 ) : -(int)( ( -acc ) >> 13 );
			if ( y >  ( 1 << 24 ) ) y =  ( 1 << 24 );
			if ( y < -( 1 << 24 ) ) y = -( 1 << 24 );
			x2 = x1; x1 = x;
			y2 = y1; y1 = y;
			for ( int k = 0; k < 2; k++ ) {
				int cx = EQMUL( y, volQ );
				unsigned p = combPos[ch][k] % (unsigned)combLen[ch][k];
				int d = combI[ch][k][p];
				combI[ch][k][p] = EQMUL( d, combFbQ ) + cx;
				combPos[ch][k]++;
				y = d;
			}
			samples[i * 2 + ch] = y;
		}
		bqXI[ch][0] = x1; bqXI[ch][1] = x2;
		bqYI[ch][0] = y1; bqYI[ch][1] = y2;
	}
	#undef EQMUL
}

#endif /* !__SND_ENVIROFX_H__ */
