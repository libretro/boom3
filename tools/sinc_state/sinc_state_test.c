/* Regression test for the sinc resampler stream state boom3 relies on:
 * the handle pool's reset must give output identical to a fresh handle,
 * and a state saved mid-stream and restored into another, already used
 * handle must continue the stream bit-exactly. Covers the fixed ratios
 * snd_decoder runs and ratios driven below the design ratio, where the
 * cascaded decimation stages engage and crossfade in, so their rings and
 * counters are live stream state.
 *
 * build: make -C tools/sinc_state
 * run:   make -C tools/sinc_state check */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <audio/audio_resampler.h>

extern retro_resampler_t sinc_resampler;
size_t rarch_sinc_resampler_state_size(void *re_);
void rarch_sinc_resampler_get_state(void *re_, void *out);
void rarch_sinc_resampler_set_state(void *re_, const void *in);

#define BLOCK  512
#define BLOCKS 64

static float input[BLOCKS][BLOCK * 2];

static void make_input(void)
{
   unsigned b, i;
   unsigned seed = 12345;
   for (b = 0; b < BLOCKS; b++)
      for (i = 0; i < BLOCK; i++)
      {
         double t = (double)(b * BLOCK + i);
         seed = seed * 1103515245u + 12345u;
         input[b][2 * i]     = (float)(0.5 * sin(t * 0.031)
               + ((seed >> 16) & 0xff) / 2048.0);
         input[b][2 * i + 1] = (float)(0.4 * sin(t * 0.0173 + 1.0));
      }
}

/* Runs blocks [first, last) through re, appending to out; returns the
 * number of output frames. */
static size_t run(void *re, double ratio, unsigned first, unsigned last,
      float *out)
{
   unsigned b;
   size_t total = 0;
   for (b = first; b < last; b++)
   {
      struct resampler_data d;
      d.data_in       = input[b];
      d.input_frames  = BLOCK;
      d.data_out      = out + 2 * total;
      d.output_frames = 0;
      d.ratio         = ratio;
      sinc_resampler.process(re, &d);
      total          += d.output_frames;
   }
   return total;
}

/* @design builds the handle, @ratio drives it, the stream is saved
 * after @split blocks. */
static int check_ratio(double design, double ratio, unsigned split)
{
   size_t n_ref, n_a, n_b, n_pool, sz;
   size_t cap = (size_t)BLOCK * BLOCKS * 2 * 8 + 4096;
   float *ref   = (float*)calloc(cap, sizeof(float));
   float *cont  = (float*)calloc(cap, sizeof(float));
   float *dirty = (float*)calloc(cap, sizeof(float));
   float *pool  = (float*)calloc(cap, sizeof(float));
   unsigned char *state;
   void *re, *re2;
   int fail = 0;

   /* Reference: one uninterrupted stream. */
   re    = sinc_resampler.init(NULL, design, RESAMPLER_QUALITY_HIGHER, 0);
   n_ref = run(re, ratio, 0, BLOCKS, ref);

   /* Pool: a used handle, reset, must match a fresh one exactly. */
   sinc_resampler.reset(re);
   n_pool = run(re, ratio, 0, BLOCKS, pool);
   if (n_pool != n_ref || memcmp(pool, ref, n_ref * 2 * sizeof(float)))
   {
      printf("FAIL %.4f@%.4f: reset handle differs from fresh\n",
            ratio, design);
      fail = 1;
   }
   sinc_resampler.free(re);

   /* Savestate: save after split, restore into a used handle, go on. */
   re    = sinc_resampler.init(NULL, design, RESAMPLER_QUALITY_HIGHER, 0);
   n_a   = run(re, ratio, 0, split, cont);
   sz    = rarch_sinc_resampler_state_size(re);
   state = (unsigned char*)malloc(sz);
   rarch_sinc_resampler_get_state(re, state);
   sinc_resampler.free(re);

   re2 = sinc_resampler.init(NULL, design, RESAMPLER_QUALITY_HIGHER, 0);
   run(re2, ratio, 7, 7 + split / 2 + 2, dirty);
   if (rarch_sinc_resampler_state_size(re2) != sz)
   {
      printf("FAIL %.4f@%.4f: state size differs between handles\n",
            ratio, design);
      fail = 1;
   }
   rarch_sinc_resampler_set_state(re2, state);
   n_b = run(re2, ratio, split, BLOCKS, cont + 2 * n_a);
   sinc_resampler.free(re2);
   free(state);

   if (n_a + n_b != n_ref
         || memcmp(cont, ref, n_ref * 2 * sizeof(float)))
   {
      printf("FAIL %.4f@%.4f split %2u: restored stream diverges "
            "(%u vs %u frames)\n", ratio, design, split,
            (unsigned)(n_a + n_b), (unsigned)n_ref);
      fail = 1;
   }
   else
      printf("ok   %.4f@%.4f split %2u: %u frames, state %u bytes\n",
            ratio, design, split, (unsigned)n_ref, (unsigned)sz);

   free(ref);
   free(cont);
   free(dirty);
   free(pool);
   return fail;
}

int main(void)
{
   int fail = 0;
   make_input();
   /* Fixed ratios, as snd_decoder runs them. */
   fail |= check_ratio(48000.0 / 22050.0, 48000.0 / 22050.0, 35);
   fail |= check_ratio(48000.0 / 44100.0, 48000.0 / 44100.0, 35);
   fail |= check_ratio(22050.0 / 48000.0, 22050.0 / 48000.0, 35);
   /* Driven below design: stages engage; split mid-crossfade and
    * settled. */
   fail |= check_ratio(1.0, 0.45, 1);
   fail |= check_ratio(1.0, 0.45, 3);
   fail |= check_ratio(1.0, 0.45, 40);
   fail |= check_ratio(1.0, 0.2, 2);
   fail |= check_ratio(1.0, 0.2, 40);
   return fail;
}
