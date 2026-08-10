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
extern "C" int _ZL16hdr_rolloff_mode;
/* retro_run clears this at the top of every frame; the harness has to
   do the same or the second frame reuses the first one's mapped image */
extern "C" bool _ZL16hdr_scene_mapped __attribute__((weak));
extern "C" bool _ZL15hdr_world_drawn __attribute__((weak));
extern "C" void _ZL14hdr_bind_scenev(void);
extern "C" void _ZL11hdr_presentj(unsigned int dstFbo);
/* Present on builds that map the scene before the HUD; weak, so this
   file drives either shape of frame. */
/* Call what RB_DrawView calls, not the static behind it - the gate that
   decides whether mapping is safe lives in the public entry point, and a
   harness that reaches past it tests a path the engine never takes. */
void HDR_MapSceneBeforeHUD(void) __attribute__((weak));
/* RB_DrawView calls these; a world view notes itself, a 2D view maps */
void HDR_NoteWorldView(void) __attribute__((weak));
extern "C" void _ZL18hdr_encode_presentj(unsigned int dstFbo) __attribute__((weak));
/* present when the scene is mapped before the HUD; absent on builds
   without the two-pass split, hence the weak symbols */
/* Call what RB_DrawView calls, not the static behind it - the gate that
   decides whether mapping is safe lives in the public entry point, and a
   harness that reaches past it tests a path the engine never takes. */
void HDR_MapSceneBeforeHUD(void) __attribute__((weak));
/* RB_DrawView calls these; a world view notes itself, a 2D view maps */
void HDR_NoteWorldView(void) __attribute__((weak));
extern "C" void _ZL18hdr_encode_presentj(unsigned int dstFbo) __attribute__((weak));
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
	/* A frame that issues a 2D view before the world: a GUI ahead of the
	   scene, which the engine does.  Mapping there would composite an
	   empty target and bind the mapped image, so the world that follows
	   would draw into a display-referred buffer and never be tone
	   mapped - a blown out picture, not a subtle one. */
	const int twoDFirst = (argc > 1 && !strcmp(argv[1], "--2d-first"));
	/* The engine pumps many swaps per retro_run - every menu frame and
	 * every loading-screen update.  Each is a whole frame: composite,
	 * present, rebind.  Testing one swap per run misses anything that
	 * only goes wrong on the second, which is where the black title
	 * screen lived. */
	const int staleMap = (argc > 1 && !strcmp(argv[1], "--stale-map"));
	const int swaps = 3;
	int swap;
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
	for (pass = 0; pass < 2; pass++) {
	_ZL16hdr_rolloff_mode = pass ? 18 : 0;   /* Filmic Log, then Reinhard */
	for (swap = 0; swap < swaps; swap++) {
	/* what GLimp_SwapBuffers does at the top of every engine frame.
	   --stale-map skips it, reproducing the black title screen. */
	if (&_ZL16hdr_scene_mapped && !staleMap)
		_ZL16hdr_scene_mapped = false;
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
	if (twoDFirst) {
		/* the early 2D view, before any world has drawn */
		if (HDR_MapSceneBeforeHUD)
			HDR_MapSceneBeforeHUD();
		drawHUD();
		if (!noBind)
			hdr_bind_scene();
	}
	if (!menuOnly) {
		if (HDR_NoteWorldView)
			HDR_NoteWorldView();   /* what a world view does in RB_DrawView */
		drawWorld();
	}
	if (HDR_MapSceneBeforeHUD && _ZL18hdr_encode_presentj) {
		/* the two-pass frame: what RB_DrawView calls when a 2D view is
		   about to draw, then the HUD, then the encode-only present */
		HDR_MapSceneBeforeHUD();
		/* The engine rebinds the scene target between views - loading
		   screen pumps do it every pump.  Anything drawn after the map
		   has to still land on the mapped image. */
		if (!noBind)
			hdr_bind_scene();
		drawHUD();
		/* the end-of-frame decision retro_run makes: encode only if the
		   scene was actually mapped, otherwise the single-pass present */
		if (&_ZL16hdr_scene_mapped && _ZL16hdr_scene_mapped)
			_ZL18hdr_encode_presentj(0);
		else
			hdr_present(0);
	} else {
		drawHUD();
		hdr_present(0);
	}
	glFinish();
	}   /* swap */

	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out);
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
	nonBlack = 0;
	}
	printf("  non-black pixels: %d of %d\n", nonBlackAll, W * H);
	if (menuOnly) {
		/* only the HUD strip is drawn, so most of the frame is legitimately
		   black; what matters is that the strip survived at all */
		printf("  menu-only: HUD %d (must be non-zero)\n", hudPx[0]);
		printf("%s\n", hudPx[0] > 2 ? "  PASS" : "  FAIL: the menu never reached the screen");
		return hudPx[0] > 2 ? 0 : 1;
	}
	if (nonBlackAll < W * H / 4) {
		printf("  FAIL: the frame came out black\n");
		fail = 1;
	}
	/* the input quads scaled to bytes; getting them back means nothing ran */
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
		printf("  FAIL: the scene did not change with the curve\n");
		fail = 1;
	}
	if (hudPx[0] != hudPx[1]) {
		printf("  FAIL: the HUD changed with the curve (%d vs %d) - it is\n"
		       "        going through the scene's tone curve\n",
			hudPx[0], hudPx[1]);
		fail = 1;
	} else {
		printf("  HUD is curve-independent at %d, scene moved %d -> %d\n",
			hudPx[0], wallPx[0], wallPx[1]);
	}
	printf("%s\n", fail ? "  FAILED" : "  PASS");
	OSMesaDestroyContext(ctx);
	return fail;
}
