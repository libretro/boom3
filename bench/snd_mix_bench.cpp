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

#define MIXBUFFER_SAMPLES 4096

#ifdef BENCH_SCALAR
#define SND_MIX_NO_SIMD 1
#endif
#include "../neo/sound/snd_mix_kernels.h"

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
	const short lastQ[2]={(short)(0.31f*32768),(short)(0.87f*32768)};
	const short curQ[2]={(short)(0.93f*32768),(short)(0.12f*32768)};

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
	}

	// Snd_SumToS16 saturation exactness
	for(int i=0;i<64;i++) accA[i]=(int)(rng())%200000-100000;
	Snd_SumToS16(outA, accA, 64);
	int sat=0; for(int i=0;i<64;i++){int v=accA[i];short e=(short)(v<-32768?-32768:(v>32767?32767:v)); if(outA[i]!=e)sat++;}
	printf("Snd_SumToS16 saturation: %s\n", sat?"FAIL":"bit-exact");


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
