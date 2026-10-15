/*
 * gles_shader_check: compiles and links every GLSL ES program the GLES2
 * (renderer/draw_gles2.cpp) and GLES3 (renderer/draw_gles3_multiview.cpp)
 * renderers build, in every variant they build it in, on a real GLES 3
 * context - headless through EGL (Mesa's llvmpipe on Linux, ANGLE on
 * Windows).
 *
 * The shader sources are read from the renderer's own .cpp files, so the
 * check sees exactly what ships.  Each program is paired as the renderer
 * pairs it.  The GLES3 vertex shaders are also built with BOOM3_MULTIVIEW,
 * as RB_GLSL_InitShaders builds them for VR, when the context has
 * GL_OVR_multiview2.  The Blinn interaction shaders are also built with the
 * specular-antialiasing and spectral-mix substitutions applied, and the
 * check fails if the stock lines those substitutions look for are gone - the
 * renderers only warn and draw the stock shader then.
 *
 * Per program it also checks the interface the renderer relies on: the
 * GLES3 programs' ViewMatrices block (two mat4, 128 bytes, the renderer's
 * VIEW_MATRICES_FLOATS) and their u_depthHack uniform, and u_hdrParms in
 * every interaction program.
 *
 * build: make
 * run:   ./gles_shader_check ../../neo/renderer
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

static const char *renderer_dir;
static int failures;
static int programs_checked;
static int have_multiview;

/* ---- sources ---- */

static char *read_file(const char *dir, const char *name)
{
   char path[1024];
   FILE *f;
   long len;
   char *buf;

   sprintf(path, "%s/%s", dir, name);
   f = fopen(path, "rb");
   if (!f)
   {
      fprintf(stderr, "FAIL cannot open %s\n", path);
      failures++;
      return NULL;
   }
   fseek(f, 0, SEEK_END);
   len = ftell(f);
   fseek(f, 0, SEEK_SET);
   buf = (char*)malloc((size_t)len + 1);
   if (fread(buf, 1, (size_t)len, f) != (size_t)len)
      len = 0;
   buf[len] = '\0';
   fclose(f);
   return buf;
}

/* The text of `const char * const NAME = R"(...)";` in a shader .cpp file,
 * as the renderer's string holds it. */
static char *shader_source(const char *file, const char *name)
{
   char key[256];
   char *text = read_file(renderer_dir, file);
   char *start;
   char *end;
   char *out;
   size_t len;

   if (!text)
      return NULL;
   sprintf(key, "const char * const %s = R\"(", name);
   start = strstr(text, key);
   if (!start)
   {
      fprintf(stderr, "FAIL %s: no shader string %s\n", file, name);
      failures++;
      free(text);
      return NULL;
   }
   start += strlen(key);
   end = strstr(start, ")\";");
   if (!end)
   {
      fprintf(stderr, "FAIL %s: %s is not terminated\n", file, name);
      failures++;
      free(text);
      return NULL;
   }
   len = (size_t)(end - start);
   out = (char*)malloc(len + 1);
   memcpy(out, start, len);
   out[len] = '\0';
   free(text);
   return out;
}

/* src with the one occurrence of find replaced, or NULL when it is absent. */
static char *replace_once(const char *src, const char *find, const char *repl)
{
   const char *at = strstr(src, find);
   size_t head;
   size_t flen;
   size_t rlen;
   char *out;

   if (!at)
      return NULL;
   head = (size_t)(at - src);
   flen = strlen(find);
   rlen = strlen(repl);
   out = (char*)malloc(strlen(src) - flen + rlen + 1);
   memcpy(out, src, head);
   memcpy(out + head, repl, rlen);
   strcpy(out + head + rlen, at + flen);
   return out;
}

/* What R_LoadGLSLShader does to a GLES3 vertex shader for VR: leading
 * newlines dropped, the define right after the #version line. */
static char *multiview_variant(const char *src)
{
   const char *v;
   const char *eol;
   static const char define[] = "#define BOOM3_MULTIVIEW\n";
   char *out;
   size_t head;

   while (*src == '\n')
      src++;
   v = strstr(src, "#version");
   eol = v ? strchr(v, '\n') : NULL;
   if (!eol)
      return NULL;
   head = (size_t)(eol + 1 - src);
   out = (char*)malloc(strlen(src) + sizeof(define));
   memcpy(out, src, head);
   memcpy(out + head, define, sizeof(define) - 1);
   strcpy(out + head + sizeof(define) - 1, eol + 1);
   return out;
}

/* ---- GL ---- */

static GLuint compile(GLenum type, const char *src, const char *what)
{
   GLuint sh;
   GLint ok = 0;
   char log[4096];

   while (*src == '\n')
      src++;
   sh = glCreateShader(type);
   glShaderSource(sh, 1, &src, NULL);
   glCompileShader(sh);
   glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
   if (!ok)
   {
      glGetShaderInfoLog(sh, sizeof(log), NULL, log);
      fprintf(stderr, "FAIL %s (%s shader) does not compile:\n%s\n", what,
            type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
      failures++;
      glDeleteShader(sh);
      return 0;
   }
   return sh;
}

#define WANT_GLES3   1   /* ViewMatrices block and u_depthHack */
#define WANT_HDR     2   /* u_hdrParms */
#define WANT_BLOCKS  4   /* the ES 3.1 renderer's DrawVertex/DrawFragment blocks */

/* the uniforms the ES 3.1 renderer moves into its per-draw blocks, as
 * draw_gles3_multiview.cpp names them */
static const char *const draw_vertex_names[] = {
   "u_modelMatrix", "u_depthHack", "u_lightProjection", "u_colorModulate", "u_colorAdd",
   "u_lightOrigin", "u_viewOrigin", "u_bumpMatrixS", "u_bumpMatrixT", "u_diffuseMatrixS",
   "u_diffuseMatrixT", "u_specularMatrixS", "u_specularMatrixT"
};
static const char *const draw_fragment_names[] = {
   "u_diffuseColor", "u_specularColor", "u_hdrParms", "u_specularExponent"
};

static void check_program(const char *what, const char *vs_src, const char *fs_src,
      unsigned want)
{
   GLuint vs;
   GLuint fs;
   GLuint prog;
   GLint ok = 0;
   char log[4096];

   if (!vs_src || !fs_src)
   {
      fprintf(stderr, "FAIL %s: a source is missing\n", what);
      failures++;
      return;
   }
   vs = compile(GL_VERTEX_SHADER, vs_src, what);
   fs = compile(GL_FRAGMENT_SHADER, fs_src, what);
   if (!vs || !fs)
      return;
   prog = glCreateProgram();
   glAttachShader(prog, vs);
   glAttachShader(prog, fs);
   /* the renderers' attribute locations */
   glBindAttribLocation(prog, 0, "attr_Vertex");
   glBindAttribLocation(prog, 1, "attr_Color");
   glBindAttribLocation(prog, 2, "attr_TexCoord");
   glBindAttribLocation(prog, 3, "attr_Normal");
   glBindAttribLocation(prog, 4, "attr_Tangent");
   glBindAttribLocation(prog, 5, "attr_Bitangent");
   glLinkProgram(prog);
   glGetProgramiv(prog, GL_LINK_STATUS, &ok);
   programs_checked++;
   if (!ok)
   {
      glGetProgramInfoLog(prog, sizeof(log), NULL, log);
      fprintf(stderr, "FAIL %s does not link:\n%s\n", what, log);
      failures++;
   }
   else
   {
      if (want & WANT_GLES3)
      {
         GLuint block = glGetUniformBlockIndex(prog, "ViewMatrices");
         GLint size = 0;
         if (block == GL_INVALID_INDEX)
         {
            fprintf(stderr, "FAIL %s: no ViewMatrices block\n", what);
            failures++;
         }
         else
         {
            glGetActiveUniformBlockiv(prog, block, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
            if (size != 2 * 16 * 4)
            {
               fprintf(stderr, "FAIL %s: ViewMatrices is %d bytes, the renderer writes %d\n",
                     what, (int)size, 2 * 16 * 4);
               failures++;
            }
         }
         if (!(want & WANT_BLOCKS) && glGetUniformLocation(prog, "u_depthHack") < 0)
         {
            fprintf(stderr, "FAIL %s: no active u_depthHack\n", what);
            failures++;
         }
      }
      if (want & WANT_BLOCKS)
      {
         /* every per-draw value comes from the blocks */
         int i;
         if (glGetUniformBlockIndex(prog, "DrawVertex") == GL_INVALID_INDEX
               || glGetUniformBlockIndex(prog, "DrawFragment") == GL_INVALID_INDEX)
         {
            fprintf(stderr, "FAIL %s: no DrawVertex or DrawFragment block\n", what);
            failures++;
         }
         for (i = 0; i < (int)(sizeof(draw_vertex_names) / sizeof(draw_vertex_names[0])); i++)
            if (glGetUniformLocation(prog, draw_vertex_names[i]) >= 0)
            {
               fprintf(stderr, "FAIL %s: %s is outside the blocks\n", what, draw_vertex_names[i]);
               failures++;
            }
         for (i = 0; i < (int)(sizeof(draw_fragment_names) / sizeof(draw_fragment_names[0])); i++)
            if (glGetUniformLocation(prog, draw_fragment_names[i]) >= 0)
            {
               fprintf(stderr, "FAIL %s: %s is outside the blocks\n", what, draw_fragment_names[i]);
               failures++;
            }
      }
      else if ((want & WANT_HDR) && glGetUniformLocation(prog, "u_hdrParms") < 0)
      {
         fprintf(stderr, "FAIL %s: no active u_hdrParms\n", what);
         failures++;
      }
   }
   glDeleteProgram(prog);
   glDeleteShader(vs);
   glDeleteShader(fs);
}

/* ---- the renderers' programs ---- */

struct pairing
{
   const char *name;
   const char *vs_file, *vs;
   const char *fs_file, *fs;
   unsigned want;
};

/* As RB_GLSL_InitShaders pairs them in draw_gles2.cpp. */
static const struct pairing gles2_programs[] = {
   { "interaction", "interactionShaderVP.cpp", "interactionShaderVP", "interactionShaderFP.cpp", "interactionShaderFP", WANT_HDR },
   { "interactionPhong", "interactionPhongShaderVP.cpp", "interactionPhongShaderVP", "interactionPhongShaderFP.cpp", "interactionPhongShaderFP", WANT_HDR },
   { "diffuseMap", "diffuseMapShaderVP.cpp", "diffuseMapShaderVP", "diffuseMapShaderFP.cpp", "diffuseMapShaderFP", 0 },
   { "skyboxCube", "skyboxCubeShaderVP.cpp", "skyboxCubeShaderVP", "cubeMapShaderFP.cpp", "cubeMapShaderFP", 0 },
   { "reflectionCube", "reflectionCubeShaderVP.cpp", "reflectionCubeShaderVP", "cubeMapShaderFP.cpp", "cubeMapShaderFP", 0 },
   { "diffuseCube", "diffuseCubeShaderVP.cpp", "diffuseCubeShaderVP", "cubeMapShaderFP.cpp", "cubeMapShaderFP", 0 },
   { "zfill", "zfillShaderVP.cpp", "zfillShaderVP", "zfillShaderFP.cpp", "zfillShaderFP", 0 },
   { "zfillOpaque", "zfillShaderVP.cpp", "zfillShaderVP", "zfillShaderFP.cpp", "zfillOpaqueShaderFP", 0 },
   { "zfillClip", "zfillClipShaderVP.cpp", "zfillClipShaderVP", "zfillClipShaderFP.cpp", "zfillClipShaderFP", 0 },
   { "fog", "fogShaderVP.cpp", "fogShaderVP", "fogShaderFP.cpp", "fogShaderFP", 0 },
   { "blendLight", "blendLightShaderVP.cpp", "blendLightShaderVP", "fogShaderFP.cpp", "fogShaderFP", 0 },
   { "stencilShadow", "stencilShadowShaderVP.cpp", "stencilShadowShaderVP", "stencilShadowShaderFP.cpp", "stencilShadowShaderFP", 0 },
   { NULL, NULL, NULL, NULL, NULL, 0 }
};

/* As RB_GLSL_InitShaders pairs them in draw_gles3_multiview.cpp. */
static const struct pairing gles3_programs[] = {
   { "interactionBlinn", "interactionShaderVP.cpp", "interactionShaderVP", "interactionShaderFP.cpp", "interactionBlinnShaderFP", WANT_GLES3 | WANT_HDR },
   { "interaction (PBR)", "interactionShaderVP.cpp", "interactionShaderVP", "interactionShaderFP.cpp", "interactionShaderFP", WANT_GLES3 | WANT_HDR },
   { "interactionPhong", "interactionPhongShaderVP.cpp", "interactionPhongShaderVP", "interactionPhongShaderFP.cpp", "interactionPhongShaderFP", WANT_GLES3 | WANT_HDR },
   { "diffuseMap", "diffuseMapShaderVP.cpp", "diffuseMapShaderVP", "diffuseMapShaderFP.cpp", "diffuseMapShaderFP", WANT_GLES3 },
   { "skyboxCube", "skyboxCubeShaderVP.cpp", "skyboxCubeShaderVP", "cubeMapShaderFP.cpp", "cubeMapShaderFP", WANT_GLES3 },
   { "reflectionCube", "reflectionCubeShaderVP.cpp", "reflectionCubeShaderVP", "cubeMapShaderFP.cpp", "cubeMapShaderFP", WANT_GLES3 },
   { "diffuseCube", "diffuseCubeShaderVP.cpp", "diffuseCubeShaderVP", "cubeMapShaderFP.cpp", "cubeMapShaderFP", WANT_GLES3 },
   { "zfill", "zfillShaderVP.cpp", "zfillShaderVP", "zfillShaderFP.cpp", "zfillShaderFP", WANT_GLES3 },
   { "zfillOpaque", "zfillShaderVP.cpp", "zfillShaderVP", "zfillShaderFP.cpp", "zfillOpaqueShaderFP", WANT_GLES3 },
   { "zfillClip", "zfillClipShaderVP.cpp", "zfillClipShaderVP", "zfillClipShaderFP.cpp", "zfillClipShaderFP", WANT_GLES3 },
   { "fog", "fogShaderVP.cpp", "fogShaderVP", "fogShaderFP.cpp", "fogShaderFP", WANT_GLES3 },
   { "blendLight", "blendLightShaderVP.cpp", "blendLightShaderVP", "fogShaderFP.cpp", "fogShaderFP", WANT_GLES3 },
   { "stencilShadow", "stencilShadowShaderVP.cpp", "stencilShadowShaderVP", "stencilShadowShaderFP.cpp", "stencilShadowShaderFP", WANT_GLES3 },
   { NULL, NULL, NULL, NULL, NULL, 0 }
};

/* The load-time substitutions both renderers make on the Blinn interaction
 * shader.  The stock text must match the strings in draw_gles2.cpp and
 * draw_gles3_multiview.cpp; the replacements only need to be what those
 * files substitute (the spectral matrices take arbitrary values here). */
static const char aa_stock[] =
   "  float NdotH2 = NdotH * NdotH;\n  float NdotH4 = NdotH2 * NdotH2;\n  float specularFalloff = NdotH4 * NdotH4 * NdotH4;";
static const char aa_repl[] =
   "  float tkLen = length(N);\n"
   "  float tkFt = tkLen / (tkLen + 12.0 * (1.0 - tkLen));\n"
   "  float specularFalloff = pow(max(NdotH, 0.0), 12.0 * tkFt);";

static void check_blinn_variants(const char *label, const char *vs, const char *fs,
      const char *out_var, unsigned want)
{
   char stock_line[128];
   char mix[1024];
   char what[512];
   char *aa;
   char *sm;
   char *both;

   if (!vs || !fs)
      return;
   aa = replace_once(fs, aa_stock, aa_repl);
   if (!aa)
   {
      fprintf(stderr, "FAIL %s: the specular AA stock lines are gone; r_specularAA would do nothing\n", label);
      failures++;
      return;
   }
   sprintf(stock_line, "%s = vec4(dhewm3Epilogue(color), 1.0) * var_Color;", out_var);
   sprintf(mix,
         "vec3 smBase = dhewm3Epilogue(color);\n"
         "  mat3 smM = mat3(0.9000000, 0.0500000, 0.0500000, 0.0500000, 0.9000000, 0.0500000, 0.0500000, 0.0500000, 0.9000000);\n"
         "  mat3 smN = mat3(1.1176471, -0.0588235, -0.0588235, -0.0588235, 1.1176471, -0.0588235, -0.0588235, -0.0588235, 1.1176471);\n"
         "  %s = vec4(smN * ((smM * smBase) * (smM * var_Color.rgb)), 1.0);\n"
         "  %s.a = var_Color.a;", out_var, out_var);
   sm = replace_once(fs, stock_line, mix);
   if (!sm)
   {
      fprintf(stderr, "FAIL %s: the spectral mix stock line is gone; r_spectralMix would do nothing\n", label);
      failures++;
      free(aa);
      return;
   }
   both = replace_once(aa, stock_line, mix);
   sprintf(what, "%s + specular AA", label);
   check_program(what, vs, aa, want);
   sprintf(what, "%s + spectral mix", label);
   check_program(what, vs, sm, want);
   sprintf(what, "%s + specular AA + spectral mix", label);
   check_program(what, vs, both, want);
   free(aa);
   free(sm);
   free(both);
}

static void check_set(const char *subdir, const struct pairing *set, int gles3)
{
   char dir[1024];
   char what[256];
   const struct pairing *p;
   const char *saved = renderer_dir;

   sprintf(dir, "%s/%s", saved, subdir);
   renderer_dir = dir;
   for (p = set; p->name; p++)
   {
      char *vs = shader_source(p->vs_file, p->vs);
      char *fs = shader_source(p->fs_file, p->fs);

      sprintf(what, "%s %s", subdir, p->name);
      check_program(what, vs, fs, p->want);
      if (gles3 && vs && have_multiview)
      {
         char *mv = multiview_variant(vs);
         sprintf(what, "%s %s (multiview)", subdir, p->name);
         check_program(what, mv, fs, p->want);
         free(mv);
      }
      /* the ES 3.1 renderer's interaction programs, fed from blocks */
      if (gles3 && vs && fs && (p->want & WANT_HDR))
      {
         char *bvs = glsl_uniforms_to_block(vs, "DrawVertex", draw_vertex_names,
               (int)(sizeof(draw_vertex_names) / sizeof(draw_vertex_names[0])));
         char *bfs = glsl_uniforms_to_block(fs, "DrawFragment", draw_fragment_names,
               (int)(sizeof(draw_fragment_names) / sizeof(draw_fragment_names[0])));
         sprintf(what, "%s %s (ES 3.1 blocks)", subdir, p->name);
         check_program(what, bvs, bfs, p->want | WANT_BLOCKS);
         if (have_multiview)
         {
            char *mv = multiview_variant(bvs);
            sprintf(what, "%s %s (ES 3.1 blocks, multiview)", subdir, p->name);
            check_program(what, mv, bfs, p->want | WANT_BLOCKS);
            free(mv);
         }
         free(bvs);
         free(bfs);
      }
      free(vs);
      free(fs);
   }
   /* the Blinn interaction programs with their substitutions */
   {
      char *vs = shader_source("interactionShaderVP.cpp", "interactionShaderVP");
      char *fs = shader_source("interactionShaderFP.cpp",
            gles3 ? "interactionBlinnShaderFP" : "interactionShaderFP");
      sprintf(what, "%s interaction (Blinn)", subdir);
      check_blinn_variants(what, vs, fs, gles3 ? "fragColor" : "gl_FragColor",
            gles3 ? (WANT_GLES3 | WANT_HDR) : WANT_HDR);
      free(vs);
      free(fs);
   }
   renderer_dir = saved;
}

/* ---- context ---- */

static int make_context(void)
{
   EGLDisplay dpy = EGL_NO_DISPLAY;
   EGLConfig cfg;
   EGLint n = 0;
   EGLContext ctx;
   EGLSurface surf = EGL_NO_SURFACE;
   const char *client_exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
   static const EGLint cfg_attrs[] = {
      EGL_RENDERABLE_TYPE, 0x40 /* EGL_OPENGL_ES3_BIT */,
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_NONE };
   static const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
   static const EGLint pb_attrs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
   const char *ext;

   /* headless where the platform offers it, the default display otherwise */
   if (client_exts && strstr(client_exts, "EGL_MESA_platform_surfaceless"))
   {
      PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
         (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
      if (get_platform_display)
         dpy = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
   }
   if (dpy == EGL_NO_DISPLAY)
      dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL))
   {
      fprintf(stderr, "FAIL no EGL display\n");
      return 0;
   }
   eglBindAPI(EGL_OPENGL_ES_API);
   if (!eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &n) || n < 1)
   {
      fprintf(stderr, "FAIL no GLES 3 EGL config\n");
      return 0;
   }
   ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
   if (ctx == EGL_NO_CONTEXT)
   {
      fprintf(stderr, "FAIL no GLES 3 context\n");
      return 0;
   }
   ext = eglQueryString(dpy, EGL_EXTENSIONS);
   if (!ext || !strstr(ext, "EGL_KHR_surfaceless_context"))
      surf = eglCreatePbufferSurface(dpy, cfg, pb_attrs);
   if (!eglMakeCurrent(dpy, surf, surf, ctx))
   {
      fprintf(stderr, "FAIL cannot make the context current\n");
      return 0;
   }
   ext = (const char*)glGetString(GL_EXTENSIONS);
   have_multiview = ext && strstr(ext, "GL_OVR_multiview2") != NULL;
   printf("%s, %s%s\n", (const char*)glGetString(GL_RENDERER),
         (const char*)glGetString(GL_VERSION),
         have_multiview ? "" : " (no GL_OVR_multiview2: multiview variants skipped)");
   return 1;
}

int main(int argc, char **argv)
{
   if (argc != 2)
   {
      fprintf(stderr, "usage: %s <neo/renderer directory>\n", argv[0]);
      return 2;
   }
   renderer_dir = argv[1];
   if (!make_context())
      return 1;
   check_set("glsl", gles2_programs, 0);
   check_set("glsl_gles3", gles3_programs, 1);
   if (failures)
   {
      printf("FAIL gles_shader_check: %d failure(s) in %d programs\n", failures, programs_checked);
      return 1;
   }
   printf("PASS gles_shader_check: %d programs\n", programs_checked);
   return 0;
}
