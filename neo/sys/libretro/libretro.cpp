/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

extern "C" {
#include "../libretro-common/include/libretro.h"
#include "../libretro-common/include/retro_dirent.h"
#include "../libretro-common/include/features/features_cpu.h"
#include "../libretro-common/include/file/file_path.h"
#include "../libretro-common/include/net/net_compat.h"
#include "../libretro-common/include/net/net_socket.h"
}

#include "../libretro-common/include/glsym/glsym.h"
#include "../libretro-common/include/glsm/glsm.h"

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include "sys/platform.h"
#include "framework/Common.h"
#include "framework/Licensee.h"
#include "framework/FileSystem.h"
#include "framework/KeyInput.h"
#include "framework/Session_local.h"
#include "renderer/ModelManager.h"
#include "renderer/tr_local.h"
#include "sys/libretro/retro_public.h"
#include "sys/sys_local.h"
#include "sound/snd_local.h"

#include "libretro_core_options.h"

#include <locale.h>

#include <glsm/glsm.h>



#define RETRO_AUDIO_BUFFER_SIZE 2048
/* Output sample rate, chosen once at startup from the doom_sound_samplerate
   core option. retro_get_system_av_info() is queried once, so it cannot change
   afterwards. 44100 is what Doom 3's assets are authored at. */
#define SAMPLE_RATE_DEFAULT	44100
static unsigned sample_rate = SAMPLE_RATE_DEFAULT;
#define SAMPLE_RATE		(sample_rate)

/* Largest number of output frames per retro_run. No ring buffer: every
 * retro_run mixes exactly the frames it outputs and hands them straight to
 * the frontend, so the per-call demand is simply sampleRate / framerate and
 * the audio latency is one video frame regardless of rate.
 *
 * That demand is bounded by three values that live in three different files:
 *
 *   AUDIO_MIN_FRAMERATE   30      here, the clamp in update_variables()
 *   AUDIO_MAX_SAMPLERATE  96000   snd_SetSampleRate() in snd_system.cpp
 *   MIXBUFFER_SAMPLES     4096    idlib/math/Simd.h, sizes fixed arrays
 *
 * The worst case is 96000/30 = 3200 frames, which fits in 4096 with room to
 * spare - but nothing tied the three together, and the frames > MAX_FRAME_
 * SAMPLES clamp below silently DROPS audio rather than failing if they ever
 * stop fitting. Lowering the framerate floor to 20, or adding a 192kHz
 * option, would quietly break playback. Assert the relationship instead. */
#define AUDIO_MIN_FRAMERATE   30
#define AUDIO_MAX_SAMPLERATE  96000
#define MAX_FRAME_SAMPLES     MIXBUFFER_SAMPLES

/* If this fires, either raise MIXBUFFER_SAMPLES in idlib/math/Simd.h (it
 * sizes fixed arrays, several on the stack - check the cost), or keep the
 * framerate floor and sample-rate ceiling where they are. */
typedef char audio_frame_budget_fits[
	( AUDIO_MAX_SAMPLERATE / AUDIO_MIN_FRAMERATE <= MAX_FRAME_SAMPLES ) ? 1 : -1 ];
#define BUFFER_SIZE 	32768

#define RETRO_DEVICE_MODERN  RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2)
#define RETRO_DEVICE_JOYPAD_ALT  RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1)

bool first_boot = true;
int invert_y_axis = 1;

bool initial_resolution_set = false;
static bool libretro_shared_context = false;

int framerate = 60;
int scr_width = 1920, scr_height = 1080;

char g_rom_dir[1024], g_pak_path[1024], g_save_dir[1024];

char *BUILD_DATADIR;

extern struct retro_hw_render_callback hw_render;

/* retail default.cfg sets these Win32-era cvars; register them as inert
   so every boot doesn't print "Unknown command" twice */
static idCVar in_mouse( "in_mouse", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "legacy, unused" );
static idCVar m_strafe( "m_strafe", "0.25", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "legacy, unused" );

/* 30-bit / HDR10 output state; the implementation lives above
   GLimp_SwapBuffers, the full design comment with it. */
bool          hdr_output_active = false;   /* chosen at load, needs restart; read by draw_arb2 */
float         hdr_specular_gain = 2.0f;    /* interaction specular scale in HDR mode; read by draw_arb2 */
static bool   hdr_rolloff_aces  = false;   /* live-switchable */

static GLuint hdr_fbo, hdr_tex, hdr_rbo, hdr_prog;
static GLint  hdr_loc_pos, hdr_loc_tex, hdr_loc_mat, hdr_loc_parms;
static GLint  hdr_loc_bloomT, hdr_loc_bloomW, hdr_loc_bloomAmt;
static int    hdr_w, hdr_h;
static bool   hdr_warned_sdr, hdr_warned_narrow;
static float  hdr_bloom_amount = 1.0f;      /* 0 = off; live-switchable */

/* bloom chain: two bands (1/4-res tight core, 1/16-res wide haze),
   each with a ping-pong pair for the separable blur */
static GLuint hdr_bloom_fbo[4], hdr_bloom_tex[4];   /* [0,1]=1/4 A/B, [2,3]=1/16 A/B */
static GLuint hdr_prog_bright, hdr_prog_blur;
static GLint  hdr_bright_loc_thresh, hdr_blur_loc_dir;
static void hdr_bind_scene( void );
static void hdr_present( GLuint dstFbo );

static retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
retro_audio_sample_batch_t audio_batch_cb;
retro_environment_t environ_cb;
static retro_input_poll_t poll_cb;
static retro_input_state_t input_cb;
retro_perf_get_time_usec_t perf_get_time_usec = NULL;
static bool libretro_supports_bitmasks = false;

static void audio_upload_frame(void);

#define MAX_PADS 1
static unsigned doom_devices[MAX_PADS];

// System analog stick range is -0x8000 to 0x8000
#define ANALOG_RANGE 0x8000
// Default deadzone: 15%
static int analog_deadzone = (int)(0.15f * ANALOG_RANGE);

#define GP_MAXBINDS 32

#define LANALOG_LEFT  0x01
#define LANALOG_RIGHT 0x02
#define LANALOG_UP    0x04
#define LANALOG_DOWN  0x08

extern void Key_Event(int button, int val);
extern void Mouse_Event(int x, int y);
uint32_t oldanalogs;
static uint32_t old_ret; // button bitmask from previous frame (bit 15 = R3, so keep it unsigned/wide)

typedef struct {
   struct retro_input_descriptor desc[GP_MAXBINDS];
   struct {
      char *key;
      char *com;
   } bind[GP_MAXBINDS];
} gp_layout_t;

static bool kb_mouse_btn[5] = { false, false, false, false, false };
static const int kb_mouse_keys[5] = {
    K_MOUSE1, K_MOUSE2, K_MOUSE3, K_MOUSE4, K_MOUSE5
};

static float mouse_sensitivity = 3.0f;

extern void Char_Event(int c);

gp_layout_t modern = {
   {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Swim Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Strafe Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Strafe Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Swim Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "Previous Weapon" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Next Weapon" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "Jump" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "Fire" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,"Show Scores" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Menu" },
      { 0 },
   },
   {
      {"JOY_DPAD_LEFT",     "_moveLeft"},
      {"JOY_DPAD_RIGHT",    "_moveRight"},
      {"JOY_DPAD_DOWN",     "_back"},
      {"JOY_DPAD_UP",       "_forward"},

      {"JOY_BTN_SOUTH",     "_moveDown"},   // B = Swim Down
      {"JOY_BTN_EAST",      "_moveRight"},  // A = Strafe Right
      {"JOY_BTN_WEST",      "_moveLeft"},   // X = Strafe Left
      {"JOY_BTN_NORTH",     "_moveUp"},     // Y = Swim Up / Jump

      {"JOY_BTN_LSHOULDER", "_impulse12"},  // Previous Weapon
      {"JOY_BTN_RSHOULDER", "_impulse10"},  // Next Weapon

      {"JOY_TRIGGER1",      "_moveUp"},     // L2 = Jump (same as Y)
      {"JOY_TRIGGER2",      "_attack"},     // R2 = Fire

      {"JOY_BTN_BACK",      "_showscores"},
      {"JOY_BTN_START",     "_escape"},

      {0},
   },
};

gp_layout_t classic = {
   {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Jump" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Cycle Weapon" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Freelook" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Fire" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "Strafe Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Strafe Right" },
//      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "Look Up" },
//      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "Look Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "Move Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "Swim Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,"Toggle Run Mode" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Menu" },
      { 0 },
   },
   {
      { "JOY_DPAD_LEFT",     "_moveLeft"   },
      { "JOY_DPAD_RIGHT",    "_moveRight"  },
      { "JOY_DPAD_DOWN",     "_back"       },
      { "JOY_DPAD_UP",       "_forward"    },
      { "JOY_BTN_SOUTH",     "_moveUp"     },  // B = Jump
      { "JOY_BTN_EAST",      "_impulse10"  },  // A = Next Weapon
      { "JOY_BTN_WEST",      "_klook"      },  // X = Freelook
      { "JOY_BTN_NORTH",     "_attack"     },  // Y = Fire
      { "JOY_BTN_LSHOULDER", "_moveLeft"   },  // L = Strafe Left
      { "JOY_BTN_RSHOULDER", "_moveRight"  },  // R = Strafe Right
//      { "JOY_TRIGGER1",      "_lookUp"     },  // L2
//      { "JOY_TRIGGER2",      "_lookDown"   },  // R2
      { "JOY_BTN_LSTICK",    "_moveDown"   },  // L3
      { "JOY_BTN_RSTICK",    "_impulse19"  },  // R3 = Use
      { "JOY_BTN_BACK",      "_speed"      },  // Select = Run
      { "JOY_BTN_START",     "_escape"  },
      { 0 },
   },
};

gp_layout_t classic_alt = {

   {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Look Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Look Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Look Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Look Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "Jump" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Fire" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "Run" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "Next Weapon" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "Move Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "Previous Weapon" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,"Toggle Run Mode" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Menu" },
      { 0 },
   },
   {
      { "JOY_DPAD_LEFT",     "_moveLeft"   },
      { "JOY_DPAD_RIGHT",    "_moveRight"  },
      { "JOY_DPAD_DOWN",     "_back"       },
      { "JOY_DPAD_UP",       "_forward"    },
      { "JOY_BTN_SOUTH",     "_lookDown"   },  // B
      { "JOY_BTN_EAST",      "_right"      },  // A = Turn Right
      { "JOY_BTN_WEST",      "_lookUp"     },  // X
      { "JOY_BTN_NORTH",     "_left"       },  // Y = Turn Left
      { "JOY_BTN_LSHOULDER", "_moveUp"     },  // L = Jump
      { "JOY_BTN_RSHOULDER", "_attack"     },  // R = Fire
      { "JOY_TRIGGER1",      "_speed"      },  // L2 = Run
      { "JOY_TRIGGER2",      "_impulse10"  },  // R2 = Next Weapon
      { "JOY_BTN_LSTICK",    "_moveDown"   },  // L3
      { "JOY_BTN_RSTICK",    "_impulse12"  },  // R3 = Prev Weapon
      { "JOY_BTN_BACK",      "_speed"      },  // Select = Toggle Run
      { "JOY_BTN_START",     "_escape"  },
      { 0 },
   },
};

static retro_hw_context_type get_hw_context_type(void)
{
#ifdef HAVE_OPENGLES
#if defined(HAVE_OPENGLES_3_2)
   return RETRO_HW_CONTEXT_OPENGLES_VERSION;   // major=3, minor=2
#elif defined(HAVE_OPENGLES_3_1)
   return RETRO_HW_CONTEXT_OPENGLES_VERSION;   // major=3, minor=1
#elif defined(HAVE_OPENGLES3)
   return RETRO_HW_CONTEXT_OPENGLES3;
#else
   return RETRO_HW_CONTEXT_OPENGLES2;
#endif
#else
   return RETRO_HW_CONTEXT_OPENGL;
#endif
}

/* Snap a host target rate to the nearest value the option advertises.
 * Same thresholds as tyrquake's backport of the prboom option. */
static unsigned nearest_supported_rate(unsigned host_rate)
{
	if (host_rate <= (32000u + 44100u) / 2) return 32000;
	if (host_rate <= (44100u + 48000u) / 2) return 44100;
	if (host_rate <= (48000u + 96000u) / 2) return 48000;
	return 96000;
}

/* Resolve the "Sound Samplerate (Hint)" option to a concrete rate.
 *
 * "auto" - the default - asks the frontend what rate its audio device is
 * actually running at via RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE and snaps
 * to the nearest advertised value, so the core renders directly at the host
 * rate and nothing in the chain has to resample. That is the point of the
 * "(Hint)" in the name. Frontends that do not implement the call fall back to
 * 44100, which is what Doom 3's assets are authored at, so they see exactly
 * the previous behaviour. An explicit "32000".."96000" is taken verbatim.
 *
 * Resolved at startup only: retro_get_system_av_info() is queried once, so
 * the rate cannot change afterwards. */
static void update_audio_samplerate(void)
{
	struct retro_variable var;
	unsigned chosen = SAMPLE_RATE_DEFAULT;

	var.key = "doom_sound_samplerate";
	var.value = NULL;

	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
	{
		if (!strcmp(var.value, "auto"))
		{
			unsigned host_rate = 0;
			if (environ_cb(RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE, &host_rate)
					&& host_rate > 0)
				chosen = nearest_supported_rate(host_rate);
			/* else: keep the 44100 fallback above */
		}
		else
		{
			unsigned hz = (unsigned)atoi(var.value);
			if (hz == 32000 || hz == 44100 || hz == 48000 || hz == 96000)
				chosen = hz;
		}
	}

	sample_rate = chosen;
	snd_SetSampleRate((int)sample_rate);

	if (log_cb)
		log_cb(RETRO_LOG_INFO, "[boom3] audio sample rate: %u Hz\n", sample_rate);
}

static void update_variables(bool startup)
{
	struct retro_variable var;

	if (startup)
		update_audio_samplerate();

	var.key = "doom_framerate";
	var.value = NULL;
	
	if (startup)
	{
		if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		{
			if (!strcmp(var.value, "auto"))
			{
				float target_framerate = 0.0f;
				if (!environ_cb(RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE, &target_framerate))
					target_framerate = 60.0f;
				framerate = (unsigned)target_framerate;
			}
			else
				framerate = atoi(var.value);
		}
		else
			framerate    = 60;

		/* Keep per-frame audio demand within the fixed buffers: the floor is
		 * what the MAX_FRAME_SAMPLES static assertion above is checked
		 * against, so the two must stay in step. */
		if (framerate < AUDIO_MIN_FRAMERATE)
			framerate = AUDIO_MIN_FRAMERATE;
		else if (framerate > 240)
			framerate = 240;

		/* feed the deterministic clock and the exact tic schedule */
		Core_SetFramerate(framerate);
		Com_SetFrameSchedule(framerate);
	}
	
	var.key = "doom_resolution";
	var.value = NULL;
	
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && !initial_resolution_set)
	{
		char *pch;
		char str[100];
		snprintf(str, sizeof(str), "%s", var.value);

		pch = strtok(str, "x");
		if (pch)
			scr_width = strtoul(pch, NULL, 0);
		pch = strtok(NULL, "x");
		if (pch)
			scr_height = strtoul(pch, NULL, 0);

		if (log_cb)
			log_cb(RETRO_LOG_INFO, "Got size: %u x %u.\n", scr_width, scr_height);

		if(pch)
		{
			glConfig.vidWidth  = scr_width;
			glConfig.vidHeight = scr_height;
			glConfig.winWidth  = scr_width;
			glConfig.winHeight = scr_height;
		}

		initial_resolution_set = true;
	}
   
	var.key = "doom_invert_y_axis";
	var.value = NULL;

	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
	{
		if (strcmp(var.value, "disabled") == 0)
			invert_y_axis = 1;
		else
			invert_y_axis = -1;
	}
	
	var.key = "doom_mouse_sensitivity";
	var.value = NULL;

	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		mouse_sensitivity = (float)atof(var.value);

	/* Quality preset override */
	var.key = "doom_shadow_smoothing";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		cvarSystem->SetCVarBool("r_perFrameShadowVolumes", strcmp(var.value, "enabled") == 0);

	/* HRTF: 'auto' means hands off - the archived s_HRTF cvar (console,
	 * config) stays in charge; only an explicit enabled/disabled from the
	 * frontend menu overrides it. Live-toggling works: the mixer reads
	 * the cvar per block and the binaural path keeps its own history
	 * validity, so switching mid-game is clean. */
	var.key = "doom_hdr_specular";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		if (!strcmp(var.value, "disabled"))     hdr_specular_gain = 1.0f;
		else if (!strcmp(var.value, "strong"))  hdr_specular_gain = 3.0f;
		else                                    hdr_specular_gain = 2.0f;
	}

	var.key = "doom_hdr_bloom";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		if (!strcmp(var.value, "disabled"))      hdr_bloom_amount = 0.0f;
		else if (!strcmp(var.value, "subtle"))   hdr_bloom_amount = 0.55f;
		else if (!strcmp(var.value, "intense"))  hdr_bloom_amount = 1.7f;
		else                                     hdr_bloom_amount = 1.0f;
	}

	var.key = "doom_hdr_rolloff";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		hdr_rolloff_aces = strcmp(var.value, "aces") == 0;

	var.key = "doom_hrtf";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "auto") != 0)
		cvarSystem->SetCVarBool("s_HRTF", strcmp(var.value, "enabled") == 0);

	var.key = "doom_machine_spec";
	var.value = NULL;

	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
	{
		if (strcmp(var.value, "auto") != 0)
		{
			int preset = atoi(var.value); /* 0–3 */
			if (preset < 0) preset = 0;
			if (preset > 3) preset = 3;

			cvarSystem->SetCVarInteger("com_machineSpec", preset);
		}
	}
}

gp_layout_t *gp_layoutp = NULL;

static void extract_directory(char *buf, const char *path, size_t size)
{
   char *base = NULL;

   if (buf != path)
   {
      // strncpy has undefined behavior on overlapping buffers; this function
      // is also called with buf == path (in-place), so only copy when needed
      strncpy(buf, path, size - 1);
      buf[size - 1] = '\0';
   }

   base = strrchr(buf, '/');
   if (!base)
      base = strrchr(buf, '\\');

   if (base)
      *base = '\0';
   else
    {
       buf[0] = '.';
       buf[1] = '\0';
    }
}

static void context_reset(void)
{
   /* previous context's HDR objects are gone with it */
   hdr_fbo = hdr_tex = hdr_rbo = hdr_prog = 0;
   hdr_prog_bright = hdr_prog_blur = 0;
   memset(hdr_bloom_fbo, 0, sizeof hdr_bloom_fbo);
   memset(hdr_bloom_tex, 0, sizeof hdr_bloom_tex);
   hdr_w = hdr_h = 0;

   if (!first_boot)
      R_ReinitOpenGL();

   glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL);

   if (libretro_shared_context)
      return;

   if (!glsm_ctl(GLSM_CTL_STATE_SETUP, NULL))
      return;

}

static void context_destroy(void)
{
    if (!first_boot && glConfig.isInitialized) {
        renderModelManager->FreeModelVertexCaches();
        R_FreeDerivedData();
    }
}

char g_game_dir_name[256];	// real name of the directory holding the paks

bool Sys_GetPath(sysPath_t type, idStr &path) {
	path.Clear();

	switch(type) {
	case PATH_BASE:
	case PATH_CONFIG:
	case PATH_SAVE:
		path = BUILD_DATADIR;
		return true;
	case PATH_EXE:
		path = ".";
		return true;
	case PATH_GAMEDIR:
		/* The directory that actually holds the paks. The engine mounts
		 * BASE_GAMEDIR ("base") by default, which fails for installs whose
		 * data directory is named differently (e.g. "base-retail"). */
		if ( !g_game_dir_name[0] )
			return false;
		path = g_game_dir_name;
		return true;
	}

	return false;
}

/*
===============
Sys_Shutdown
===============
*/
void Sys_Shutdown( void ) {
	LibRetro_Shutdown();
}

/*
================
Sys_GetSystemRam
returns in megabytes
================
*/
int Sys_GetSystemRam( void ) {
#ifdef __linux__
	int mb;
	long page_size;
	long count = sysconf( _SC_PHYS_PAGES );
	if ( count == -1 )
		return 512;
	page_size = sysconf( _SC_PAGE_SIZE );
	if ( page_size == -1 )
		return 512;
	mb = (int)( (double)count * (double)page_size / ( 1024 * 1024 ) );
	// round to the nearest 16Mb
	return ( mb + 8 ) & ~15;
#elif defined(_WIN32)
        int physRam;
	MEMORYSTATUSEX statex;
	statex.dwLength = sizeof ( statex );
	GlobalMemoryStatusEx (&statex);
	physRam = statex.ullTotalPhys / ( 1024 * 1024 );
	// HACK: For some reason, ullTotalPhys is sometimes off by a meg or two, so we round up to the nearest 16 megs
	return ( physRam + 8 ) & ~15;
#else
	return 1024;
#endif
}

/*
=================
Sys_OpenURL
=================
*/
void idSysLocal::OpenURL( const char *url, bool quit ) {
	static bool	quit_spamguard = false;

	if ( quit_spamguard ) {
		common->DPrintf( "Sys_OpenURL: already in a doexit sequence, ignoring %s\n", url );
		return;
	}

	printf( "Sys_OpenURL: unimplemented\n" );

	if ( quit ) quit_spamguard = true;

	// execute this just for the quit side effect
	sys->StartProcess( "wewlad", quit );
}

/*
===============
main
===============
*/

/* Runtime game selection. The game module compiled into this core is the
 * d3xp (Resurrection of Evil) code, which is a superset of the base Doom 3
 * game module (every base entity class and script event exists in it - the
 * same unification the BFG edition shipped). One binary therefore serves
 * both titles; which content set the engine mounts is decided per load via
 * fs_game: unset for Doom 3, "d3xp" for RoE. Selected automatically from
 * the content's directory name, overridable with the doom_game core option. */
static int fake_argc = 0;
static char *fake_argv[8] = { nullptr };

static char game_base_arg[1024];

static void set_game_args(bool roe, const char *content_dir_name)
{
	fake_argc = 0;

	if (roe) {
		fake_argv[fake_argc++] = (char *)"+set";
		fake_argv[fake_argc++] = (char *)"fs_game";
		fake_argv[fake_argc++] = (char *)"d3xp";
	}

	/* The engine always mounts BASE_GAMEDIR ("base") under fs_basepath, so a
	 * install whose game data directory is not literally named "base" (e.g.
	 * "base-retail") would find nothing and die with "Couldn't load
	 * default.cfg". Pass the real directory name as fs_gamedirname, which the
	 * engine mounts in addition to BASE_GAMEDIR, so those layouts work. It is
	 * deliberately not fs_game/fs_game_base: those are the mod paths and the
	 * demo fallback overwrites them. */
	if (content_dir_name && content_dir_name[0]
	    && idStr::Icmp(content_dir_name, BASE_GAMEDIR) != 0
	    && idStr::Icmp(content_dir_name, "d3xp") != 0) {
		snprintf(game_base_arg, sizeof(game_base_arg), "%s", content_dir_name);
		fake_argv[fake_argc++] = (char *)"+set";
		fake_argv[fake_argc++] = (char *)"fs_gamedirname";
		fake_argv[fake_argc++] = game_base_arg;
	}

	fake_argv[fake_argc] = nullptr;
}

static void extract_basename(char *buf, const char *path, size_t size)
{
   char *ext        = NULL;
   const char *base = strrchr(path, '/');
   if (!base)
      base = strrchr(path, '\\');
   if (!base)
      base = path;

   if (*base == '\\' || *base == '/')
      base++;

   {
      size_t len = strlen(base);
      if (len > size - 1)
         len = size - 1;
      memcpy(buf, base, len);
      buf[len] = '\0';
   }

   ext = strrchr(buf, '.');
   if (ext)
      *ext = '\0';
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

static void audio_process(void)
{
}

static int ShiftChar(int c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    switch (c) {
        case '1': return '!'; case '2': return '@'; case '3': return '#';
        case '4': return '$'; case '5': return '%'; case '6': return '^';
        case '7': return '&'; case '8': return '*'; case '9': return '(';
        case '0': return ')'; case '-': return '_'; case '=': return '+';
        case '[': return '{'; case ']': return '}'; case '\\': return '|';
        case ';': return ':'; case '\'': return '"'; case ',': return '<';
        case '.': return '>'; case '/': return '?';
        default: return c;
    }
}

void Sys_SetKeys(){
	int port;
	uint32_t virt_buttons = 0x00;
	
	if (!poll_cb)
		return;

	poll_cb();

	if (!input_cb)
		return;

	for (port = 0; port < MAX_PADS; port++)
	{
		if (!input_cb)
			break;

		switch (doom_devices[port])
		{
		case RETRO_DEVICE_JOYPAD:
		case RETRO_DEVICE_JOYPAD_ALT:
		case RETRO_DEVICE_MODERN:
		{
			unsigned i;
			uint32_t ret   = 0;
			if (libretro_supports_bitmasks)
				ret = (uint16_t)input_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
			else
			{
				for (i=RETRO_DEVICE_ID_JOYPAD_B; i <= RETRO_DEVICE_ID_JOYPAD_R3; ++i)
				{
					if (input_cb(port, RETRO_DEVICE_JOYPAD, 0, i))
						ret |= (1 << i);
				}
			}

			// D-Pad
			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_UP)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_UP)))
				Key_Event(K_JOY_DPAD_UP, 1);
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_UP)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_UP)))
				Key_Event(K_JOY_DPAD_UP, 0);

			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)))
				Key_Event(K_JOY_DPAD_DOWN, 1);
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)))
				Key_Event(K_JOY_DPAD_DOWN, 0);

			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT)))
				Key_Event(K_JOY_DPAD_LEFT, 1);
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT)))
				Key_Event(K_JOY_DPAD_LEFT, 0);

			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)))
				Key_Event(K_JOY_DPAD_RIGHT, 1);
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)))
				Key_Event(K_JOY_DPAD_RIGHT, 0);

			// Face buttons - A and B need to be reversed for menu actions
			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_B)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_B)))
				Key_Event(K_JOY_BTN_SOUTH, 1);
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_B)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_B)))
				Key_Event(K_JOY_BTN_SOUTH, 0);

			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_A)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_A)))
				Key_Event(K_JOY_BTN_EAST, 1);
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_A)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_A)))
				Key_Event(K_JOY_BTN_EAST, 0);

			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_X)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_X)))
				Key_Event(K_JOY_BTN_WEST, 1);
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_X)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_X)))
				Key_Event(K_JOY_BTN_WEST, 0);

			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_Y)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_Y)))
				Key_Event(K_JOY_BTN_NORTH, 1);
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_Y)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_Y)))
				Key_Event(K_JOY_BTN_NORTH, 0);

			// Shoulders
			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_L)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_L)))
				Key_Event(K_JOY_BTN_LSHOULDER, 1);
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_L)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_L)))
				Key_Event(K_JOY_BTN_LSHOULDER, 0);

			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_R)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_R)))
				Key_Event(K_JOY_BTN_RSHOULDER, 1);
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_R)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_R)))
				Key_Event(K_JOY_BTN_RSHOULDER, 0);

			// Triggers
			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_L2)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_L2)))
			{
				if (doom_devices[port] == RETRO_DEVICE_MODERN)
					Key_Event(K_SPACE, 1);
				else
					Key_Event(K_JOY_TRIGGER1, 1);
			}
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_L2)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_L2)))
			{
				if (doom_devices[port] == RETRO_DEVICE_MODERN)
					Key_Event(K_SPACE, 0);
				else
					Key_Event(K_JOY_TRIGGER1, 0);
			}

			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_R2)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_R2)))
			{
				if (doom_devices[port] == RETRO_DEVICE_MODERN)
					Key_Event(K_MOUSE1, 1);
				else
					Key_Event(K_JOY_TRIGGER2, 1);
			}
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_R2)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_R2)))
			{
				if (doom_devices[port] == RETRO_DEVICE_MODERN)
					Key_Event(K_MOUSE1, 0);
				else
					Key_Event(K_JOY_TRIGGER2, 0); // was 1: fire button got stuck on release
			}

			// Stick buttons
			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_L3)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_L3)))
				Key_Event(K_JOY_BTN_LSTICK, 1);
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_L3)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_L3)))
				Key_Event(K_JOY_BTN_LSTICK, 0);

			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_R3)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_R3)))
				Key_Event(K_JOY_BTN_RSTICK, 1);
			else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_R3)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_R3)))
				Key_Event(K_JOY_BTN_RSTICK, 0);

			// Start/Select still mapped to ESC/TAB for menus/scores
			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_START)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_START))) {
				Key_Event(K_ESCAPE, 1);
				//Key_Event(K_JOY_BTN_START, 1);
			} else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_START)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_START))) {
				Key_Event(K_ESCAPE, 0);
				//Key_Event(K_JOY_BTN_START, 0);
			}

			if ((ret & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT)) && !(old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT))) {
				Key_Event(K_TAB, 1);
				//Key_Event(K_JOY_BTN_BACK, 1);
			} else if (!(ret & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT)) && (old_ret & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT))) {
				Key_Event(K_TAB, 0);
				//Key_Event(K_JOY_BTN_BACK, 0);
			}

			int lsx, lsy;
			lsx = input_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
               RETRO_DEVICE_ID_ANALOG_X);
			lsy = input_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
               RETRO_DEVICE_ID_ANALOG_Y);

			if (lsx > analog_deadzone || lsx < -analog_deadzone) {
				if (lsx > analog_deadzone)
					virt_buttons += LANALOG_RIGHT;
				if (lsx < -analog_deadzone)
					virt_buttons += LANALOG_LEFT;
			}
	  
			if (lsy > analog_deadzone || lsy < -analog_deadzone) {
				if (lsy > analog_deadzone)
					virt_buttons += LANALOG_UP;
				if (lsy < -analog_deadzone)
					virt_buttons += LANALOG_DOWN;
			}
			
			if (virt_buttons != oldanalogs){
				if((virt_buttons & LANALOG_LEFT) != (oldanalogs & LANALOG_LEFT))
					Key_Event(K_AUX7, (virt_buttons & LANALOG_LEFT) == LANALOG_LEFT);
				if((virt_buttons & LANALOG_RIGHT) != (oldanalogs & LANALOG_RIGHT))
					Key_Event(K_AUX8, (virt_buttons & LANALOG_RIGHT) == LANALOG_RIGHT);
				if((virt_buttons & LANALOG_UP) != (oldanalogs & LANALOG_UP))
					Key_Event(K_AUX10, (virt_buttons & LANALOG_UP) == LANALOG_UP);
				if((virt_buttons & LANALOG_DOWN) != (oldanalogs & LANALOG_DOWN))
					Key_Event(K_AUX9, (virt_buttons & LANALOG_DOWN) == LANALOG_DOWN);
			}
			
			oldanalogs = virt_buttons;
			old_ret = ret;
		}
		break;
		case RETRO_DEVICE_NONE:
			break;
		}

		// Always poll keyboard regardless of device - needed for console access
		static const struct { unsigned retrok; int doom_key; } kb_map[] = {
			// existing game keys
			{ RETROK_w,         'w'         },
			{ RETROK_s,         's'         },
			{ RETROK_a,         'a'         },
			{ RETROK_d,         'd'         },
			{ RETROK_e,         'e'         },
			{ RETROK_r,         'r'         },
			{ RETROK_f,         'f'         },
			{ RETROK_q,         'q'         },
			{ RETROK_c,         'c'         },
			// other keys
			{ RETROK_t,         't'         },
			{ RETROK_y,         'y'         },
			{ RETROK_u,         'u'         },
			{ RETROK_i,         'i'         },
			{ RETROK_o,         'o'         },
			{ RETROK_p,         'p'         },
			{ RETROK_g,         'g'         },
			{ RETROK_h,         'h'         },
			{ RETROK_j,         'j'         },
			{ RETROK_k,         'k'         },
			{ RETROK_l,         'l'         },
			{ RETROK_z,         'z'         },
			{ RETROK_x,         'x'         },
			{ RETROK_v,         'v'         },
			{ RETROK_b,         'b'         },
			{ RETROK_n,         'n'         },
			{ RETROK_m,         'm'         },
			// numbers
			{ RETROK_1,         '1'         },
			{ RETROK_2,         '2'         },
			{ RETROK_3,         '3'         },
			{ RETROK_4,         '4'         },
			{ RETROK_5,         '5'         },
			{ RETROK_6,         '6'         },
			{ RETROK_7,         '7'         },
			{ RETROK_8,         '8'         },
			{ RETROK_9,         '9'         },
			{ RETROK_0,         '0'         },
			// symbols
			{ RETROK_SPACE,     K_SPACE     },
			{ RETROK_MINUS,     '-'         },
			{ RETROK_EQUALS,    '='         },
			{ RETROK_LEFTBRACKET,  '['      },
			{ RETROK_RIGHTBRACKET, ']'      },
			{ RETROK_BACKSLASH, '\\'        },
			{ RETROK_SEMICOLON, ';'         },
			{ RETROK_QUOTE,     '\''        },
			{ RETROK_COMMA,     ','         },
			{ RETROK_PERIOD,    '.'         },
			{ RETROK_SLASH,     '/'         },
			// control keys
			{ RETROK_LSHIFT,    K_SHIFT     },
			{ RETROK_RSHIFT,    K_SHIFT     },
			{ RETROK_LCTRL,     K_CTRL      },
			{ RETROK_RCTRL,     K_CTRL      },
			{ RETROK_LALT,      K_ALT       },
			{ RETROK_RALT,      K_ALT       },
			{ RETROK_ESCAPE,    K_ESCAPE    },
			{ RETROK_RETURN,    K_ENTER     },
			{ RETROK_BACKSPACE, K_BACKSPACE },
			{ RETROK_TAB,       K_TAB       },
			{ RETROK_UP,        K_UPARROW   },
			{ RETROK_DOWN,      K_DOWNARROW },
			{ RETROK_LEFT,      K_LEFTARROW },
			{ RETROK_RIGHT,     K_RIGHTARROW},
			{ RETROK_F1,        K_F1        },
			{ RETROK_F2,        K_F2        },
			{ RETROK_F3,        K_F3        },
			{ RETROK_F4,        K_F4        },
			{ RETROK_F5,        K_F5        },
			{ RETROK_BACKQUOTE, '`'         },
		};
		static const int kb_map_size = sizeof(kb_map) / sizeof(kb_map[0]);
		static bool kb_prev[sizeof(kb_map) / sizeof(kb_map[0])] = {};

		for (int i = 0; i < kb_map_size; i++)
		{
			bool now = !!input_cb(port, RETRO_DEVICE_KEYBOARD, 0, kb_map[i].retrok);
			if (now != kb_prev[i])
			{
				Key_Event(kb_map[i].doom_key, now ? 1 : 0);

				static bool kb_shift = false;
				if (kb_map[i].retrok == RETROK_LSHIFT || kb_map[i].retrok == RETROK_RSHIFT)
					kb_shift = now;

				if (now) {
					int c = kb_map[i].doom_key;
					if (c == K_BACKSPACE) {
						Char_Event(8);
					} else if (c != '`' && c >= 32 && c < 127) {
						Char_Event(kb_shift ? ShiftChar(c) : c);
					}
				}
				kb_prev[i] = now;
			}
		}

		// Mouse buttons (edge-detected state)
		static const struct { unsigned retro_id; int doom_key; } mouse_buttons[] = {
			{ RETRO_DEVICE_ID_MOUSE_LEFT,      K_MOUSE1 },
			{ RETRO_DEVICE_ID_MOUSE_RIGHT,     K_MOUSE2 },
			{ RETRO_DEVICE_ID_MOUSE_MIDDLE,    K_MOUSE3 },
			{ RETRO_DEVICE_ID_MOUSE_BUTTON_4,  K_MOUSE4 },
			{ RETRO_DEVICE_ID_MOUSE_BUTTON_5,  K_MOUSE5 },
		};
		for (int i = 0; i < 5; i++)
		{
			bool now = !!input_cb(port, RETRO_DEVICE_MOUSE, 0, mouse_buttons[i].retro_id);
			if (now != kb_mouse_btn[i])
			{
				Key_Event(mouse_buttons[i].doom_key, now ? 1 : 0);
				kb_mouse_btn[i] = now;
			}
		}
		// Mouse wheel: the frontend reports a one-frame pulse per detent.
		// The old code edge-detected it like a held button mapped to
		// K_MOUSE4/5, which (a) broke the game's default MWHEELUP/DOWN
		// weapon-cycle binds and (b) delivered the release a frame late.
		// Send a proper press+release pulse on the real wheel keys in the
		// same frame the detent arrives.
		if (input_cb(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP)) {
			Key_Event(K_MWHEELUP, 1);
			Key_Event(K_MWHEELUP, 0);
		}
		if (input_cb(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN)) {
			Key_Event(K_MWHEELDOWN, 1);
			Key_Event(K_MWHEELDOWN, 0);
		}
	}
}

void Sys_SetMouse() {
    int rsx, rsy;
    int slowdown = 1024 * (framerate / 60.0f);
    int effective_invert = (sessLocal.guiActive != NULL) ? 1 : invert_y_axis;

    // Always read physical mouse delta regardless of device mode.
    // Scale in float and carry the fractional remainder across frames:
    // the old (int) truncation silently dropped sub-unit deltas, so slow
    // precise aiming lost movement entirely at sensitivity < 1 and gained
    // quantization notchiness at any non-integer sensitivity.
    {
        static float mrem_x = 0.0f, mrem_y = 0.0f;
        float fdx = input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) * mouse_sensitivity + mrem_x;
        float fdy = effective_invert * input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y) * mouse_sensitivity + mrem_y;
        int dx = (int)fdx;
        int dy = (int)fdy;
        mrem_x = fdx - dx;
        mrem_y = fdy - dy;
        if (dx || dy)
            Mouse_Event(dx, dy);
    }

    if (doom_devices[0] == RETRO_DEVICE_KEYBOARD)
        return;

    // Right stick look for gamepad modes
    rsx = input_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
        RETRO_DEVICE_ID_ANALOG_X);
    rsy = effective_invert * input_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
        RETRO_DEVICE_ID_ANALOG_Y);

    if (rsx > analog_deadzone || rsx < -analog_deadzone) {
        if (rsx > analog_deadzone) rsx = rsx - analog_deadzone;
        if (rsx < -analog_deadzone) rsx = rsx + analog_deadzone;
    } else rsx = 0;
    if (rsy > analog_deadzone || rsy < -analog_deadzone) {
        if (rsy > analog_deadzone) rsy = rsy - analog_deadzone;
        if (rsy < -analog_deadzone) rsy = rsy + analog_deadzone;
    } else rsy = 0;

    // float scaling with fractional carry: the old integer division by
    // 'slowdown' (1024 * framerate/60) truncated every deflection below one
    // full step to zero, adding a huge artificial dead band on top of the
    // configured deadzone and making slow analog aiming skip
    {
        static float arem_x = 0.0f, arem_y = 0.0f;
        float fx = rsx / (float)slowdown + arem_x;
        float fy = rsy / (float)slowdown + arem_y;
        int ix = (int)fx;
        int iy = (int)fy;
        arem_x = fx - ix;
        arem_y = fy - iy;
        if (ix || iy)
            Mouse_Event(ix, iy);
    }
}

#define MAX_CHANNELS 2


/* Float output negotiation (RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT):
 * decided once per loaded game; never mix formats afterwards. */
static struct retro_audio_sample_float_callback audio_float_cb;
static bool audio_output_float = false;

/* Deterministic per-frame sample budget.
 * The exact rational SAMPLE_RATE/framerate frames per retro_run is distributed
 * with an integer remainder accumulator, then rounded down to a multiple
 * of 8 with a sample carry (11kHz sources decode with a >>2 offset shift,
 * stereo doubles it: 8-sample alignment keeps decode offsets exact - the
 * same constraint the old engine satisfied by rounding its ms-derived
 * sample time to multiples of 8). Long-run average is exactly the
 * resolved output rate and every quantity is an integer: the emitted
 * count sequence is a pure function of the frame index (and, since the
 * SND3 footer, of the restored accumulator phase). */
static int audio_rem_acc    = 0;  /* rational remainder, in units of 1/framerate frame */
static int audio_frame_carry = 0; /* 0..7 frames deferred by the multiple-of-8 rounding */

static void audio_upload_frame(void)
{
	if (first_boot)
		return;

	unsigned fps = framerate > 0 ? framerate : 60;

	/* exact rational distribution of SAMPLE_RATE/fps */
	audio_rem_acc += SAMPLE_RATE;
	int want = audio_rem_acc / (int)fps;
	audio_rem_acc -= want * (int)fps;

	/* round to multiple of 8, carrying the remainder to the next frame */
	want += audio_frame_carry;
	int frames = want & ~7;
	audio_frame_carry = want - frames;

	if (frames <= 0)
		return;
	if (frames > MAX_FRAME_SAMPLES)
		frames = MAX_FRAME_SAMPLES;

	if (audio_output_float) {
		/* float pipeline: MixFrameFloat writes [-1,1] normalized stereo -
		 * the mix accumulation buffer IS the output buffer */
		static float outF[MAX_FRAME_SAMPLES * MAX_CHANNELS];
		soundSystem->MixFrameFloat(outF, frames);
		audio_float_cb.batch(outF, frames);
	} else {
		/* all-s16 pipeline: integer mix (s16 samples, Q15 gains, int32
		 * accumulation, saturating narrow) - bit-deterministic */
		static int16_t outS[MAX_FRAME_SAMPLES * MAX_CHANNELS];
		soundSystem->MixFrameS16(outS, frames);
		audio_batch_cb(outS, frames);
	}
}

/* Upload one frame's worth of silence to the frontend without mixing the
 * sound world (so the deterministic sound clock is NOT advanced) and
 * without disturbing the in-game audio pacing accumulators. Used to keep
 * the audio buffer fed during a synchronous map load so the frontend does
 * not underrun. Independent accumulators mean the post-load audio stream
 * is bit-identical to a build without this path. */
static int load_rem_acc = 0;
static int load_frame_carry = 0;
static void audio_upload_silence(void)
{
	if (first_boot)
		return;

	unsigned fps = framerate > 0 ? framerate : 60;

	load_rem_acc += SAMPLE_RATE;
	int want = load_rem_acc / (int)fps;
	load_rem_acc -= want * (int)fps;

	want += load_frame_carry;
	int frames = want & ~7;
	load_frame_carry = want - frames;

	if (frames <= 0)
		return;
	if (frames > MAX_FRAME_SAMPLES)
		frames = MAX_FRAME_SAMPLES;

	if (audio_output_float) {
		static float zeroF[MAX_FRAME_SAMPLES * MAX_CHANNELS];
		memset(zeroF, 0, frames * MAX_CHANNELS * sizeof(float));
		audio_float_cb.batch(zeroF, frames);
	} else {
		static int16_t zeroS[MAX_FRAME_SAMPLES * MAX_CHANNELS];
		memset(zeroS, 0, frames * MAX_CHANNELS * sizeof(int16_t));
		audio_batch_cb(zeroS, frames);
	}
}

static bool context_framebuffer_lock(void *data)
{
    return false;
}

static bool initialize_opengl(void)
{
   glsm_ctx_params_t params = {0};

   params.context_type 	   = get_hw_context_type();
   params.context_reset    = context_reset;
   params.context_destroy  = context_destroy;
   params.environ_cb       = environ_cb;
   params.stencil          = true;
   params.framebuffer_lock = context_framebuffer_lock;

   if (!glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params))
   {
      log_cb(RETRO_LOG_ERROR, "Could not setup glsm.\n");
      return false;
   }

	if (environ_cb(RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT, NULL))
      libretro_shared_context = true;
   else
      libretro_shared_context = false;

   return true;
}

void destroy_opengl(void)
{
   if (!glsm_ctl(GLSM_CTL_STATE_CONTEXT_DESTROY, NULL))
   {
      log_cb(RETRO_LOG_ERROR, "Could not destroy glsm context.\n");
   }

   libretro_shared_context = false;
}

bool retro_load_game(const struct retro_game_info *info)
{
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	{
		/* Color Format option, resolved once at load (the pixel format
		   cannot change mid-session): 30-bit HDR asks for the HDR10
		   surface and enables the PQ conversion pass; refusal by an
		   older frontend falls back to the stock 24-bit path. */
		struct retro_variable cfv = { "doom_color_format", NULL };
		if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &cfv) && cfv.value
				&& strcmp(cfv.value, "30bit-hdr") == 0) {
			enum retro_pixel_format hf = RETRO_PIXEL_FORMAT_HDR10_2101010;
			if (environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &hf)) {
				hdr_output_active = true;
				if (log_cb)
					log_cb(RETRO_LOG_INFO, "[boom3] 30-bit HDR10 output enabled\n");
			} else if (log_cb) {
				log_cb(RETRO_LOG_WARN, "[boom3] frontend refused HDR10_2101010; using 24-bit\n");
			}
		}
	}
	if (hdr_output_active)
		fmt = RETRO_PIXEL_FORMAT_HDR10_2101010;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
	{
		if (log_cb)
			log_cb(RETRO_LOG_INFO, "XRGB8888 is not supported.\n");
		return false;
	}

	{
		/*
		   MUST_INITIALIZE: retro_serialize_size() honestly returns 0
		   until a map is loaded (there is no session to serialize), and
		   without this quirk the frontend reads that early 0 as "this
		   core cannot serialize at all" and disables rewind/savestate
		   features permanently at init. The quirk says: support exists,
		   ask again once content is actually running.
		*/
		uint64_t quirks = RETRO_SERIALIZATION_QUIRK_MUST_INITIALIZE
		                | RETRO_SERIALIZATION_QUIRK_CORE_VARIABLE_SIZE
		                | RETRO_SERIALIZATION_QUIRK_ENDIAN_DEPENDENT
		                | RETRO_SERIALIZATION_QUIRK_PLATFORM_DEPENDENT;
		environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &quirks);
	}

	hw_render.context_type    = get_hw_context_type();
	hw_render.context_reset   = context_reset;
	hw_render.context_destroy = context_destroy;
	hw_render.bottom_left_origin = true;
	hw_render.depth = true;
	hw_render.stencil = true;

#if defined(HAVE_OPENGLES_3_2)
   hw_render.version_major = 3;
   hw_render.version_minor = 2;
#elif defined(HAVE_OPENGLES_3_1)
   hw_render.version_major = 3;
   hw_render.version_minor = 1;
#elif defined(HAVE_OPENGLES3)
   hw_render.version_major = 3;
   hw_render.version_minor = 0;
#endif

	if (!initialize_opengl())
	{
		if (log_cb)
			log_cb(RETRO_LOG_ERROR, "boom3: libretro frontend doesn't have OpenGL support.\n");
		return false;
	}
	
#if defined(_WIN32)
	char slash = '\\';
#else
	char slash = '/';
#endif
	bool use_external_savedir = false;
	const char *base_save_dir = NULL;

	if (!info)
		return false;

	update_variables(true);

	// negotiate float audio output (RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT):
	// decided once per loaded game. On success the whole pipeline runs the
	// all-float mixer with [-1,1] output; otherwise the all-s16 integer mixer.
	audio_output_float = false;
	memset(&audio_float_cb, 0, sizeof(audio_float_cb));
	if (environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT, &audio_float_cb)
	    && audio_float_cb.batch) {
		audio_output_float = true;
	}
	soundSystem->SetOutputFloat(audio_output_float);
	if (log_cb)
		log_cb(RETRO_LOG_INFO, "[boom3] audio output format: %s\n",
		       audio_output_float ? "float32 [-1,1] (negotiated)" : "int16 (deterministic integer mixer)");
	// reset the per-frame sample budget for the new session
	audio_rem_acc = 0;
	audio_frame_carry = 0;
	
	extract_directory(g_rom_dir, info->path, sizeof(g_rom_dir));
	
	snprintf(g_pak_path, sizeof(g_pak_path), "%s", info->path);

	/* game selection: auto-detect RoE from the content's directory name
	 * (retail installs keep RoE data in <install>/d3xp/ beside base/),
	 * with a core option override for unconventional layouts */
	{
		char content_dir_name[1024];
		bool roe = false;
		struct retro_variable gv = { "doom_game", NULL };

		extract_basename(content_dir_name, g_rom_dir, sizeof(content_dir_name));
		if (idStr::Icmp(content_dir_name, "d3xp") == 0)
			roe = true;

		if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &gv) && gv.value)
		{
			if (strcmp(gv.value, "doom3") == 0)
				roe = false;
			else if (strcmp(gv.value, "d3xp") == 0)
				roe = true;
			/* "auto": keep the detection result */
		}
		set_game_args(roe, content_dir_name);
		if (log_cb)
			log_cb(RETRO_LOG_INFO, "[boom3] game: %s (content dir '%s')\n",
			       roe ? "Doom 3: Resurrection of Evil (fs_game d3xp)" : "Doom 3",
			       content_dir_name);
	}
	
	if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &base_save_dir) && base_save_dir)
	{
		if (strlen(base_save_dir) > 0)
		{
			// Get game 'name' (i.e. subdirectory)
			// half of g_save_dir each, so "<base><slash><name>" always fits
			// and snprintf() never has to truncate
			char game_name[sizeof(g_save_dir) / 2 - 1];
			extract_basename(game_name, g_rom_dir, sizeof(game_name));

			// > Build final save path
			snprintf(g_save_dir, sizeof(g_save_dir), "%.*s%c%s",
					(int)(sizeof(g_save_dir) / 2 - 1), base_save_dir, slash, game_name);
			use_external_savedir = true;
			
			// > Create save directory, if required
			if (!path_is_directory(g_save_dir))
			{
				use_external_savedir = path_mkdir(g_save_dir);
			}
		}
	}
	
	// > Error check
	if (!use_external_savedir)
	{
		// > Use ROM directory fallback...
		snprintf(g_save_dir, sizeof(g_save_dir), "%s", g_rom_dir);
	}
	else
	{
		// > Final check: is the save directory the same as the 'rom' directory?
		//   (i.e. ensure logical behaviour if user has set a bizarre save path...)
		use_external_savedir = (strcmp(g_save_dir, g_rom_dir) != 0);
	}
	

	extract_directory(g_rom_dir, g_rom_dir, sizeof(g_rom_dir));
	BUILD_DATADIR = g_rom_dir;

	return true;
}

/*
===================
GLimp_ExtensionPointer
===================
*/
GLExtension_t GLimp_ExtensionPointer(const char *name) {
	return (GLExtension_t)hw_render.get_proc_address(name);
}

static const gp_layout_t *pending_layout = &classic;

static void gp_layout_set_bind(const gp_layout_t *layout)
{
    // clear all joy keys
    for (int k = K_FIRST_JOY; k <= K_LAST_JOY; k++) {
        idKeyInput::SetBinding(k, "");
    }
    // set each binding directly
    for (unsigned i = 0; layout->bind[i].key; ++i) {
        int keynum = idKeyInput::StringToKeyNum(layout->bind[i].key);
        if (keynum != -1)
            idKeyInput::SetBinding(keynum, layout->bind[i].com);
    }
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   if (port == 0)
   {
      switch (device)
      {
         case RETRO_DEVICE_JOYPAD:
            doom_devices[port] = RETRO_DEVICE_JOYPAD;
            environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, classic.desc);
            pending_layout = &classic;
            break;
         case RETRO_DEVICE_JOYPAD_ALT:
            doom_devices[port] = RETRO_DEVICE_JOYPAD;
            environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, classic_alt.desc);
            pending_layout = &classic_alt;
            break;
         case RETRO_DEVICE_MODERN:
            doom_devices[port] = RETRO_DEVICE_MODERN;
            environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, modern.desc);
            pending_layout = &modern;
            break;
         case RETRO_DEVICE_NONE:
         default:
            doom_devices[port] = RETRO_DEVICE_NONE;
            if (log_cb)
               log_cb(RETRO_LOG_ERROR, "[libretro]: Invalid device.\n");
      }

      if (!first_boot)
            gp_layout_set_bind(pending_layout);
   }
}



void retro_run(void)
{
   /* Advance the deterministic clock by exactly one frame. All engine
    * timing (game tic schedule, com_frameTime, the render-side tic
    * fraction, sound sample time) derives from the frame COUNT - never
    * from a wall clock - so core behavior is a pure function of the
    * retro_run() call count and polled input, independent of host speed,
    * fast-forward or frame stepping. */
   Core_AdvanceFrame();

   if (!libretro_shared_context)
      glsm_ctl(GLSM_CTL_STATE_BIND, NULL);

	if (first_boot) {
		network_init();
		common->Init( fake_argc, fake_argv );
		first_boot = false;
		update_variables(false);
		gp_layout_set_bind(pending_layout);
	}
	
	bool updated = false;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
		update_variables(false);

	hdr_bind_scene();

	common->Frame();

   if (!libretro_shared_context)
      glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL);
	
	audio_process();
	audio_upload_frame();
}

/*
===================
GLimp_SwapBuffers
===================
*/
/*
===========================================================================
30-bit / HDR10 output (doom_color_format).

The engine renders exactly as it always has - gamma-encoded, SDR,
Rec.709 content - but into a private RGB10_A2 scene target instead of
the frontend framebuffer. At presentation a fullscreen pass converts
that scene into what an HDR10 swapchain actually expects, per the
contract in libretro.h:

 - EOTF: the scene is sRGB-encoded and an HDR10 surface is PQ (SMPTE
   ST 2084) absolute luminance; presenting sRGB numbers on a PQ
   swapchain is the classic washed-out-and-dim mismatch. The pass
   linearizes with the exact inverse-sRGB EOTF, anchors 1.0 at the
   frontend's paper-white nits (RETRO_ENVIRONMENT_GET_HDR_PAPER_WHITE_
   NITS, default 200), and PQ-encodes over the 0..10000 nit range.
 - Gamut: Rec.709 -> Rec.2020 rotation on linear values, honouring the
   frontend's Colour Boost setting (GET_HDR_EXPAND_GAMUT): Accurate is
   the true colorimetric matrix, Super skips rotation so the display's
   Rec.2020 interpretation provides the boost, Wide reinterprets the
   709 coordinates as DCI-P3, Expanded sits halfway between Accurate
   and Wide. The middle modes approximate the frontend's SDR
   treatment; flagged for verification against RetroArch's own tables.
 - Highlight roll-off (doom_hdr_rolloff): the headroom between paper
   white and the display peak (GET_HDR_MAX_NITS, default 1000) is
   where highlights may expand. Reinhard (Soft-Knee) is mid-tone
   exact - identity up to the knee, then the same slope-continuous
   rational approach the audio saturator uses, toward the peak
   asymptote. ACES (Filmic) runs the Narkowicz ACES fit normalized to
   the peak: filmic contrast through the mids, gentler shoulder. With
   peak <= paper white the contract says treat as zero headroom: both
   curves collapse to a clamp.
 - Quantization: the scene target is 10-bit so the conversion source
   never drops below the output depth, and the PQ result is dithered
   with a +-0.5 LSB (of 10 bits) spatial hash before the hardware
   quantizes - the sRGB->PQ curve remap otherwise bands visibly in
   dark gradients, which is the classic 10-bit HDR quantization
   artifact.
 - Output mode (GET_HDR_OUTPUT_MODE): HDR10 and scRGB swapchains both
   receive the same PQ Rec.2020 frame here. The colorimetric argument:
   every gamut intent above is expressed as actual Rec.2020
   coordinates, and a conversion to scRGB that respects colorimetry
   preserves them. VERIFIED on hardware (RetroArch 1.22.2 Win32 gl,
   FP16 scRGB swapchain, RTX 5090): 24-bit and 30-bit side by side
   show no hue rotation or saturation shift - the shared encoding is
   correct and no per-mode compensation is needed. Mode 0 (HDR off
   while the HDR10 format is set) logs once and keeps encoding - the
   image stays viewable, just tone-shifted - and the right fix is
   switching the core option back to 24-bit.

Shaders are legacy GLSL (attribute/varying/texture2D) with a small
GLES precision prefix: the engine requests compatibility GL / GLES2
contexts, which is what both the gl2 and glcore frontend drivers
serve it, so one dialect covers every target.

24-bit mode touches none of this: no scene FBO, no pass, the exact
pre-existing XRGB8888 path byte for byte.
===========================================================================
*/


#ifndef GL_RGB10_A2
#define GL_RGB10_A2 0x8059
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif

static const char *hdr_vs_src =
	"attribute vec2 aPos;\n"
	"varying vec2 vUV;\n"
	"void main() {\n"
	"  vUV = aPos * 0.5 + 0.5;\n"
	"  gl_Position = vec4(aPos, 0.0, 1.0);\n"
	"}\n";

static const char *hdr_fs_src =
	"#ifdef GL_ES\nprecision highp float;\n#endif\n"
	"varying vec2 vUV;\n"
	"uniform sampler2D uScene;\n"
	"uniform sampler2D uBloomT;\n"
	"uniform sampler2D uBloomW;\n"
	"uniform float uBloomAmt;\n"
	"uniform mat3 uGamut;\n"
	/* parms: x = paperWhite/10000, y = headroom H (>=1), z = aces flag,
	   w = knee (Reinhard) */
	"uniform vec4 uParms;\n"
	"vec3 srgbToLinear(vec3 c) {\n"
	"  vec3 lo = c / 12.92;\n"
	"  vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));\n"
	"  return mix(lo, hi, step(0.04045, c));\n"
	"}\n"
	"float rolloff(float v) {\n"
	"  float H = uParms.y;\n"
	"  if (H <= 1.0001) return min(v, 1.0);\n"
	"  if (uParms.z > 0.5) {\n"
	/*   Narkowicz ACES fit, normalized so 1.0 lands on the peak */
	"    float n = v * (2.51 * v + 0.03) / (v * (2.43 * v + 0.59) + 0.14);\n"
	"    return H * n / 0.8037;\n"
	"  }\n"
	"  float K = uParms.w;\n"
	"  if (v <= K) return v;\n"
	"  float e = v - K, A = H - K;\n"
	"  return K + e * A / (e + A);\n"
	"}\n"
	"float pq(float y) {\n"
	"  float p = pow(max(y, 0.0), 0.1593017578125);\n"
	"  return pow((0.8359375 + 18.8515625 * p) / (1.0 + 18.6875 * p), 78.84375);\n"
	"}\n"
	"void main() {\n"
	"  vec3 lin = srgbToLinear(texture2D(uScene, vUV).rgb);\n"
	/* bloom joins in LINEAR, BEFORE the roll-off: bloomed highlights
	   ride the same curve into the paper-white..peak headroom, which
	   is the part SDR output cannot express at all */
	"  lin += uBloomAmt * (0.22 * texture2D(uBloomT, vUV).rgb\n"
	"                    + 0.14 * texture2D(uBloomW, vUV).rgb);\n"
	"  lin = vec3(rolloff(lin.r), rolloff(lin.g), rolloff(lin.b));\n"
	"  lin = uGamut * lin;\n"
	"  vec3 y = clamp(lin * uParms.x, 0.0, 1.0);\n"
	"  vec3 e = vec3(pq(y.r), pq(y.g), pq(y.b));\n"
	/* +-0.5 LSB (10-bit) spatial hash dither against PQ-remap banding */
	"  float d = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;\n"
	"  gl_FragColor = vec4(e + d / 1023.0, 1.0);\n"
	"}\n";

/*
   Bright pass: linearize, threshold with a normalized soft knee, and
   Reinhard-compress the extraction ( b / (1 + luma) ) - the firefly
   fix: a sub-pixel specular spike is energy-limited BEFORE it is
   blurred across the neighborhood, so bloom cannot flicker with it
   frame to frame. The scene is SDR-clamped at 1.0 by construction,
   so the threshold sits below white (0.62 linear): saturated lamps,
   plasma, and the engine's own additive glare quads extract; lit
   walls do not.
*/
static const char *hdr_bright_fs_src =
	"#ifdef GL_ES\nprecision highp float;\n#endif\n"
	"varying vec2 vUV;\n"
	"uniform sampler2D uScene;\n"
	"uniform float uThresh;\n"
	"vec3 srgbToLinear(vec3 c) {\n"
	"  vec3 lo = c / 12.92;\n"
	"  vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));\n"
	"  return mix(lo, hi, step(0.04045, c));\n"
	"}\n"
	"void main() {\n"
	"  vec3 lin = srgbToLinear(texture2D(uScene, vUV).rgb);\n"
	"  vec3 b = max(lin - vec3(uThresh), 0.0) / (1.0 - uThresh);\n"
	"  float l = dot(b, vec3(0.2126, 0.7152, 0.0722));\n"
	"  gl_FragColor = vec4(b / (1.0 + l), 1.0);\n"
	"}\n";

/*
   Separable Gaussian, 5 bilinear fetches for a 9-tap kernel
   (offsets/weights are the classic linear-sampling optimization,
   renormalized to sum exactly 1 so a (0,0) direction acts as a pure
   downsample copy - which is how the wide band is seeded from the
   tight band without a dedicated copy program).
*/
static const char *hdr_blur_fs_src =
	"#ifdef GL_ES\nprecision highp float;\n#endif\n"
	"varying vec2 vUV;\n"
	"uniform sampler2D uScene;\n"
	"uniform vec2 uDir;\n"   /* texel-size-scaled direction, or 0,0 for copy */
	"void main() {\n"
	"  vec3 c = texture2D(uScene, vUV).rgb * 0.23727;\n"
	"  c += texture2D(uScene, vUV + uDir * 1.38461).rgb * 0.33053;\n"
	"  c += texture2D(uScene, vUV - uDir * 1.38461).rgb * 0.33053;\n"
	"  c += texture2D(uScene, vUV + uDir * 3.23077).rgb * 0.05084;\n"
	"  c += texture2D(uScene, vUV - uDir * 3.23077).rgb * 0.05084;\n"
	"  gl_FragColor = vec4(c, 1.0);\n"
	"}\n";

static GLuint hdr_compile( GLenum type, const char *src ) {
	GLuint sh = glCreateShader( type );
	glShaderSource( sh, 1, &src, NULL );
	glCompileShader( sh );
	GLint ok = 0;
	glGetShaderiv( sh, GL_COMPILE_STATUS, &ok );
	if ( !ok ) {
		char msg[512];
		glGetShaderInfoLog( sh, sizeof( msg ), NULL, msg );
		if ( log_cb ) log_cb( RETRO_LOG_ERROR, "[boom3] HDR shader: %s\n", msg );
		glDeleteShader( sh );
		return 0;
	}
	return sh;
}

/* (re)create GL objects for the current context and size; safe to call
   per frame, does work only on change. Context loss zeroes the ids. */
static bool hdr_ensure_target( int w, int h ) {
	if ( hdr_prog == 0 ) {
		GLuint vs = hdr_compile( GL_VERTEX_SHADER, hdr_vs_src );
		GLuint fs = hdr_compile( GL_FRAGMENT_SHADER, hdr_fs_src );
		if ( !vs || !fs )
			return false;
		hdr_prog = glCreateProgram();
		glAttachShader( hdr_prog, vs );
		glAttachShader( hdr_prog, fs );
		glBindAttribLocation( hdr_prog, 0, "aPos" );
		glLinkProgram( hdr_prog );
		glDeleteShader( vs );
		glDeleteShader( fs );
		GLint ok = 0;
		glGetProgramiv( hdr_prog, GL_LINK_STATUS, &ok );
		if ( !ok ) {
			glDeleteProgram( hdr_prog );
			hdr_prog = 0;
			return false;
		}
		hdr_loc_tex   = glGetUniformLocation( hdr_prog, "uScene" );
		hdr_loc_mat   = glGetUniformLocation( hdr_prog, "uGamut" );
		hdr_loc_parms = glGetUniformLocation( hdr_prog, "uParms" );
		hdr_loc_bloomT  = glGetUniformLocation( hdr_prog, "uBloomT" );
		hdr_loc_bloomW  = glGetUniformLocation( hdr_prog, "uBloomW" );
		hdr_loc_bloomAmt= glGetUniformLocation( hdr_prog, "uBloomAmt" );
	}
	if ( hdr_prog_bright == 0 ) {
		GLuint vs = hdr_compile( GL_VERTEX_SHADER, hdr_vs_src );
		GLuint fs = hdr_compile( GL_FRAGMENT_SHADER, hdr_bright_fs_src );
		GLuint fs2 = hdr_compile( GL_VERTEX_SHADER == 0 ? 0 : GL_FRAGMENT_SHADER, hdr_blur_fs_src );
		if ( vs && fs && fs2 ) {
			hdr_prog_bright = glCreateProgram();
			glAttachShader( hdr_prog_bright, vs );
			glAttachShader( hdr_prog_bright, fs );
			glBindAttribLocation( hdr_prog_bright, 0, "aPos" );
			glLinkProgram( hdr_prog_bright );
			hdr_prog_blur = glCreateProgram();
			glAttachShader( hdr_prog_blur, vs );
			glAttachShader( hdr_prog_blur, fs2 );
			glBindAttribLocation( hdr_prog_blur, 0, "aPos" );
			glLinkProgram( hdr_prog_blur );
			hdr_bright_loc_thresh = glGetUniformLocation( hdr_prog_bright, "uThresh" );
			hdr_blur_loc_dir      = glGetUniformLocation( hdr_prog_blur, "uDir" );
		}
		if ( vs ) glDeleteShader( vs );
		if ( fs ) glDeleteShader( fs );
		if ( fs2 ) glDeleteShader( fs2 );
	}
	if ( hdr_fbo == 0 || w != hdr_w || h != hdr_h ) {
		if ( hdr_tex ) glDeleteTextures( 1, &hdr_tex );
		if ( hdr_rbo ) glDeleteRenderbuffers( 1, &hdr_rbo );
		if ( hdr_fbo ) glDeleteFramebuffers( 1, &hdr_fbo );
		glGenTextures( 1, &hdr_tex );
		glBindTexture( GL_TEXTURE_2D, hdr_tex );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB10_A2, w, h, 0, GL_RGBA,
				GL_UNSIGNED_INT_2_10_10_10_REV, NULL );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glGenRenderbuffers( 1, &hdr_rbo );
		glBindRenderbuffer( GL_RENDERBUFFER, hdr_rbo );
		/* the engine needs depth AND stencil (stencil shadow volumes) */
		glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h );
		glGenFramebuffers( 1, &hdr_fbo );
		glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_fbo );
		glFramebufferTexture2D( RARCH_GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_2D, hdr_tex, 0 );
		glFramebufferRenderbuffer( RARCH_GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
				GL_RENDERBUFFER, hdr_rbo );
		if ( glCheckFramebufferStatus( RARCH_GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
			if ( log_cb ) log_cb( RETRO_LOG_ERROR, "[boom3] HDR scene FBO incomplete, disabling HDR pass\n" );
			hdr_output_active = false;
			return false;
		}
		/* the scene texture is sampled with bilinear taps by the bright
		   pass and 1:1 by the composite - LINEAR serves both */
		glBindTexture( GL_TEXTURE_2D, hdr_tex );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		/* bloom chain: [0,1] at 1/4 res, [2,3] at 1/16 */
		{
			int i;
			for ( i = 0; i < 4; i++ ) {
				int bw = ( i < 2 ) ? w / 4 : w / 16;
				int bh = ( i < 2 ) ? h / 4 : h / 16;
				if ( bw < 1 ) bw = 1;
				if ( bh < 1 ) bh = 1;
				if ( hdr_bloom_tex[i] ) glDeleteTextures( 1, &hdr_bloom_tex[i] );
				if ( hdr_bloom_fbo[i] ) glDeleteFramebuffers( 1, &hdr_bloom_fbo[i] );
				glGenTextures( 1, &hdr_bloom_tex[i] );
				glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[i] );
				glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB10_A2, bw, bh, 0, GL_RGBA,
						GL_UNSIGNED_INT_2_10_10_10_REV, NULL );
				glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
				glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
				glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
				glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
				glGenFramebuffers( 1, &hdr_bloom_fbo[i] );
				glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[i] );
				glFramebufferTexture2D( RARCH_GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_2D, hdr_bloom_tex[i], 0 );
			}
		}
		hdr_w = w;
		hdr_h = h;
	}
	return true;
}

/* bind the scene target as the engine's render destination */
static void hdr_bind_scene( void ) {
	if ( !hdr_output_active )
		return;
	if ( !hdr_ensure_target( scr_width, scr_height ) )
		return;
	glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_fbo );
}

/* Rec.709 -> Rec.2020 (column-major for glUniformMatrix3fv) */
static const float hdr_m709to2020[9] = {
	0.6274f, 0.0691f, 0.0164f,
	0.3293f, 0.9195f, 0.0880f,
	0.0433f, 0.0114f, 0.8956f
};
/* DCI-P3(D65) -> Rec.2020: for the Wide mode's 709-as-P3 reinterpretation */
static const float hdr_mP3to2020[9] = {
	0.7538f, 0.0457f, -0.0012f,
	0.1986f, 0.9418f,  0.0176f,
	0.0476f, 0.0125f,  0.9837f
};
static const float hdr_mIdentity[9] = {
	1.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 1.0f
};

/* run the conversion pass from the scene target into dstFbo */
static void hdr_present( GLuint dstFbo ) {
	if ( !hdr_output_active || hdr_prog == 0 || hdr_fbo == 0 )
		return;

	/* frontend HDR state, re-queried per present as the contract asks */
	float paperWhite = 200.0f, maxNits = 1000.0f;
	unsigned gamutMode = 0, outMode = 1;
	environ_cb( RETRO_ENVIRONMENT_GET_HDR_PAPER_WHITE_NITS, &paperWhite );
	environ_cb( RETRO_ENVIRONMENT_GET_HDR_MAX_NITS, &maxNits );
	environ_cb( RETRO_ENVIRONMENT_GET_HDR_EXPAND_GAMUT, &gamutMode );
	if ( environ_cb( RETRO_ENVIRONMENT_GET_HDR_OUTPUT_MODE, &outMode ) ) {
		if ( outMode == 0 && !hdr_warned_sdr && log_cb ) {
			log_cb( RETRO_LOG_WARN, "[boom3] Color Format is 30-bit HDR but frontend HDR output is off; switch the core option to 24-bit for correct SDR colors\n" );
			hdr_warned_sdr = true;
		}
	}
	{
		bool native10 = false;
		if ( environ_cb( RETRO_ENVIRONMENT_GET_SCREEN_10BPC_CAPABLE, &native10 )
				&& !native10 && !hdr_warned_narrow && log_cb ) {
			log_cb( RETRO_LOG_WARN, "[boom3] video driver narrows 10-bit to 8-bit; 30-bit mode gains nothing here\n" );
			hdr_warned_narrow = true;
		}
	}

	float mat[9];
	int i;
	switch ( gamutMode ) {
		case 3:   /* Super: no rotation, the display's interpretation boosts */
			memcpy( mat, hdr_mIdentity, sizeof( mat ) );
			break;
		case 2:   /* Wide: 709 coordinates reinterpreted as DCI-P3 */
			memcpy( mat, hdr_mP3to2020, sizeof( mat ) );
			break;
		case 1:   /* Expanded: halfway between Accurate and Wide */
			for ( i = 0; i < 9; i++ )
				mat[i] = 0.5f * ( hdr_m709to2020[i] + hdr_mP3to2020[i] );
			break;
		default:  /* Accurate */
			memcpy( mat, hdr_m709to2020, sizeof( mat ) );
			break;
	}

	float H = maxNits / ( paperWhite > 1.0f ? paperWhite : 1.0f );
	if ( H < 1.0f )
		H = 1.0f;   /* the contract's zero-headroom clamp */

	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	glDisable( GL_BLEND );
	glDisable( GL_CULL_FACE );
	glDepthMask( GL_FALSE );

	static const float triV[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 0, triV );

	int haveBloom = ( hdr_bloom_amount > 0.0f && hdr_prog_bright && hdr_prog_blur );
	if ( haveBloom ) {
		int qw = hdr_w / 4 < 1 ? 1 : hdr_w / 4,  qh = hdr_h / 4 < 1 ? 1 : hdr_h / 4;
		int sw = hdr_w / 16 < 1 ? 1 : hdr_w / 16, sh = hdr_h / 16 < 1 ? 1 : hdr_h / 16;
		glActiveTexture( GL_TEXTURE0 );
		/* bright pass: scene -> tight A */
		glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[0] );
		glViewport( 0, 0, qw, qh );
		glUseProgram( hdr_prog_bright );
		glBindTexture( GL_TEXTURE_2D, hdr_tex );
		glUniform1f( hdr_bright_loc_thresh, 0.62f );
		glDrawArrays( GL_TRIANGLES, 0, 3 );
		/* tight band blur: A -> B (H), B -> A (V) */
		glUseProgram( hdr_prog_blur );
		glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[1] );
		glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[0] );
		glUniform2f( hdr_blur_loc_dir, 1.0f / qw, 0.0f );
		glDrawArrays( GL_TRIANGLES, 0, 3 );
		glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[0] );
		glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[1] );
		glUniform2f( hdr_blur_loc_dir, 0.0f, 1.0f / qh );
		glDrawArrays( GL_TRIANGLES, 0, 3 );
		/* wide band: seeded from the blurred tight band via a (0,0)
		   "blur" (pure downsample copy), then its own H+V at 1/16 */
		glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[2] );
		glViewport( 0, 0, sw, sh );
		glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[0] );
		glUniform2f( hdr_blur_loc_dir, 0.0f, 0.0f );
		glDrawArrays( GL_TRIANGLES, 0, 3 );
		glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[3] );
		glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[2] );
		glUniform2f( hdr_blur_loc_dir, 1.0f / sw, 0.0f );
		glDrawArrays( GL_TRIANGLES, 0, 3 );
		glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[2] );
		glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[3] );
		glUniform2f( hdr_blur_loc_dir, 0.0f, 1.0f / sh );
		glDrawArrays( GL_TRIANGLES, 0, 3 );
	}

	glBindFramebuffer( RARCH_GL_FRAMEBUFFER, dstFbo );
	glViewport( 0, 0, hdr_w, hdr_h );

	glUseProgram( hdr_prog );
	glActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[0] );
	glActiveTexture( GL_TEXTURE2 );
	glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[2] );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, hdr_tex );
	glUniform1i( hdr_loc_tex, 0 );
	glUniform1i( hdr_loc_bloomT, 1 );
	glUniform1i( hdr_loc_bloomW, 2 );
	glUniform1f( hdr_loc_bloomAmt, haveBloom ? hdr_bloom_amount : 0.0f );
	glUniformMatrix3fv( hdr_loc_mat, 1, GL_FALSE, mat );
	glUniform4f( hdr_loc_parms, paperWhite / 10000.0f, H,
			hdr_rolloff_aces ? 1.0f : 0.0f, 0.75f /* Reinhard knee */ );

	glDrawArrays( GL_TRIANGLES, 0, 3 );
	glDisableVertexAttribArray( 0 );
	glActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glActiveTexture( GL_TEXTURE2 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
	/* engine state is restored by glsm on the next STATE_BIND; the
	   depth mask matters before that, so put it back */
	glDepthMask( GL_TRUE );
}

void GLimp_SwapBuffers() {
   /*
      Frame-time fix: flush only when the frontend reads our FBO from a
      DIFFERENT context - cross-context visibility genuinely needs it.
      In the default single-context mode, command ordering within the
      context already guarantees the frontend sees the finished frame,
      and an unconditional glFlush was one more per-frame driver
      synchronization with driver-dependent, variable cost.
   */
   /* 30-bit HDR: convert the scene target into the frontend framebuffer
      before presentation; 24-bit mode skips straight past. */
   hdr_present((GLuint)hw_render.get_current_framebuffer());

   if (libretro_shared_context)
      glFlush();
   if (!libretro_shared_context)
      glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL);
	video_cb(RETRO_HW_FRAME_BUFFER_VALID, scr_width, scr_height, 0);
   if (!libretro_shared_context)
      glsm_ctl(GLSM_CTL_STATE_BIND, NULL);
	glBindFramebuffer(RARCH_GL_FRAMEBUFFER, hw_render.get_current_framebuffer());
	hdr_bind_scene();   /* loading-screen pumps and the next frame render here */

	/* A map load runs synchronously inside a single retro_run(): the engine
	 * pumps the loading screen through UpdateScreen()->GLimp_SwapBuffers()
	 * many times before common->Frame() returns. Without help the frontend
	 * receives no audio and no frame-time advance for that whole span, so it
	 * sees one multi-second frame - an audio underrun and a large
	 * frametime-deviation spike.
	 *
	 * Keep the frontend fed by uploading this displayed frame's worth of
	 * SILENCE and advancing the frame clock. Crucially we do NOT mix the
	 * sound world here: MixFrame* would advance the deterministic sound
	 * clock for frames that are not game tics, desyncing audio once the map
	 * starts. Silence keeps the buffer full without touching game state, so
	 * the load stays deterministic (verified: post-load audio unchanged)
	 * while the frontend lifecycle no longer stalls. Gated on the load flag
	 * so normal in-game presentation is untouched. */
	extern bool G_SessionInsideMapChange(void);
	if (!first_boot && G_SessionInsideMapChange()) {
		audio_upload_silence();
	}
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

void retro_reset(void)
{
}

void retro_set_rumble_strong(void)
{
}

void retro_unset_rumble_strong(void)
{
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_unload_game(void)
{
	// common->Init() only runs on the first retro_run(); if the frontend
	// unloads before ever running a frame, shutting down an uninitialized
	// engine would touch unconstructed subsystems.
	if (common && !first_boot)
		common->Shutdown();

	// reset for a potential re-load within the same core instance
	extern void LibRetro_ResetInputQueues(void);
	LibRetro_ResetInputQueues();
	old_ret = 0;
	oldanalogs = 0;
	memset((void*)kb_mouse_btn, 0, sizeof(kb_mouse_btn));
	audio_output_float = false;
	memset(&audio_float_cb, 0, sizeof(audio_float_cb));
	first_boot = true;
	initial_resolution_set = false;
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

/* ============ savestates ============
 * v1: route retro_serialize through the engine's own savegame machinery,
 * which already serializes complete game state (entities, scripts, physics,
 * player). SaveGame is synchronous; LoadGame restores synchronously via
 * ExecuteMapChange. retro_savestate_active suppresses all rendering side
 * effects during both. The state buffer is the .save file's bytes.
 *
 * Properties (declared via serialization quirks): variable size per state,
 * endian- and platform-dependent (the savegame format writes native types),
 * and NOT fast enough for run-ahead (a restore is a synchronous map load).
 * Fast in-memory states for run-ahead remain future work.
 */
extern bool retro_savestate_active;
#define RETRO_STATE_NAME "retro_state"

static idList<byte> retro_state_cache;
static int retro_state_cache_tic = -1;

static bool RetroBuildState(void)
{
	extern volatile int com_ticNumber;
	if (retro_state_cache_tic == com_ticNumber && retro_state_cache.Num() > 0)
		return true;	// still current: state can only change on a tic

	if (!sessLocal.mapSpawned) {
		if (log_cb) log_cb(RETRO_LOG_INFO, "[boom3] state: refused, mapSpawned=false\n");
		return false;
	}

	// serialize straight into memory: no disk I/O anywhere in this path
	idFile_Memory mem(RETRO_STATE_NAME ".save");
	/* seed the buffer from the previous state's size: state sizes are
	 * stable frame to frame, so the steady state is exactly one
	 * allocation and zero growth copies per build */
	if (retro_state_cache.Num() > 0)
		mem.SetGranularity(retro_state_cache.Num() + 65536);
	int stT0 = Core_Milliseconds();
	retro_savestate_active = true;
	bool ok = sessLocal.SaveGame(RETRO_STATE_NAME, true, NULL, &mem);
	retro_savestate_active = false;
	int stT1 = Core_Milliseconds();
	if (ok) {
		/* Append the mixer's DSP section (see WriteDSPState) with a
		 * trailing [size]['SND2'] footer. LoadGame reads its own chunks
		 * and ignores trailing bytes, so the savegame parser is
		 * untouched; unserialize locates the section from the footer. */
		idSoundWorldLocal *sw = static_cast<idSoundWorldLocal *>(soundSystem->GetPlayingSoundWorld());
		if (sw) {
			int before = mem.Length();
			sw->WriteDSPState(&mem);
			/*
			 * v3 footer additions, after the DSP blob:
			 *  - the output sample rate. Every sample-time field in the
			 *    state (trigger times, the sound clock, the DSP stream
			 *    states) is in output samples; loading under a different
			 *    doom_sound_samplerate silently misinterprets all of
			 *    them. The reader skips this whole section on mismatch
			 *    and says so, instead of failing subtly.
			 *  - the audio pacing accumulators. They define the per-run
			 *    block-size sequence, and block boundaries shape the mix
			 *    (per-block gain ramps, per-block reverb param steps):
			 *    without them, post-restore audio is bit-reproducible
			 *    only if the restore happens to land on the same
			 *    accumulator phase as the save.
			 */
			mem.WriteInt((int)sample_rate);
			mem.WriteInt(audio_rem_acc);
			mem.WriteInt(audio_frame_carry);
			int payload = mem.Length() - before;
			mem.WriteInt(payload);
			mem.WriteInt(0x33444E53);	/* 'SND3' */
		}
	}
	/* the audit numbers for the fast-savestate work: what a state build
	 * actually costs, split game-vs-dsp. Logged only on cache misses, so
	 * run-ahead's per-frame serialize stays silent once cached. */
	if (log_cb) log_cb(RETRO_LOG_INFO, "[boom3] state build: game %dms dsp %dms size %d\n",
			stT1 - stT0, Core_Milliseconds() - stT1, mem.Length());
	if (!ok || mem.Length() <= 0) {
		if (log_cb) log_cb(RETRO_LOG_INFO, "[boom3] state: SaveGame ok=%d len=%d\n", (int)ok, mem.Length());
		return false;
	}

	retro_state_cache.SetNum(mem.Length());
	memcpy(retro_state_cache.Ptr(), mem.GetDataPtr(), mem.Length());
	retro_state_cache_tic = com_ticNumber;
	return true;
}

size_t retro_serialize_size(void)
{
	if (!RetroBuildState())
		return 0;
	return (size_t)retro_state_cache.Num();
}

bool retro_serialize(void *data_, size_t size)
{
	if (!RetroBuildState())
		return false;
	if (size < (size_t)retro_state_cache.Num())
		return false;
	memcpy(data_, retro_state_cache.Ptr(), retro_state_cache.Num());
	return true;
}

bool retro_unserialize(const void *data_, size_t size)
{
	if (!data_ || size == 0 || !fileSystem || !fileSystem->IsInitialized())
		return false;

	// wrap the frontend's buffer in a read-mode memory file: no disk I/O.
	// Heap-allocated because LoadGame's savegameFile lifecycle deletes it
	// (fileSystem->CloseFile) on every exit path.
	idFile_Memory *mem = new idFile_Memory(RETRO_STATE_NAME ".save",
	                                       (const char *)data_, (int)size);

	int stT0 = Core_Milliseconds();
	retro_savestate_active = true;
	bool ok = sessLocal.LoadGame(RETRO_STATE_NAME, mem);
	retro_savestate_active = false;
	/* the restore is a synchronous map change today: this number is the
	 * whole case for the same-map fast-restore architecture. */
	if (log_cb) log_cb(RETRO_LOG_INFO, "[boom3] state restore: %dms ok=%d\n",
			Core_Milliseconds() - stT0, (int)ok);

	if (ok && size >= 8) {
		/* apply the DSP section after LoadGame has rebuilt the emitters */
		const unsigned char *bytes = (const unsigned char *)data_;
		int magic, payload;
		memcpy(&magic,   bytes + size - 4, 4);
		memcpy(&payload, bytes + size - 8, 4);
		/* 'SND2' footers from older builds fail the magic check and are
		 * skipped cleanly, which is the correct migration: the DSP
		 * section is same-binary-contract data anyway. */
		if (magic == 0x33444E53 && payload > 12 && (size_t)payload <= size - 8) {
			const unsigned char *sect = bytes + size - 8 - payload;
			int rate = 0, remAcc = 0, carry = 0;
			memcpy(&rate,   sect + payload - 12, 4);
			memcpy(&remAcc, sect + payload - 8,  4);
			memcpy(&carry,  sect + payload - 4,  4);
			if (rate != (int)sample_rate) {
				if (log_cb)
					log_cb(RETRO_LOG_WARN,
						"[boom3] state was saved at %d Hz, running at %u Hz: "
						"skipping the mixer DSP section; sample-time fields in "
						"the savegame keep their saved units, so timing and "
						"pitch of in-flight sounds may be off. Set "
						"doom_sound_samplerate to match the save for exact "
						"restore.\n", rate, sample_rate);
			} else {
				audio_rem_acc     = remAcc;
				audio_frame_carry = carry;
				idSoundWorldLocal *sw = static_cast<idSoundWorldLocal *>(soundSystem->GetPlayingSoundWorld());
				if (sw) {
					idFile_Memory dsp("dsp", (const char *)sect, payload - 12);
					sw->ReadDSPState(&dsp);
				}
			}
		}
	}

	// state changed out from under the cache
	retro_state_cache_tic = -1;
	return ok;
}

void *retro_get_memory_data(unsigned id)
{
   (void)id;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   (void)id;
   return 0;
}

void retro_deinit(void)
{
   libretro_supports_bitmasks = false;
}

void retro_init(void)
{
   struct retro_log_callback log;

   if(environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "boom3";
   info->library_version  = "v1.5.0" ;
   info->need_fullpath    = true;
   info->valid_extensions = "pk4";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->timing.fps            = framerate;
   info->timing.sample_rate    = SAMPLE_RATE;

   info->geometry.base_width   = scr_width;
   info->geometry.base_height  = scr_height;
   info->geometry.max_width    = scr_width;
   info->geometry.max_height   = scr_height;
   info->geometry.aspect_ratio = (scr_width * 1.0f) / (scr_height * 1.0f);
}

void retro_set_environment(retro_environment_t cb)
{
   static bool libretro_supports_option_categories = false;
   static const struct retro_controller_description port_1[] = {
      { "Gamepad Classic", RETRO_DEVICE_JOYPAD },
      { "Gamepad Classic Alt", RETRO_DEVICE_JOYPAD_ALT },
      { "Gamepad Modern", RETRO_DEVICE_MODERN }
   };

   static const struct retro_controller_info ports[] = {
      /*
         num_types said 4 for a 3-entry array since this table was
         written. The frontend only dereferences the phantom fourth
         desc when debug logging is on, so it lay dormant until other
         rodata (the HDR pass's -1.0f triangle constants, whose bit
         pattern was the faulting "pointer") happened to land next to
         the array. sizeof-derived so the count can never desync from
         the table again.
      */
      { port_1, sizeof( port_1 ) / sizeof( port_1[0] ) },
      { 0 },
   };

   environ_cb = cb;

   libretro_set_core_options(environ_cb,
         &libretro_supports_option_categories);
   cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);

   struct retro_perf_callback perf;
   if (environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf))
      perf_get_time_usec = perf.get_time_usec;
}
