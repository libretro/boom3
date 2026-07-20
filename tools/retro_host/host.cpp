// mini host: dlopen boom3_libretro.so, provide GL HW-render env, run frames,
// capture audio in BOTH formats (run twice: FLOAT=1 negotiates float).
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include "../../neo/sys/libretro-common/include/libretro.h"

static retro_hw_render_callback hwr;
static SDL_Window *win; static SDL_GLContext glc;
static FILE *fcap;
static bool want_float=false, negotiated_float=false;
const char *g_framerate = "60";
const char *g_samplerate = "44100";	// served to doom_sound_samplerate; argv[9]
// pixel probe: mean luminance of the viewmodel region (bottom-center),
// sampled after each retro_run inside [probe_from, probe_to)
static int probe_from=0, probe_to=0;
static float probe_lum[4096];
static unsigned frames_run=0;
static uint64_t audio_frames=0;

static void log_cb(enum retro_log_level, const char *fmt, ...) {
	va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
static uintptr_t get_fb(void){ return 0; }
static retro_proc_address_t get_proc(const char *sym){ return (retro_proc_address_t)SDL_GL_GetProcAddress(sym); }

static size_t batch_float(const float *data, size_t frames){
	if (fcap) fwrite(data, sizeof(float)*2, frames, fcap);
	audio_frames += frames; return frames;
}
static size_t batch_s16(const int16_t *data, size_t frames){
	if (fcap) fwrite(data, sizeof(int16_t)*2, frames, fcap);
	audio_frames += frames; return frames;
}
static void sample_s16(int16_t, int16_t){}
static void input_poll(void){}
static int16_t input_state(unsigned, unsigned, unsigned, unsigned){ return 0; }
static int cur_frame = -1;
static void video_refresh(const void *data, unsigned w, unsigned h, size_t){
	if (cur_frame >= probe_from && cur_frame < probe_to) {
		static void (*bindFB)(GLenum, GLuint) = (void(*)(GLenum,GLuint))SDL_GL_GetProcAddress("glBindFramebuffer");
		if (bindFB) bindFB(0x8CA8, 0);
		glReadBuffer(GL_BACK);
		static unsigned char px[160*100*4];
		glReadPixels(240, 20, 160, 100, GL_RGBA, GL_UNSIGNED_BYTE, px);
		double sum=0; for (int p=0;p<160*100;p++) sum += 0.299*px[p*4]+0.587*px[p*4+1]+0.114*px[p*4+2];
		probe_lum[cur_frame-probe_from] = (float)(sum/(160*100));
		if (cur_frame == probe_from + 50) {
			static unsigned char full[640*480*3];
			glReadPixels(0,0,640,480,GL_RGB,GL_UNSIGNED_BYTE,full);
			FILE *pf=fopen("/tmp/probe_frame.ppm","wb");
			fprintf(pf,"P6 640 480 255\n");
			for(int y=479;y>=0;y--) fwrite(full+y*640*3,3,640,pf);
			fclose(pf);
		}
	}
}

static bool env_cb(unsigned cmd, void *data){
	switch (cmd & 0xffff) {
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT & 0xffff: return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE & 0xffff:
		((struct retro_log_callback*)data)->log = log_cb; return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY & 0xffff:
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY & 0xffff:
		*(const char**)data = "/tmp/d3sys"; return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE & 0xffff: {
		struct retro_variable *v = (struct retro_variable*)data;
		if (!strcmp(v->key,"doom_resolution")) { v->value = "640x480"; return true; }
		if (!strcmp(v->key,"doom_framerate"))  { extern const char *g_framerate; v->value = g_framerate; return true; }
		if (!strcmp(v->key,"doom_sound_samplerate")) { extern const char *g_samplerate; v->value = g_samplerate; return true; }
		v->value = NULL; return false; }
	case RETRO_ENVIRONMENT_SET_HW_RENDER & 0xffff: {
		struct retro_hw_render_callback *cb = (struct retro_hw_render_callback*)data;
		hwr = *cb;
		cb->get_current_framebuffer = get_fb;
		cb->get_proc_address = get_proc;
		hwr = *cb;
		return true; }
	}
	if (cmd == RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT) {
		if (!want_float) return false;
		((struct retro_audio_sample_float_callback*)data)->batch = batch_float;
		negotiated_float = true;
		return true;
	}
	// benign defaults
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool*)data = true; return true;
	case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE: *(float*)data = 60.0f; return true;
	}
	return false;
}

typedef void (*fv)(void);
int main(int argc, char **argv){
	want_float = argc > 2 && atoi(argv[2]) == 1;
	const char *cap = argc > 3 ? argv[3] : NULL;
	int nframes = argc > 4 ? atoi(argv[4]) : 600;

	SDL_Init(SDL_INIT_VIDEO);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	win = SDL_CreateWindow("h", 0,0, 640,480, SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
	glc = SDL_GL_CreateContext(win);
	if (!glc){ fprintf(stderr,"no GL\n"); return 2; }

	void *so = dlopen("./boom3_libretro.so", RTLD_NOW);
	if (!so){ fprintf(stderr,"dlopen: %s\n", dlerror()); return 2; }
	#define SYM(n) auto p_##n = (decltype(&n))dlsym(so, #n)
	auto set_env   = (void(*)(retro_environment_t))dlsym(so,"retro_set_environment");
	auto set_vid   = (void(*)(retro_video_refresh_t))dlsym(so,"retro_set_video_refresh");
	auto set_as    = (void(*)(retro_audio_sample_t))dlsym(so,"retro_set_audio_sample");
	auto set_ab    = (void(*)(retro_audio_sample_batch_t))dlsym(so,"retro_set_audio_sample_batch");
	auto set_ip    = (void(*)(retro_input_poll_t))dlsym(so,"retro_set_input_poll");
	auto set_is    = (void(*)(retro_input_state_t))dlsym(so,"retro_set_input_state");
	auto r_init    = (void(*)(void))dlsym(so,"retro_init");
	auto r_load    = (bool(*)(const struct retro_game_info*))dlsym(so,"retro_load_game");
	auto r_run     = (void(*)(void))dlsym(so,"retro_run");
	auto r_unload  = (void(*)(void))dlsym(so,"retro_unload_game");
	auto r_deinit  = (void(*)(void))dlsym(so,"retro_deinit");

	set_env(env_cb); set_vid(video_refresh); set_as(sample_s16);
	set_ab(batch_s16); set_ip(input_poll); set_is(input_state);
	r_init();

	/* options must be in place before retro_load_game: the core reads
	   doom_framerate and doom_sound_samplerate from update_variables(true)
	   inside load, so parsing them after it serves stale defaults */
	if (argc > 6) g_framerate = argv[6];
	if (argc > 9) g_samplerate = argv[9];

	struct retro_game_info gi; memset(&gi,0,sizeof gi);
	gi.path = argv[1];
	if (!r_load(&gi)){ fprintf(stderr,"load failed\n"); return 2; }

	if (hwr.context_reset) hwr.context_reset();
	if (cap) fcap = fopen(cap, "wb");

	int state_at = argc > 5 ? atoi(argv[5]) : 0;
	auto r_ssize = (size_t(*)(void))dlsym(so,"retro_serialize_size");
	auto r_ser   = (bool(*)(void*,size_t))dlsym(so,"retro_serialize");
	auto r_unser = (bool(*)(const void*,size_t))dlsym(so,"retro_unserialize");
	void *state_buf = NULL; size_t state_size = 0;

	probe_from = argc > 7 ? atoi(argv[7]) : 0;
	probe_to   = argc > 8 ? atoi(argv[8]) : 0;
	if (probe_to - probe_from > 4096) probe_to = probe_from + 4096;

	for (int i=0;i<nframes;i++){
		cur_frame = i;
		r_run(); frames_run++;
		if (state_at && i == state_at) {
			state_size = r_ssize();
			fprintf(stderr, "[host] serialize_size at frame %d: %zu\n", i, state_size);
			if (state_size) {
				state_buf = malloc(state_size);
				bool ok = r_ser(state_buf, state_size);
				fprintf(stderr, "[host] serialize: %s\n", ok?"OK":"FAIL");
				if (!ok) { free(state_buf); state_buf=NULL; }
			}
		}
		if (state_buf && i == state_at + 120) {
			bool ok = r_unser(state_buf, state_size);
			fprintf(stderr, "[host] unserialize: %s\n", ok?"OK":"FAIL");
		}
	}

	if (fcap) fclose(fcap);
	if (probe_to > probe_from) {
		// report per-frame luminance and an alternation metric: mean |L[i]-L[i-1]|
		// vs mean |L[i]-L[i-2]| - frame-alternating flicker makes the first
		// large and the second small
		double d1=0,d2=0; int n=probe_to-probe_from;
		for (int i=1;i<n;i++) d1 += fabs(probe_lum[i]-probe_lum[i-1]);
		for (int i=2;i<n;i++) d2 += fabs(probe_lum[i]-probe_lum[i-2]);
		d1/= (n-1); d2/=(n-2);
		fprintf(stderr, "[probe] frames %d..%d lum mean=%.2f d1=%.3f d2=%.3f ratio=%.2f%s\n",
			probe_from, probe_to, probe_lum[0], d1, d2, d2>0.001?d1/d2:0.0,
			(d1 > 3.0 && d1 > 4.0*d2) ? "  <-- FRAME-ALTERNATING FLICKER" : "");
		for (int i=0;i<n && i<24;i++) fprintf(stderr, "%.1f ", probe_lum[i]);
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "\n[host] mode=%s frames=%u audio_frames=%llu (%.2f/frame)\n",
		negotiated_float?"FLOAT":"S16", frames_run,
		(unsigned long long)audio_frames, (double)audio_frames/frames_run);
	r_unload(); r_deinit();
	return 0;
}
