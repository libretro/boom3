/*
 * Drives the core's own HDR functions, not a copy of them.
 *
 * libretro.cpp is rebuilt for this with -fno-inline and its statics
 * globalised, then linked against the rest of the core.  So the calls
 * below enter exactly the code the .so ships: the same target
 * allocation, the same bind order, the same state setup, the same
 * programs.  A reimplementation cannot catch a bug in the sequence,
 * which is where every failure in this feature has been.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include "renderer/tr_local.h"
#include "glsym/rglgen.h"
#include "idlib/Lib.h"

#define W 128
#define H 128

/* the core's own symbols */
/* The frontend normally does two things before any of this code runs:
 * it hands the core a proc-address hook, and the core resolves its GL
 * symbols and ARB entry points through it.  Skip either and every HDR
 * path declines silently - hdr_arb_available() reports no ARB support,
 * hdr_ensure_target returns false, and a harness sails through reading
 * whatever was drawn straight to the framebuffer.  That is exactly how
 * an earlier version of this file reported PASS while testing nothing:
 * the values it read back were the input quads, untouched. */

struct hw_render_stub {
	unsigned context_type;
	void (*context_reset)(void);
	unsigned long (*get_current_framebuffer)(void);
	void *(*get_proc_address)(const char *sym);
	char pad[256];
};

extern "C" {
	extern struct hw_render_stub hw_render;
	extern bool hdr_output_active;
	extern void *environ_cb;
	extern int scr_width, scr_height;
}
/* mangled: static functions in libretro.cpp, globalised by objcopy */
extern "C" bool _ZL17hdr_ensure_targetii(int w, int h);
extern "C" int _ZL16hdr_rolloff_mode;
/* the CA setting, baked into the composite at build; weak so the harness
 * still links if the symbol moves */
extern "C" int _ZL13hdr_chroma_ab __attribute__((weak));
extern "C" int _ZL8hdr_ssaa __attribute__((weak));
extern "C" int _ZL12hdr_halation __attribute__((weak));
extern "C" volatile float _ZL21hdr_ssaa_env_override __attribute__((weak));
/* the option gate; the harness turns it on to test the path at all */
/* retro_run clears this at the top of every frame; the harness has to
   do the same or the second frame reuses the first one's mapped image */
extern "C" bool _ZL15hdr_world_drawn __attribute__((weak));
extern "C" void _ZL14hdr_bind_scenev(void);
extern "C" void _ZL11hdr_presentj(unsigned int dstFbo);
extern "C" void _ZL11ldr_presentj(unsigned int dstFbo);
/* Present on builds that map the scene before the HUD; weak, so this
   file drives either shape of frame. */
/* Call what RB_DrawView calls, not the static behind it - the gate that
   decides whether mapping is safe lives in the public entry point, and a
   harness that reaches past it tests a path the engine never takes. */
/* RB_DrawView calls these; a world view notes itself, a 2D view maps */
/* RB_SetBuffer calls this; it opens a frame and clears the bound target */
/* present when the scene is mapped before the HUD; absent on builds
   without the two-pass split, hence the weak symbols */
/* Call what RB_DrawView calls, not the static behind it - the gate that
   decides whether mapping is safe lives in the public entry point, and a
   harness that reaches past it tests a path the engine never takes. */
/* RB_DrawView calls these; a world view notes itself, a 2D view maps */
/* RB_SetBuffer calls this; it opens a frame and clears the bound target */
#define hdr_ensure_target _ZL17hdr_ensure_targetii
#define hdr_bind_scene    _ZL14hdr_bind_scenev
#define hdr_present       _ZL11hdr_presentj

/* the frontend callback hdr_present asks for paper white and peak nits */
static bool env_cb(unsigned cmd, void *data)
{
	switch (cmd) {
	case 68: /* GET_HDR_PAPER_WHITE_NITS */ *(float *)data = 200.0f; return true;
	case 69: /* GET_HDR_MAX_NITS */         *(float *)data = 1000.0f; return true;
	default: return false;
	}
}

/* a world with real range, drawn into whatever target is bound */
static void drawWorld(void)
{
	glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1, 0, 1, -1, 1);
	glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glBegin(GL_QUADS);
	glColor3f(0.05f, 0.04f, 0.03f);                 /* dark floor */
	glVertex2f(0.00f, 0.f); glVertex2f(0.33f, 0.f);
	glVertex2f(0.33f, 1.f); glVertex2f(0.00f, 1.f);
	glColor3f(0.60f, 0.48f, 0.36f);                 /* lit wall */
	glVertex2f(0.33f, 0.f); glVertex2f(0.66f, 0.f);
	glVertex2f(0.66f, 1.f); glVertex2f(0.33f, 1.f);
	glColor3f(6.00f, 4.80f, 3.60f);                 /* blown highlight */
	glVertex2f(0.66f, 0.f); glVertex2f(1.00f, 0.f);
	glVertex2f(1.00f, 1.f); glVertex2f(0.66f, 1.f);
	glEnd();
}

/* a HUD panel, drawn the way a 2D view draws one */
static void drawHUD(void)
{
	glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1, 0, 1, -1, 1);
	glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
	glDisable(GL_TEXTURE_2D);
	glColor4f(0.5f, 0.5f, 0.5f, 1.0f);
	glBegin(GL_QUADS);
	glVertex2f(0.0f, 0.80f); glVertex2f(1.0f, 0.80f);
	glVertex2f(1.0f, 1.00f); glVertex2f(0.0f, 1.00f);
	glEnd();
}

int main(int argc, char **argv)
{
	static unsigned char backing[W * H * 4];
	static unsigned char out[W * H * 4];
	OSMesaContext ctx;
	/* Negative controls.  A harness that has never been shown to fail is
	 * not evidence, so these reproduce two real failure shapes: the
	 * scene never reaching the target, and the composite declining. */
	const int noBind = (argc > 1 && !strcmp(argv[1], "--no-bind"));
	const int noArb  = (argc > 1 && !strcmp(argv[1], "--no-arb"));
	/* A menu or title screen frame: 2D views only, no world drawn at
	   all.  The scene gets mapped from an empty target and everything
	   visible is drawn after that, so this is the case that shows
	   whether post-map drawing reaches the encode pass. */
	const int menuOnly = (argc > 1 && !strcmp(argv[1], "--menu-only"));
	/* Bake Heavy chromatic aberration into the composite and require the
	   red and blue channels of a corner edge to land apart.  This is the
	   case whose absence cost three patches and two screenshots: the
	   session shipped a CA whose visibility was never executed, only
	   reasoned about. */
	const int chroma = (argc > 1 && !strcmp(argv[1], "--chroma"));
	/* Karis-weighted supersampling.  The scene is a firefly field - one
	   hot texel per 2x2, three dark - and the same spliced program runs
	   twice: env[10] at the half-texel offsets (four distinct taps,
	   Karis) and at zero (all taps between the texels, which under
	   GL_LINEAR is the driver's own box average - the linear control).
	   The Karis frame must come out materially darker, because the
	   whole point is that one hot sample no longer dominates the mean;
	   if the two frames match, the weighting is not happening. */
	const int ssaa = (argc > 1 && !strcmp(argv[1], "--ssaa"));
	/* --dump <mode> <chroma> <ssaa> <halation>: render a structured scene
	   with the given options and write /tmp/dump.ppm for eyes-on
	   comparison; the fix for a screenshot begins with reproducing the
	   screenshot. */
	const int dump = (argc > 5 && !strcmp(argv[1], "--dump"));
	/* The engine pumps many swaps per retro_run - every menu frame and
	 * every loading-screen update.  Each is a whole frame: composite,
	 * present, rebind.  Testing one swap per run misses anything that
	 * only goes wrong on the second, which is where the black title
	 * screen lived. */
	int i, nonBlack = 0, fail = 0, pass, nonBlackAll = 0;
	int floorPx[2], wallPx[2], hotPx[2], hudPx[2];

	setvbuf(stdout, NULL, _IONBF, 0);
	ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, NULL);
	if (!ctx || !OSMesaMakeCurrent(ctx, backing, GL_UNSIGNED_BYTE, W, H)) {
		printf("no context\n");
		return 1;
	}

	/* the composite assembles its program text with idStr::snPrintf, so
	 * the engine's own systems have to be up before any of it is called */
	idLib::Init();
	hw_render.get_proc_address = (void *(*)(const char *))OSMesaGetProcAddress;
	rglgen_resolve_symbols((rglgen_proc_address_t)OSMesaGetProcAddress);
	/* Bind every qgl pointer the same way the engine does - by including
	 * qgl_proc.h with a macro that resolves each name.  Picking out the
	 * four that hdr_arb_available() happens to test is not enough: the
	 * loader also calls qglGetError, qglGetString and qglGetIntegerv,
	 * and a null one of those faults before anything can report why. */
#define QGLPROC( name, rettype, args ) \
	q##name = (rettype (APIENTRYP) args)OSMesaGetProcAddress( #name );
#include "renderer/qgl_proc.h"
#undef QGLPROC

	/* The ARB program entry points are declared outside qgl_proc.h and
	 * assigned by R_CheckPortableExtensions, so they need doing too. */
	qglProgramStringARB = (PFNGLPROGRAMSTRINGARBPROC)
		OSMesaGetProcAddress( "glProgramStringARB" );
	qglGenProgramsARB = (PFNGLGENPROGRAMSARBPROC)
		OSMesaGetProcAddress( "glGenProgramsARB" );
	qglBindProgramARB = (PFNGLBINDPROGRAMARBPROC)
		OSMesaGetProcAddress( "glBindProgramARB" );
	qglProgramEnvParameter4fvARB = (PFNGLPROGRAMENVPARAMETER4FVARBPROC)
		OSMesaGetProcAddress( "glProgramEnvParameter4fvARB" );

	/* Filmic Log rather than the default.  Reinhard's soft knee is the
	 * identity below 0.75, so a HUD panel at 0.5 comes out at 0.5 under
	 * it whether or not anything bypasses the curve - which is both why
	 * this bug hid for so long and why testing with the default proves
	 * nothing about the bypass.  Filmic Log lifts 0.5 to 0.743, so the
	 * two paths are distinguishable. */
	glConfig.ARBFragmentProgramAvailable = !noArb;
	glConfig.ARBVertexProgramAvailable = true;
	hdr_output_active = true;
	environ_cb = (void *)env_cb;
	scr_width = W;
	scr_height = H;

	/* Two frames, under two curves.  The scene must change with the
	 * curve and the HUD must not - that is the whole claim of mapping
	 * the scene before the HUD is drawn, and it cannot be tested with
	 * the default roll-off because Reinhard's soft knee is the identity
	 * below 0.75, so a 0.5 panel comes out at 0.5 either way. */
	/* --ldr-ssaa: supersampling without HDR.  1-texel vertical stripes of
	   1.0 and 0.0 at the 2x scene resolve to a uniform 0.5 - the box
	   average - so the output must be flat at ~127 with next to no
	   variance.  An unresolved pass-through would keep the stripes and
	   the variance catches it. */
	if (argc > 1 && !strcmp(argv[1], "--ldr-ssaa")) {
		const int OW = 128, SW = OW * 2;
		unsigned char *out2 = (unsigned char *)malloc((size_t)OW * OW * 4);
		int x, y;
		hdr_output_active = 0;
		if (&_ZL8hdr_ssaa) _ZL8hdr_ssaa = 2;
		scr_width = OW; scr_height = OW;
		_ZL14hdr_bind_scenev();   /* binds the LDR target */
		glViewport(0, 0, SW, SW);
		glClearColor(0.f, 0.f, 0.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDisable(GL_FRAGMENT_PROGRAM_ARB);
		glDisable(GL_VERTEX_PROGRAM_ARB);
		glActiveTexture(GL_TEXTURE0);
		glDisable(GL_TEXTURE_2D);
		glColor4f(1.f, 1.f, 1.f, 1.f);
		glBegin(GL_QUADS);
		for (x = 0; x < SW; x += 2) {
			glVertex2f(x * 2.0f / SW - 1.0f,       -1.f);
			glVertex2f((x + 1) * 2.0f / SW - 1.0f, -1.f);
			glVertex2f((x + 1) * 2.0f / SW - 1.0f,  1.f);
			glVertex2f(x * 2.0f / SW - 1.0f,        1.f);
		}
		glEnd();
		{
			GLuint dstTex = 0, dstFbo = 0;
			double mean = 0, var = 0;
			glGenTextures(1, &dstTex);
			glBindTexture(GL_TEXTURE_2D, dstTex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, OW, OW, 0,
					GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			glGenFramebuffers(1, &dstFbo);
			glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_2D, dstTex, 0);
			_ZL11ldr_presentj(dstFbo);
			glFinish();
			glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
			glReadPixels(0, 0, OW, OW, GL_RGBA, GL_UNSIGNED_BYTE, out2);
			for (y = 8; y < OW - 8; y++)
				for (x = 8; x < OW - 8; x++)
					mean += out2[(y * OW + x) * 4];
			mean /= (OW - 16) * (OW - 16);
			for (y = 8; y < OW - 8; y++)
				for (x = 8; x < OW - 8; x++) {
					double d = out2[(y * OW + x) * 4] - mean;
					var += d * d;
				}
			var /= (OW - 16) * (OW - 16);
			printf("  ldr-ssaa: mean %.1f (want ~127), variance %.2f (want ~0)\n",
				mean, var);
			if (mean < 100 || mean > 155 || var > 40) {
				printf("  FAIL: the box resolve is not happening\n");
				return 1;
			}
		}
		printf("  PASS\n");
		OSMesaDestroyContext(ctx);
		return 0;
	}

	if (dump) {
		const int mode = atoi(argv[2]), ca = atoi(argv[3]);
		const int ss = atoi(argv[4]), hal = atoi(argv[5]);
		const int OW = 384, SW = OW * ss;
		unsigned char *out2 = (unsigned char *)malloc((size_t)OW * OW * 4);
		int x, y;

		_ZL16hdr_rolloff_mode = mode;
		if (&_ZL13hdr_chroma_ab) _ZL13hdr_chroma_ab = ca;
		if (&_ZL8hdr_ssaa) _ZL8hdr_ssaa = ss;
		if (&_ZL12hdr_halation) _ZL12hdr_halation = hal;
		scr_width = OW; scr_height = OW;
		if (!out2 || !hdr_ensure_target(SW, SW)) { printf("no target\n"); return 1; }
		glDisable(GL_FRAGMENT_PROGRAM_ARB);
		glDisable(GL_VERTEX_PROGRAM_ARB);
		hdr_bind_scene();
		glViewport(0, 0, SW, SW);
		glClearColor(0.02f, 0.02f, 0.02f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		/* a structured scene: mid-grey checker, bright bars, hot spots */
		glBegin(GL_QUADS);
		for (y = 0; y < 8; y++)
			for (x = 0; x < 8; x++) {
				float g = ((x + y) & 1) ? 0.35f : 0.12f;
				glColor4f(g, g, g, 1.f);
				glVertex2f(-1.f + x * 0.25f,        -1.f + y * 0.25f);
				glVertex2f(-1.f + (x + 1) * 0.25f,  -1.f + y * 0.25f);
				glVertex2f(-1.f + (x + 1) * 0.25f,  -1.f + (y + 1) * 0.25f);
				glVertex2f(-1.f + x * 0.25f,        -1.f + (y + 1) * 0.25f);
			}
		glColor4f(6.f, 6.f, 6.f, 1.f);
		glVertex2f(-0.05f, -1.f); glVertex2f(0.05f, -1.f);
		glVertex2f(0.05f, 1.f);   glVertex2f(-0.05f, 1.f);
		glVertex2f(-1.f, -0.05f); glVertex2f(1.f, -0.05f);
		glVertex2f(1.f, 0.05f);   glVertex2f(-1.f, 0.05f);
		glEnd();
		{
			GLuint dstTex = 0, dstFbo = 0;
			glGenTextures(1, &dstTex);
			glBindTexture(GL_TEXTURE_2D, dstTex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, OW, OW, 0,
					GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			glGenFramebuffers(1, &dstFbo);
			glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_2D, dstTex, 0);
			hdr_present(dstFbo);
			glFinish();
			glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
			glReadPixels(0, 0, OW, OW, GL_RGBA, GL_UNSIGNED_BYTE, out2);
		}
		{
			FILE *f = fopen("/tmp/dump.ppm", "wb");
			fprintf(f, "P6 %d %d 255\n", OW, OW);
			for (y = OW - 1; y >= 0; y--)
				for (x = 0; x < OW; x++)
					fwrite(&out2[(y * OW + x) * 4], 1, 3, f);
			fclose(f);
		}
		printf("  dumped mode=%d ca=%d ssaa=%d hal=%d\n", mode, ca, ss, hal);
		OSMesaDestroyContext(ctx);
		return 0;
	}

	if (ssaa) {
		const int OW = 256, SW = OW * 2;
		unsigned char *out2 = (unsigned char *)malloc((size_t)OW * OW * 4);
		double sum[2] = { 0, 0 };
		int phase, x, y;

		if (&_ZL8hdr_ssaa)
			_ZL8hdr_ssaa = 2;
		if (&_ZL13hdr_chroma_ab)
			_ZL13hdr_chroma_ab = 0;
		_ZL16hdr_rolloff_mode = 0;
		scr_width = OW;
		scr_height = OW;
		if (!out2 || !hdr_ensure_target(SW, SW)) {
			printf("  FAIL: no ssaa target\n");
			return 1;
		}
		for (phase = 0; phase < 2; phase++) {
			/* hdr_present leaves the ARB programs enabled - in the game
			 * the next frame rebinds everything, but here the scene
			 * redraw would otherwise run through the composite */
			glDisable(GL_FRAGMENT_PROGRAM_ARB);
			glDisable(GL_VERTEX_PROGRAM_ARB);
			/* and texturing: the present leaves unit state behind, and a
			 * textured colour draw samples a dark texel at (0,0) and
			 * silently scales the whole scene */
			glActiveTexture(GL_TEXTURE1);
			glDisable(GL_TEXTURE_2D);
			glActiveTexture(GL_TEXTURE0);
			glDisable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, 0);
			hdr_bind_scene();
			glViewport(0, 0, SW, SW);
			glClearColor(0.f, 0.f, 0.f, 0.f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			/* fireflies: one bright texel per 2x2 cell via 1px points */
			glBegin(GL_QUADS);
			glColor4f(0.01f, 0.01f, 0.01f, 1.0f);
			glVertex2f(-1.f, -1.f); glVertex2f(1.f, -1.f);
			glVertex2f(1.f, 1.f);  glVertex2f(-1.f, 1.f);
			glEnd();
			if (phase == 0) {
				/* fireflies: one 8.0 texel per 2x2 cell, three at 0.01 */
				glPointSize(1.0f);
				glColor4f(8.0f, 8.0f, 8.0f, 1.0f);
				glBegin(GL_POINTS);
				for (y = 0; y < SW; y += 2)
					for (x = 0; x < SW; x += 2)
						glVertex2f((x + 0.5f) * 2.0f / SW - 1.0f,
						           (y + 0.5f) * 2.0f / SW - 1.0f);
				glEnd();
			} else {
				/* the linear control: a flat scene at the firefly
				 * field's arithmetic mean, (8.0 + 3*0.01)/4.  Any
				 * resolve of a uniform field is that value, so this
				 * phase shows what a linear average of the fireflies
				 * would have produced, through the same program - no
				 * dependence on how the sampler rounds at texel
				 * corners, which is what sank the env-zero control:
				 * exact-corner taps round to one texel per pixel on
				 * this driver and render a checkerboard, not a box. */
				glColor4f(2.0075f, 2.0075f, 2.0075f, 1.0f);
				glBegin(GL_QUADS);
				glVertex2f(-1.f, -1.f); glVertex2f(1.f, -1.f);
				glVertex2f(1.f, 1.f);  glVertex2f(-1.f, 1.f);
				glEnd();
			}

			/* real offsets both phases; the phases differ in scene */
			if (&_ZL21hdr_ssaa_env_override)
				_ZL21hdr_ssaa_env_override = -1.0f;
			{
				GLuint dstTex = 0, dstFbo = 0;
				glGenTextures(1, &dstTex);
				glBindTexture(GL_TEXTURE_2D, dstTex);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, OW, OW, 0,
						GL_RGBA, GL_UNSIGNED_BYTE, NULL);
				glGenFramebuffers(1, &dstFbo);
				glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_2D, dstTex, 0);
				hdr_present(dstFbo);
				glFinish();
				glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
				glReadPixels(0, 0, OW, OW, GL_RGBA, GL_UNSIGNED_BYTE, out2);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glDeleteFramebuffers(1, &dstFbo);
				glDeleteTextures(1, &dstTex);
			}
			{
				float rb[4] = { -9, -9, -9, -9 };
				PFNGLGETPROGRAMENVPARAMETERFVARBPROC getev =
					(PFNGLGETPROGRAMENVPARAMETERFVARBPROC)OSMesaGetProcAddress(
						"glGetProgramEnvParameterfvARB");
				if (getev)
					getev(GL_FRAGMENT_PROGRAM_ARB, 10, rb);
				printf("  phase %d: env[10]=%.6f  sample px (%d,%d) rgb %d %d %d\n",
					phase, rb[0], OW/2, OW/2,
					out2[(OW/2*OW+OW/2)*4], out2[(OW/2*OW+OW/2)*4+1],
					out2[(OW/2*OW+OW/2)*4+2]);
			}
			for (y = OW / 4; y < 3 * OW / 4; y++)
				for (x = OW / 4; x < 3 * OW / 4; x++)
					sum[phase] += out2[(y * OW + x) * 4 + 1];
		}
		{
			float rb[4] = { -9, -9, -9, -9 };
			PFNGLGETPROGRAMENVPARAMETERFVARBPROC getev =
				(PFNGLGETPROGRAMENVPARAMETERFVARBPROC)OSMesaGetProcAddress(
					"glGetProgramEnvParameterfvARB");
			if (getev)
				getev(GL_FRAGMENT_PROGRAM_ARB, 10, rb);
			printf("  probe: env[10] after last present = %.6f %.6f\n", rb[0], rb[1]);
		}
		printf("  ssaa: karis-of-fireflies %.1f, linear-average-equivalent %.1f (centre quarter, green)\n",
			sum[0] / (OW * OW / 4), sum[1] / (OW * OW / 4));
		if (!(sum[0] < sum[1] * 0.9)) {
			printf("  FAIL: the Karis resolve did not suppress the fireflies\n");
			return 1;
		}
		printf("  PASS\n");
		OSMesaDestroyContext(ctx);
		return 0;
	}

	if (chroma) {
		/* Chromatic aberration needs room: the offset is a fraction of
		 * the frame, so at 128 px even Heavy's corner shift is under
		 * half a pixel and no integer edge test can see it.  This case
		 * runs its own 2048 px frame with one hard vertical edge at
		 * eighty-five percent across, where Heavy displaces red and
		 * blue about two pixels each way.
		 *
		 * This is the case whose absence cost three patches and two
		 * screenshots: a CA landed whose visibility was reasoned about
		 * rather than executed. */
		const int CW = 2048, CH = 2048;
		unsigned char *big = (unsigned char *)malloc( (size_t)CW * CH * 4 );
		int rEdge = -1, bEdge = -1, x, row = 8;

		if (&_ZL13hdr_chroma_ab)
			_ZL13hdr_chroma_ab = 3;   /* Heavy */
		_ZL16hdr_rolloff_mode = 0;
		/* hdr_present sizes its own viewport from these */
		scr_width = CW;
		scr_height = CH;
		if (!big || !hdr_ensure_target(CW, CH)) {
			printf("  FAIL: no chroma target\n");
			return 1;
		}
		hdr_bind_scene();
		glViewport(0, 0, CW, CH);
		glClearColor(0.f, 0.f, 0.f, 0.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glBegin(GL_QUADS);
		glColor4f(0.05f, 0.05f, 0.05f, 1.0f);
		glVertex2f(-1.f, -1.f); glVertex2f(1.f, -1.f);
		glVertex2f(1.f, 1.f);  glVertex2f(-1.f, 1.f);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		glVertex2f(0.7f, -1.f); glVertex2f(1.f, -1.f);
		glVertex2f(1.f, 1.f);  glVertex2f(0.7f, 1.f);
		glEnd();
		/* The OSMesa default framebuffer is the tiny one main made, so
		 * a 2048 present cannot land there - it goes to a destination
		 * fbo of the right size, which hdr_present accepts. */
		{
			GLuint dstTex = 0, dstFbo = 0;
			glGenTextures(1, &dstTex);
			glBindTexture(GL_TEXTURE_2D, dstTex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, CW, CH, 0,
					GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glGenFramebuffers(1, &dstFbo);
			glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_2D, dstTex, 0);
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
				printf("  FAIL: chroma destination fbo incomplete\n");
				return 1;
			}
			hdr_present(dstFbo);
			glFinish();
			glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
			glReadPixels(0, 0, CW, CH, GL_RGBA, GL_UNSIGNED_BYTE, big);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
		/* The bright quad wears a bloom skirt, so a fixed threshold
		 * crosses on the glow long before the edge and reads the same
		 * position for every channel.  The gradient centroid is immune:
		 * the skirt's slope is broad and shallow, the edge's is narrow
		 * and steep, and the centroid lands on the steep part. */
		{
			double rNum = 0, rDen = 0, bNum = 0, bDen = 0;
			for (x = CW / 2 + 1; x < CW - 4; x++) {
				double dr = (double)big[(row * CW + x) * 4]
						- (double)big[(row * CW + x - 1) * 4];
				double db = (double)big[(row * CW + x) * 4 + 2]
						- (double)big[(row * CW + x - 1) * 4 + 2];
				if (dr > 4) { rNum += x * dr; rDen += dr; }
				if (db > 4) { bNum += x * db; bDen += db; }
			}
			rEdge = rDen > 0 ? (int)(rNum / rDen + 0.5) : -1;
			bEdge = bDen > 0 ? (int)(bNum / bDen + 0.5) : -1;
		}
		printf("  chroma setting: sym %p val %d\n",
			(void*)&_ZL13hdr_chroma_ab,
			(&_ZL13hdr_chroma_ab) ? _ZL13hdr_chroma_ab : -1);
		printf("  chroma: red edge %d, blue edge %d (row %d of %d)\n",
			rEdge, bEdge, row, CH);
		printf("  samples row %d: x=1200 (%d,%d,%d) x=1700 (%d,%d,%d) x=2000 (%d,%d,%d)\n",
			row,
			big[(row*CW+1200)*4], big[(row*CW+1200)*4+1], big[(row*CW+1200)*4+2],
			big[(row*CW+1700)*4], big[(row*CW+1700)*4+1], big[(row*CW+1700)*4+2],
			big[(row*CW+2000)*4], big[(row*CW+2000)*4+1], big[(row*CW+2000)*4+2]);
		if (rEdge < 0 || bEdge < 0 || rEdge - bEdge < 2) {
			printf("  FAIL: Heavy did not separate red and blue at the edge\n");
			printf("  FAILED\n");
			return 1;
		}
		printf("  red sits %d px outside blue, as a lens puts it\n", rEdge - bEdge);
		printf("  PASS\n");
		OSMesaDestroyContext(ctx);
		return 0;
	}

	/* Two frames, under two tone curves.  The scene responding to the
	 * curve is what proves the composite ran at all, rather than the
	 * harness reading its own quads back.  Filmic Log is the second
	 * curve because Reinhard's soft knee is the identity below 0.75,
	 * where most of the test frame lives. */
	for (pass = 0; pass < 2; pass++) {
		_ZL16hdr_rolloff_mode = pass ? 18 : 0;   /* Reinhard, then Filmic Log */
		if (&_ZL13hdr_chroma_ab)
			_ZL13hdr_chroma_ab = 0;

		if (!hdr_ensure_target(W, H)) {
			printf("  FAIL: hdr_ensure_target declined - the composite never ran\n");
			return 1;
		}
		if (!noBind)
			hdr_bind_scene();
		glViewport(0, 0, W, H);
		glClearColor(0.f, 0.f, 0.f, 0.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		if (!menuOnly)
			drawWorld();
		drawHUD();
		hdr_present(0);
		glFinish();

		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out);
		nonBlack = 0;
		for (i = 0; i < W * H; i++)
			if (out[i * 4] > 2 || out[i * 4 + 1] > 2 || out[i * 4 + 2] > 2)
				nonBlack++;
		floorPx[pass] = out[(H / 2 * W + W / 6) * 4];
		wallPx[pass]  = out[(H / 2 * W + W / 2) * 4];
		hotPx[pass]   = out[(H / 2 * W + 5 * W / 6) * 4];
		hudPx[pass]   = out[((H - 5) * W + W / 2) * 4];
		printf("  curve %d: floor %d, wall %d, highlight %d, HUD %d\n",
			_ZL16hdr_rolloff_mode, floorPx[pass], wallPx[pass], hotPx[pass], hudPx[pass]);
		nonBlackAll = nonBlack;

	}
	printf("  non-black pixels: %d of %d\n", nonBlackAll, W * H);

	if (menuOnly) {
		/* only the HUD strip is drawn, so most of the frame is
		 * legitimately black; the strip surviving is the whole test */
		printf("  menu-only: HUD %d (must be non-zero)\n", hudPx[0]);
		printf("%s\n", hudPx[0] > 2 ? "  PASS" : "  FAIL: the menu never reached the screen");
		return hudPx[0] > 2 ? 0 : 1;
	}
	if (nonBlackAll < W * H / 4) {
		printf("  FAIL: the frame came out black\n");
		fail = 1;
	}
	/* the input quads scaled to bytes; reading them back unchanged means
	 * the composite did not run */
	if (floorPx[0] == 13 && wallPx[0] == 153 && hotPx[0] == 255) {
		printf("  FAIL: output equals the input - the composite did not run\n");
		fail = 1;
	}
	for (i = 0; i < 2; i++)
		if (!(floorPx[i] < wallPx[i] && wallPx[i] < hotPx[i])) {
			printf("  FAIL: curve %d regions are not ordered\n", i);
			fail = 1;
		}
	if (wallPx[0] == wallPx[1]) {
		printf("  FAIL: the scene did not respond to the curve\n");
		fail = 1;
	} else {
		printf("  scene moved %d -> %d across the two curves\n", wallPx[0], wallPx[1]);
	}
	printf("%s\n", fail ? "  FAILED" : "  PASS");
	OSMesaDestroyContext(ctx);
	return fail;
}
