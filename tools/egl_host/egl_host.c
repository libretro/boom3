/*
 * egl_host: a headless libretro frontend for comparing boom3's renderers.
 *
 * It creates the context the core asks for with SET_HW_RENDER - desktop GL
 * (compatibility or core, any version) or GLES 2, 3 or 3.x - on an EGL
 * display without a window (Mesa's surfaceless platform; llvmpipe needs no
 * GPU), hands the core an 8-bit framebuffer object with depth and stencil
 * as the frontend's framebuffer, runs the frames and prints a hash of each
 * frame the core presents.  Two runs of the same content and options with
 * different renderers must print the same hashes when the renderers are
 * bit-exact with each other on that driver.
 *
 *   egl_host <core.so> <content> <frames> [key=value ...]
 *
 * key=value pairs answer the core's options (doom_renderer=es31, ...);
 * ppm=<file> writes the last frame; from=<n> starts printing hashes at
 * frame n; shots=<n>,<n>,... with ppm=<prefix> writes those frames as
 * <prefix>_<n>.ppm.
 *
 * Linux: Mesa's EGL and GL (libegl-dev libgl-dev libgles-dev).
 *   cc -std=c89 -O1 -g -o egl_host egl_host.c -ldl -lEGL -Wl,--no-as-needed -lm
 */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "../../neo/sys/libretro-common/include/libretro.h"

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x40
#endif
#ifndef EGL_CONTEXT_MAJOR_VERSION
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x00000001
#define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT 0x00000002
#endif

/* the GL the host itself calls, through eglGetProcAddress */
typedef unsigned int GLuint_t;
typedef void (*gl_gen_t)(int, GLuint_t *);
typedef void (*gl_bind_t)(unsigned, GLuint_t);
typedef void (*gl_rbstorage_t)(unsigned, unsigned, int, int);
typedef void (*gl_fbrb_t)(unsigned, unsigned, unsigned, GLuint_t);
typedef unsigned (*gl_check_t)(unsigned);
typedef void (*gl_read_t)(int, int, int, int, unsigned, unsigned, void *);
typedef void (*gl_pixelstore_t)(unsigned, int);
typedef void (*gl_finish_t)(void);

static gl_gen_t        pGenFramebuffers, pGenRenderbuffers;
static gl_bind_t       pBindFramebuffer, pBindRenderbuffer;
static gl_rbstorage_t  pRenderbufferStorage;
static gl_fbrb_t       pFramebufferRenderbuffer;
static gl_check_t      pCheckFramebufferStatus;
static gl_read_t       pReadPixels;
static gl_pixelstore_t pPixelStorei;

#define MAX_OPTIONS 64
static const char *opt_key[MAX_OPTIONS];
static const char *opt_val[MAX_OPTIONS];
static int num_opts;

static struct retro_hw_render_callback hw;
static int have_hw;
static GLuint_t fbo, fbo_color, fbo_depth;
static unsigned fb_w = 640, fb_h = 480;
static unsigned frame_w, frame_h;
static int presented;
static int frame_index;
static int print_from;
static const char *ppm_path;
static const char *shots;
static unsigned char *pixels;
static EGLDisplay dpy;

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   (void)level;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

static uintptr_t get_fb(void) { return fbo; }

static retro_proc_address_t get_proc(const char *sym)
{
   return (retro_proc_address_t)eglGetProcAddress(sym);
}

/* the frame into pixels */
static void read_frame(unsigned w, unsigned h)
{
   pBindFramebuffer(0x8CA8 /* GL_READ_FRAMEBUFFER */, fbo);
   pPixelStorei(0x0D05 /* GL_PACK_ALIGNMENT */, 1);
   pReadPixels(0, 0, (int)w, (int)h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, pixels);
}

/* Two 32-bit FNV-1a lanes over the colour bytes (alpha is the frontend's
 * to ignore), printed as one 64-bit hex value. */
static void hash_frame(unsigned w, unsigned h, char out[17])
{
   unsigned long a = 2166136261UL;
   unsigned long b = 0x811c9dc5UL ^ 0x5bd1e995UL;
   unsigned i;
   read_frame(w, h);
   for (i = 0; i < w * h * 4; i++)
   {
      if ((i & 3) == 3)
         continue;
      a = ((a ^ pixels[i]) * 16777619UL) & 0xffffffffUL;
      b = ((b ^ pixels[i] ^ (i & 0xff)) * 16777619UL) & 0xffffffffUL;
   }
   sprintf(out, "%08lx%08lx", a, b);
}

static void write_ppm(unsigned w, unsigned h)
{
   FILE *f = fopen(ppm_path, "wb");
   int y;
   unsigned x;
   if (!f)
      return;
   fprintf(f, "P6 %u %u 255\n", w, h);
   for (y = (int)h - 1; y >= 0; y--)
      for (x = 0; x < w; x++)
         fwrite(pixels + ((unsigned)y * w + x) * 4, 1, 3, f);
   fclose(f);
}

static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch)
{
   char hash[17];
   (void)pitch;
   if (data != RETRO_HW_FRAME_BUFFER_VALID)
      return;
   presented++;
   frame_w = w;
   frame_h = h;
   if (frame_index >= print_from)
   {
      hash_frame(w, h, hash);
      printf("frame %d %ux%u %s\n", frame_index, w, h, hash);
   }
   if (shots && ppm_path)
   {
      const char *p = shots;
      while (p && *p)
      {
         if (atoi(p) == frame_index)
         {
            char name[512];
            const char *keep = ppm_path;
            sprintf(name, "%s_%d.ppm", keep, frame_index);
            read_frame(w, h);
            ppm_path = name;
            write_ppm(w, h);
            ppm_path = keep;
            break;
         }
         p = strchr(p, ',');
         if (p)
            p++;
      }
   }
}

static size_t audio_batch(const int16_t *d, size_t frames) { (void)d; return frames; }
static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static void input_poll(void) { }
static int16_t input_state(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

/* the HDR presentation contract a frontend with HDR output on hands the
 * core: a PQ swapchain, paper white 200 nits, peak 1000, accurate gamut */
static bool hdr_metadata(void *data)
{
   struct retro_hdr_metadata *md = (struct retro_hdr_metadata *)data;
   if (md->version != 1 || md->size < sizeof(*md))
      return false;
   md->output_mode      = 1;
   md->screen_10bpc     = true;
   md->paper_white_nits = 200.0f;
   md->max_nits         = 1000.0f;
   md->min_nits         = 0.0f;
   md->expand_gamut     = 0;
   md->color_primaries  = 9;
   md->transfer_function = 16;
   return true;
}

static bool env_cb(unsigned cmd, void *data)
{
   int i;
   switch (cmd & 0xffff)
   {
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return true;
   case RETRO_ENVIRONMENT_GET_HDR_METADATA & 0xffff:
      return hdr_metadata(data);
   case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      ((struct retro_log_callback *)data)->log = log_cb;
      return true;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = "/tmp/egl_host_sys";
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE:
   {
      struct retro_variable *v = (struct retro_variable *)data;
      for (i = 0; i < num_opts; i++)
         if (!strcmp(v->key, opt_key[i]))
         {
            v->value = opt_val[i];
            return true;
         }
      v->value = NULL;
      return false;
   }
   case RETRO_ENVIRONMENT_SET_HW_RENDER:
   {
      struct retro_hw_render_callback *cb = (struct retro_hw_render_callback *)data;
      cb->get_current_framebuffer = get_fb;
      cb->get_proc_address = get_proc;
      hw = *cb;
      have_hw = 1;
      return true;
   }
   case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      *(bool *)data = true;
      return true;
   case RETRO_ENVIRONMENT_SET_GEOMETRY:
   case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      return true;
   }
   return false;
}

static int make_context(void)
{
   const char *client = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
   EGLConfig cfg;
   EGLint n = 0;
   EGLint cfg_attrs[] = { EGL_RENDERABLE_TYPE, 0, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
   EGLint ctx_attrs[16];
   int a = 0;
   EGLContext ctx;
   EGLSurface surf;
   static const EGLint pb[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
   int es = 0;
   unsigned major = hw.version_major, minor = hw.version_minor;

   switch (hw.context_type)
   {
   case RETRO_HW_CONTEXT_OPENGLES2:       es = 1; major = 2; minor = 0; break;
   case RETRO_HW_CONTEXT_OPENGLES3:       es = 1; major = 3; minor = 0; break;
   case RETRO_HW_CONTEXT_OPENGLES_VERSION: es = 1; break;
   case RETRO_HW_CONTEXT_OPENGL_CORE:     break;
   case RETRO_HW_CONTEXT_OPENGL:          if (!major) { major = 2; minor = 1; } break;
   default:
      fprintf(stderr, "egl_host: context type %d is not GL\n", (int)hw.context_type);
      return 0;
   }

   dpy = EGL_NO_DISPLAY;
   if (client && strstr(client, "EGL_MESA_platform_surfaceless"))
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

   cfg_attrs[1] = es ? (major >= 3 ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT) : EGL_OPENGL_BIT;
   eglBindAPI(es ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
   if (!eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &n) || n < 1)
      return 0;
   ctx_attrs[a++] = EGL_CONTEXT_MAJOR_VERSION;
   ctx_attrs[a++] = (EGLint)major;
   ctx_attrs[a++] = EGL_CONTEXT_MINOR_VERSION;
   ctx_attrs[a++] = (EGLint)minor;
   if (!es && (major > 3 || (major == 3 && minor >= 2)))
   {
      ctx_attrs[a++] = EGL_CONTEXT_OPENGL_PROFILE_MASK;
      ctx_attrs[a++] = hw.context_type == RETRO_HW_CONTEXT_OPENGL_CORE
         ? EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT : EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT;
   }
   ctx_attrs[a] = EGL_NONE;
   ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
   if (ctx == EGL_NO_CONTEXT)
      return 0;
   surf = eglCreatePbufferSurface(dpy, cfg, pb);
   if (!eglMakeCurrent(dpy, surf, surf, ctx))
      return 0;
   fprintf(stderr, "egl_host: %s %u.%u%s: %s\n", es ? "GLES" : "GL", major, minor,
         hw.context_type == RETRO_HW_CONTEXT_OPENGL_CORE ? " core" : "",
         (const char *)((const unsigned char *(*)(unsigned))eglGetProcAddress("glGetString"))(0x1F02));
   return 1;
}

static int make_framebuffer(void)
{
   pGenFramebuffers = (gl_gen_t)eglGetProcAddress("glGenFramebuffers");
   pGenRenderbuffers = (gl_gen_t)eglGetProcAddress("glGenRenderbuffers");
   pBindFramebuffer = (gl_bind_t)eglGetProcAddress("glBindFramebuffer");
   pBindRenderbuffer = (gl_bind_t)eglGetProcAddress("glBindRenderbuffer");
   pRenderbufferStorage = (gl_rbstorage_t)eglGetProcAddress("glRenderbufferStorage");
   pFramebufferRenderbuffer = (gl_fbrb_t)eglGetProcAddress("glFramebufferRenderbuffer");
   pCheckFramebufferStatus = (gl_check_t)eglGetProcAddress("glCheckFramebufferStatus");
   pReadPixels = (gl_read_t)eglGetProcAddress("glReadPixels");
   pPixelStorei = (gl_pixelstore_t)eglGetProcAddress("glPixelStorei");
   if (!pGenFramebuffers || !pReadPixels)
      return 0;
   pGenFramebuffers(1, &fbo);
   pBindFramebuffer(0x8D40 /* GL_FRAMEBUFFER */, fbo);
   pGenRenderbuffers(1, &fbo_color);
   pBindRenderbuffer(0x8D41, fbo_color);
   pRenderbufferStorage(0x8D41, 0x8058 /* GL_RGBA8 */, (int)fb_w, (int)fb_h);
   pFramebufferRenderbuffer(0x8D40, 0x8CE0 /* COLOR0 */, 0x8D41, fbo_color);
   pGenRenderbuffers(1, &fbo_depth);
   pBindRenderbuffer(0x8D41, fbo_depth);
   pRenderbufferStorage(0x8D41, 0x88F0 /* GL_DEPTH24_STENCIL8 */, (int)fb_w, (int)fb_h);
   pFramebufferRenderbuffer(0x8D40, 0x821A /* DEPTH_STENCIL */, 0x8D41, fbo_depth);
   if (pCheckFramebufferStatus(0x8D40) != 0x8CD5)
      return 0;
   pixels = (unsigned char *)malloc(fb_w * fb_h * 4);
   return 1;
}

int main(int argc, char **argv)
{
   void *so;
   int frames, i;
   struct retro_game_info gi;
   struct retro_system_av_info av;
   void (*set_env)(retro_environment_t);
   void (*set_vid)(retro_video_refresh_t);
   void (*set_as)(retro_audio_sample_t);
   void (*set_ab)(retro_audio_sample_batch_t);
   void (*set_ip)(retro_input_poll_t);
   void (*set_is)(retro_input_state_t);
   void (*r_init)(void);
   bool (*r_load)(const struct retro_game_info *);
   void (*r_av)(struct retro_system_av_info *);
   void (*r_run)(void);
   void (*r_unload)(void);
   void (*r_deinit)(void);

   if (argc < 4)
   {
      fprintf(stderr, "usage: %s <core.so> <content> <frames> [key=value ...]\n", argv[0]);
      return 2;
   }
   frames = atoi(argv[3]);
   for (i = 4; i < argc && num_opts < MAX_OPTIONS; i++)
   {
      char *eq = strchr(argv[i], '=');
      if (!eq)
         continue;
      *eq = '\0';
      if (!strcmp(argv[i], "ppm"))
         ppm_path = eq + 1;
      else if (!strcmp(argv[i], "shots"))
         shots = eq + 1;
      else if (!strcmp(argv[i], "from"))
         print_from = atoi(eq + 1);
      else
      {
         opt_key[num_opts] = argv[i];
         opt_val[num_opts] = eq + 1;
         num_opts++;
      }
   }

   so = dlopen(argv[1], RTLD_NOW);
   if (!so)
   {
      fprintf(stderr, "dlopen: %s\n", dlerror());
      return 2;
   }
   *(void **)&set_env  = dlsym(so, "retro_set_environment");
   *(void **)&set_vid  = dlsym(so, "retro_set_video_refresh");
   *(void **)&set_as   = dlsym(so, "retro_set_audio_sample");
   *(void **)&set_ab   = dlsym(so, "retro_set_audio_sample_batch");
   *(void **)&set_ip   = dlsym(so, "retro_set_input_poll");
   *(void **)&set_is   = dlsym(so, "retro_set_input_state");
   *(void **)&r_init   = dlsym(so, "retro_init");
   *(void **)&r_load   = dlsym(so, "retro_load_game");
   *(void **)&r_av     = dlsym(so, "retro_get_system_av_info");
   *(void **)&r_run    = dlsym(so, "retro_run");
   *(void **)&r_unload = dlsym(so, "retro_unload_game");
   *(void **)&r_deinit = dlsym(so, "retro_deinit");

   set_env(env_cb);
   set_vid(video_refresh);
   set_as(audio_sample);
   set_ab(audio_batch);
   set_ip(input_poll);
   set_is(input_state);
   r_init();

   memset(&gi, 0, sizeof(gi));
   gi.path = argv[2];
   if (!r_load(&gi))
   {
      fprintf(stderr, "egl_host: load failed\n");
      return 2;
   }
   if (!have_hw)
   {
      fprintf(stderr, "egl_host: the core asked for no hardware context\n");
      return 2;
   }
   memset(&av, 0, sizeof(av));
   r_av(&av);
   if (av.geometry.max_width && av.geometry.max_height)
   {
      fb_w = av.geometry.max_width;
      fb_h = av.geometry.max_height;
   }
   if (!make_context() || !make_framebuffer())
   {
      fprintf(stderr, "egl_host: no context or framebuffer for what the core asked\n");
      return 2;
   }
   if (hw.context_reset)
      hw.context_reset();

   for (frame_index = 0; frame_index < frames; frame_index++)
      r_run();

   if (ppm_path && presented && !shots)
   {
      read_frame(frame_w, frame_h);
      write_ppm(frame_w, frame_h);
   }
   fprintf(stderr, "egl_host: %d frames run, %d presented\n", frames, presented);
   r_unload();
   r_deinit();
   return 0;
}
