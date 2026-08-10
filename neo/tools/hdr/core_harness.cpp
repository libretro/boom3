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
extern "C" void _ZL14hdr_bind_scenev(void);
extern "C" void _ZL11hdr_presentj(unsigned int dstFbo);
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
	int i, nonBlack = 0, fail = 0;

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

	glConfig.ARBFragmentProgramAvailable = !noArb;
	glConfig.ARBVertexProgramAvailable = true;
	hdr_output_active = true;
	environ_cb = (void *)env_cb;
	scr_width = W;
	scr_height = H;

	/* the frame, in the order retro_run runs it */
	if (!hdr_ensure_target(W, H)) {
		printf("  FAIL: hdr_ensure_target declined - the composite never ran\n");
		return 1;
	}
	if (!noBind)
		hdr_bind_scene();
	glViewport(0, 0, W, H);
	glClearColor(0.f, 0.f, 0.f, 0.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	drawWorld();
	drawHUD();
	hdr_present(0);
	glFinish();

	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out);
	for (i = 0; i < W * H; i++)
		if (out[i * 4] > 2 || out[i * 4 + 1] > 2 || out[i * 4 + 2] > 2)
			nonBlack++;
	printf("  non-black pixels: %d of %d\n", nonBlack, W * H);
	if (nonBlack < W * H / 4) {
		printf("  FAIL: the frame came out black\n");
		fail = 1;
	} else {
		int floorPx = out[(H / 2 * W + W / 6) * 4];
		int wallPx  = out[(H / 2 * W + W / 2) * 4];
		int hotPx   = out[(H / 2 * W + 5 * W / 6) * 4];
		int hudPx   = out[((H - 5) * W + W / 2) * 4];
		printf("  floor %d, wall %d, highlight %d, HUD %d\n",
			floorPx, wallPx, hotPx, hudPx);
		/* 13, 153 and 255 are the input quads scaled to bytes.  Getting
		   those back means every HDR path declined and glReadPixels
		   handed over what the harness itself drew - which is exactly
		   how an earlier version of this file reported PASS. */
		if (floorPx == 13 && wallPx == 153 && hotPx == 255) {
			printf("  FAIL: output equals the input - the composite did not run\n");
			fail = 1;
		}
		if (!(floorPx < wallPx && wallPx < hotPx)) {
			printf("  FAIL: the regions are not ordered\n");
			fail = 1;
		}
	}
	printf("%s\n", fail ? "  FAILED" : "  PASS");
	OSMesaDestroyContext(ctx);
	return fail;
}
