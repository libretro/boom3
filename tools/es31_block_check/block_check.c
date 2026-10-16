/*
 * es31_block_check: the ES 3.1 renderer's interaction programs against the
 * ES 3 renderer's, pixel for pixel.
 *
 * The ES 3.1 renderer moves each interaction program's per-draw uniforms
 * into two std140 blocks (renderer/glsl_blocks.c, the code that ships) and
 * writes them per draw at the offsets the program reports.  This draws the
 * same random interactions both ways - the ES 3 programs with glUniform*,
 * the ES 3.1 ones from a uniform buffer filled the way
 * draw_gles3_multiview.cpp fills it - and requires identical framebuffers:
 * an 8-bit and, where the context can render to it, a half-float target,
 * additively blended as the interaction passes blend, depth-tested EQUAL
 * against a depth fill drawn by the stock zfill program.
 *
 * Linux: Mesa's EGL and GLES (libegl-dev libgles-dev), llvmpipe headless.
 * Windows (MSYS2 MINGW64): pacman -S mingw-w64-x86_64-angleproject.
 *
 *   make
 *   ./es31_block_check ../../neo/renderer
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include "glsl_blocks.h"

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x40
#endif

#define SIZE      96
#define DRAWS     48
#define TRIS      24
#define TEXSIZE   32

static const char *renderer_dir;
static int failures;

/* the names draw_gles3_multiview.cpp moves, in its order */
static const char *const vertex_names[] = {
   "u_modelMatrix", "u_depthHack", "u_lightProjection", "u_colorModulate", "u_colorAdd",
   "u_lightOrigin", "u_viewOrigin", "u_bumpMatrixS", "u_bumpMatrixT", "u_diffuseMatrixS",
   "u_diffuseMatrixT", "u_specularMatrixS", "u_specularMatrixT"
};
static const int vertex_sizes[] = { 16, 1, 16, 1, 1, 4, 4, 4, 4, 4, 4, 4, 4 };
#define NV 13
static const char *const fragment_names[] = {
   "u_diffuseColor", "u_specularColor", "u_hdrParms", "u_specularExponent"
};
static const int fragment_sizes[] = { 4, 4, 4, 1 };
#define NF 4

/* ---- sources, as gles_shader_check reads them ---- */

static char *read_file(const char *name)
{
   char path[1024];
   FILE *f;
   long len;
   char *buf;

   sprintf(path, "%s/glsl_gles3/%s", renderer_dir, name);
   f = fopen(path, "rb");
   if (!f)
      return NULL;
   fseek(f, 0, SEEK_END);
   len = ftell(f);
   fseek(f, 0, SEEK_SET);
   buf = (char *)malloc((size_t)len + 1);
   if (fread(buf, 1, (size_t)len, f) != (size_t)len)
      len = 0;
   buf[len] = '\0';
   fclose(f);
   return buf;
}

static char *shader_source(const char *file, const char *name)
{
   char key[256];
   char *text = read_file(file);
   char *start;
   char *end;
   char *out;

   if (!text)
      return NULL;
   sprintf(key, "const char * const %s = R\"(", name);
   start = strstr(text, key);
   if (!start)
   {
      free(text);
      return NULL;
   }
   start += strlen(key);
   while (*start == '\n')
      start++;
   end = strstr(start, ")\";");
   out = (char *)malloc((size_t)(end - start) + 1);
   memcpy(out, start, (size_t)(end - start));
   out[end - start] = '\0';
   free(text);
   return out;
}

/* ---- GL ---- */

static GLuint compile(GLenum type, const char *src, const char *what)
{
   GLuint sh = glCreateShader(type);
   GLint ok = 0;
   char log[4096];

   glShaderSource(sh, 1, &src, NULL);
   glCompileShader(sh);
   glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
   if (!ok)
   {
      glGetShaderInfoLog(sh, sizeof(log), NULL, log);
      fprintf(stderr, "FAIL %s: does not compile:\n%s\n", what, log);
      failures++;
   }
   return sh;
}

static GLuint link_program(const char *vs, const char *fs, const char *what)
{
   GLuint p = glCreateProgram();
   GLint ok = 0;
   char log[4096];

   glAttachShader(p, compile(GL_VERTEX_SHADER, vs, what));
   glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs, what));
   glBindAttribLocation(p, 0, "attr_Vertex");
   glBindAttribLocation(p, 1, "attr_Color");
   glBindAttribLocation(p, 2, "attr_TexCoord");
   glBindAttribLocation(p, 3, "attr_Normal");
   glBindAttribLocation(p, 4, "attr_Tangent");
   glBindAttribLocation(p, 5, "attr_Bitangent");
   glLinkProgram(p);
   glGetProgramiv(p, GL_LINK_STATUS, &ok);
   if (!ok)
   {
      glGetProgramInfoLog(p, sizeof(log), NULL, log);
      fprintf(stderr, "FAIL %s: does not link:\n%s\n", what, log);
      failures++;
      return 0;
   }
   return p;
}

/* ---- random inputs ---- */

static unsigned long rng = 1;

static float frand(float lo, float hi)
{
   rng = rng * 1103515245ul + 12345ul;
   return lo + (hi - lo) * (float)((rng >> 8) & 0xffffff) / 16777215.0f;
}

typedef struct draw_s {
   float v[NV][16];
   float f[NF][4];
} draw_t;

static void random_draw(draw_t *d, int clamp_mode)
{
   int i, j;
   float mod_add[3][2] = { { 0.0f, 1.0f }, { 1.0f / 255.0f, 0.0f }, { -1.0f / 255.0f, 1.0f } };
   int mode;

   for (i = 0; i < NV; i++)
      for (j = 0; j < 16; j++)
         d->v[i][j] = frand(-1.0f, 1.0f);
   /* a model matrix near identity, so the triangles stay on screen */
   for (j = 0; j < 16; j++)
      d->v[0][j] = (j % 5 == 0 ? 1.0f : 0.0f) + frand(-0.05f, 0.05f);
   d->v[1][0] = frand(-0.01f, 0.01f);
   mode = (int)frand(0.0f, 2.99f);
   d->v[3][0] = mod_add[mode][0];
   d->v[4][0] = mod_add[mode][1];
   for (i = 0; i < NF; i++)
      for (j = 0; j < 4; j++)
         d->f[i][j] = frand(0.0f, 1.0f);
   d->f[1][0] *= 3.0f;                     /* boosted specular above 1 */
   d->f[2][1] = (float)clamp_mode;             /* the epilogue's clamp mode */
   d->f[2][3] = frand(0.3f, 1.0f);
   d->f[3][0] = frand(1.0f, 64.0f);
}

/* ---- the two ways of feeding a program ---- */

static void set_uniforms(GLuint p, const draw_t *d)
{
   int i;
   for (i = 0; i < NV; i++)
   {
      GLint loc = glGetUniformLocation(p, vertex_names[i]);
      if (loc < 0)
         continue;
      if (vertex_sizes[i] == 16)
         glUniformMatrix4fv(loc, 1, GL_FALSE, d->v[i]);
      else if (vertex_sizes[i] == 4)
         glUniform4fv(loc, 1, d->v[i]);
      else
         glUniform1fv(loc, 1, d->v[i]);
   }
   for (i = 0; i < NF; i++)
   {
      GLint loc = glGetUniformLocation(p, fragment_names[i]);
      if (loc < 0)
         continue;
      if (fragment_sizes[i] == 4)
         glUniform4fv(loc, 1, d->f[i]);
      else
         glUniform1fv(loc, 1, d->f[i]);
   }
}

/* What RB_GLSL_BlockMembers asks: the block's size and its members'
 * offsets (-1 absent), bound to binding. */
static GLint block_layout(GLuint p, const char *block, GLuint binding,
      const char *const *names, int count, GLint *offsets)
{
   GLuint idx[16];
   GLint size = 0;
   GLuint b = glGetUniformBlockIndex(p, block);
   int i;

   for (i = 0; i < count; i++)
      offsets[i] = -1;
   if (b == GL_INVALID_INDEX)
      return 0;
   glUniformBlockBinding(p, b, binding);
   glGetActiveUniformBlockiv(p, b, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
   glGetUniformIndices(p, count, names, idx);
   for (i = 0; i < count; i++)
   {
      GLint v;
      if (idx[i] == GL_INVALID_INDEX)
         continue;
      glGetActiveUniformsiv(p, 1, &idx[i], GL_UNIFORM_OFFSET, &v);
      offsets[i] = v;
      glGetActiveUniformsiv(p, 1, &idx[i], GL_UNIFORM_TYPE, &v);
      if (v == GL_FLOAT_MAT4)
      {
         glGetActiveUniformsiv(p, 1, &idx[i], GL_UNIFORM_MATRIX_STRIDE, &v);
         if (v != 16)
         {
            fprintf(stderr, "FAIL %s: matrix stride %d\n", names[i], (int)v);
            failures++;
         }
      }
   }
   return size;
}

/* ---- scene ---- */

static GLuint vbo, ubo, view_ubo, tex[5];
static GLint ubo_align;

static void make_scene(void)
{
   float verts[TRIS * 3][18];
   unsigned char pix[TEXSIZE * TEXSIZE * 4];
   float view[32];
   int i, j;

   for (i = 0; i < TRIS * 3; i++)
   {
      float *v = verts[i];
      /* attr_Vertex xyzw, normal, tangent, bitangent, st, then color as bytes */
      v[0] = frand(-1.0f, 1.0f); v[1] = frand(-1.0f, 1.0f); v[2] = frand(-0.5f, 0.5f); v[3] = 1.0f;
      for (j = 4; j < 13; j++)
         v[j] = frand(-1.0f, 1.0f);
      v[13] = frand(-2.0f, 2.0f); v[14] = frand(-2.0f, 2.0f);
      {
         unsigned char c[4];
         c[0] = (unsigned char)frand(0.0f, 255.0f); c[1] = (unsigned char)frand(0.0f, 255.0f);
         c[2] = (unsigned char)frand(0.0f, 255.0f); c[3] = (unsigned char)frand(0.0f, 255.0f);
         memcpy(&v[15], c, 4);
      }
      v[16] = v[17] = 0.0f;
   }
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 72, (void *)0);
   glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 72, (void *)16);
   glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, 72, (void *)28);
   glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, 72, (void *)40);
   glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 72, (void *)52);
   glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_FALSE, 72, (void *)60);
   for (i = 0; i < 6; i++)
      glEnableVertexAttribArray((GLuint)i);

   glGenTextures(5, tex);
   for (i = 0; i < 5; i++)
   {
      for (j = 0; j < TEXSIZE * TEXSIZE * 4; j++)
         pix[j] = (unsigned char)frand(0.0f, 255.0f);
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, tex[i]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TEXSIZE, TEXSIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   }

   /* ViewMatrices: identity for both views */
   memset(view, 0, sizeof(view));
   for (i = 0; i < 2; i++)
      for (j = 0; j < 4; j++)
         view[i * 16 + j * 5] = 1.0f;
   glGenBuffers(1, &view_ubo);
   glBindBuffer(GL_UNIFORM_BUFFER, view_ubo);
   glBufferData(GL_UNIFORM_BUFFER, sizeof(view), view, GL_STATIC_DRAW);
   glBindBufferBase(GL_UNIFORM_BUFFER, 0, view_ubo);

   glGenBuffers(1, &ubo);
   glBindBuffer(GL_UNIFORM_BUFFER, ubo);
   glBufferData(GL_UNIFORM_BUFFER, 1 << 20, NULL, GL_DYNAMIC_DRAW);
   glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_align);
}

static void bind_view_block(GLuint p)
{
   GLuint b = glGetUniformBlockIndex(p, "ViewMatrices");
   if (b != GL_INVALID_INDEX)
      glUniformBlockBinding(p, b, 0);
}

static void set_samplers(GLuint p)
{
   int i;
   char name[32];
   glUseProgram(p);
   for (i = 0; i < 5; i++)
   {
      sprintf(name, "u_fragmentMap%d", i);
      glUniform1i(glGetUniformLocation(p, name), i);
   }
}

/* Depth from the stock zfill program, then the draws additively at
 * GL_EQUAL, as the interaction passes run; the target read back. */
static void render(GLuint fbo, GLuint zfill, GLuint prog, int blocks,
      const draw_t *draws, int ndraws, unsigned char *out, int bytes_per_pixel)
{
   GLint voff[NV], foff[NF];
   GLint vsize = 0, fsize = 0;
   int i, j;
   GLintptr at = 0;
   static const float zcolor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glViewport(0, 0, SIZE, SIZE);
   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   glUseProgram(zfill);
   glUniform4fv(glGetUniformLocation(zfill, "u_glColor"), 1, zcolor);
   glUniformMatrix4fv(glGetUniformLocation(zfill, "u_modelMatrix"), 1, GL_FALSE, draws[0].v[0]);
   glUniform1fv(glGetUniformLocation(zfill, "u_depthHack"), 1, draws[0].v[1]);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LESS);
   glDisable(GL_BLEND);
   glDrawArrays(GL_TRIANGLES, 0, TRIS * 3);

   glUseProgram(prog);
   glDepthFunc(GL_EQUAL);
   glDepthMask(GL_FALSE);
   glEnable(GL_BLEND);
   glBlendFunc(GL_ONE, GL_ONE);
   if (blocks)
   {
      vsize = block_layout(prog, "DrawVertex", 1, vertex_names, NV, voff);
      fsize = block_layout(prog, "DrawFragment", 2, fragment_names, NF, foff);
      if (!vsize)
      {
         fprintf(stderr, "FAIL: no DrawVertex block\n");
         failures++;
      }
   }
   for (i = 0; i < ndraws; i++)
   {
      draw_t d = draws[i];
      /* every draw of a surface shares the depth fill's space */
      memcpy(d.v[0], draws[0].v[0], sizeof(d.v[0]));
      d.v[1][0] = draws[0].v[1][0];
      if (!blocks)
         set_uniforms(prog, &d);
      else
      {
         unsigned char buf[2][4096];
         memset(buf, 0, sizeof(buf));
         for (j = 0; j < NV; j++)
            if (voff[j] >= 0)
               memcpy(buf[0] + voff[j], d.v[j], (size_t)vertex_sizes[j] * sizeof(float));
         for (j = 0; j < NF; j++)
            if (foff[j] >= 0)
               memcpy(buf[1] + foff[j], d.f[j], (size_t)fragment_sizes[j] * sizeof(float));
         glBindBuffer(GL_UNIFORM_BUFFER, ubo);
         at = ((at + ubo_align - 1) / ubo_align) * ubo_align;
         glBufferSubData(GL_UNIFORM_BUFFER, at, vsize, buf[0]);
         glBindBufferRange(GL_UNIFORM_BUFFER, 1, ubo, at, vsize);
         at += vsize;
         if (fsize)
         {
            at = ((at + ubo_align - 1) / ubo_align) * ubo_align;
            glBufferSubData(GL_UNIFORM_BUFFER, at, fsize, buf[1]);
            glBindBufferRange(GL_UNIFORM_BUFFER, 2, ubo, at, fsize);
            at += fsize;
         }
      }
      glDrawArrays(GL_TRIANGLES, 0, TRIS * 3);
   }
   glDepthMask(GL_TRUE);
   glDisable(GL_BLEND);
   glDisable(GL_DEPTH_TEST);
   if (bytes_per_pixel == 4)
      glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, out);
   else
      glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_FLOAT, out);
}

static GLuint make_target(GLenum format)
{
   GLuint fbo, color, depth;
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glGenRenderbuffers(1, &color);
   glBindRenderbuffer(GL_RENDERBUFFER, color);
   glRenderbufferStorage(GL_RENDERBUFFER, format, SIZE, SIZE);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
   glGenRenderbuffers(1, &depth);
   glBindRenderbuffer(GL_RENDERBUFFER, depth);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, SIZE, SIZE);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      return 0;
   return fbo;
}

struct pairing {
   const char *name, *vs_file, *vs, *fs_file, *fs;
};

static const struct pairing programs[] = {
   { "interactionBlinn", "interactionShaderVP.cpp", "interactionShaderVP", "interactionShaderFP.cpp", "interactionBlinnShaderFP" },
   { "interaction (PBR)", "interactionShaderVP.cpp", "interactionShaderVP", "interactionShaderFP.cpp", "interactionShaderFP" },
   { "interactionPhong", "interactionPhongShaderVP.cpp", "interactionPhongShaderVP", "interactionPhongShaderFP.cpp", "interactionPhongShaderFP" },
   { NULL, NULL, NULL, NULL, NULL }
};

static int compare_program(const struct pairing *pr, GLuint zfill, GLuint fbo8, GLuint fbo16)
{
   char *vs = shader_source(pr->vs_file, pr->vs);
   char *fs = shader_source(pr->fs_file, pr->fs);
   char *bvs, *bfs;
   GLuint a, b;
   static draw_t draws[DRAWS];
   static unsigned char pa[SIZE * SIZE * 16], pb[SIZE * SIZE * 16];
   int i, pass, lit = 0;
   int checked = 0;

   if (!vs || !fs)
   {
      fprintf(stderr, "FAIL %s: sources not found\n", pr->name);
      failures++;
      return 0;
   }
   bvs = glsl_uniforms_to_block(vs, "DrawVertex", vertex_names, NV);
   bfs = glsl_uniforms_to_block(fs, "DrawFragment", fragment_names, NF);
   a = link_program(vs, fs, pr->name);
   b = link_program(bvs, bfs, pr->name);
   if (!a || !b)
      return 0;
   if (glGetUniformLocation(b, "u_lightProjection") >= 0 || glGetUniformLocation(b, "u_diffuseColor") >= 0)
   {
      fprintf(stderr, "FAIL %s: per-draw uniforms left outside the blocks\n", pr->name);
      failures++;
   }
   bind_view_block(a);
   bind_view_block(b);
   set_samplers(a);
   set_samplers(b);

   for (pass = 0; pass < 2; pass++)
   {
      GLuint fbo = pass ? fbo16 : fbo8;
      int bpp = pass ? 16 : 4;
      if (!fbo)
         continue;
      for (i = 0; i < DRAWS; i++)
         random_draw(&draws[i], i % 3);
      render(fbo, zfill, a, 0, draws, DRAWS, pa, bpp);
      render(fbo, zfill, b, 1, draws, DRAWS, pb, bpp);
      if (memcmp(pa, pb, (size_t)SIZE * SIZE * bpp) != 0)
      {
         int diff = 0;
         for (i = 0; i < SIZE * SIZE * bpp; i++)
            diff += pa[i] != pb[i];
         fprintf(stderr, "FAIL %s: %s target differs in %d bytes\n", pr->name,
               pass ? "half-float" : "8-bit", diff);
         failures++;
      }
      for (i = 0; i < SIZE * SIZE * bpp; i++)
         lit += pa[i] != 0;
      checked++;
   }
   if (!lit)
   {
      fprintf(stderr, "FAIL %s: nothing was drawn, the comparison proves nothing\n", pr->name);
      failures++;
   }
   printf("%s: %d target(s), %d nonzero bytes\n", pr->name, checked, lit);
   free(vs); free(fs); free(bvs); free(bfs);
   glDeleteProgram(a);
   glDeleteProgram(b);
   return 1;
}

static int make_context(void)
{
   EGLDisplay dpy = EGL_NO_DISPLAY;
   EGLConfig cfg;
   EGLint n = 0;
   EGLContext ctx;
   const char *client_exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
   static const EGLint cfg_attrs[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
   static const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
   static const EGLint pb_attrs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
   EGLSurface surf;

   if (client_exts && strstr(client_exts, "EGL_MESA_platform_surfaceless"))
   {
      PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
         (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
      if (gpd)
         dpy = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
   }
   if (dpy == EGL_NO_DISPLAY)
      dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL))
      return 0;
   eglBindAPI(EGL_OPENGL_ES_API);
   if (!eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &n) || n < 1)
      return 0;
   ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
   if (ctx == EGL_NO_CONTEXT)
      return 0;
   surf = eglCreatePbufferSurface(dpy, cfg, pb_attrs);
   if (!eglMakeCurrent(dpy, surf, surf, ctx))
      return 0;
   printf("%s, %s\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
   return 1;
}

int main(int argc, char **argv)
{
   const struct pairing *pr;
   GLuint zfill, fbo8, fbo16 = 0;
   const char *ext;
   char *zvs, *zfs;

   if (argc != 2)
   {
      fprintf(stderr, "usage: %s <neo/renderer directory>\n", argv[0]);
      return 2;
   }
   renderer_dir = argv[1];
   if (!make_context())
   {
      fprintf(stderr, "FAIL no GLES 3 context\n");
      return 1;
   }
   make_scene();
   zvs = shader_source("zfillShaderVP.cpp", "zfillShaderVP");
   zfs = shader_source("zfillShaderFP.cpp", "zfillOpaqueShaderFP");
   if (!zvs || !zfs)
   {
      fprintf(stderr, "FAIL zfill sources not found\n");
      return 1;
   }
   zfill = link_program(zvs, zfs, "zfillOpaque");
   bind_view_block(zfill);
   fbo8 = make_target(GL_RGBA8);
   ext = (const char *)glGetString(GL_EXTENSIONS);
   if (ext && strstr(ext, "GL_EXT_color_buffer_float"))
      fbo16 = make_target(GL_RGBA16F);
   if (!fbo8)
   {
      fprintf(stderr, "FAIL no 8-bit target\n");
      return 1;
   }
   for (pr = programs; pr->name; pr++)
      compare_program(pr, zfill, fbo8, fbo16);
   if (failures)
   {
      printf("FAIL es31_block_check: %d failure(s)\n", failures);
      return 1;
   }
   printf("PASS es31_block_check: the block-fed programs draw the same pixels\n");
   return 0;
}
