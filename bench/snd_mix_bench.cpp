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
	return 0;
}
static int _rt = reverb_test();
