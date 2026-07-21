#include <initializer_list>
// snd_mix_bench.cpp - correctness + performance harness for snd_mix_kernels.h
// Compares: OLD generic (fixed-4096, /MIXBUFFER divisor), NEW scalar C,
// NEW SIMD (SSE2 or NEON per target). Verifies s16 kernels bit-exact
// between scalar and SIMD paths, and float kernels within ulp tolerance.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <chrono>
#if defined(__x86_64__) || defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>   // _mm_getcsr/_mm_setcsr for the FTZ tests
#endif

#define MIXBUFFER_SAMPLES 4096

#ifdef BENCH_SCALAR
#define SND_MIX_NO_SIMD 1
#endif
#include "../neo/sound/snd_mix_kernels.h"
#include "../neo/sound/snd_hrtf.h"

// ---- the OLD kernels, verbatim semantics (idSIMD_Generic) ----
static void OLD_MixSoundTwoSpeakerMono( float *mixBuffer, const float *samples, const int numSamples, const float lastV[2], const float currentV[2] ) {
	float sL = lastV[0], sR = lastV[1];
	float incL = ( currentV[0] - lastV[0] ) / MIXBUFFER_SAMPLES;
	float incR = ( currentV[1] - lastV[1] ) / MIXBUFFER_SAMPLES;
	for( int j = 0; j < MIXBUFFER_SAMPLES; j++ ) {
		mixBuffer[j*2+0] += samples[j] * sL;
		mixBuffer[j*2+1] += samples[j] * sR;
		sL += incL; sR += incR;
	}
}
static void OLD_MixedSoundToSamples( short *samples, const float *mixBuffer, const int numSamples ) {
	for ( int i = 0; i < numSamples; i++ ) {
		if ( mixBuffer[i] <= -32768.0f )      samples[i] = -32768;
		else if ( mixBuffer[i] >= 32767.0f )  samples[i] = 32767;
		else                                   samples[i] = (short)mixBuffer[i];
	}
}

static double now_ms() {
	return std::chrono::duration<double,std::milli>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

static uint32_t rng_state = 0x12345678u;
static uint32_t rng() { rng_state ^= rng_state<<13; rng_state ^= rng_state>>17; rng_state ^= rng_state<<5; return rng_state; }
static float frand_pm32k() { return ( (int)(rng() & 0xffff) - 32768 ) * 1.0f; }

int main() {
#if SND_MIX_SSE2
	const char *simd = "SSE2";
#elif SND_MIX_NEON
	const char *simd = "NEON";
#else
	const char *simd = "scalar";
#endif
	printf("kernel build: %s | ptr=%zu bits\n", simd, sizeof(void*)*8);

	static float src[MIXBUFFER_SAMPLES*2];
	static short srcS[MIXBUFFER_SAMPLES*2];
	static float dstA[MIXBUFFER_SAMPLES*2], dstB[MIXBUFFER_SAMPLES*2];
	static int   accA[MIXBUFFER_SAMPLES*2], accB[MIXBUFFER_SAMPLES*2];
	static short outA[MIXBUFFER_SAMPLES*2], outB[MIXBUFFER_SAMPLES*2];

	for (int i=0;i<MIXBUFFER_SAMPLES*2;i++) { src[i]=frand_pm32k(); srcS[i]=(short)(rng()&0xffff); }
	const float lastV[2]={0.31f,0.87f}, curV[2]={0.93f,0.12f};
	const int lastQ[2]={(int)(0.31f*32768),(int)(0.87f*32768)};
	const int curQ[2]={(int)(0.93f*32768),(int)(0.12f*32768)};
	// over-unity endpoints (SSF_UNCLAMPED range, 2.0 ceiling = 65534)
	const int lastQU[2]={65534,(int)(1.37f*32767)};
	const int curQU[2]={(int)(0.05f*32767),65534};

	// ---------- correctness ----------
	// float new-vs-old at n=4096 (same effective ramp): expect tiny ulp diffs
	memset(dstA,0,sizeof dstA); memset(dstB,0,sizeof dstB);
	OLD_MixSoundTwoSpeakerMono(dstA, src, MIXBUFFER_SAMPLES, lastV, curV);
	Snd_MixTwoSpeakerMono(dstB, src, MIXBUFFER_SAMPLES, lastV, curV);
	double maxdiff=0, maxrel=0;
	for (int i=0;i<MIXBUFFER_SAMPLES*2;i++){ double d=fabs((double)dstA[i]-dstB[i]); if(d>maxdiff)maxdiff=d; double m=fabs((double)dstA[i]); if(m>1e-6 && d/m>maxrel)maxrel=d/m; }
	printf("float mono new-vs-old  (n=4096): max abs diff %.6g  max rel %.3g\n", maxdiff, maxrel);

	// MixedSoundToSamples exactness incl. clamp edges
	static float conv[MIXBUFFER_SAMPLES*2];
	for (int i=0;i<MIXBUFFER_SAMPLES*2;i++) conv[i]=frand_pm32k()*1.5f; // exceed clamp range
	conv[0]=-32768.0f; conv[1]=32767.0f; conv[2]=-40000.f; conv[3]=50000.f; conv[4]=-0.7f; conv[5]=0.7f;
	/* the narrow now rounds half away from zero (the old kernel truncated):
	   verify (a) saturation identical to old, (b) exact integers unchanged
	   (WAV losslessness), (c) error vs the ideal <= 0.5 LSP and unbiased,
	   and report the old truncation error alongside for the record */
	Snd_MixedSoundToSamples(outB, conv, MIXBUFFER_SAMPLES*2);
	OLD_MixedSoundToSamples(outA, conv, MIXBUFFER_SAMPLES*2);
	{
		int satm=0, intm=0; double wnew=0, wold=0, mnew=0, mold=0; long cnt=0;
		for (int i=0;i<MIXBUFFER_SAMPLES*2;i++) {
			double v = conv[i];
			if (v <= -32768.0 || v >= 32767.0) { if (outA[i]!=outB[i]) satm++; continue; }
			if (v == floor(v) && outB[i] != (short)v) intm++;
			double en = outB[i] - v, eo = outA[i] - v;
			if (fabs(en)>wnew) wnew=fabs(en);
			if (fabs(eo)>wold) wold=fabs(eo);
			mnew += en; mold += eo; cnt++;
		}
		printf("MixedSoundToSamples: saturation vs old %s, exact-int lossless %s\n",
			satm?"FAIL":"bit-exact", intm?"FAIL":"OK");
		printf("  error vs ideal: new max %.4f mean %+.4f | old max %.4f mean %+.4f LSB %s\n",
			wnew, mnew/cnt, wold, mold/cnt, (wnew<=0.5001 && fabs(mnew/cnt)<0.02)?"OK":"FAIL");
	}

	// s16 kernels: SIMD-vs-scalar bit exactness at awkward sizes
	int total_mism=0;
	for (int n=1;n<=1470;n+=7) {
		memset(accA,0,sizeof accA); memset(accB,0,sizeof accB);
		// scalar reference computed inline (closed-form, mirrors kernel tail)
		{
			const int baseL=(int)lastQ[0]<<8, baseR=(int)lastQ[1]<<8;
			const int incL=(((int)curQ[0]-lastQ[0])<<8)/n, incR=(((int)curQ[1]-lastQ[1])<<8)/n;
			for(int i=0;i<n;i++){ int gL=(baseL+incL*i)>>8, gR=(baseR+incR*i)>>8;
				accA[i*2+0]+=(srcS[i]*gL+0x4000)>>15; accA[i*2+1]+=(srcS[i]*gR+0x4000)>>15; }
		}
		Snd_MixTwoSpeakerMonoS16(accB, srcS, n, lastQ, curQ);
		for(int i=0;i<n*2;i++) if(accA[i]!=accB[i]) total_mism++;
	}
	printf("s16 mono %s-vs-reference over n=1..1470: %s (%d mismatches)\n", simd, total_mism?"FAIL":"bit-exact", total_mism);

	// s16 stereo kernel (new SIMD path): bit exactness vs closed-form reference
	{
		short srcSt[MIXBUFFER_SAMPLES*2];
		for (int i=0;i<MIXBUFFER_SAMPLES*2;i++) srcSt[i]=(short)((int)(rng())%65536-32768);
		int stm=0;
		for (int n=1;n<=1470;n+=7) {
			memset(accA,0,sizeof accA); memset(accB,0,sizeof accB);
			const int baseL=(int)lastQ[0]<<8, baseR=(int)lastQ[1]<<8;
			const int incL=(((int)curQ[0]-lastQ[0])<<8)/n, incR=(((int)curQ[1]-lastQ[1])<<8)/n;
			for(int i=0;i<n;i++){ int gL=(baseL+incL*i)>>8, gR=(baseR+incR*i)>>8;
				accA[i*2+0]+=(srcSt[i*2+0]*gL+0x4000)>>15; accA[i*2+1]+=(srcSt[i*2+1]*gR+0x4000)>>15; }
			Snd_MixTwoSpeakerStereoS16(accB, srcSt, n, lastQ, curQ);
			for(int i=0;i<n*2;i++) if(accA[i]!=accB[i]) stm++;
		}
		printf("s16 stereo %s-vs-reference over n=1..1470: %s (%d mismatches)\n", simd, stm?"FAIL":"bit-exact", stm);

		// -------- over-unity gains (2.0 ceiling), full-scale samples --------
		// int64 shadow reference proves no int32 step of the kernel can
		// overflow at the documented extremes, and that the ramp between
		// over-unity endpoints stays bit-exact.
		{
			short srcX[MIXBUFFER_SAMPLES];
			for (int i=0;i<MIXBUFFER_SAMPLES;i++)
				srcX[i] = (i&1) ? (short)0x8000 : (short)0x7fff;   // alternate full-scale extremes
			int um=0, ovf=0;
			for (int n=1;n<=1470;n+=13) {
				memset(accA,0,sizeof accA); memset(accB,0,sizeof accB);
				const int baseL=lastQU[0]<<8, baseR=lastQU[1]<<8;
				const int incL=((curQU[0]-lastQU[0])<<8)/n, incR=((curQU[1]-lastQU[1])<<8)/n;
				for(int i=0;i<n;i++){
					int gL=(baseL+incL*i)>>8, gR=(baseR+incR*i)>>8;
					long long pL=(long long)srcX[i]*gL+0x4000, pR=(long long)srcX[i]*gR+0x4000;
					if (pL>0x7fffffffLL||pL<-0x80000000LL||pR>0x7fffffffLL||pR<-0x80000000LL) ovf++;
					accA[i*2+0]+=(int)(pL>>15); accA[i*2+1]+=(int)(pR>>15);
				}
				Snd_MixTwoSpeakerMonoS16(accB, srcX, n, lastQU, curQU);
				for(int i=0;i<n*2;i++) if(accA[i]!=accB[i]) um++;
			}
			printf("s16 mono over-unity gains (65534, FS samples): products in int32: %s | %s-vs-int64-reference: %s\n",
				ovf?"FAIL":"proven", simd, um?"FAIL":"bit-exact");
		}
	}

	// Snd_SumToS16 saturation exactness
	for(int i=0;i<64;i++) accA[i]=(int)(rng())%200000-100000;
	Snd_SumToS16(outA, accA, 64);
	int sat=0; for(int i=0;i<64;i++){int v=accA[i];short e=(short)(v<-32768?-32768:(v>32767?32767:v)); if(outA[i]!=e)sat++;}
	printf("Snd_SumToS16 saturation: %s\n", sat?"FAIL":"bit-exact");

	// -------- soft-knee saturator properties --------
	{
		int fail = 0;
		// identity region, exhaustive: every |v| <= A passes bit-exact
		for ( int v = -SND_KNEE_A; v <= SND_KNEE_A; v++ )
			if ( Snd_SoftKneeS16(v) != (short)v ) fail++;
		printf("knee identity |v|<=%d (exhaustive): %s\n", SND_KNEE_A, fail?"FAIL":"bit-exact");

		// symmetry + bound + monotonicity over a dense sweep incl. extremes
		fail = 0; int mono = 0, bound = 0;
		long long prev = -0x7fffffffLL;
		for ( long long v = -0x7fffffff; v <= 0x7fffffff; v += 4093 ) {
			short y = Snd_SoftKneeS16( (int)v );
			short yn = Snd_SoftKneeS16( (int)-v );
			if ( yn != (short)-y ) fail++;
			if ( y > 32767 || y < -32767 ) bound++;
			(void)prev;
		}
		for ( int v = -200000, py = -32767-1; v <= 200000; v++ ) {
			int y = Snd_SoftKneeS16(v);
			if ( y < py ) mono++;
			py = y;
		}
		// knee continuity + documented single-FS value
		int c0 = ( Snd_SoftKneeS16(SND_KNEE_A) == SND_KNEE_A );
		int fs = Snd_SoftKneeS16(32767);
		printf("knee symmetry: %s | bound<=32767: %s | monotone [-200k,200k]: %s | f(A)=A: %s | f(32767)=%d\n",
			fail?"FAIL":"ok", bound?"FAIL":"ok", mono?"FAIL":"ok", c0?"ok":"FAIL", fs);

		// int32 extremes must not trap or wrap
		short a = Snd_SoftKneeS16( 0x7fffffff );
		short b = Snd_SoftKneeS16( (int)0x80000000 );
		printf("knee extremes: f(INT_MAX)=%d f(INT_MIN)=%d %s\n", a, b,
			( a <= 32767 && b >= -32767 ) ? "ok" : "FAIL");

		// Snd_SumToS16Soft (SIMD fast path + scalar route) vs pure scalar curve,
		// on mixed content: mostly in-knee with hot bursts, odd tails
		fail = 0;
		static int kin[MIXBUFFER_SAMPLES*2]; static short koutA[MIXBUFFER_SAMPLES*2], koutB[MIXBUFFER_SAMPLES*2];
		for ( int trial = 0; trial < 200; trial++ ) {
			int n = 1 + (int)( rng() % (MIXBUFFER_SAMPLES*2) );
			for ( int i = 0; i < n; i++ ) {
				int v = (int)( rng() % 40000 ) - 20000;              // in-knee
				if ( ( rng() & 15 ) == 0 ) v *= 7;                    // hot burst
				kin[i] = v;
			}
			Snd_SumToS16Soft( koutA, kin, n );
			for ( int i = 0; i < n; i++ ) koutB[i] = Snd_SoftKneeS16( kin[i] );
			for ( int i = 0; i < n; i++ ) if ( koutA[i] != koutB[i] ) fail++;
		}
		printf("Snd_SumToS16Soft %s vs scalar curve: %s\n", simd, fail?"FAIL":"bit-exact");

		// float knee: shape parity with the s16 curve at int16 scale, and
		// SIMD-vs-scalar bit-exactness of Snd_SoftKneeFloatOutput
		double worst = 0.0;
		for ( int v = -120000; v <= 120000; v += 7 ) {
			float f = Snd_SoftKneeF( v / 32768.0f ) * 32768.0f;
			int   s = Snd_SoftKneeS16( v );
			double d = f - s; if ( d < 0 ) d = -d;
			if ( d > worst ) worst = d;
		}
		printf("float knee vs s16 knee, worst |delta| over [-120k,120k]: %.3f LSB %s\n",
			worst, worst < 2.0 ? "ok" : "FAIL");

		fail = 0;
		static float fA[MIXBUFFER_SAMPLES*2], fB[MIXBUFFER_SAMPLES*2];
		for ( int trial = 0; trial < 100; trial++ ) {
			int n = 1 + (int)( rng() % (MIXBUFFER_SAMPLES*2) );
			for ( int i = 0; i < n; i++ ) {
				float v = ( (int)( rng() % 40000 ) - 20000 ) / 32768.0f;
				if ( ( rng() & 15 ) == 0 ) v *= 7.0f;
				fA[i] = fB[i] = v;
			}
			Snd_SoftKneeFloatOutput( fA, n );
			for ( int i = 0; i < n; i++ ) fB[i] = Snd_SoftKneeF( fB[i] );
			for ( int i = 0; i < n; i++ ) if ( fA[i] != fB[i] ) fail++;
		}
		printf("Snd_SoftKneeFloatOutput %s vs scalar curve: %s\n", simd, fail?"FAIL":"bit-exact");
	}


	// float stereo + small-n mono: SIMD vs serial reference
	{
		double wm=0, wrel=0;
		for (int n : {1,2,3,7,8,367,368,735,736,1470}) {
			memset(dstA,0,sizeof dstA); memset(dstB,0,sizeof dstB);
			// serial reference with correct divisor
			{ float sL=lastV[0],sR=lastV[1];
			  float iL=(curV[0]-lastV[0])/n, iR=(curV[1]-lastV[1])/n;
			  for(int i=0;i<n;i++){dstA[i*2]+=src[i*2]*sL;dstA[i*2+1]+=src[i*2+1]*sR;sL+=iL;sR+=iR;} }
			Snd_MixTwoSpeakerStereo(dstB, src, n, lastV, curV);
			for(int i=0;i<n*2;i++){double d=fabs((double)dstA[i]-dstB[i]);if(d>wm)wm=d;double m=fabs((double)dstA[i]);if(m>1e-3&&d/m>wrel)wrel=d/m;}
		}
		printf("float stereo SIMD-vs-serial (10 sizes): max abs %.4g max rel %.3g %s\n", wm, wrel, wrel<1e-3?"OK":"FAIL");
	}
	{
		double wm=0, wrel=0;
		for (int n : {1,3,5,367,735}) {
			memset(dstA,0,sizeof dstA); memset(dstB,0,sizeof dstB);
			{ float sL=lastV[0],sR=lastV[1];
			  float iL=(curV[0]-lastV[0])/n, iR=(curV[1]-lastV[1])/n;
			  for(int i=0;i<n;i++){dstA[i*2]+=src[i]*sL;dstA[i*2+1]+=src[i]*sR;sL+=iL;sR+=iR;} }
			Snd_MixTwoSpeakerMono(dstB, src, n, lastV, curV);
			for(int i=0;i<n*2;i++){double d=fabs((double)dstA[i]-dstB[i]);if(d>wm)wm=d;double m=fabs((double)dstA[i]);if(m>1e-3&&d/m>wrel)wrel=d/m;}
		}
		printf("float mono small-n SIMD-vs-serial: max abs %.4g max rel %.3g %s\n", wm, wrel, wrel<1e-3?"OK":"FAIL");
	}

	// ---------- performance ----------
	const int REPS = 20000;
	const int N = 735; // typical 60fps frame
	volatile float sinkf=0; volatile int sinki=0;

	memset(dstA,0,sizeof dstA);
	double t0=now_ms();
	for(int r=0;r<REPS;r++) OLD_MixSoundTwoSpeakerMono(dstA, src, MIXBUFFER_SAMPLES, lastV, curV);
	double t1=now_ms();
	sinkf+=dstA[100];
	printf("OLD  float mono 4096: %8.2f ns/frame-sample\n", (t1-t0)*1e6/((double)REPS*MIXBUFFER_SAMPLES));

	memset(dstA,0,sizeof dstA);
	t0=now_ms();
	for(int r=0;r<REPS;r++) Snd_MixTwoSpeakerMono(dstA, src, MIXBUFFER_SAMPLES, lastV, curV);
	t1=now_ms();
	sinkf+=dstA[100];
	printf("NEW  float mono 4096: %8.2f ns/frame-sample\n", (t1-t0)*1e6/((double)REPS*MIXBUFFER_SAMPLES));

	memset(dstA,0,sizeof dstA);
	t0=now_ms();
	for(int r=0;r<REPS*5;r++) Snd_MixTwoSpeakerMono(dstA, src, N, lastV, curV);
	t1=now_ms();
	sinkf+=dstA[100];
	printf("NEW  float mono  735: %8.2f ns/frame-sample\n", (t1-t0)*1e6/((double)REPS*5*N));

	memset(accA,0,sizeof accA);
	t0=now_ms();
	for(int r=0;r<REPS*5;r++) Snd_MixTwoSpeakerMonoS16(accA, srcS, N, lastQ, curQ);
	t1=now_ms();
	sinki+=accA[100];
	printf("NEW  s16   mono  735: %8.2f ns/frame-sample\n", (t1-t0)*1e6/((double)REPS*5*N));

	{
		static short srcSt2[MIXBUFFER_SAMPLES*2];
		for (int i=0;i<MIXBUFFER_SAMPLES*2;i++) srcSt2[i]=(short)((i*2654435761u)>>17);
		memset(accA,0,sizeof accA);
		t0=now_ms();
		for(int r=0;r<REPS*5;r++) Snd_MixTwoSpeakerStereoS16(accA, srcSt2, N, lastQ, curQ);
		t1=now_ms();
		sinki+=accA[100];
		printf("NEW  s16 stereo  735: %8.2f ns/frame-sample\n", (t1-t0)*1e6/((double)REPS*5*N));
	}

	t0=now_ms();
	for(int r=0;r<REPS;r++){ OLD_MixedSoundToSamples(outA, conv, MIXBUFFER_SAMPLES*2); asm volatile("" ::: "memory"); }
	t1=now_ms(); sinki+=outA[9];
	printf("OLD  f->s16 conv 8192: %7.2f ns/sample\n", (t1-t0)*1e6/((double)REPS*MIXBUFFER_SAMPLES*2));

	t0=now_ms();
	for(int r=0;r<REPS;r++){ Snd_MixedSoundToSamples(outA, conv, MIXBUFFER_SAMPLES*2); asm volatile("" ::: "memory"); }
	t1=now_ms(); sinki+=outA[9];
	printf("NEW  f->s16 conv 8192: %7.2f ns/sample\n", (t1-t0)*1e6/((double)REPS*MIXBUFFER_SAMPLES*2));

	t0=now_ms();
	for(int r=0;r<REPS;r++){ Snd_SumToS16(outA, accA, MIXBUFFER_SAMPLES*2); asm volatile("" ::: "memory"); }
	t1=now_ms(); sinki+=outA[9];
	printf("NEW  i32->s16 sat 8192:%7.2f ns/sample\n", (t1-t0)*1e6/((double)REPS*MIXBUFFER_SAMPLES*2));

	(void)sinkf;(void)sinki;
	return 0;
}
// ---- reverb unit test (appended) ----
#define ID_INLINE inline
/* snd_reverb.h reads the output rate through snd_SampleRate(); the core
   defines the variable in snd_system.cpp, the bench provides it here */
int snd_sampleRate = 44100;
#include "../neo/sound/snd_reverb.h"
#include "../neo/sound/snd_envirofx.h"
/* scalar twin of the same header for SIMD equivalence testing: the include
   guard is reopened inside a namespace with the kernel forced off, giving a
   second, independent instantiation of the identical DSP */
namespace rvb_scalar {
	int snd_sampleRate = 44100;
#define REVERB_FORCE_SCALAR 1
#undef __SND_REVERB_H__
#include "../neo/sound/snd_reverb.h"
#undef REVERB_FORCE_SCALAR
}
static idSoundReverb rvbF, rvbI;
int reverb_test() {
	rvbF.Init(); rvbI.Init();
	sndReverbParams_t p; p.SetDefaults();
	p.decayTime = 1.5f; p.reflectionsDelay = 0.02f; p.lateReverbDelay = 0.03f;
	rvbF.SetParams(p); rvbI.SetParams(p);
	// warm past the crossfade
	static float zf[512], df[1024]; static int zi[512], di[1024];
	for (int b=0;b<REVERB_XFADE_BLOCKS+2;b++){ memset(df,0,sizeof df); memset(di,0,sizeof di);
		rvbF.ProcessFloat(zf, df, 512, 0.5f); rvbI.ProcessS16(zi, di, 512, 0.5f); }
	// impulse at int16 scale
	static float sf[512]; static int si[512];
	sf[0]=16384.0f; si[0]=16384;
	double eF=0,eI=0; double e60F=0,e60I=0; int totalBlocks= (int)(3.0*44100/512);
	double tail_start=1.5*44100;
	long n=0; double firstF=0, firstI=0;
	for (int b=0;b<totalBlocks;b++){
		memset(df,0,sizeof df); memset(di,0,sizeof di);
		rvbF.ProcessFloat(b?zf:sf, df, 512, 0.5f);
		rvbI.ProcessS16(b?zi:si, di, 512, 0.5f);
		for (int i=0;i<512*2;i++){
			double vF=df[i]*32768.0, vI=(double)di[i];
			eF+=vF*vF; eI+=vI*vI;
			if (n>tail_start*2){ e60F+=vF*vF; e60I+=vI*vI; }
			if (b==4 && i<4 && firstF==0){ firstF=vF; firstI=vI; }
			n++;
		}
	}
	printf("reverb energy: float=%.3g int=%.3g ratio=%.3f\n", eF, eI, eI>0?eF/eI:0);
	printf("reverb tail(>1.5s)/total: float=%.4f int=%.4f (RT60=1.5s => small but nonzero)\n",
		e60F/eF, e60I/eI);
	printf("reverb float/int agreement: %s\n",
		(eI>0 && eF/eI>0.5 && eF/eI<2.0) ? "OK (same order, same envelope)" : "FAIL");
	// int path determinism: run twice, compare
	idSoundReverb a,b2; a.Init(); b2.Init(); a.SetParams(p); b2.SetParams(p);
	static int o1[1024],o2[1024]; int mism=0;
	for (int b=0;b<40;b++){ memset(o1,0,sizeof o1); memset(o2,0,sizeof o2);
		a.ProcessS16(b?zi:si,o1,512,0.5f); b2.ProcessS16(b?zi:si,o2,512,0.5f);
		for(int i=0;i<1024;i++) if(o1[i]!=o2[i]) mism++;
	}
	printf("reverb int replay determinism: %s\n", mism?"FAIL":"bit-exact");

	// -------- dormancy gate + denormal flush (new) --------
	{
		// (a) replay determinism through gate transitions: two objects
		//     driven identically through signal / long silence / signal,
		//     full streams compared. Transparency of the gate itself is
		//     by construction (it only skips when state is exactly zero
		//     and the send is exactly zero, and zero is a fixed point);
		//     what needs testing is that the transition logic is
		//     deterministic and the flush converges - (b) and (c).
		static idSoundReverb gA, gB; gA.Init(); gB.Init();
		sndReverbParams_t gp; gp.SetDefaults(); gp.decayTime = 0.6f;
		gA.SetParams(gp); gB.SetParams(gp);
		static float gs[512], gdA[1024], gdB[1024];
		int gmis = 0;
		/* window sized to the physics: states start near 2^14 and the
		   flush threshold is 1e-20, i.e. ~484 dB of decay. RT60 0.6s is
		   100 dB/s, so ~4.9s to cross; 7s covers the slower LF band. */
		int silentBlocks = (int)(7.0*44100/512);
		for (int phase=0; phase<2; phase++) {
			for (int b=0;b<silentBlocks;b++) {
				memset(gs,0,sizeof gs);
				if (b==0) for (int i=0;i<64;i++) gs[i] = (float)((int)(rng()%32768)-16384);
				memset(gdA,0,sizeof gdA); memset(gdB,0,sizeof gdB);
				gA.ProcessFloat(gs,gdA,512,0.5f);
				gB.ProcessFloat(gs,gdB,512,0.5f);
				for (int i=0;i<1024;i++) if (gdA[i]!=gdB[i]) gmis++;
			}
		}
		printf("reverb float gate replay (signal/silence x2): %s\n", gmis?"FAIL":"bit-exact");

		// (b) exact-zero convergence of the float path: after long
		//     silence the flush must drive every state to exact 0 and
		//     the output to exact 0.0f
		int nz = 0;
		memset(gs,0,sizeof gs); memset(gdA,0,sizeof gdA);
		gA.ProcessFloat(gs,gdA,512,0.5f);
		for (int i=0;i<1024;i++) if (gdA[i]!=0.0f) nz++;
		printf("reverb float tail after 7s silence: %d nonzero %s\n", nz, nz?"FAIL":"OK (exact zero)");

#if defined(__x86_64__) || defined(__SSE2__) || defined(_M_X64)
		// (c) FTZ invariance of the terminal state: run the same
		//     program under FTZ+DAZ on vs off; streams must reconverge
		//     to bit-identical exact zero within the silence window
		{
			unsigned csr = _mm_getcsr();
			static idSoundReverb fA, fB; fA.Init(); fB.Init();
			fA.SetParams(gp); fB.SetParams(gp);
			static float o1f[1024], o2f[1024];
			int diverged_end = 0;
			for (int b=0;b<silentBlocks;b++) {
				memset(gs,0,sizeof gs);
				if (b==0) for (int i=0;i<64;i++) gs[i] = (float)((int)(rng()%32768)-16384);
				_mm_setcsr(csr & ~0x8040u);            // FTZ,DAZ off
				memset(o1f,0,sizeof o1f); fA.ProcessFloat(gs,o1f,512,0.5f);
				_mm_setcsr(csr | 0x8040u);             // FTZ,DAZ on
				memset(o2f,0,sizeof o2f); fB.ProcessFloat(gs,o2f,512,0.5f);
				if (b == silentBlocks-1)
					for (int i=0;i<1024;i++) if (o1f[i]!=o2f[i] || o1f[i]!=0.0f) diverged_end++;
			}
			_mm_setcsr(csr);
			printf("reverb float FTZ-on vs FTZ-off, end of tail: %s\n",
				diverged_end?"FAIL":"reconverged to identical exact zero");
		}
#endif

		// (d) int path: gate must not change the (already exact-zero
		//     decaying) stream
		static idSoundReverb iA; iA.Init(); iA.SetParams(gp);
		static int is_[512], id_[1024]; int inz=0;
		for (int b=0;b<silentBlocks;b++) {
			memset(is_,0,sizeof is_);
			if (b==0) is_[0]=16384;
			memset(id_,0,sizeof id_);
			iA.ProcessS16(is_,id_,512,0.5f);
		}
		memset(id_,0,sizeof id_); iA.ProcessS16(is_,id_,512,0.5f);
		for (int i=0;i<1024;i++) if (id_[i]) inz++;
		printf("reverb int gated tail: %d nonzero %s\n", inz, inz?"FAIL":"OK (exact zero)");
	}

	// -------- HRTF renderer (s_HRTF) --------
	{
		static idSoundHRTF H;
		H.Init(44100);
		static short hLq[SND_HRTF_MAX_TAPS], hRq[SND_HRTF_MAX_TAPS];
		static float hLf[SND_HRTF_MAX_TAPS], hRf[SND_HRTF_MAX_TAPS];
		printf("hrtf init 44100: taps=%d %s\n", H.Taps(), H.Taps()==128?"ok":"FAIL");

		// (a) end-to-end ITD/ILD at az=+90 (source hard right): impulse
		//     through blend + s16 conv kernel; right ear must peak
		//     earlier and larger
		H.BlendDirection(90.0f, 0.0f, hLq, hRq, hLf, hRf);
		{
			const int taps=H.Taps(), N=256;
			static short src[SND_HRTF_HIST+512]; memset(src,0,sizeof src);
			src[taps-1]=16384;      // impulse at block start, zero history
			static int acc[512*2]; memset(acc,0,sizeof acc);
			const int g[2]={32767,32767};
			Snd_HrtfConvolveMixS16(acc,src,N,hLq,hRq,taps,g,g);
			int pL=0,pR=0,iL=0,iR=0;
			for(int i=0;i<N;i++){
				int aL=acc[i*2]<0?-acc[i*2]:acc[i*2], aR=acc[i*2+1]<0?-acc[i*2+1]:acc[i*2+1];
				if(aL>pL){pL=aL;iL=i;} if(aR>pR){pR=aR;iR=i;}
			}
			printf("hrtf az+90 s16: peak L=%d@%d R=%d@%d  %s\n", pL,iL,pR,iR,
				(pR>2*pL && iR<iL)?"ok (right louder+earlier)":"FAIL");
		}

		// (b) history carry: one long block vs odd-sized split blocks,
		//     accumulators bit-identical (the world's [hist|block] scheme)
		{
			const int taps=H.Taps(), N=2000;
			H.BlendDirection(37.0f, -12.0f, hLq, hRq, hLf, hRf);
			static short sig[2000];
			for(int i=0;i<N;i++) sig[i]=(short)((int)(rng()%65536)-32768);
			static int accA[2000*2], accB[2000*2];
			memset(accA,0,sizeof accA); memset(accB,0,sizeof accB);
			const int gA[2]={29000,14000};
			// single block
			static short bufA[SND_HRTF_HIST+2000];
			memset(bufA,0,(taps-1)*sizeof(short));
			memcpy(bufA+taps-1,sig,N*sizeof(short));
			Snd_HrtfConvolveMixS16(accA,bufA,N,hLq,hRq,taps,gA,gA);
			// split blocks with carried history; constant gains so the
			// per-block ramp is flat and comparable
			static short hist[SND_HRTF_HIST]; memset(hist,0,sizeof hist);
			static short buf[SND_HRTF_HIST+2000];
			int pos=0, step=0; int sizes[]={1,7,64,381,997,550};
			while(pos<N){
				int n=sizes[step%6]; if(pos+n>N)n=N-pos; step++;
				memcpy(buf,hist,(taps-1)*sizeof(short));
				memcpy(buf+taps-1,sig+pos,n*sizeof(short));
				Snd_HrtfConvolveMixS16(accB+pos*2,buf,n,hLq,hRq,taps,gA,gA);
				memcpy(hist,buf+n,(taps-1)*sizeof(short));
				pos+=n;
			}
			int mism=0; for(int i=0;i<N*2;i++) if(accA[i]!=accB[i]) mism++;
			printf("hrtf history carry (split vs single): %s (%d mismatches)\n", mism?"FAIL":"bit-exact", mism);
		}

		// (c) float vs s16 conv agreement on the same content
		{
			const int taps=H.Taps(), N=512;
			static float fsrc[SND_HRTF_HIST+512]; static short ssrc[SND_HRTF_HIST+512];
			for(int i=0;i<taps-1+N;i++){ int v=(int)(rng()%65536)-32768; ssrc[i]=(short)v; fsrc[i]=(float)v; }
			static float fdst[512*2]; static int sdst[512*2];
			memset(fdst,0,sizeof fdst); memset(sdst,0,sizeof sdst);
			const float gf[2]={0.7f/32768.0f,0.7f/32768.0f};
			const int gq[2]={22938,22938};   // 0.7 Q15
			Snd_HrtfConvolveMixFloat(fdst,fsrc,N,hLf,hRf,taps,gf,gf);
			Snd_HrtfConvolveMixS16(sdst,ssrc,N,hLq,hRq,taps,gq,gq);
			double worst=0;
			for(int i=0;i<N*2;i++){ double d=fdst[i]*32768.0-(double)sdst[i]; if(d<0)d=-d; if(d>worst)worst=d; }
			printf("hrtf float-vs-s16 conv worst delta: %.2f LSB %s\n", worst, worst<16.0?"ok":"FAIL");
		}

		// (d) resample to 48000: tap count, and the ITD ordering survives
		H.Init(48000);
		printf("hrtf init 48000: taps=%d %s\n", H.Taps(), H.Taps()==140?"ok":"FAIL");
		H.BlendDirection(90.0f, 0.0f, hLq, hRq, hLf, hRf);
		{
			const int taps=H.Taps(), N=300;
			static short src[SND_HRTF_HIST+512]; memset(src,0,sizeof src);
			src[taps-1]=16384;
			static int acc[512*2]; memset(acc,0,sizeof acc);
			const int g[2]={32767,32767};
			Snd_HrtfConvolveMixS16(acc,src,N,hLq,hRq,taps,g,g);
			int pL=0,pR=0,iL=0,iR=0;
			for(int i=0;i<N;i++){
				int aL=acc[i*2]<0?-acc[i*2]:acc[i*2], aR=acc[i*2+1]<0?-acc[i*2+1]:acc[i*2+1];
				if(aL>pL){pL=aL;iL=i;} if(aR>pR){pR=aR;iR=i;}
			}
			printf("hrtf az+90 s16 @48k: peak L=%d@%d R=%d@%d  %s\n", pL,iL,pR,iR,
				(pR>2*pL && iR<iL)?"ok (right louder+earlier)":"FAIL");
		}
		H.Init(44100);   // restore for any later use
	}

	/* ---- SIMD/scalar equivalence (appended with the SSE2 line stage) ----
	   The header compiles its SSE2 path by default on x86; a second copy
	   of the class is instantiated in a namespace with the kernel forced
	   off, and both run the same stream: 8 seconds of noise through
	   defaults -> all features hot -> back, so the tap slew, the
	   crossfade, the LFO and the echo all pass through the comparison.
	   Every output sample must match. On targets without the kernel both
	   copies are scalar and the test is a tautology, which is fine. */
	{
		int mism2 = 0;
		rvb_scalar::idSoundReverb rs; rs.Init();
		idSoundReverb rv; rv.Init();
		sndReverbParams_t pa; pa.SetDefaults(); pa.decayTime=4.0f;
		sndReverbParams_t pb = pa; pb.decayLFRatio=1.8f; pb.gainLF=0.3f; pb.gainHF=0.2f;
		pb.echoDepth=0.9f; pb.echoTime=0.09f; pb.modulationDepth=1.0f; pb.modulationTime=0.11f;
		pb.density=0.15f; pb.airAbsorptionGainHF=0.9f; pb.decayHFLimit=1;
		/* pc exercises the negative lfAdj (decayLFRatio < 1): the one
		   signed gain, and exactly the case a non-negative-only vector
		   multiply gets wrong while every other stream passes */
		sndReverbParams_t pc = pb; pc.decayLFRatio=0.3f;
		rvb_scalar::sndReverbParams_t sa, sb, sc;
		memcpy(&sa,&pa,sizeof sa); memcpy(&sb,&pb,sizeof sb); memcpy(&sc,&pc,sizeof sc);
		rv.SetParams(pa); rs.SetParams(sa);
		unsigned rng=0xC0FFEE;
		static int s2[512], d2a[1024], d2b[1024];
		for (int blk=0; blk<690; blk++){
			if (blk==120){ rv.SetParams(pb); rs.SetParams(sb); }
			if (blk==430){ rv.SetParams(pc); rs.SetParams(sc); }
			if (blk==560){ rv.SetParams(pa); rs.SetParams(sa); }
			for (int i=0;i<512;i++){ rng=rng*1664525u+1013904223u; s2[i]=(int)(rng>>17)-16384; }
			memset(d2a,0,sizeof d2a); memset(d2b,0,sizeof d2b);
			rv.ProcessS16(s2,d2a,512,0.7f);
			rs.ProcessS16(s2,d2b,512,0.7f);
			for (int i=0;i<1024;i++) if (d2a[i]!=d2b[i]) mism2++;
		}
		printf("reverb SSE2-vs-scalar (8s, all features, retargets): %s\n",
			mism2 ? "FAIL" : "bit-exact");
	}

	/* ---- NEON float kernel parity (appended with the float line stage;
	        tautological where no float SIMD compiles in) ---- */
	{
		idSoundReverb rA; rvb_scalar::idSoundReverb rB;
		rA.Init(); rB.Init();
		sndReverbParams_t qa; rvb_scalar::sndReverbParams_t qb;
		qa.SetDefaults(); qa.decayTime=2.2f; qa.decayLFRatio=0.3f; qa.decayHFRatio=0.4f;
		qa.echoTime=0.09f; qa.echoDepth=0.6f; qa.modulationTime=0.9f; qa.modulationDepth=0.7f;
		qa.density=0.35f; qa.gainLF=0.4f; qa.gainHF=0.5f; qa.lateReverbPan[0]=0.6f;
		memcpy(&qb,&qa,sizeof qb);
		rA.SetParams(qa); rB.SetParams(qb);
		static float sf2[512], oA[1024], oB[1024];
		unsigned rng=99; long long mismF=0;
		for (int b=0;b<300;b++){
			if (b==120){
				qa.density=1.0f; qa.modulationDepth=0.0f; qa.decayLFRatio=1.6f; qa.lateReverbPan[0]=-1.0f;
				memcpy(&qb,&qa,sizeof qb); rA.SetParams(qa); rB.SetParams(qb);
			}
			for (int i=0;i<512;i++){ rng=rng*1664525u+1013904223u; sf2[i]=(b<220)?((int)(rng>>17)-16384)/32768.0f:0.0f; }
			memset(oA,0,sizeof oA); memset(oB,0,sizeof oB);
			rA.ProcessFloat(sf2,oA,512,0.9f);
			rB.ProcessFloat(sf2,oB,512,0.9f);
			for (int i=0;i<1024;i++) if (memcmp(&oA[i],&oB[i],4)) mismF++;
		}
		printf("reverb float kernel vs scalar-float (all features, retarget): %s\n",
			mismF ? "FAIL" : "bit-identical");
	}

	/* ---- reverb pan vectors (appended with the pan implementation) ---- */
	{
		/* late pan hard right vs unpanned: R wet energy must dominate L */
		for (int pan = 0; pan < 2; pan++) {
			idSoundReverb rv; rv.Init();
			sndReverbParams_t pp; pp.SetDefaults();
			pp.lateReverbGain = 1.0f; pp.reflectionsGain = 0.2f;
			if (pan) { pp.lateReverbPan[0] = 1.0f; pp.reflectionsPan[0] = 1.0f; }
			rv.SetParams(pp);
			static int send[512], out[1024];
			double eL=0, eR=0; unsigned rng=77;
			for (int b=0;b<160;b++){
				for (int i=0;i<512;i++){ rng=rng*1664525u+1013904223u; send[i]=(b<80)?((int)(rng>>18)-8192):0; }
				memset(out,0,sizeof out);
				rv.ProcessS16(send,out,512,1.0f);
				if (b>30){ for(int i=0;i<512;i++){ eL+=(double)out[i*2]*out[i*2]; eR+=(double)out[i*2+1]*out[i*2+1]; } }
			}
			if (!pan)
				printf("reverb pan zero: L %.3g R %.3g ratio %.2f %s\n", eL, eR, eR/(eL+1),
					(eR/(eL+1)>0.5 && eR/(eL+1)<2.0)?"OK (balanced)":"FAIL");
			else
				printf("reverb pan +1 (hard right): L %.3g R %.3g ratio %.1f %s\n", eL, eR, eR/(eL+1),
					(eR>eL*20)?"OK (right-dominant)":"FAIL");
		}
	}

	/* ---- enviro-suit chain (appended with the ROE suit port) ---- */
	{
		static float sf[1024]; static int si[1024];
		/* 1. muffling: steady 6kHz must attenuate far more than 400Hz */
		double rms[2][2];
		for (int f = 0; f < 2; f++) {
			double freq = f ? 6000.0 : 400.0;
			idEnviroSuitFX ef2, ei2; ef2.Init(); ei2.Init();
			double eEF=0, eEI=0; long n=0;
			for (int b = 0; b < 200; b++) {
				for (int i = 0; i < 512; i++) {
					double v = 9000.0 * sin(2*3.14159265*freq*(b*512+i)/44100.0);
					sf[i*2] = sf[i*2+1] = (float)(v/32768.0);
					si[i*2] = si[i*2+1] = (int)v;
				}
				ef2.ProcessFloat(sf, 512);
				ei2.ProcessS16(si, 512);
				if (b > 60) { for (int i=0;i<1024;i++){ eEF += (double)sf[i]*32768.0*sf[i]*32768.0; eEI += (double)si[i]*si[i]; n++; } }
			}
			rms[f][0] = sqrt(eEF/n); rms[f][1] = sqrt(eEI/n);
		}
		printf("enviro muffle: 400Hz float %.1f int %.1f | 6kHz float %.1f int %.1f  %s\n",
			rms[0][0], rms[0][1], rms[1][0], rms[1][1],
			(rms[1][0] < 0.2*rms[0][0] && rms[1][1] < 0.2*rms[0][1]) ? "OK" : "FAIL");
		/* 2. float/int agreement on the passband */
		printf("enviro float/int agreement (400Hz): ratio %.3f  %s\n",
			rms[0][0]/(rms[0][1]+1e-9),
			(rms[0][0]/(rms[0][1]+1e-9) > 0.9 && rms[0][0]/(rms[0][1]+1e-9) < 1.1) ? "OK" : "FAIL");
		/* 3. int replay determinism + tail to exact zero */
		{
			idEnviroSuitFX r1, r2; r1.Init(); r2.Init();
			static int o1[1024], o2[1024]; int mism=0; unsigned rng=0x5EED;
			for (int b=0;b<200;b++){
				for (int i=0;i<1024;i++){ rng=rng*1664525u+1013904223u; o1[i]=o2[i]= (b<40) ? (int)(rng>>17)-16384 : 0; }
				r1.ProcessS16(o1,512); r2.ProcessS16(o2,512);
				for (int i=0;i<1024;i++) if (o1[i]!=o2[i]) mism++;
			}
			long long tail=0; for (int i=0;i<1024;i++) tail += (long long)o1[i]*o1[i];
			printf("enviro int determinism: %s; tail after 1.9s silence: %lld %s\n",
				mism?"FAIL":"bit-exact", tail, tail==0?"OK (exact zero)":(tail<100?"OK":"FAIL"));
		}
	}

	/* ---- EAXREVERB feature tests: steady-sine and noise-burst probes ----
	   Single impulses die to exact zero in the integer path long before a
	   tail window opens (truncation is designed to do exactly that), so
	   every decay measurement here uses a deterministic noise burst, and
	   every spectral-gain measurement a steady sine placed far from the
	   first-order corners. */
	{
		struct Probe {
			unsigned rng;
			Probe():rng(0x12345u){}
			int noise(){ rng = rng*1664525u+1013904223u; return (int)(rng>>17)-16384; }
			/* steady sine at f, return wet output RMS after settling */
			static double sineRMS( const sndReverbParams_t &pp, double f ) {
				static int zi[512], di[1024], si[512];
				idSoundReverb r; r.Init(); r.SetParams(pp);
				memset(zi,0,sizeof zi);
				for (int b=0;b<REVERB_XFADE_BLOCKS+2;b++){ memset(di,0,sizeof di); r.ProcessS16(zi,di,512,0.5f); }
				double e=0; long n=0;
				for (int b=0;b*512<44100*2;b++){
					for (int i=0;i<512;i++) si[i]=(int)(12000.0*sin(2*3.14159265*f*(b*512+i)/44100.0));
					memset(di,0,sizeof di);
					r.ProcessS16(si,di,512,0.5f);
					if (b*512>44100){ for (int i=0;i<512;i++){ e+=(double)di[i*2]*di[i*2]; n++; } }
				}
				return sqrt(e/(double)n);
			}
			/* noise burst 0.5s, then band energy (4x one-pole cascade, LP or
			   HP at fSplit) in [t0,t0+0.3] and [t1,t1+0.3] */
			static void decayBands( const sndReverbParams_t &pp, double fSplit, int highBand,
			                        double t0, double t1, double *e0, double *e1 ) {
				static int zi[512], di[1024], si[512];
				idSoundReverb r; r.Init(); r.SetParams(pp);
				memset(zi,0,sizeof zi);
				for (int b=0;b<REVERB_XFADE_BLOCKS+2;b++){ memset(di,0,sizeof di); r.ProcessS16(zi,di,512,0.5f); }
				Probe pr; double lp[4]={0,0,0,0};
				const double c=1.0-2*3.14159265*fSplit/44100.0;
				*e0=0; *e1=0; long n=0;
				for (int b=0;b*512<44100*4;b++){
					int burst = (b*512 < 22050);
					for (int i=0;i<512;i++) si[i]= burst ? pr.noise() : 0;
					memset(di,0,sizeof di);
					r.ProcessS16(si,di,512,0.5f);
					for (int i=0;i<512;i++){
						double v=di[i*2];
						for (int k=0;k<4;k++){ lp[k]+=(1.0-c)*((k?lp[k-1]:v)-lp[k]); }
						double band = highBand ? (v-lp[3]) : lp[3];
						double t=(double)n/44100.0;
						if (t>=t0 && t<t0+0.3) *e0+=band*band;
						if (t>=t1 && t<t1+0.3) *e1+=band*band;
						n++;
					}
				}
			}
		};
		sndReverbParams_t base; base.SetDefaults();
		base.decayTime=2.0f; base.reflectionsDelay=0.02f; base.lateReverbDelay=0.03f;

		/* 1. decayLFRatio: low band (<=300Hz) tail slope, ratio 0.3 vs 2.0;
		   burst ends 0.5s, windows at 1.0s and 2.0s */
		{
			sndReverbParams_t a1=base; a1.decayLFRatio=0.3f; a1.lfReference=400.0f;
			sndReverbParams_t a2=base; a2.decayLFRatio=2.0f; a2.lfReference=400.0f;
			double x0,x1,y0,y1;
			/* windows right after the burst: r=0.3 reaches the integer
			   quantization floor within ~0.5s, so late windows would
			   compare limit-cycle residue with itself */
			Probe::decayBands(a1, 300.0, 0, 0.55, 1.0, &x0, &x1);
			Probe::decayBands(a2, 300.0, 0, 0.55, 1.0, &y0, &y1);
			double s1 = x1/(x0+1e-9), s2 = y1/(y0+1e-9);
			printf("feature decayLFRatio: low tail slope r=0.3: %.4g  r=2.0: %.4g  %s\n",
				s1, s2, (s1 < s2*0.5 && y1 > 0) ? "OK" : "FAIL");
		}

		/* 2. gainHF at 10kHz steady sine (hfReference 5kHz default) */
		{
			sndReverbParams_t b1=base; b1.gainHF=1.0f;
			sndReverbParams_t b2p=base; b2p.gainHF=0.05f;
			double r1=Probe::sineRMS(b1,10000.0), r2=Probe::sineRMS(b2p,10000.0);
			printf("feature gainHF: 10kHz wet RMS 1.0: %.4g  0.05: %.4g  %s\n",
				r1, r2, (r2 < r1*0.5) ? "OK" : "FAIL");
		}

		/* 3. gainLF at 60Hz steady sine (lfReference 400Hz) */
		{
			sndReverbParams_t c1=base; c1.gainLF=1.0f; c1.lfReference=400.0f;
			sndReverbParams_t c2=base; c2.gainLF=0.05f; c2.lfReference=400.0f;
			double r1=Probe::sineRMS(c1,60.0), r2=Probe::sineRMS(c2,60.0);
			printf("feature gainLF: 60Hz wet RMS 1.0: %.4g  0.05: %.4g  %s\n",
				r1, r2, (r2 < r1*0.5) ? "OK" : "FAIL");
		}

		/* 4. echo: impulse periodicity via autocorrelation at the period */
		{
			sndReverbParams_t e1=base; e1.echoDepth=1.0f; e1.echoTime=0.1f; e1.diffusion=0.0f;
			static int zi[512], di[1024], si[512];
			memset(zi,0,sizeof zi); memset(si,0,sizeof si); si[0]=16384;
			idSoundReverb r; r.Init(); r.SetParams(e1);
			for (int b=0;b<REVERB_XFADE_BLOCKS+2;b++){ memset(di,0,sizeof di); r.ProcessS16(zi,di,512,0.5f); }
			static double sig[44100];
			for (int b=0;b*512<44100;b++){ memset(di,0,sizeof di);
				r.ProcessS16(b?zi:si,di,512,0.5f);
				for (int i=0;i<512 && b*512+i<44100;i++) sig[b*512+i]=di[i*2]; }
			int lag=(int)(0.1*44100), lagOff=(int)(0.073*44100);
			double acPeriod=0, acOff=0;
			for (int i=0;i<44100-lag;i++) acPeriod += sig[i]*sig[i+lag];
			for (int i=0;i<44100-lagOff;i++) acOff += sig[i]*sig[i+lagOff];
			printf("feature echo: autocorr at period %.4g vs off-period %.4g  %s\n",
				acPeriod, acOff, (acPeriod > 4.0*fabs(acOff)+1e-9) ? "OK" : "FAIL");
		}

		/* 5. modulation: steady 882Hz (exactly 50 samples); vibrato
		   decorrelates the wet output from a one-period shift of itself */
		{
			sndReverbParams_t m0=base, m1=base;
			m1.modulationDepth=1.0f; m1.modulationTime=0.25f;
			double corr[2];
			for (int variant=0; variant<2; variant++){
				static int zi[512], di[1024], si[512];
				memset(zi,0,sizeof zi);
				idSoundReverb r; r.Init(); r.SetParams(variant?m1:m0);
				for (int b=0;b<REVERB_XFADE_BLOCKS+2;b++){ memset(di,0,sizeof di); r.ProcessS16(zi,di,512,0.5f); }
				static double sig[88200];
				for (int b=0;b*512<88200;b++){
					for (int i=0;i<512;i++) si[i]=(int)(12000.0*sin(2*3.14159265*882.0*(b*512+i)/44100.0));
					memset(di,0,sizeof di);
					r.ProcessS16(si,di,512,0.5f);
					for (int i=0;i<512 && b*512+i<88200;i++) sig[b*512+i]=di[i*2];
				}
				/* tone purity: energy in the 882Hz bin (Goertzel over the
				   settled second) vs total. Eight phase-offset vibratos
				   summed keep rough carrier coherence, so period
				   correlation misses the effect - sideband spread does not */
				double w0=2*3.14159265*882.0/44100.0, cw=2*cos(w0);
				double s0=0,s1=0,s2=0, tot=0; long n3=0;
				for (int i=44100;i<88200;i++){ s0 = sig[i] + cw*s1 - s2; s2=s1; s1=s0; tot+=sig[i]*sig[i]; n3++; }
				double binP = (s1*s1 + s2*s2 - cw*s1*s2)/(double)n3;
				corr[variant]= binP/(tot/(double)n3+1e-9);
			}
			printf("feature modulation: tone purity off=%.4f on=%.4f  %s\n",
				corr[0], corr[1], (corr[1] < 0.5*corr[0]) ? "OK" : "FAIL");
		}

		/* 6. density: shorter effective taps -> more energy in the first
		   100 ms of the late field */
		{
			static int zi[512], di[1024], si[512];
			double e[2];
			for (int variant=0; variant<2; variant++){
				sndReverbParams_t dp=base; dp.density = variant? 0.05f : 1.0f;
				memset(zi,0,sizeof zi); memset(si,0,sizeof si); si[0]=16384;
				idSoundReverb r; r.Init(); r.SetParams(dp);
				/* warm past the crossfade AND the read-tap slew (worst-case
				   retarget ~1.5s at REVERB_TAP_SLEW_Q16) */
				for (int b=0;b*512<44100*5/2;b++){ memset(di,0,sizeof di); r.ProcessS16(zi,di,512,0.5f); }
				double ee=0;
				for (int b=0;b*512<4410;b++){ memset(di,0,sizeof di);
					r.ProcessS16(b?zi:si,di,512,0.5f);
					for (int i=0;i<1024;i++) ee+=(double)di[i]*di[i]; }
				e[variant]=ee;
			}
			printf("feature density: first-100ms energy d=1: %.4g  d=0.05: %.4g  %s\n",
				e[0], e[1], (e[1] > e[0]*1.05) ? "OK" : "FAIL");
		}

		/* 7. decayHFLimit: high band (>=6kHz) tail with decayHFRatio 2 and
		   maximum air absorption: the limit must steepen the decay */
		{
			sndReverbParams_t f1=base; f1.decayTime=8.0f; f1.decayHFRatio=2.0f;
			f1.airAbsorptionGainHF=0.892f; f1.decayHFLimit=0;
			sndReverbParams_t f2=f1; f2.decayHFLimit=1;
			double x0,x1,y0,y1;
			/* split low (3kHz) and window early (0.6s/1.4s): the integer
			   tail quantizes to zero before late windows open */
			Probe::decayBands(f1, 3000.0, 1, 0.6, 1.4, &x0, &x1);
			Probe::decayBands(f2, 3000.0, 1, 0.6, 1.4, &y0, &y1);
			double s1=x1/(x0+1e-9), s2=y1/(y0+1e-9);
			printf("feature decayHFLimit: high tail slope off=%.4g on=%.4g  %s\n",
				s1, s2, (s2 < s1*0.6 && x1 > 0) ? "OK" : "FAIL");
		}

		/* 8. stability: worst-case params, int path, long run: tail -> 0 */
		{
			sndReverbParams_t w=base; w.decayTime=20.0f; w.decayLFRatio=2.0f;
			w.modulationDepth=1.0f; w.modulationTime=0.04f; w.echoDepth=1.0f;
			w.density=0.05f; w.diffusion=1.0f;
			static int zi[512], di[1024], si[512];
			memset(zi,0,sizeof zi); memset(si,0,sizeof si); si[0]=32767;
			idSoundReverb r; r.Init(); r.SetParams(w);
			for (int b=0;b<REVERB_XFADE_BLOCKS+2;b++){ memset(di,0,sizeof di); r.ProcessS16(zi,di,512,0.5f); }
			long long lastE=0;
			for (int b=0;b*512<44100*80;b++){ memset(di,0,sizeof di);
				r.ProcessS16(b?zi:si,di,512,0.5f);
				if (b*512 > 44100*78){ for (int i=0;i<1024;i++) lastE += (long long)di[i]*di[i]; }
			}
			printf("feature stability: worst-case int tail energy after 78s: %lld  %s\n",
				lastE, lastE==0 ? "OK (decayed to exact zero)" : (lastE<100 ? "OK (below noise)" : "FAIL"));
		}
	}
	return 0;
}
static int _rt = reverb_test();
