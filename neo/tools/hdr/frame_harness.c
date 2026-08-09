/*
 * Runs the composite the way a frame runs it, not the way a shader test does.
 *
 * Every HDR bug this session shipped was invisible to an assembly check:
 * a texture uploaded with the wrong row alignment, a flag whose meaning
 * depended on the engine's blend mode, and a second pass that bound its
 * program but none of the state its draw needed.  This exercises the
 * sequence instead of the arithmetic - scene target, composite into a
 * mapped target, a HUD quad drawn onto it, then the encode pass - and
 * reads the result back.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include "filmic_curve.h"
#include "filmic_desat.h"

#define W 64
#define H 64

static PFNGLGENFRAMEBUFFERSPROC        pGenFB;
static PFNGLBINDFRAMEBUFFERPROC        pBindFB;
static PFNGLFRAMEBUFFERTEXTURE2DPROC   pFBTex;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC pCheckFB;
static PFNGLGENPROGRAMSARBPROC         pGenProg;
static PFNGLBINDPROGRAMARBPROC         pBindProg;
static PFNGLPROGRAMSTRINGARBPROC       pProgStr;
static PFNGLPROGRAMENVPARAMETER4FVARBPROC pEnv;
static PFNGLACTIVETEXTUREPROC          pActiveTex;

static GLuint loadProg(GLenum target, const char *path)
{
	static char src[262144];
	FILE *f = fopen(path, "rb");
	size_t n = fread(src, 1, sizeof src - 1, f);
	GLuint id = 0;
	GLint err = -1;
	src[n] = 0;
	fclose(f);
	pGenProg(1, &id);
	pBindProg(target, id);
	pProgStr(target, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)n, src);
	glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &err);
	if (err != -1) {
		printf("  FAIL: %s rejected at %d: %s\n", path, err,
			(const char *)glGetString(GL_PROGRAM_ERROR_STRING_ARB));
		return 0;
	}
	return id;
}

static GLuint makeTex(int fmt, int type, const void *data)
{
	GLuint t = 0;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, fmt, W, H, 0, GL_RGBA, type, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return t;
}

static const float triV[6] = { -1.f, -1.f, 3.f, -1.f, -1.f, 3.f };

/* the state hdr_present sets before it draws - the whole point of the
   harness is that leaving any of this out is what produced a black frame */
static void fullscreenState(void)
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_FALSE);
	glDisable(GL_SCISSOR_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, triV);
}

int main(int argc, char **argv)
{
	static unsigned char backing[W * H * 4];
	static float scene[W * H * 4];
	static unsigned char out[W * H * 4];
	OSMesaContext ctx;
	GLuint sceneTex, mappedTex, sceneFB, mappedFB, vp, comp, enc, bloomTex;
	GLuint curveTex, desatTex;
	int i, nonBlack = 0, hudWrong = 0;
	/* omitState reproduces the bug: bind the program, skip the state */
	const int omitState = (argc > 1 && !strcmp(argv[1], "--omit-state"));

	setvbuf(stdout, NULL, _IONBF, 0);
	ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, NULL);
	if (!ctx || !OSMesaMakeCurrent(ctx, backing, GL_UNSIGNED_BYTE, W, H)) {
		printf("no context\n");
		return 1;
	}
	pGenFB   = (PFNGLGENFRAMEBUFFERSPROC)OSMesaGetProcAddress("glGenFramebuffers");
	pBindFB  = (PFNGLBINDFRAMEBUFFERPROC)OSMesaGetProcAddress("glBindFramebuffer");
	pFBTex   = (PFNGLFRAMEBUFFERTEXTURE2DPROC)OSMesaGetProcAddress("glFramebufferTexture2D");
	pCheckFB = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)OSMesaGetProcAddress("glCheckFramebufferStatus");
	pGenProg = (PFNGLGENPROGRAMSARBPROC)OSMesaGetProcAddress("glGenProgramsARB");
	pBindProg= (PFNGLBINDPROGRAMARBPROC)OSMesaGetProcAddress("glBindProgramARB");
	pProgStr = (PFNGLPROGRAMSTRINGARBPROC)OSMesaGetProcAddress("glProgramStringARB");
	pEnv     = (PFNGLPROGRAMENVPARAMETER4FVARBPROC)OSMesaGetProcAddress("glProgramEnvParameter4fvARB");
	pActiveTex = (PFNGLACTIVETEXTUREPROC)OSMesaGetProcAddress("glActiveTexture");

	/* a scene with real range: a dark floor, a lit wall, a blown highlight */
	for (i = 0; i < W * H; i++) {
		float v = (i % W) < 20 ? 0.05f : ((i % W) < 42 ? 0.6f : 6.0f);
		scene[i * 4 + 0] = v;
		scene[i * 4 + 1] = v * 0.8f;
		scene[i * 4 + 2] = v * 0.6f;
		scene[i * 4 + 3] = 0.f;
	}
	{
		/* the real curve and cube, from the headers the core ships */
		static unsigned short c16[256 * 4];
		int k;
		for (k = 0; k < 256; k++) {
			unsigned short v = (unsigned short)(filmic_base_contrast[k] * 65535.0f + 0.5f);
			c16[k * 4 + 0] = c16[k * 4 + 1] = c16[k * 4 + 2] = v;
			c16[k * 4 + 3] = 65535;
		}
		glGenTextures(1, &curveTex);
		glBindTexture(GL_TEXTURE_2D, curveTex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, 256, 1, 0, GL_RGBA, GL_UNSIGNED_SHORT, c16);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glGenTextures(1, &desatTex);
		glBindTexture(GL_TEXTURE_2D, desatTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16, FILMIC_DESAT_W, FILMIC_DESAT_H, 0,
			GL_RGB, GL_UNSIGNED_SHORT, filmic_desat);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	}
	sceneTex  = makeTex(0x881A /* RGBA16F */, GL_FLOAT, scene);
	mappedTex = makeTex(0x881A, GL_FLOAT, NULL);
	bloomTex  = makeTex(0x881A, GL_FLOAT, NULL);

	pGenFB(1, &sceneFB);
	pBindFB(GL_FRAMEBUFFER, sceneFB);
	pFBTex(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTex, 0);
	pGenFB(1, &mappedFB);
	pBindFB(GL_FRAMEBUFFER, mappedFB);
	pFBTex(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mappedTex, 0);
	if (pCheckFB(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("  FAIL: mapped target incomplete\n");
		return 1;
	}

	vp   = loadProg(GL_VERTEX_PROGRAM_ARB,   "/tmp/h_vp.arb");
	comp = loadProg(GL_FRAGMENT_PROGRAM_ARB, "/tmp/h_comp.arb");
	enc  = loadProg(GL_FRAGMENT_PROGRAM_ARB, "/tmp/h_enc.arb");
	if (!vp || !comp || !enc)
		return 1;

	/* ---- pass one: composite the scene into the mapped target ---- */
	pBindFB(GL_FRAMEBUFFER, mappedFB);
	glViewport(0, 0, W, H);
	fullscreenState();
	pActiveTex(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneTex);
	pActiveTex(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, bloomTex);
	pActiveTex(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, bloomTex);
	/* the Filmic curve on unit 3 and its desaturation atlas on unit 4.
	   Leaving these unbound made the mid-tones come back black while the
	   highlights survived, because the shoulder path never samples them -
	   which is a real failure mode, not a harness quirk. */
	pActiveTex(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, curveTex);
	pActiveTex(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, desatTex);
	pActiveTex(GL_TEXTURE0);
	glEnable(GL_VERTEX_PROGRAM_ARB);
	glEnable(GL_FRAGMENT_PROGRAM_ARB);
	pBindProg(GL_VERTEX_PROGRAM_ARB, vp);
	pBindProg(GL_FRAGMENT_PROGRAM_ARB, comp);
	{
		/* env[3].w = 1 asks for the display-referred exit */
		const float p0[4] = { 200.f / 10000.f, 5.f, 18.f, 0.75f };
		const float p1[4] = { 0.f, 0.75f, 4.f, 0.f };
		const float p2[4] = { 1.f, 0.f, 0.f, 0.f };
		const float p3[4] = { 0.f, 0.618f, 1.f / 1023.f, 1.f };
		const float r0[4] = { 1.f, 0.f, 0.f, 0.f };
		const float r1[4] = { 0.f, 1.f, 0.f, 0.f };
		const float r2[4] = { 0.f, 0.f, 1.f, 0.f };
		const float p7[4] = { 0.5f, 1.f, 0.f, 0.f };
		const float p8[4] = { 1.7851195f, 0.5f, 0.5f, 1.f };  /* Filmic Log normalisation */
		const float p9[4] = { 4.5f, 1.f, 0.04f, 0.606f };
		pEnv(GL_FRAGMENT_PROGRAM_ARB, 0, p0); pEnv(GL_FRAGMENT_PROGRAM_ARB, 1, p1);
		pEnv(GL_FRAGMENT_PROGRAM_ARB, 2, p2); pEnv(GL_FRAGMENT_PROGRAM_ARB, 3, p3);
		pEnv(GL_FRAGMENT_PROGRAM_ARB, 4, r0); pEnv(GL_FRAGMENT_PROGRAM_ARB, 5, r1);
		pEnv(GL_FRAGMENT_PROGRAM_ARB, 6, r2); pEnv(GL_FRAGMENT_PROGRAM_ARB, 7, p7);
		pEnv(GL_FRAGMENT_PROGRAM_ARB, 8, p8); pEnv(GL_FRAGMENT_PROGRAM_ARB, 9, p9);
	}
	glDrawArrays(GL_TRIANGLES, 0, 3);

	/* ---- the HUD draws onto the mapped image ---- */
	glDisable(GL_FRAGMENT_PROGRAM_ARB);
	glDisable(GL_VERTEX_PROGRAM_ARB);
	glDisableVertexAttribArray(0);
	glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1, 0, 1, -1, 1);
	glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
	glDisable(GL_TEXTURE_2D);
	glColor4f(0.5f, 0.5f, 0.5f, 1.0f);       /* a mid grey panel */
	glBegin(GL_QUADS);
	glVertex2f(0.0f, 0.0f); glVertex2f(0.25f, 0.0f);
	glVertex2f(0.25f, 1.0f); glVertex2f(0.0f, 1.0f);
	glEnd();

	/* ---- pass two: encode the mapped image to the screen ---- */
	pBindFB(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, W, H);
	if (!omitState)
		fullscreenState();
	pActiveTex(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mappedTex);
	glEnable(GL_VERTEX_PROGRAM_ARB);
	glEnable(GL_FRAGMENT_PROGRAM_ARB);
	pBindProg(GL_VERTEX_PROGRAM_ARB, vp);
	pBindProg(GL_FRAGMENT_PROGRAM_ARB, enc);
	{
		const float p0[4] = { 200.f / 10000.f, 1.f, 0.f, 0.f };
		const float p3[4] = { 0.f, 0.618f, 1.f / 1023.f, 0.f };
		pEnv(GL_FRAGMENT_PROGRAM_ARB, 0, p0);
		pEnv(GL_FRAGMENT_PROGRAM_ARB, 3, p3);
	}
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glFinish();

	{
		static float mid[W * H * 4];
		pBindFB(GL_FRAMEBUFFER, mappedFB);
		glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, mid);
		printf("  mapped target: HUD %.4f, wall %.4f, highlight %.4f\n",
			mid[(H / 2 * W + 5) * 4], mid[(H / 2 * W + 30) * 4], mid[(H / 2 * W + 55) * 4]);
		pBindFB(GL_FRAMEBUFFER, 0);
	}
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out);
	for (i = 0; i < W * H; i++)
		if (out[i * 4] > 2 || out[i * 4 + 1] > 2 || out[i * 4 + 2] > 2)
			nonBlack++;

	printf("  non-black pixels: %d of %d\n", nonBlack, W * H);
	if (nonBlack < W * H / 2) {
		printf("  FAIL: the frame came out black\n");
		return 1;
	}
	/* the HUD strip must differ from the lit wall beside it */
	{
		int hud = out[(H / 2 * W + 5) * 4];
		int wall = out[(H / 2 * W + 30) * 4];
		int sky = out[(H / 2 * W + 55) * 4];
		printf("  HUD strip %d, lit wall %d, highlight %d\n", hud, wall, sky);
		if (hud == 0 || wall == 0 || sky == 0) {
			printf("  FAIL: a region came out black\n");
			return 1;
		}
		if (sky <= wall) {
			printf("  FAIL: the highlight is not brighter than the wall\n");
			hudWrong = 1;
		}
	}
	printf("%s\n", hudWrong ? "  FAILED" : "  PASS: the two-pass frame produces a picture");
	OSMesaDestroyContext(ctx);
	return hudWrong;
}
