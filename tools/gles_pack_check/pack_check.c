/*
 * gles_pack_check: the GLES texture packings (neo/renderer/gles_pack.c)
 * against plain RGBA, sample for sample.
 *
 * For each packing an image of that kind, with its mip chain made the way
 * the engine makes it (R_MipMap's per-channel averages), is uploaded once
 * as RGBA and once packed, then drawn into an RGBA8 target with trilinear
 * filtering at several sizes and offsets, so every level and fractional
 * positions between texels are sampled.  The two pictures must be equal
 * byte for byte.  Done in a GLES 2 context (packings without swizzles) and
 * a GLES 3 one (R8 and RG8 with swizzles), where the packed and the plain
 * texture are also drawn from glTexStorage2D storage filled level by
 * level, as the engine does; and once more with a colour image forced into
 * a one-byte packing, which must differ - the check can fail.
 *
 * Linux: Mesa's EGL and GLES (libegl-dev libgles-dev); runs on llvmpipe.
 *
 * C89.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include "gles_pack.h"

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x40
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#define GL_RG8 0x822B
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_GREEN
#define GL_GREEN 0x1904
#define GL_BLUE 0x1905
#endif
#ifndef GL_TEXTURE_SWIZZLE_R
#define GL_TEXTURE_SWIZZLE_R 0x8E42
#define GL_TEXTURE_SWIZZLE_G 0x8E43
#define GL_TEXTURE_SWIZZLE_B 0x8E44
#define GL_TEXTURE_SWIZZLE_A 0x8E45
#endif

#define SIZE 64
#define TARGET 61

static unsigned int seed = 12345u;

static unsigned char rnd(void)
{
   seed = seed * 1103515245u + 12345u;
   return (unsigned char)(seed >> 16);
}

/* An image of the kind the packing holds; for RGBA, colour. */
static void make_image(gles_pack_t pack, unsigned char *rgba)
{
   int i;
   for (i = 0; i < SIZE * SIZE; i++, rgba += 4)
   {
      const unsigned char v = rnd();
      const unsigned char a = rnd();
      switch (pack)
      {
         case GLES_PACK_L:  rgba[0] = rgba[1] = rgba[2] = v; rgba[3] = 255; break;
         case GLES_PACK_I:  rgba[0] = rgba[1] = rgba[2] = rgba[3] = v;    break;
         case GLES_PACK_LA: rgba[0] = rgba[1] = rgba[2] = v; rgba[3] = a; break;
         case GLES_PACK_A0: rgba[0] = rgba[1] = rgba[2] = 0; rgba[3] = a; break;
         case GLES_PACK_A1: rgba[0] = rgba[1] = rgba[2] = 255; rgba[3] = a; break;
         default:           rgba[0] = v; rgba[1] = a; rgba[2] = rnd(); rgba[3] = rnd(); break;
      }
   }
}

/* R_MipMap, without the border option */
static unsigned char *mip(const unsigned char *in, int w, int h)
{
   const int nw = w > 1 ? w / 2 : 1;
   const int nh = h > 1 ? h / 2 : 1;
   unsigned char *out = (unsigned char *)malloc(nw * nh * 4);
   int x, y, c;
   for (y = 0; y < nh; y++)
      for (x = 0; x < nw; x++)
         for (c = 0; c < 4; c++)
         {
            const unsigned char *p = in + ((y * 2) * w + x * 2) * 4 + c;
            out[(y * nw + x) * 4 + c] = (unsigned char)((p[0] + p[4] + p[w * 4] + p[w * 4 + 4]) >> 2);
         }
   return out;
}

typedef void (GL_APIENTRYP tex_storage_t)(GLenum target, GLsizei levels, GLenum internalformat,
      GLsizei width, GLsizei height);
static tex_storage_t tex_storage;

static int levels_for(int w, int h)
{
   int n = 1;
   while (w > 1 || h > 1)
   {
      w = w > 1 ? w / 2 : 1;
      h = h > 1 ? h / 2 : 1;
      n++;
   }
   return n;
}

/* One texture from the image, as the engine uploads it: RGBA, or the
 * packing given - on GLES 3 (swizzle) in R8 or RG8 read through the
 * packing's swizzle, on GLES 2 in luminance, alpha or luminance-alpha -
 * with every level allocated by glTexStorage2D first when storage is set. */
static GLuint upload(const unsigned char *image, int packed, gles_pack_t pack, int swizzle, int storage)
{
   static const GLint from[6] = { GL_ZERO, GL_ONE, GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };
   GLuint tex;
   unsigned char *level = (unsigned char *)malloc(SIZE * SIZE * 4);
   unsigned char *bytes = (unsigned char *)malloc(SIZE * SIZE * 4);
   int w = SIZE, h = SIZE, l = 0;
   const int n = packed ? gles_pack_bytes(pack, swizzle) : 4;
   GLenum format = GL_RGBA;
   GLenum sized = GL_RGBA8;

   if (packed && swizzle)
   {
      format = n == 2 ? GL_RG : GL_RED;
      sized  = n == 2 ? GL_RG8 : GL_R8;
   }
   else if (packed)
   {
      if (pack == GLES_PACK_L)
         format = GL_LUMINANCE;
      else if (pack == GLES_PACK_A0)
         format = GL_ALPHA;
      else
         format = GL_LUMINANCE_ALPHA;
      sized = format;
   }

   glGenTextures(1, &tex);
   glBindTexture(GL_TEXTURE_2D, tex);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   if (storage)
      tex_storage(GL_TEXTURE_2D, levels_for(SIZE, SIZE), sized, SIZE, SIZE);
   memcpy(level, image, SIZE * SIZE * 4);
   for (;;)
   {
      unsigned char *next;
      if (packed)
         gles_pack_texels(pack, swizzle, level, w * h, bytes);
      else
         memcpy(bytes, level, w * h * 4);
      if (storage)
         glTexSubImage2D(GL_TEXTURE_2D, l, 0, 0, w, h, format, GL_UNSIGNED_BYTE, bytes);
      else
         glTexImage2D(GL_TEXTURE_2D, l, swizzle ? sized : format, w, h, 0, format, GL_UNSIGNED_BYTE, bytes);
      if (w == 1 && h == 1)
         break;
      next = mip(level, w, h);
      free(level);
      level = next;
      w = w > 1 ? w / 2 : 1;
      h = h > 1 ? h / 2 : 1;
      l++;
   }
   if (packed && swizzle)
   {
      int sw[4];
      gles_pack_swizzle(pack, sw);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, from[sw[0]]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, from[sw[1]]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, from[sw[2]]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, from[sw[3]]);
   }
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
   free(level);
   free(bytes);
   return tex;
}

static const char vs[] =
   "attribute vec2 aPos;\n"
   "uniform vec4 uMap;\n"
   "varying vec2 vUV;\n"
   "void main() {\n"
   "  vUV = (aPos * 0.5 + 0.5) * uMap.xy + uMap.zw;\n"
   "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
   "}\n";
static const char fs[] =
   "precision highp float;\n"
   "uniform sampler2D uTex;\n"
   "varying vec2 vUV;\n"
   "void main() { gl_FragColor = texture2D(uTex, vUV); }\n";

static GLuint program(void)
{
   const char *src[2];
   GLuint p = glCreateProgram();
   GLuint s[2];
   GLint ok = 0;
   int i;
   src[0] = vs;
   src[1] = fs;
   for (i = 0; i < 2; i++)
   {
      s[i] = glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
      glShaderSource(s[i], 1, &src[i], NULL);
      glCompileShader(s[i]);
      glAttachShader(p, s[i]);
   }
   glBindAttribLocation(p, 0, "aPos");
   glLinkProgram(p);
   glGetProgramiv(p, GL_LINK_STATUS, &ok);
   if (!ok)
   {
      fprintf(stderr, "gles_pack_check: the program did not link\n");
      exit(1);
   }
   glUseProgram(p);
   return p;
}

/* The texture drawn at several scales and offsets, every picture
 * appended to out. */
static void draw(GLuint prog, GLuint tex, unsigned char *out)
{
   static const float maps[][4] = {
      { 1.0f, 1.0f, 0.0f, 0.0f },
      { 0.37f, 0.41f, 0.123f, 0.456f },
      { 3.3f, 2.7f, 0.31f, 0.77f },
      { 9.1f, 13.7f, 0.5f, 0.25f },
      { 61.0f, 47.0f, 0.03f, 0.01f },
   };
   static const int sizes[] = { TARGET, 23, 5 };
   int m, s;
   const GLint map = glGetUniformLocation(prog, "uMap");

   glBindTexture(GL_TEXTURE_2D, tex);
   for (s = 0; s < 3; s++)
      for (m = 0; m < 5; m++)
      {
         glViewport(0, 0, sizes[s], sizes[s]);
         glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
         glClear(GL_COLOR_BUFFER_BIT);
         glUniform4fv(map, 1, maps[m]);
         glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
         glReadPixels(0, 0, sizes[s], sizes[s], GL_RGBA, GL_UNSIGNED_BYTE, out);
         out += sizes[s] * sizes[s] * 4;
      }
}

#define PICTURE_BYTES ((TARGET * TARGET + 23 * 23 + 5 * 5) * 4 * 5)

/* RGBA against the packing; 1 when they match. */
static int compare(GLuint prog, gles_pack_t kind, gles_pack_t pack, int swizzle)
{
   unsigned char *image = (unsigned char *)malloc(SIZE * SIZE * 4);
   unsigned char *a = (unsigned char *)malloc(PICTURE_BYTES);
   unsigned char *b = (unsigned char *)malloc(PICTURE_BYTES);
   GLuint t[2];
   int same = 1;
   int v;

   make_image(kind, image);
   t[0] = upload(image, 0, pack, swizzle, 0);
   draw(prog, t[0], a);
   /* packed; then, where storage exists, packed and plain in storage */
   for (v = 0; v < (swizzle ? 3 : 1); v++)
   {
      t[1] = upload(image, v != 2, pack, swizzle, v != 0);
      draw(prog, t[1], b);
      same &= glGetError() == GL_NO_ERROR && memcmp(a, b, PICTURE_BYTES) == 0;
      glDeleteTextures(1, &t[1]);
   }
   glDeleteTextures(1, &t[0]);
   free(image);
   free(a);
   free(b);
   return same;
}

static int check_classify(void)
{
   static const gles_pack_t kinds[] = {
      GLES_PACK_L, GLES_PACK_I, GLES_PACK_LA, GLES_PACK_A0, GLES_PACK_A1, GLES_PACK_RGBA
   };
   unsigned char *image = (unsigned char *)malloc(SIZE * SIZE * 4);
   int failed = 0;
   int k;
   for (k = 0; k < 6; k++)
   {
      gles_pack_t got;
      make_image(kinds[k], image);
      got = gles_pack_classify(image, SIZE * SIZE);
      if (got != kinds[k])
      {
         printf("classify: kind %d came back as %d\n", (int)kinds[k], (int)got);
         failed = 1;
      }
   }
   /* one texel off the relation makes it RGBA (or a wider packing) */
   make_image(GLES_PACK_L, image);
   image[(SIZE * SIZE - 1) * 4 + 1] ^= 1;
   if (gles_pack_classify(image, SIZE * SIZE) != GLES_PACK_RGBA)
   {
      printf("classify: a grey image with one coloured texel was packed\n");
      failed = 1;
   }
   make_image(GLES_PACK_L, image);
   image[(SIZE * SIZE - 1) * 4 + 3] = 7;
   if (gles_pack_classify(image, SIZE * SIZE) != GLES_PACK_LA)
   {
      printf("classify: a grey image with one translucent texel was not luminance-alpha\n");
      failed = 1;
   }
   free(image);
   return failed;
}

static int run(int major)
{
   static const gles_pack_t kinds[] = {
      GLES_PACK_L, GLES_PACK_I, GLES_PACK_LA, GLES_PACK_A0, GLES_PACK_A1
   };
   static const char *names[] = { "L", "I", "LA", "A0", "A1" };
   EGLDisplay dpy = EGL_NO_DISPLAY;
   EGLConfig cfg;
   EGLint n = 0;
   EGLContext ctx;
   EGLSurface surf;
   EGLint cfg_attrs[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
   EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
   static const EGLint pb_attrs[] = { EGL_WIDTH, TARGET, EGL_HEIGHT, TARGET, EGL_NONE };
   const char *client_exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
   static const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
   const int swizzle = major >= 3;
   GLuint prog, vbo;
   int failed = 0;
   int k;

   cfg_attrs[1] = major >= 3 ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT;
   ctx_attrs[1] = major;
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
   {
      fprintf(stderr, "gles_pack_check: no EGL display\n");
      return 1;
   }
   eglBindAPI(EGL_OPENGL_ES_API);
   if (!eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &n) || n < 1)
   {
      fprintf(stderr, "gles_pack_check: no GLES %d config\n", major);
      return 1;
   }
   ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
   surf = eglCreatePbufferSurface(dpy, cfg, pb_attrs);
   if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, surf, surf, ctx))
   {
      fprintf(stderr, "gles_pack_check: no GLES %d context\n", major);
      return 1;
   }

   tex_storage = (tex_storage_t)eglGetProcAddress("glTexStorage2D");
   if (swizzle && !tex_storage)
   {
      fprintf(stderr, "gles_pack_check: no glTexStorage2D in the GLES 3 context\n");
      return 1;
   }
   prog = program();
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);

   for (k = 0; k < 5; k++)
   {
      const int same = compare(prog, kinds[k], kinds[k], swizzle);
      printf("GLES %d %-2s (%d byte%s): %s\n", major, names[k], gles_pack_bytes(kinds[k], swizzle),
            gles_pack_bytes(kinds[k], swizzle) > 1 ? "s" : "", same ? "same" : "DIFFERENT");
      failed |= !same;
   }
   /* the check has to be able to fail: colour in a one-byte packing */
   if (compare(prog, GLES_PACK_RGBA, GLES_PACK_L, swizzle))
   {
      printf("GLES %d negative: a colour image packed as luminance compared equal\n", major);
      failed = 1;
   }
   else
      printf("GLES %d negative: a colour image packed as luminance differs, as it must\n", major);

   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroySurface(dpy, surf);
   eglDestroyContext(dpy, ctx);
   eglTerminate(dpy);
   return failed;
}

int main(void)
{
   int failed = check_classify();
   failed |= run(2);
   failed |= run(3);
   printf("%s\n", failed ? "FAILED" : "all packings sample as RGBA");
   return failed;
}
