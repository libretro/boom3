/*
 * vr_stack_check: in VR, _currentRender holds both eyes stacked in one 2D
 * texture, the left eye in the bottom half and the right eye in the top
 * half, and the GLES3 post-process programs add half the texture's height
 * for the right eye.  This draws those programs - the shipped sources, read
 * from renderer/glsl_gles3 - once per eye, sampling a stacked texture whose
 * left half is pure red and right half pure green, and fails unless every
 * pixel of the left eye is red and every pixel of the right eye green.
 *
 * The view the vertex programs take from gl_ViewID_OVR comes from a
 * uniform here, the only change made to the sources; their multiview build
 * is checked by tools/gles_shader_check.
 *
 * The heat haze programs are drawn with their deform saturated both ways,
 * so every fragment's texcoord lands on the clamp at the top or the bottom
 * edge of its eye: a clamp onto the boundary between the halves filters in
 * the other eye's edge row there.
 *
 * Headless through EGL (Mesa's llvmpipe on Linux, ANGLE on Windows);
 * GALLIUM_DRIVER=softpipe runs it where llvmpipe's JIT cannot.
 * llvmpipe runs mediump at full precision, so this checks placement and
 * filtering, not fp16 resolution.
 *
 * build: make
 * run:   ./vr_stack_check ../../neo/renderer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

/* an eye: not a power of two, as GLES3 copies the scene at its own size */
#define EYE_W 60
#define EYE_H 44

static const char *renderer_dir;
static int failures;

static char *read_file(const char *name)
{
   char path[1024];
   FILE *f;
   long len;
   char *buf;

   sprintf(path, "%s/glsl_gles3/%s", renderer_dir, name);
   f = fopen(path, "rb");
   if (!f)
   {
      fprintf(stderr, "FAIL cannot open %s\n", path);
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

/* the named R"( )" string of its .cpp; in a vertex program, the single-view
 * VIEW_ID becomes a uniform */
static char *shader_source(const char *name)
{
   static const char single[] = "#define VIEW_ID 0\n";
   static const char uniform[] = "uniform int u_testView;\n#define VIEW_ID u_testView\n";
   char file[256];
   char key[256];
   char *text;
   char *start;
   char *end;
   char *view;
   char *out;
   size_t head;
   size_t len;
   size_t out_len;

   sprintf(file, "%s.cpp", name);
   text = read_file(file);
   if (!text)
      return NULL;
   sprintf(key, "const char * const %s = R\"(", name);
   start = strstr(text, key);
   end = start ? strstr(start, ")\";") : NULL;
   if (!start || !end)
   {
      fprintf(stderr, "FAIL %s: no shader string %s\n", file, name);
      free(text);
      return NULL;
   }
   start += strlen(key);
   while (*start == '\n' || *start == '\r')
      start++;
   len = (size_t)(end - start);
   view = strstr(start, single);
   if (!view || view > end)
   {
      out = (char*)malloc(len + 1);
      memcpy(out, start, len);
      out[len] = '\0';
      free(text);
      return out;
   }
   head = (size_t)(view - start);
   out_len = len - (sizeof(single) - 1) + (sizeof(uniform) - 1);
   out = (char*)malloc(out_len + 1);
   memcpy(out, start, head);
   memcpy(out + head, uniform, sizeof(uniform) - 1);
   memcpy(out + head + sizeof(uniform) - 1, view + sizeof(single) - 1,
         len - head - (sizeof(single) - 1));
   out[out_len] = '\0';
   free(text);
   return out;
}

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
      fprintf(stderr, "FAIL %s does not compile:\n%s\n", what, log);
      glDeleteShader(sh);
      return 0;
   }
   return sh;
}

static GLuint build(const char *vs_name, const char *fs_name)
{
   char *vs_src = shader_source(vs_name);
   char *fs_src = shader_source(fs_name);
   GLuint vs = 0;
   GLuint fs = 0;
   GLuint prog = 0;
   GLint ok = 0;
   char log[4096];

   if (vs_src && fs_src)
   {
      vs = compile(GL_VERTEX_SHADER, vs_src, vs_name);
      fs = compile(GL_FRAGMENT_SHADER, fs_src, fs_name);
   }
   if (vs && fs)
   {
      prog = glCreateProgram();
      glAttachShader(prog, vs);
      glAttachShader(prog, fs);
      glLinkProgram(prog);
      glGetProgramiv(prog, GL_LINK_STATUS, &ok);
      if (!ok)
      {
         glGetProgramInfoLog(prog, sizeof(log), NULL, log);
         fprintf(stderr, "FAIL %s + %s does not link:\n%s\n", vs_name, fs_name, log);
         glDeleteProgram(prog);
         prog = 0;
      }
   }
   if (vs)
      glDeleteShader(vs);
   if (fs)
      glDeleteShader(fs);
   free(vs_src);
   free(fs_src);
   return prog;
}

static void set_vec4(GLuint prog, const char *name, float x, float y, float z, float w)
{
   GLint loc = glGetUniformLocation(prog, name);
   if (loc >= 0)
      glUniform4f(loc, x, y, z, w);
}

static void set_mat4_identity(GLuint prog, const char *name)
{
   static const GLfloat ident[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
   GLint loc = glGetUniformLocation(prog, name);
   if (loc >= 0)
      glUniformMatrix4fv(loc, 1, GL_FALSE, ident);
}

static void set_int(GLuint prog, const char *name, int v)
{
   GLint loc = glGetUniformLocation(prog, name);
   if (loc >= 0)
      glUniform1i(loc, v);
}

/* the uniforms the backend sets for these stages, with an identity view:
 * u_screenParms zw is the eye's extent of the stacked texture */
static void common_uniforms(GLuint prog, GLuint ubo)
{
   GLuint block;
   GLint loc;

   glUseProgram(prog);
   block = glGetUniformBlockIndex(prog, "ViewMatrices");
   if (block != GL_INVALID_INDEX)
   {
      glUniformBlockBinding(prog, block, 0);
      glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
   }
   set_mat4_identity(prog, "u_modelMatrix");
   set_mat4_identity(prog, "u_modelViewMatrix");
   set_mat4_identity(prog, "u_projectionMatrix");
   set_int(prog, "u_fragmentMap0", 0);
   set_int(prog, "u_fragmentMap1", 1);
   set_int(prog, "u_fragmentMap2", 2);
   set_vec4(prog, "u_screenParms", 1.0f / EYE_W, 1.0f / EYE_H,
         1.0f, (float)EYE_H / (float)(EYE_H * 2));
   set_vec4(prog, "u_hdrParms", 1.0f, 0.0f, 0.0f, 1.0f);
   loc = glGetUniformLocation(prog, "u_depthHack");
   if (loc >= 0)
      glUniform1f(loc, 0.0f);
}

/* one full-eye quad through prog for each eye: the left eye all red, the
 * right eye all green */
static void draw_and_check(GLuint prog, GLuint fbo, GLuint vbo, const char *what)
{
   static unsigned char px[EYE_W * EYE_H * 4];
   GLint loc;
   GLint view;
   int eye;
   int i;
   int bad;

   glUseProgram(prog);
   view = glGetUniformLocation(prog, "u_testView");
   if (view < 0)
   {
      fprintf(stderr, "FAIL %s: the vertex program has no view\n", what);
      failures++;
      return;
   }
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glViewport(0, 0, EYE_W, EYE_H);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   loc = glGetAttribLocation(prog, "attr_Vertex");
   if (loc >= 0)
   {
      glEnableVertexAttribArray((GLuint)loc);
      glVertexAttribPointer((GLuint)loc, 4, GL_FLOAT, GL_FALSE, 0, (const void*)0);
   }
   loc = glGetAttribLocation(prog, "attr_Color");
   if (loc >= 0)
      glVertexAttrib4f((GLuint)loc, 1.0f, 1.0f, 1.0f, 1.0f);
   loc = glGetAttribLocation(prog, "attr_TexCoord");
   if (loc >= 0)
      glVertexAttrib4f((GLuint)loc, 0.0f, 0.0f, 0.0f, 1.0f);

   for (eye = 0; eye < 2; eye++)
   {
      glUniform1i(view, eye);
      glClearColor(0.0f, 0.0f, 1.0f, 1.0f);   /* blue: a fragment not drawn */
      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glReadPixels(0, 0, EYE_W, EYE_H, GL_RGBA, GL_UNSIGNED_BYTE, px);
      bad = 0;
      for (i = 0; i < EYE_W * EYE_H; i++)
      {
         const unsigned char *p = px + i * 4;
         const int want_r = eye == 0 ? 255 : 0;
         const int want_g = eye == 0 ? 0 : 255;
         if (p[0] != want_r || p[1] != want_g || p[2] != 0)
         {
            if (!bad)
               fprintf(stderr, "FAIL %s: %s eye pixel (%d,%d) is %d,%d,%d, not %s\n",
                     what, eye ? "right" : "left", i % EYE_W, i / EYE_W,
                     p[0], p[1], p[2], eye ? "green" : "red");
            bad++;
         }
      }
      if (bad)
      {
         fprintf(stderr, "FAIL %s: %d of %d %s eye pixels from the wrong place\n",
               what, bad, EYE_W * EYE_H, eye ? "right" : "left");
         failures++;
      }
      else
         printf("ok   %s: %s eye\n", what, eye ? "right" : "left");
   }
}

static GLuint make_texture_2d(int w, int h, const unsigned char *rgba)
{
   GLuint tex;
   glGenTextures(1, &tex);
   glBindTexture(GL_TEXTURE_2D, tex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   return tex;
}

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
   printf("%s, %s\n", (const char*)glGetString(GL_RENDERER),
         (const char*)glGetString(GL_VERSION));
   return 1;
}

int main(int argc, char **argv)
{
   static unsigned char stacked[EYE_W * EYE_H * 2 * 4];
   static const GLfloat quad[16] = {
      -1.0f, -1.0f, 0.0f, 1.0f,   1.0f, -1.0f, 0.0f, 1.0f,
      -1.0f,  1.0f, 0.0f, 1.0f,   1.0f,  1.0f, 0.0f, 1.0f };
   static const GLfloat views[32] = {
      1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,
      1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
   /* rxgb normals: localNormal.xy comes from alpha and green */
   static const unsigned char normal_up[4]   = { 0, 255, 0, 255 };
   static const unsigned char normal_down[4] = { 0, 0, 0, 0 };
   /* the masked programs' mask: full strength everywhere */
   static const unsigned char white[4] = { 255, 255, 255, 255 };
   static const char *const haze_fps[3] = {
      "heatHazeShaderFP", "heatHazeMaskShaderFP", "heatHazeMaskVertexShaderFP" };
   GLuint eye;
   GLuint fbo;
   GLuint vbo;
   GLuint ubo;
   GLuint scene;
   GLuint normals[2];
   GLuint mask;
   GLuint prog;
   char what[256];
   int i;
   int n;

   if (argc != 2)
   {
      fprintf(stderr, "usage: %s <neo/renderer directory>\n", argv[0]);
      return 2;
   }
   renderer_dir = argv[1];
   if (!make_context())
      return 1;

   /* the stacked _currentRender: left eye red, right eye green */
   for (i = 0; i < EYE_W * EYE_H * 2; i++)
   {
      const int right = i >= EYE_W * EYE_H;
      stacked[i * 4 + 0] = right ? 0 : 255;
      stacked[i * 4 + 1] = right ? 255 : 0;
      stacked[i * 4 + 2] = 0;
      stacked[i * 4 + 3] = 255;
   }
   glActiveTexture(GL_TEXTURE2);
   mask = make_texture_2d(1, 1, white);
   glActiveTexture(GL_TEXTURE1);
   normals[0] = make_texture_2d(1, 1, normal_up);
   normals[1] = make_texture_2d(1, 1, normal_down);
   glActiveTexture(GL_TEXTURE0);
   scene = make_texture_2d(EYE_W, EYE_H * 2, stacked);

   /* an eye's target, RGBA8 as the core's */
   glGenTextures(1, &eye);
   glBindTexture(GL_TEXTURE_2D, eye);
   glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, EYE_W, EYE_H);
   glBindTexture(GL_TEXTURE_2D, scene);
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, eye, 0);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
   {
      fprintf(stderr, "FAIL the eye target is incomplete\n");
      return 1;
   }

   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
   glGenBuffers(1, &ubo);
   glBindBuffer(GL_UNIFORM_BUFFER, ubo);
   glBufferData(GL_UNIFORM_BUFFER, sizeof(views), views, GL_STATIC_DRAW);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_BLEND);

   /* the heat haze programs, deform saturated up and then down */
   for (n = 0; n < 3; n++)
   {
      prog = build("heatHazeShaderVP", haze_fps[n]);
      if (!prog)
      {
         failures++;
         continue;
      }
      common_uniforms(prog, ubo);
      set_vec4(prog, "u_vertexParms[0]", 0.0f, 0.0f, 0.0f, 0.0f);
      set_vec4(prog, "u_vertexParms[1]", 1000.0f, 1000.0f, 0.0f, 0.0f);
      for (i = 0; i < 2; i++)
      {
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, normals[i]);
         glActiveTexture(GL_TEXTURE0);
         sprintf(what, "%s, deform %s", haze_fps[n], i ? "down" : "up");
         draw_and_check(prog, fbo, vbo, what);
      }
      glDeleteProgram(prog);
   }

   /* colour process: the scene through unchanged */
   prog = build("colorProcessShaderVP", "colorProcessShaderFP");
   if (prog)
   {
      common_uniforms(prog, ubo);
      set_vec4(prog, "u_vertexParms[0]", 0.0f, 0.0f, 0.0f, 0.0f);
      set_vec4(prog, "u_vertexParms[1]", 0.0f, 0.0f, 0.0f, 0.0f);
      draw_and_check(prog, fbo, vbo, "colorProcessShaderFP");
      glDeleteProgram(prog);
   }
   else
      failures++;

   glDeleteTextures(1, &mask);
   if (glGetError() != GL_NO_ERROR)
   {
      fprintf(stderr, "FAIL a GL error was raised\n");
      failures++;
   }
   if (failures)
   {
      fprintf(stderr, "FAIL vr_stack_check: %d failures\n", failures);
      return 1;
   }
   printf("PASS vr_stack_check\n");
   return 0;
}
