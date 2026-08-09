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
#include "renderer/aces2_jmh.h"
#include "renderer/aces2_arb.h"
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

#include <vfs/vfs_hybrid.h>

/* retail default.cfg sets these Win32-era cvars; register them as inert
   so every boot doesn't print "Unknown command" twice */
static idCVar in_mouse( "in_mouse", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "legacy, unused" );
static idCVar m_strafe( "m_strafe", "0.25", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "legacy, unused" );

/* 30-bit / HDR10 output state; the implementation lives above
   GLimp_SwapBuffers, the full design comment with it. */
bool          hdr_output_active = false;   /* chosen at load, needs restart; read by draw_arb2 */
int           r_specularFalloffShape = 0;  /* 0 original hard-knee quadratic, 1 tailed power; read by Image_init */
float         hdr_specular_gain = 2.0f;    /* interaction specular scale in HDR mode; read by draw_arb2 */
float         hdr_scene_encode_scale = 1.0f; /* 0.5 = one gamma-domain stop of scene headroom; read by the render backend */
bool          hdr_fp16_scene = false;      /* FP16 scene target: per-pass quantization gone */
bool          hdr_fp32_scene = false;      /* FP32 scene target: full-float accumulation for extreme translucent stacking */
bool          hdr_luma_clamp = false;      /* luminance-aware highlight blending in the clamped epilogue; live-switchable */
bool          hdr_unbounded_blend = true;  /* unclamped epilogue on float targets: true multi-pass accumulation past 1.0 */
enum {
	/* Scalar curves: one channel at a time.  Numbered below the RGB
	   block so both backends can tell the two kinds apart with a single
	   comparison. */
	HDR_ROLLOFF_REINHARD  = 0,
	HDR_ROLLOFF_ACES      = 1,
	HDR_ROLLOFF_HEJL      = 2,
	HDR_ROLLOFF_GT        = 3,
	HDR_ROLLOFF_HABLE     = 4,
	HDR_ROLLOFF_LOTTES    = 5,
	HDR_ROLLOFF_DRAGO     = 6,
	HDR_ROLLOFF_UNREAL    = 7,
	HDR_ROLLOFF_FILMICALU = 8,
	HDR_ROLLOFF_RPLAIN    = 9,
	HDR_ROLLOFF_REXT      = 10,
	HDR_ROLLOFF_EXPO      = 11,
	HDR_ROLLOFF_HABLE2017 = 12,
	HDR_ROLLOFF_ACES2     = 13,
	HDR_ROLLOFF_TUMBLIN   = 14,
	HDR_ROLLOFF_WARD      = 15,
	HDR_ROLLOFF_SCHLICK   = 16,
	HDR_ROLLOFF_DEVLIN    = 17,
	HDR_ROLLOFF_FILMICLOG = 18,

	/* RGB operators: the result of one channel depends on the other
	   two, so these run once per pixel rather than three times. */
	HDR_ROLLOFF_JODIE     = 19,
	HDR_ROLLOFF_ACESFIT   = 20,
	HDR_ROLLOFF_NEUTRAL   = 21,
	HDR_ROLLOFF_AGX       = 22,
	HDR_ROLLOFF_ACES2FULL = 23
};

#define HDR_ROLLOFF_FIRST_RGB HDR_ROLLOFF_JODIE
static int    hdr_rolloff_mode  = HDR_ROLLOFF_REINHARD;   /* live-switchable */
/* GT's toe and shoulder, live-switchable.  The published defaults are
 * 0.22 and 0.4; every other constant in the curve is derived from them,
 * so they are solved once per present rather than baked into the
 * shaders the way they used to be. */
/* Peak nits the frontend reports; the ACES 2.0 tables are solved for it.
 * Declared with the shared HDR state rather than beside the rest of the
 * ACES 2.0 resources, because it is written on the common path and read
 * on the desktop one - put it with the resources and the GLES build
 * compiles the writer without the declaration. */
static float  hdr_aces2_wanted_peak = 1000.0f;
static float  hdr_gt_toe        = 0.22f;
static float  hdr_gt_shoulder   = 0.40f;
static int    hdr_expand_mode   = 0;       /* 0 none, 1 per-channel inverse tonemap, 2 hue-preserving; live-switchable */

static GLuint hdr_fbo, hdr_tex, hdr_rbo, hdr_prog;
static GLint  hdr_loc_tex, hdr_loc_mat, hdr_loc_parms;
static GLint  hdr_loc_bloomT, hdr_loc_bloomW, hdr_loc_bloomAmt, hdr_loc_encScale;
static GLint  hdr_loc_frame;
static GLint  hdr_loc_expand;
static GLint  hdr_loc_aces;
static GLint  hdr_loc_gt;
static unsigned hdr_frame_counter;
static GLint  hdr_bright_loc_enc;
static int    hdr_w, hdr_h;
static bool   hdr_warned_sdr, hdr_warned_narrow;
static float  hdr_bloom_amount = 1.0f;      /* 0 = off; live-switchable */

/* bloom chain: two bands (1/4-res tight core, 1/16-res wide haze),
   each with a ping-pong pair for the separable blur */
static GLuint hdr_bloom_fbo[4], hdr_bloom_tex[4];   /* [0,1]=1/4 A/B, [2,3]=1/16 A/B */
/* Convolution bloom pyramid: level 0 at 1/4, halving to 1/128. Separate from
   the two-band buffers above so switching the option needs no reallocation. */
#define HDR_CONV_MAX 6
static GLuint hdr_conv_fbo[HDR_CONV_MAX], hdr_conv_tex[HDR_CONV_MAX];
static GLuint hdr_prog_down, hdr_prog_up;
#ifndef HAVE_OPENGLES
static GLuint hdr_arb_vp, hdr_arb_down, hdr_arb_up;
static GLuint hdr_arb_bright, hdr_arb_blur;
static GLuint hdr_arb_comp1, hdr_arb_comp2;
static int    hdr_arb_comp_mode = -1;   /* roll-off curve baked into the pair above */
/* Every program in the chain is loaded.  On this build there is no other
   chain to run, so this is a load-state flag rather than a path
   selector - the dispatch below is not conditional. */
static bool   hdr_arb_pyramid;
#endif
static GLint  hdr_down_loc_texel, hdr_up_loc_texel, hdr_up_loc_radius;
static GLint  hdr_loc_bandW;
static bool   hdr_bloom_convolution;   /* live-switchable */
static GLuint hdr_prog_bright, hdr_prog_blur;
/* bloom is optional: either of these disables it without disabling the
   HDR pass itself. Kept apart so a resize that rebuilds the targets
   cannot resurrect a program that failed to link. */
static bool   hdr_bloom_prog_bad, hdr_bloom_tex_bad;
static GLint  hdr_bright_loc_thresh, hdr_blur_loc_dir, hdr_bright_loc_texel;
static GLint  hdr_bright_loc_knee;
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

/* Keyboard/mouse mode. Keyboard and mouse are polled unconditionally in
 * Sys_SetKeys()/Sys_SetMouse() regardless of the selected device, so this
 * mode only has to switch the joypad path off: an empty descriptor table
 * clears the frontend's remap list and the empty bind table releases all
 * JOY_* binds via gp_layout_set_bind(). */
static gp_layout_t kb_mouse = {
   {
      { 0 },
   },
   {
      { 0 },
   },
};

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
	var.key = "doom_hdr_headroom";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		hdr_scene_encode_scale = (hdr_output_active
				&& !((hdr_fp16_scene || hdr_fp32_scene) && hdr_unbounded_blend)
				&& strcmp(var.value, "disabled") != 0) ? 0.5f : 1.0f;

	/* Luminance-aware highlight blending: only does anything where the
	 * epilogue actually clamps, so it is forced off when the unbounded
	 * float path is in use - there is no ceiling there to roll off
	 * against, and the extra instructions would be pure cost. */
	var.key = "doom_hdr_luma_blend";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		hdr_luma_clamp = hdr_output_active
				&& !((hdr_fp16_scene || hdr_fp32_scene) && hdr_unbounded_blend)
				&& strcmp(var.value, "enabled") == 0;

	var.key = "doom_hdr_expansion";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		if (!strcmp(var.value, "inverse"))       hdr_expand_mode = 1;
		else if (!strcmp(var.value, "hue"))      hdr_expand_mode = 2;
		else                                     hdr_expand_mode = 0;
	}

	/* Reshaping the falloff means rebuilding the ramp texture, which only
	 * happens when the images are regenerated, so this takes effect on
	 * the next image reload rather than on the next frame. */
	var.key = "doom_specular_falloff";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		int want = strcmp(var.value, "tailed") == 0 ? 1 : 0;
		if (want != r_specularFalloffShape) {
			r_specularFalloffShape = want;
			if (!first_boot && glConfig.isInitialized) {
				cmdSystem->BufferCommandText(CMD_EXEC_APPEND, "reloadImages\n");
			}
		}
	}

	var.key = "doom_hdr_particle_lights";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		extern int hdr_particle_light_count_opt;
		if (!strcmp(var.value, "disabled"))  hdr_particle_light_count_opt = 0;
		else if (!strcmp(var.value, "4"))    hdr_particle_light_count_opt = 4;
		else                                 hdr_particle_light_count_opt = 2;
	}

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

	var.key = "doom_hdr_bloom_convolution";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		hdr_bloom_convolution = !strcmp(var.value, "enabled");

	var.key = "doom_hdr_rolloff";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
	{
		if (!strcmp(var.value, "aces"))      hdr_rolloff_mode = HDR_ROLLOFF_ACES;
		else if (!strcmp(var.value, "hejl")) hdr_rolloff_mode = HDR_ROLLOFF_HEJL;
		else if (!strcmp(var.value, "gt"))   hdr_rolloff_mode = HDR_ROLLOFF_GT;
		else if (!strcmp(var.value, "hable"))  hdr_rolloff_mode = HDR_ROLLOFF_HABLE;
		else if (!strcmp(var.value, "lottes")) hdr_rolloff_mode = HDR_ROLLOFF_LOTTES;
		else if (!strcmp(var.value, "drago"))  hdr_rolloff_mode = HDR_ROLLOFF_DRAGO;
		else if (!strcmp(var.value, "unreal")) hdr_rolloff_mode = HDR_ROLLOFF_UNREAL;
		else if (!strcmp(var.value, "filmicalu")) hdr_rolloff_mode = HDR_ROLLOFF_FILMICALU;
		else if (!strcmp(var.value, "jodie"))     hdr_rolloff_mode = HDR_ROLLOFF_JODIE;
		else if (!strcmp(var.value, "acesfit"))   hdr_rolloff_mode = HDR_ROLLOFF_ACESFIT;
		else if (!strcmp(var.value, "neutral"))   hdr_rolloff_mode = HDR_ROLLOFF_NEUTRAL;
		else if (!strcmp(var.value, "agx"))       hdr_rolloff_mode = HDR_ROLLOFF_AGX;
		else if (!strcmp(var.value, "aces2full")) hdr_rolloff_mode = HDR_ROLLOFF_ACES2FULL;
		else if (!strcmp(var.value, "rplain"))    hdr_rolloff_mode = HDR_ROLLOFF_RPLAIN;
		else if (!strcmp(var.value, "rext"))      hdr_rolloff_mode = HDR_ROLLOFF_REXT;
		else if (!strcmp(var.value, "expo"))      hdr_rolloff_mode = HDR_ROLLOFF_EXPO;
		else if (!strcmp(var.value, "hable2017")) hdr_rolloff_mode = HDR_ROLLOFF_HABLE2017;
		else if (!strcmp(var.value, "aces2"))     hdr_rolloff_mode = HDR_ROLLOFF_ACES2;
		else if (!strcmp(var.value, "tumblin"))   hdr_rolloff_mode = HDR_ROLLOFF_TUMBLIN;
		else if (!strcmp(var.value, "ward"))      hdr_rolloff_mode = HDR_ROLLOFF_WARD;
		else if (!strcmp(var.value, "schlick"))   hdr_rolloff_mode = HDR_ROLLOFF_SCHLICK;
		else if (!strcmp(var.value, "devlin"))    hdr_rolloff_mode = HDR_ROLLOFF_DEVLIN;
		else if (!strcmp(var.value, "filmiclog")) hdr_rolloff_mode = HDR_ROLLOFF_FILMICLOG;
		else                                     hdr_rolloff_mode = HDR_ROLLOFF_REINHARD;
	}

	var.key = "doom_hdr_gt_toe";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		hdr_gt_toe = (float)atof(var.value);

	var.key = "doom_hdr_gt_shoulder";
	var.value = NULL;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		hdr_gt_shoulder = (float)atof(var.value);

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
   hdr_bloom_prog_bad = hdr_bloom_tex_bad = false;
   hdr_prog_down = hdr_prog_up = 0;
#ifndef HAVE_OPENGLES
   hdr_arb_vp = hdr_arb_down = hdr_arb_up = 0;
   hdr_arb_bright = hdr_arb_blur = 0;
   hdr_arb_comp1 = hdr_arb_comp2 = 0;
   hdr_arb_comp_mode = -1;
   hdr_arb_pyramid = false;
#endif
   memset(hdr_conv_fbo, 0, sizeof hdr_conv_fbo);
   memset(hdr_conv_tex, 0, sizeof hdr_conv_tex);
   memset(hdr_bloom_fbo, 0, sizeof hdr_bloom_fbo);
   memset(hdr_bloom_tex, 0, sizeof hdr_bloom_tex);
   hdr_w = hdr_h = 0;

   if (!first_boot)
   {
      R_ReinitOpenGL();

      /* context_destroy freed the world's derived data - the area
         surfaces and light interactions built for the map - and
         R_ReinitOpenGL does not rebuild them.  Without this the map
         geometry never comes back after a context reset: entities,
         the HUD and the player's own model still draw, because those
         are regenerated from their models, so it reads as "most of
         the environment stopped rendering" rather than as a failure.

         The engine's own vid restart ends exactly this way, which is
         what this path was missing. */
      tr.viewCount++;
      tr.viewDef = NULL;
      R_RegenerateWorld_f( idCmdArgs() );
   }

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
		case RETRO_DEVICE_KEYBOARD:
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
	if (hdr_output_active) {
		fmt = RETRO_PIXEL_FORMAT_HDR10_2101010;
		struct retro_variable pv = { "doom_hdr_precision", NULL };
		if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pv) && pv.value) {
			hdr_fp16_scene = strcmp(pv.value, "fp16") == 0;
			hdr_fp32_scene = strcmp(pv.value, "fp32") == 0;
		}
		pv.key = "doom_hdr_true_blend";
		pv.value = NULL;
		if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &pv) && pv.value)
			hdr_unbounded_blend = strcmp(pv.value, "disabled") != 0;
		if ((hdr_fp16_scene || hdr_fp32_scene) && log_cb)
			log_cb(RETRO_LOG_INFO, "[boom3] %s scene target, multi-pass blending %s\n",
					hdr_fp32_scene ? "FP32" : "FP16",
					hdr_unbounded_blend ? "unbounded" : "clamped per pass");
	}
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
         case RETRO_DEVICE_KEYBOARD:
            doom_devices[port] = RETRO_DEVICE_KEYBOARD;
            environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, kb_mouse.desc);
            pending_layout = &kb_mouse;
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
#ifndef GL_UNSIGNED_INT_2_10_10_10_REV
/* absent from GLES2 headers (it is a GLES3 enum) - the only place the
   HDR module referenced a GL identifier its oldest target does not
   define, which broke every GLES platform build while all desktop GL
   targets passed. Runtime is unaffected either way: GLES2 has no
   10-bit color-renderable format, so the scene FBO comes back
   incomplete and the HDR pass disables itself with a log line. */
#define GL_UNSIGNED_INT_2_10_10_10_REV 0x8368
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif

#ifdef HAVE_OPENGLES
static const char *hdr_vs_src =
	"attribute vec2 aPos;\n"
	"varying vec2 vUV;\n"
	"void main() {\n"
	"  vUV = aPos * 0.5 + 0.5;\n"
	"  gl_Position = vec4(aPos, 0.0, 1.0);\n"
	"}\n";
#endif /* HAVE_OPENGLES */

#ifdef HAVE_OPENGLES
static const char *hdr_fs_src =
	"#ifdef GL_ES\nprecision highp float;\n#endif\n"
	"varying vec2 vUV;\n"
	"uniform sampler2D uScene;\n"
	"uniform sampler2D uBloomT;\n"
	"uniform sampler2D uBloomW;\n"
	"uniform float uBloomAmt;\n"
	"uniform vec2 uBandW;\n"
	"uniform float uEncScale;\n"
	"uniform float uFrame;\n"
	"uniform mat3 uGamut;\n"
	/* parms: x = paperWhite/10000, y = headroom H (>=1), z = aces flag,
	   w = knee (Reinhard) */
	"uniform vec4 uParms;\n"
	"uniform vec2 uExpand;\n"
	"uniform vec3 uAces;\n"
	"uniform vec4 uGt;\n"     /* GT: toe m, S0, 1-S1, C2 - solved on the CPU */
	/* Decode with a pure 2.4 power, matching RetroArch.

	   The frontend converts SDR core output for an HDR swapchain with
	   pow(abs(sdr.rgb), 2.4) - identically in hdr.frag and
	   hdr_sm5.hlsl.h - and re-encodes with 1/2.4. That is also what
	   BT.1886 specifies, so it is what an HDR display applies to the
	   24-bit signal this mode is meant to be compared against.

	   The inverse-sRGB curve that used to be here differs from it
	   almost entirely in the toe: sRGB is linear below 0.04045, a much
	   shallower approach to black than any power function, so decoded
	   shadows came out 18.5x too bright at code 0.02, 5.2x at 0.05 and
	   2.5x at 0.10, converging by mid grey (1.13x at 0.50) and exact at
	   1.0. Paper white is unaffected either way - only the shape below
	   it changes. On a game built around its shadows that was the most
	   visible remaining difference between 30-bit and 24-bit output,
	   and it made boom3 inconsistent with every other core the frontend
	   renders.

	   abs() rather than max(): parity with the frontend, and the FP16
	   epilogue already clamps its low side. */
	"vec3 srgbToLinear(vec3 c) {\n"
	"  return pow(abs(c), vec3(2.4));\n"
	"}\n"
	/*
	   Highlight roll-off, three curves behind one shape.

	   Each curve is normalised so paper white lands on paper white,
	   which also pins its asymptote: ACES flattens at 1.285x paper
	   white and Hejl at 1.463x, whatever the display can do.  So the
	   curve runs up to paper white and a shoulder carries everything
	   above it out to H, joined at the curve's own gradient there.
	   uAces.x is A = (H-1)/slope and uAces.y is that gradient, both
	   measured on the CPU from whichever curve is selected.

	   Reinhard needs none of this - its shoulder is built from H
	   already - so it keeps its own form below.

	   Hejl's published curve bakes in a display gamma.  This pipeline
	   works in linear light and encodes PQ at the end, so the 2.2 is
	   undone rather than inherited.
	*/
	"float shoulder(float v, float base) {\n"
	"  if (v <= 1.0) return base;\n"
	"  float e = v - 1.0;\n"
	"  return 1.0 + e * uAces.x * uAces.y / (e + uAces.x);\n"
	"}\n"
	/*
	   The RGB operators.  Each reads luminance, the peak channel or a
	   matrix, so a channel's result depends on the other two - which is
	   what lets a highlight desaturate toward white as it clips instead
	   of sliding toward whichever primary saturated last.  Normalised
	   by their own value at white like the scalar curves.
	*/
	"vec3 acesFitRRT(vec3 v) {\n"
	"  vec3 a = v * (v + 0.0245786) - 0.000090537;\n"
	"  vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;\n"
	"  return a / b;\n"
	"}\n"
	"vec3 rolloffRGB(vec3 c) {\n"
	"  float H = uParms.y;\n"
	"  if (H <= 1.0001) return min(c, vec3(1.0));\n"
	"  if (uParms.z > 21.5) {\n"                       /* AgX */
	"    mat3 mi = mat3(0.842479062253094, 0.0784335999999992, 0.0792237451477643,\n"
	"                   0.0423282422610123, 0.878468636469772, 0.0791661274605434,\n"
	"                   0.0423756549057051, 0.0784336, 0.879142973793104);\n"
	"    mat3 mo = mat3(1.19687900512017, -0.0980208811401368, -0.0990297440797205,\n"
	"                   -0.0528968517574562, 1.15190312990417, -0.0989611768448433,\n"
	"                   -0.0529716355144438, -0.0980434501171241, 1.15107367264116);\n"
	"    vec3 v = mi * max(c, vec3(0.0));\n"
	"    v = clamp((log2(max(v, vec3(1e-10))) + 12.47393) / 16.500009, 0.0, 1.0);\n"
	"    vec3 v2 = v * v, v4 = v2 * v2;\n"
	"    v = 15.5 * v4 * v2 - 40.14 * v4 * v + 31.96 * v4\n"
	"      - 6.868 * v2 * v + 0.4298 * v2 + 0.1191 * v - 0.00232;\n"
	"    v = mo * v;\n"
	"    return pow(max(v, vec3(0.0)), vec3(2.2)) * 1.4491792;\n"
	"  }\n"
	"  if (uParms.z > 20.5) {\n"                       /* Khronos PBR Neutral */
	"    float sc = 0.76, des = 0.15;\n"
	"    float x = min(c.r, min(c.g, c.b));\n"
	"    float off = x < 0.08 ? x - 6.25 * x * x : 0.04;\n"
	"    vec3 v = c - off;\n"
	"    float peak = max(v.r, max(v.g, v.b));\n"
	"    if (peak < sc) return v * 1.1506276;\n"
	"    float d = 1.0 - sc;\n"
	"    float np = 1.0 - d * d / (peak + d - sc);\n"
	"    v *= np / peak;\n"
	"    float g = 1.0 - 1.0 / (des * (peak - np) + 1.0);\n"
	"    return mix(v, vec3(np), g) * 1.1506276;\n"
	"  }\n"
	"  if (uParms.z > 19.5) {\n"                        /* ACES Fitted */
	"    mat3 mi = mat3(0.59719, 0.07600, 0.02840,\n"
	"                   0.35458, 0.90834, 0.13383,\n"
	"                   0.04823, 0.01566, 0.83777);\n"
	"    mat3 mo = mat3( 1.60475, -0.10208, -0.00327,\n"
	"                   -0.53108,  1.10813, -0.07276,\n"
	"                   -0.07367, -0.00605,  1.07602);\n"
	"    vec3 v = mo * acesFitRRT(mi * c);\n"
	"    return max(v, vec3(0.0)) * 1.6152077;\n"
	"  }\n"
	"  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"   /* Reinhard-Jodie */
	"  vec3 tv = c / (1.0 + c);\n"
	"  return mix(c / (1.0 + l), tv, tv) * 2.0;\n"
	"}\n"
	"float rolloff(float v) {\n"
	"  float H = uParms.y;\n"
	"  if (H <= 1.0001) return min(v, 1.0);\n"
	/*
	   Uchimura's GT curve, published defaults folded to constants:
	   a = 1, m = 0.22, l = 0.4, c = 1.33, b = 0, P = 1, which give
	   l0 = 0.312 and S0 = S1 = 0.532.  With a = 1 the linear segment
	   collapses to v itself.  Normalised by its value at 1.0 (0.827832)
	   like the other two, so paper white stays put; it saturates at
	   1.208x paper white and keeps mid grey at 0.216, closer to where
	   Reinhard leaves it than ACES or Hejl.
	*/
	/* Four more, each normalised by its own value at 1.0 so paper white
	   stays put, and each handed to the same shoulder above it.  Hable
	   and Lottes are linear in and out; Unreal bakes in a display gamma
	   and has it undone; Drago is used as published. */
	"  if (uParms.z > 6.5) {\n"
	"    float n = v / (v + 0.155) * 1.019;\n"
	"    return shoulder(v, pow(n, 2.2) * 1.3173380);\n"
	"  }\n"
	"  if (uParms.z > 5.5) {\n"
	"    float n = log(1.0 + max(v, 0.0)) / log(2.0 + 8.0 * pow(max(v, 0.0) / 8.0, 0.2344653));\n"
	"    return shoulder(v, n * (1.0 / 0.9542425) * 2.6616800);\n"
	"  }\n"
	"  if (uParms.z > 4.5) {\n"
	"    float p = pow(max(v, 0.0), 1.6);\n"
	"    float q = pow(max(v, 0.0), 1.5632);\n"
	"    return shoulder(v, (p / (q * 0.1674199 + 1.0730397)) * 1.2404596);\n"
	"  }\n"
	"  if (uParms.z > 3.5) {\n"
	"    float n = ((v * (0.15 * v + 0.05) + 0.004) / (v * (0.15 * v + 0.50) + 0.06)) - 0.0666667;\n"
	"    return shoulder(v, n * 4.5319149);\n"
	"  }\n"
	"  if (uParms.z > 17.5 && uParms.z < 18.5) {\n"      /* Filmic log + contrast */
	"    float t = clamp((log2(max(v, 1e-10)) + 12.4739312) / 16.500000, 0.0, 1.0);\n"
	"    float sg = 1.0 / (1.0 + exp(-8.0 * (t - 0.6060791)));\n"
	"    return shoulder(v, pow(sg, 2.2) * 1.7852541);\n"
	"  }\n"
	"  if (uParms.z > 16.5 && uParms.z < 17.5) {\n"      /* Reinhard-Devlin */
	"    float x = max(v, 0.0);\n"
	"    return shoulder(v, (x / (x + 0.3010864)) * 1.3010864);\n"
	"  }\n"
	"  if (uParms.z > 15.5 && uParms.z < 16.5) {\n"      /* Schlick */
	"    float x = max(v, 0.0);\n"
	"    return shoulder(v, 2.3 * x / (2.3 * x - x + 1.0));\n"
	"  }\n"
	"  if (uParms.z > 14.5 && uParms.z < 15.5) {\n"      /* Ward: linear */
	"    return shoulder(v, max(v, 0.0));\n"
	"  }\n"
	"  if (uParms.z > 13.5 && uParms.z < 14.5) {\n"      /* Tumblin-Rushmeier */
	"    return shoulder(v, pow(max(v, 0.0), 0.6075));\n"
	"  }\n"
	"  if (uParms.z > 12.5 && uParms.z < 13.5) {\n"      /* ACES 2.0 tone scale */
	"    float x = max(v, 0.0);\n"
	"    float f = 1.0471038 * pow(x / (x + 0.9198583), 1.15);\n"
	"    return shoulder(v, (f * f / (f + 0.04)) * 2.1854781);\n"
	"  }\n"
	"  if (uParms.z > 11.5 && uParms.z < 12.5) {\n"      /* Hable 2017 piecewise */
	"    float x = max(v, 0.0);\n"
	"    float f;\n"
	"    if (x < 0.25) f = 1.1313708 * pow(x, 1.25);\n"
	"    else if (x < 0.70) f = 0.20 + (x - 0.25);\n"
	"    else f = 0.65 + 0.35 * (x - 0.70) / ((x - 0.70) + 0.35);\n"
	"    return shoulder(v, f * 1.2322275);\n"
	"  }\n"
	"  if (uParms.z > 10.5 && uParms.z < 11.5) {\n"      /* Exponential */
	"    return shoulder(v, (1.0 - exp(-max(v, 0.0))) * 1.5819767);\n"
	"  }\n"
	"  if (uParms.z > 9.5 && uParms.z < 10.5) {\n"       /* Reinhard extended */
	"    float x = max(v, 0.0);\n"
	"    return shoulder(v, (x * (1.0 + x / 16.0) / (1.0 + x)) * 1.8823529);\n"
	"  }\n"
	"  if (uParms.z > 8.5 && uParms.z < 9.5) {\n"        /* Reinhard plain */
	"    float x = max(v, 0.0);\n"
	"    return shoulder(v, (x / (1.0 + x)) * 2.0);\n"
	"  }\n"
	"  if (uParms.z > 7.5 && uParms.z < 8.5) {\n"
	"    float x = max(v, 0.0);\n"
	"    float va = 1.425 * x + 0.05;\n"
	"    float f = ((x * va + 0.004) / ((x * va + 0.055) + 0.0491)) - 0.0821;\n"
	"    return shoulder(v, pow(max(f, 0.0), 2.2) * 1.4132626);\n"
	"  }\n"
	"  if (uParms.z > 2.5) {\n"
	"    float t = clamp(v / uGt.x, 0.0, 1.0);\n"
	"    float w0 = 1.0 - (t * t * (3.0 - 2.0 * t));\n"
	"    float w2 = step(uGt.y, v);\n"
	"    float w1 = 1.0 - w0 - w2;\n"
	"    float T = uGt.x * pow(max(v, 0.0) / uGt.x, 1.33);\n"
	"    float S = 1.0 - uGt.z * exp(-uGt.w * (v - uGt.y));\n"
	"    return (T * w0 + v * w1 + S * w2) * uAces.z;\n"
	"  }\n"
	"  if (uParms.z > 1.5) {\n"
	"    float x = max(v - 0.004, 0.0);\n"
	"    float n = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);\n"
	"    return shoulder(v, pow(n, 2.2) * 1.4629683);\n"
	"  }\n"
	"  if (uParms.z > 0.5) {\n"
	"    float n = v * (2.51 * v + 0.03) / (v * (2.43 * v + 0.59) + 0.14);\n"
	"    return shoulder(v, n * 1.2440945);\n"
	"  }\n"
	"  float K = uParms.w;\n"
	"  if (v <= K) return v;\n"
	"  float e = v - K, A = H - K;\n"
	"  return K + e * A / (e + A);\n"
	"}\n"
	/*
	   HDR expansion.

	   The scene arrives in the 0..1 range the game renders into, so
	   without this the headroom above paper white only ever carries
	   bloom and whatever the FP16 path accumulated past 1.0 - the
	   ordinary bright pixels of the image sit at paper white and stop.
	   Expansion pushes the top end of that range up into the headroom so
	   highlights read as brighter than white rather than as white.

	   Below the knee nothing moves: those are the diffuse mid-tones that
	   should stay exactly where the game put them, and lifting them is
	   what makes naive inverse tonemapping look washed out. Above it, a
	   quadratic carries knee..1 onto knee..E:

	     t   = (v - K) / (1 - K)
	     v'  = K + (1 - K) * t + (E - 1) * t^2

	   The linear term is chosen so the slope is continuous at the knee -
	   no visible crease where expansion starts - and the quadratic term
	   is exactly what lands v = 1 on E. At E = 1 the quadratic term
	   vanishes and the whole thing is the identity, which is why "None"
	   and an SDR display both come out bit-identical to the old path.

	   Mode 1 runs that per channel. It is the straightforward reading of
	   "inverse tonemap", and it does to colour what per-channel clipping
	   does in reverse: channels expand by different factors, so an
	   already-saturated highlight gets more saturated and its hue drifts
	   toward whichever channel was largest.

	   Mode 2 runs the same curve once, on the pixel's peak channel, and
	   scales all three channels by the resulting ratio. A uniform scale
	   leaves chromaticity untouched by construction, so the pixel gets
	   brighter along its own colour and only the amount of light
	   changes, which is what expansion should mean.

	   Driven by the peak channel rather than by luminance, deliberately.
	   Luminance is the textbook choice, but it makes the mode fire on
	   different pixels than mode 1 does: a saturated highlight like
	   (0.95, 0.55, 0.20) has a peak of 0.95, well over the knee, and a
	   luminance of 0.61, well under it - so a luminance-driven version
	   returns it untouched while mode 1 expands it hard. Coloured lights
	   and fire are exactly the content this option is for, and having
	   the hue-preserving setting quietly skip them would read as the
	   option not working. Peak-driven, the two modes reach the same
	   pixels and differ only in whether the channels move together.
	*/
	/* t is clamped to 1, and anything above 1.0 keeps its distance
	 * above it.  The quadratic is only defined as an expansion of the
	 * knee..1 range; extended past 1 it grows as the square, so a scene
	 * value of 2.0 - ordinary on the unbounded FP16 path, where
	 * translucent passes accumulate past white on purpose - would come
	 * out at 77.0 instead of 5.0, and 4.0 at 511.0.  Clamping keeps the
	 * curve on the domain it was derived for and leaves already-HDR
	 * content alone: it arrives above paper white and stays where it
	 * was, shifted by the expansion applied at 1.0. */
	"vec3 expandCurve(vec3 v, float E, float K) {\n"
	"  vec3 t = clamp((v - K) / max(1.0 - K, 1e-4), 0.0, 1.0);\n"
	"  vec3 hi = K + (1.0 - K) * t + (E - 1.0) * t * t + max(v - 1.0, 0.0);\n"
	"  return mix(v, hi, step(K, v));\n"
	"}\n"
	"vec3 expand(vec3 c) {\n"
	"  float E = uParms.y;\n"
	"  if (uExpand.x < 0.5 || E <= 1.0001) return c;\n"
	"  float K = uExpand.y;\n"
	"  if (uExpand.x < 1.5) return expandCurve(c, E, K);\n"
	"  float p = max(c.r, max(c.g, c.b));\n"
	"  if (p <= 1e-5) return c;\n"
	"  return c * (expandCurve(vec3(p), E, K).x / p);\n"
	"}\n"
	"float ign(vec2 p) {\n"
	"  return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));\n"
	"}\n"
	"float pq(float y) {\n"
	"  float p = pow(max(y, 0.0), 0.1593017578125);\n"
	"  return pow((0.8359375 + 18.8515625 * p) / (1.0 + 18.6875 * p), 78.84375);\n"
	"}\n"
	"void main() {\n"
	"  vec3 lin = srgbToLinear(texture2D(uScene, vUV).rgb * uEncScale);\n"
	/* bloom joins in LINEAR, BEFORE the roll-off: bloomed highlights
	   ride the same curve into the paper-white..peak headroom, which
	   is the part SDR output cannot express at all */
	/* The second band is fetched only when it is weighted. Convolution
	   bloom puts everything in one accumulated pyramid and sets
	   uBandW.y to zero, but the fetch still executed - a full-res
	   bilinear sample per pixel, 8.3M of them a frame at 4K, whose
	   result was multiplied by zero. uBandW is a uniform, so this
	   branch is uniform across the whole draw: every lane agrees, no
	   divergence, and the compiler can hoist it.

	   Bit-exact in both modes, and the shape matters. Accumulating into
	   a local and multiplying once at the end preserves the original
	   uBloomAmt * (w0*a + w1*b) association - folding uBloomAmt into
	   each term instead would have rounded differently and changed the
	   two-band path. With w1 == 0 the skipped term was w*b for finite
	   b, and adding +0.0 to a finite value is exact. */
	"  vec3 bl = uBandW.x * texture2D(uBloomT, vUV).rgb;\n"
	"  if (uBandW.y > 0.0)\n"
	"    bl += uBandW.y * texture2D(uBloomW, vUV).rgb;\n"
	"  lin += uBloomAmt * bl;\n"
	/* SDR-range content expanded into the display's headroom before the
	   roll-off sees it - see the expand() comment. */
	"  lin = expand(lin);\n"
	/* Modes 9 and up look at the whole colour, so they run once rather
	   than once per channel. */
	"  if (uParms.z > 18.5) lin = rolloffRGB(lin);\n"
	"  else lin = vec3(rolloff(lin.r), rolloff(lin.g), rolloff(lin.b));\n"
	"  lin = uGamut * lin;\n"
	"  vec3 y = clamp(lin * uParms.x, 0.0, 1.0);\n"
	"  vec3 e = vec3(pq(y.r), pq(y.g), pq(y.b));\n"
	/* +-0.5 LSB (10-bit) dither against PQ-remap banding.

	   Interleaved gradient noise rather than the usual sin() hash: sin()
	   hashes degenerate into visible structure once the argument gets
	   large, and dot(gl_FragCoord, vec2(12.9898, 78.233)) reaches ~1e5 at
	   1080p and ~2e5 at 4K, which is exactly where fp32 argument
	   reduction starts costing significant bits. IGN is also better
	   distributed and cheaper.

	   Per channel, not one scalar for all three: a single shared offset
	   dithers luminance only and leaves chroma contours - the thing PQ
	   banding in near-neutral darks actually looks like - untouched.

	   uFrame rotates the pattern so it dissolves into motion instead of
	   sitting on top of it as a fixed grain. */
	/* Decorrelate the three channels, and the frames, by rotating the
	   noise VALUE rather than translating the field.

	   Offsetting gl_FragCoord instead - which is what this did - makes
	   frame f the base field sampled at p + (f, f). That is a rigid
	   translation by construction, so the grain slid diagonally across
	   the screen at one pixel per frame. Coherent motion is far easier
	   to see than shimmer, so as a temporal dither it was worse than
	   leaving the pattern static.

	   fract(n + k*phi) keeps every pixel where it is and rotates its
	   value instead. k = 3*frame + channel makes each (channel, frame)
	   pair distinct, and successive k land on a low-discrepancy
	   sequence, so the offsets stay well spread. A constant offset
	   under fract is a bijection on [0,1), so the marginal
	   distribution is exact and the blue-noise spectrum is preserved:
	   measured 0.042% low-frequency power for every channel and frame,
	   against the base field's 0.042% and white noise's 1.56%.

	   Chroma contour energy on a near-neutral dark ramp, low-frequency
	   fraction of the R-G quantization error: 4.72% undithered, 0.260%
	   with one scalar shared across channels, 0.112% with the
	   translated fields, 0.099% here. Also one ign() call rather than
	   three. */
	"  float n = ign(gl_FragCoord.xy);\n"
	"  float k = uFrame * 3.0;\n"
	"  vec3 d = vec3(fract(n + (k + 0.0) * 0.6180339887),\n"
	"                fract(n + (k + 1.0) * 0.6180339887),\n"
	"                fract(n + (k + 2.0) * 0.6180339887)) - 0.5;\n"
	"  gl_FragColor = vec4(clamp(e + d / 1023.0, 0.0, 1.0), 1.0);\n"
	"}\n";
#endif /* HAVE_OPENGLES */

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
#ifndef HAVE_OPENGLES
/*
   ARB versions of the bloom pyramid's downsample and upsample passes.

   Same taps and the same weights as the GLSL above, written as ARB
   assembly so a desktop build running the ARB2 renderer has no GLSL in
   this part of the chain.  program.env[0].xy carries what uTexel did;
   for the upsample, env[0].zw carries the radius-scaled offset so the
   multiply is not repeated per tap.

   These are the two simplest passes in the chain and go first
   deliberately: they exercise the loader, the env-parameter path and
   the state discipline around ARB programs with the least that can go
   wrong, before the bright pass and the composite follow.
*/
static const char *hdr_arb_vp_src =
	"!!ARBvp1.0\n"
	"PARAM half = { 0.5, 0.5, 0.0, 0.0 };\n"
	"MOV result.position, vertex.attrib[0];\n"
	"MAD result.texcoord[0], vertex.attrib[0], half, half;\n"
	"END\n";

static const char *hdr_arb_down_fs_src =
	"!!ARBfp1.0\n"
	"OPTION ARB_precision_hint_nicest;\n"
	"PARAM texel = program.env[0];\n"
	/* 16 temporaries is the ARB minimum, and the first version of this
	   program used exactly that.  Summing each weight group as its taps
	   are fetched needs six. */
	"TEMP uv, t, e, ring, mid, o;\n"
	/* centre tap, weight 0.125 */
	"TEX e, fragment.texcoord[0], texture[0], 2D;\n"
	"MUL o, e, 0.125;\n"
	/* outer ring corners, weight 0.03125 */
	"MAD uv, texel, { -2.0,  2.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX ring, uv, texture[0], 2D;\n"
	"MAD uv, texel, {  2.0,  2.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD ring, ring, t;\n"
	"MAD uv, texel, { -2.0, -2.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD ring, ring, t;\n"
	"MAD uv, texel, {  2.0, -2.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD ring, ring, t;\n"
	"MAD o, ring, 0.03125, o;\n"
	/* outer ring edges, weight 0.0625 */
	"MAD uv, texel, {  0.0,  2.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX ring, uv, texture[0], 2D;\n"
	"MAD uv, texel, { -2.0,  0.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD ring, ring, t;\n"
	"MAD uv, texel, {  2.0,  0.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD ring, ring, t;\n"
	"MAD uv, texel, {  0.0, -2.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD ring, ring, t;\n"
	"MAD o, ring, 0.0625, o;\n"
	/* inner quad, weight 0.125 */
	"MAD uv, texel, { -1.0,  1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX mid, uv, texture[0], 2D;\n"
	"MAD uv, texel, {  1.0,  1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD mid, mid, t;\n"
	"MAD uv, texel, { -1.0, -1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD mid, mid, t;\n"
	"MAD uv, texel, {  1.0, -1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD mid, mid, t;\n"
	"MAD o, mid, 0.125, o;\n"
	"MOV o.w, 1.0;\n"
	"MOV result.color, o;\n"
	"END\n";

static const char *hdr_arb_up_fs_src =
	"!!ARBfp1.0\n"
	"OPTION ARB_precision_hint_nicest;\n"
	/* env[0].xy = texel * radius, precomputed on the CPU */
	"PARAM o = program.env[0];\n"
	"TEMP uv, t, s;\n"
	"MAD uv, o, { -1.0, -1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX s, uv, texture[0], 2D;\n"
	"MAD uv, o, {  0.0, -1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"MAD s, t, 2.0, s;\n"
	"MAD uv, o, {  1.0, -1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD s, s, t;\n"
	"MAD uv, o, { -1.0,  0.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"MAD s, t, 2.0, s;\n"
	"TEX t, fragment.texcoord[0], texture[0], 2D;\n"
	"MAD s, t, 4.0, s;\n"
	"MAD uv, o, {  1.0,  0.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"MAD s, t, 2.0, s;\n"
	"MAD uv, o, { -1.0,  1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD s, s, t;\n"
	"MAD uv, o, {  0.0,  1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"MAD s, t, 2.0, s;\n"
	"MAD uv, o, {  1.0,  1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD s, s, t;\n"
	"MUL s, s, 0.0625;\n"
	"MOV s.w, 1.0;\n"
	"MOV result.color, s;\n"
	"END\n";

/*
   ARB composite.

   Statement for statement the same chain as hdr_fs_src: linearize,
   add bloom, expand, roll off, gamut, PQ encode, dither.  Two things
   are shaped differently because ARB is not GLSL.

   No branches.  rolloff() and expand() both select between forms with
   CMP, which means every form is evaluated and the unused one thrown
   away.  That is fine for the arithmetic but not for division: the
   guards that keep the unused branch's RCP away from zero have to be
   real, not just correct-when-taken.  Every RCP below is preceded by
   the MAX that bounds its argument.

   The reciprocals that are pass constants are computed on the CPU and
   arrive in env: 1/(1-expandKnee) and the PQ and dither scales.  The
   per-pixel ones - the ACES and Reinhard denominators and the PQ
   denominator - are per channel and stay here, three RCPs each,
   because ARB's RCP is scalar.

   env[0] = uParms   (paperWhite/10000, H, acesFlag, reinhardKnee)
   env[1] = uExpand  (mode, knee, 1/max(1-knee,1e-4), 0)
   env[2] =          (encScale, bloomAmt, bandW.x, bandW.y)
   env[3] =          (frame*3, golden ratio, 1/1023, 0)
   env[4..6] = gamut matrix rows
   texture[0] scene, texture[1] bloom band 0, texture[2] bloom band 1
*/
#define HDR_ARB_COMPOSITE_BODY \
	/* POW takes scalar operands and a scalar operand needs an explicit
	   component selector, which a bare literal cannot carry - the
	   exponents have to arrive as PARAMs and be selected.  Getting this
	   wrong is a parse error, not a silent one: "expected '.'". */ \
	"PARAM kSrgb = { 2.4, 2.4, 2.4, 2.4 };\n" \
	"PARAM kPqA = { 0.1593017578125, 0.1593017578125, 0.1593017578125, 0.1593017578125 };\n" \
	"PARAM kPqB = { 78.84375, 78.84375, 78.84375, 78.84375 };\n" \
	"PARAM kHejlG = { 2.2, 2.2, 2.2, 2.2 };\n" \
	"PARAM kGtC = { 1.33, 1.33, 1.33, 1.33 };\n" \
	"PARAM kGamma22 = { 2.2, 2.2, 2.2, 2.2 };\n" \
	"PARAM kLuma = { 0.2126, 0.7152, 0.0722, 0.0 };\n" \
	"PARAM kHableB = { 1.25, 1.25, 1.25, 1.25 };\n" \
	"PARAM kAces2G = { 1.15, 1.15, 1.15, 1.15 };\n" \
	"PARAM kTumblin = { 0.6075, 0.6075, 0.6075, 0.6075 };\n" \
	"PARAM kAcesIn0 = { 0.59719, 0.35458, 0.04823, 0.0 };\n" \
	"PARAM kAcesIn1 = { 0.07600, 0.90834, 0.01566, 0.0 };\n" \
	"PARAM kAcesIn2 = { 0.02840, 0.13383, 0.83777, 0.0 };\n" \
	"PARAM kAcesOut0 = { 1.60475, -0.53108, -0.07367, 0.0 };\n" \
	"PARAM kAcesOut1 = { -0.10208, 1.10813, -0.00605, 0.0 };\n" \
	"PARAM kAcesOut2 = { -0.00327, -0.07276, 1.07602, 0.0 };\n" \
	"PARAM kAgxIn0 = { 0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0 };\n" \
	"PARAM kAgxIn1 = { 0.0784335999999992, 0.878468636469772, 0.0784336, 0.0 };\n" \
	"PARAM kAgxIn2 = { 0.0792237451477643, 0.0791661274605434, 0.879142973793104, 0.0 };\n" \
	"PARAM kAgxOut0 = { 1.19687900512017, -0.0528968517574562, -0.0529716355144438, 0.0 };\n" \
	"PARAM kAgxOut1 = { -0.0980208811401368, 1.15190312990417, -0.0980434501171241, 0.0 };\n" \
	"PARAM kAgxOut2 = { -0.0990297440797205, -0.0989611768448433, 1.15107367264116, 0.0 };\n" \
	"PARAM kInvM = { 4.5454545, 4.5454545, 4.5454545, 4.5454545 };\n" \
	"PARAM kE = { 2.7182818, 2.7182818, 2.7182818, 2.7182818 };\n" \
	"PARAM kLotA = { 1.6, 1.6, 1.6, 1.6 };\n" \
	"PARAM kLotAD = { 1.5632, 1.5632, 1.5632, 1.5632 };\n" \
	"PARAM kDragoE = { 0.2344653, 0.2344653, 0.2344653, 0.2344653 };\n" \
	"TEMP s, lin, bl, t, u, v, e, a, r, m, y, p, n, d, g, h;\n" \
	/* lin = pow(abs(scene * encScale), 2.4) */ \
	"TEX s, fragment.texcoord[0], texture[0], 2D;\n" \
	"MUL s, s, program.env[2].x;\n" \
	"ABS s, s;\n" \
	"POW lin.x, s.x, kSrgb.x;\n" \
	"POW lin.y, s.y, kSrgb.x;\n" \
	"POW lin.z, s.z, kSrgb.x;\n"

/* One of these is selected as HDR_ARB_ROLLOFF_CURVE when the composite
   is built.  Each leaves the curve value in t and reads lin; all are
   normalised so paper white lands on paper white, exactly as their CPU
   twins in hdr_curve_* are, and each is followed by the shared shoulder
   that carries values above 1 out to the display headroom. */
#define HDR_ARB_CURVE_REINHARD \
	"SUB u, lin, program.env[0].w;\n" \
	"SUB v.x, program.env[0].y, program.env[0].w;\n" \
	"ADD a, u, v.x;\n" \
	"MAX a, a, 0.00001;\n" \
	"RCP m.x, a.x;\n" \
	"RCP m.y, a.y;\n" \
	"RCP m.z, a.z;\n" \
	"MUL a, u, v.x;\n" \
	"MUL a, a, m;\n" \
	"ADD a, a, program.env[0].w;\n" \
	"SUB v, lin, program.env[0].w;\n" \
	"CMP t, v, lin, a;\n"

#define HDR_ARB_CURVE_ACES \
	"MAD t, lin, 2.51, 0.03;\n" \
	"MUL t, t, lin;\n" \
	"MAD u, lin, 2.43, 0.59;\n" \
	"MAD u, u, lin, 0.14;\n" \
	"MAX u, u, 0.00001;\n" \
	"RCP a.x, u.x;\n" \
	"RCP a.y, u.y;\n" \
	"RCP a.z, u.z;\n" \
	"MUL t, t, a;\n" \
	"MUL t, t, 1.2440945;\n"

#define HDR_ARB_CURVE_HEJL \
	"SUB u, lin, 0.004;\n" \
	"MAX u, u, 0.0;\n" \
	"MAD t, u, 6.2, 0.5;\n" \
	"MUL t, t, u;\n" \
	"MAD v, u, 6.2, 1.7;\n" \
	"MAD v, v, u, 0.06;\n" \
	"MAX v, v, 0.00001;\n" \
	"RCP a.x, v.x;\n" \
	"RCP a.y, v.y;\n" \
	"RCP a.z, v.z;\n" \
	"MUL t, t, a;\n" \
	"MAX t, t, 0.0;\n" \
	"POW t.x, t.x, kGamma22.x;\n" \
	"POW t.y, t.y, kGamma22.x;\n" \
	"POW t.z, t.z, kGamma22.x;\n" \
	"MUL t, t, 1.4629683;\n"

#define HDR_ARB_CURVE_GT \
	/* env[8] = (toe m, S0, 1-S1, C2), env[9].x = 1/m, env[9].y = norm.
	   All four are solved on the CPU from the toe and shoulder options,
	   so moving either slider is a uniform upload, not a rebuild. */ \
	"MUL u, lin, program.env[9].x;\n" \
	"MIN u, u, 1.0;\n" \
	"MAX u, u, 0.0;\n" \
	"MAD v, u, -2.0, 3.0;\n" \
	"MUL v, v, u;\n" \
	"MUL v, v, u;\n" \
	"SUB v, 1.0, v;\n" \
	"SGE m, lin, program.env[8].y;\n" \
	"SUB a, 1.0, v;\n" \
	"SUB a, a, m;\n" \
	"MAX u, lin, 0.0;\n" \
	"MUL u, u, program.env[9].x;\n" \
	"POW u.x, u.x, kGtC.x;\n" \
	"POW u.y, u.y, kGtC.x;\n" \
	"POW u.z, u.z, kGtC.x;\n" \
	"MUL u, u, program.env[8].x;\n" \
	"SUB t, lin, program.env[8].y;\n" \
	"MUL t, t, program.env[8].w;\n" \
	"MUL t, t, -1.4426950;\n" \
	"EX2 t.x, t.x;\n" \
	"EX2 t.y, t.y;\n" \
	"EX2 t.z, t.z;\n" \
	"MUL t, t, program.env[8].z;\n" \
	"SUB t, 1.0, t;\n" \
	"MUL u, u, v;\n" \
	"MAD u, lin, a, u;\n" \
	"MAD t, t, m, u;\n" \
	"MUL t, t, program.env[9].y;\n"

#define HDR_ARB_CURVE_HABLE \
	"MAD t, lin, 0.15, 0.05;\n" \
	"MAD t, t, lin, 0.004;\n" \
	"MAD u, lin, 0.15, 0.50;\n" \
	"MAD u, u, lin, 0.06;\n" \
	"MAX u, u, 0.00001;\n" \
	"RCP a.x, u.x;\n" \
	"RCP a.y, u.y;\n" \
	"RCP a.z, u.z;\n" \
	"MUL t, t, a;\n" \
	"SUB t, t, 0.0666667;\n" \
	"MUL t, t, 4.5319149;\n"

#define HDR_ARB_CURVE_LOTTES \
	"MAX u, lin, 0.0;\n" \
	"POW t.x, u.x, kLotA.x;\n" \
	"POW t.y, u.y, kLotA.x;\n" \
	"POW t.z, u.z, kLotA.x;\n" \
	"POW v.x, u.x, kLotAD.x;\n" \
	"POW v.y, u.y, kLotAD.x;\n" \
	"POW v.z, u.z, kLotAD.x;\n" \
	"MAD v, v, 0.1674199, 1.0730397;\n" \
	"MAX v, v, 0.00001;\n" \
	"RCP a.x, v.x;\n" \
	"RCP a.y, v.y;\n" \
	"RCP a.z, v.z;\n" \
	"MUL t, t, a;\n" \
	"MUL t, t, 1.2404596;\n"

#define HDR_ARB_CURVE_DRAGO \
	"MAX u, lin, 0.0;\n" \
	"ADD v, u, 1.0;\n" \
	"LG2 t.x, v.x;\n" \
	"LG2 t.y, v.y;\n" \
	"LG2 t.z, v.z;\n" \
	"MUL u, u, 0.125;\n" \
	"POW u.x, u.x, kDragoE.x;\n" \
	"POW u.y, u.y, kDragoE.x;\n" \
	"POW u.z, u.z, kDragoE.x;\n" \
	"MAD u, u, 8.0, 2.0;\n" \
	"LG2 v.x, u.x;\n" \
	"LG2 v.y, u.y;\n" \
	"LG2 v.z, u.z;\n" \
	"MAX v, v, 0.00001;\n" \
	"RCP a.x, v.x;\n" \
	"RCP a.y, v.y;\n" \
	"RCP a.z, v.z;\n" \
	"MUL t, t, a;\n" \
	"MUL t, t, 2.7893119;\n"

#define HDR_ARB_CURVE_UNREAL \
	"ADD u, lin, 0.155;\n" \
	"MAX u, u, 0.00001;\n" \
	"RCP a.x, u.x;\n" \
	"RCP a.y, u.y;\n" \
	"RCP a.z, u.z;\n" \
	"MUL t, lin, a;\n" \
	"MUL t, t, 1.019;\n" \
	"MAX t, t, 0.0;\n" \
	"POW t.x, t.x, kGamma22.x;\n" \
	"POW t.y, t.y, kGamma22.x;\n" \
	"POW t.z, t.z, kGamma22.x;\n" \
	"MUL t, t, 1.3173380;\n"

#define HDR_ARB_CURVE_FILMICALU \
	"MAD u, lin, 1.425, 0.05;\n" \
	"MUL t, lin, u;\n" \
	"ADD v, t, 0.004;\n" \
	"ADD u, t, 0.1041;\n" \
	"MAX u, u, 0.00001;\n" \
	"RCP a.x, u.x;\n" \
	"RCP a.y, u.y;\n" \
	"RCP a.z, u.z;\n" \
	"MUL t, v, a;\n" \
	"SUB t, t, 0.0821;\n" \
	"MAX t, t, 0.0;\n" \
	"POW t.x, t.x, kGamma22.x;\n" \
	"POW t.y, t.y, kGamma22.x;\n" \
	"POW t.z, t.z, kGamma22.x;\n" \
	"MUL t, t, 1.4132626;\n"

/* The RGB operators.  These read the whole colour - luminance, the peak
   channel or a matrix - so unlike the curves above they cannot be
   applied per channel.  They write t directly and skip the shared
   shoulder, since each already decides its own top end. */
#define HDR_ARB_CURVE_FILMICLOG \
	"MAX u, lin, 0.0000000001;\n" \
	"LG2 v.x, u.x;\n" \
	"LG2 v.y, u.y;\n" \
	"LG2 v.z, u.z;\n" \
	"ADD v, v, 12.4739312;\n" \
	"MUL v, v, 0.06060606;\n" \
	"MAX v, v, 0.0;\n" \
	"MIN v, v, 1.0;\n" \
	"SUB v, v, 0.6060791;\n" \
	"MUL v, v, -11.5415603;\n" \
	"EX2 n.x, v.x;\n" \
	"EX2 n.y, v.y;\n" \
	"EX2 n.z, v.z;\n" \
	"ADD n, n, 1.0;\n" \
	"RCP a.x, n.x;\n" \
	"RCP a.y, n.y;\n" \
	"RCP a.z, n.z;\n" \
	"POW t.x, a.x, kGamma22.x;\n" \
	"POW t.y, a.y, kGamma22.x;\n" \
	"POW t.z, a.z, kGamma22.x;\n" \
	"MUL t, t, 1.7852541;\n"

#define HDR_ARB_CURVE_TUMBLIN \
	"MAX u, lin, 0.0;\n" \
	"POW t.x, u.x, kTumblin.x;\n" \
	"POW t.y, u.y, kTumblin.x;\n" \
	"POW t.z, u.z, kTumblin.x;\n"

#define HDR_ARB_CURVE_WARD \
	"MAX t, lin, 0.0;\n"

#define HDR_ARB_CURVE_SCHLICK \
	"MAX u, lin, 0.0;\n" \
	"MUL v, u, 2.3;\n" \
	"SUB n, v, u;\n" \
	"ADD n, n, 1.0;\n" \
	"MAX n, n, 0.00001;\n" \
	"RCP a.x, n.x;\n" \
	"RCP a.y, n.y;\n" \
	"RCP a.z, n.z;\n" \
	"MUL t, v, a;\n"

#define HDR_ARB_CURVE_DEVLIN \
	"MAX u, lin, 0.0;\n" \
	"ADD n, u, 0.3010864;\n" \
	"MAX n, n, 0.00001;\n" \
	"RCP a.x, n.x;\n" \
	"RCP a.y, n.y;\n" \
	"RCP a.z, n.z;\n" \
	"MUL t, u, a;\n" \
	"MUL t, t, 1.3010864;\n"

#define HDR_ARB_CURVE_RPLAIN \
	"MAX u, lin, 0.0;\n" \
	"ADD v, u, 1.0;\n" \
	"RCP a.x, v.x;\n" \
	"RCP a.y, v.y;\n" \
	"RCP a.z, v.z;\n" \
	"MUL t, u, a;\n" \
	"MUL t, t, 2.0;\n"

#define HDR_ARB_CURVE_REXT \
	"MAX u, lin, 0.0;\n" \
	"MAD v, u, 0.0625, 1.0;\n" \
	"MUL v, v, u;\n" \
	"ADD n, u, 1.0;\n" \
	"RCP a.x, n.x;\n" \
	"RCP a.y, n.y;\n" \
	"RCP a.z, n.z;\n" \
	"MUL t, v, a;\n" \
	"MUL t, t, 1.8823529;\n"

#define HDR_ARB_CURVE_EXPO \
	"MAX u, lin, 0.0;\n" \
	"MUL u, u, -1.4426950;\n" \
	"EX2 t.x, u.x;\n" \
	"EX2 t.y, u.y;\n" \
	"EX2 t.z, u.z;\n" \
	"SUB t, 1.0, t;\n" \
	"MUL t, t, 1.5819767;\n"

#define HDR_ARB_CURVE_HABLE2017 \
	"MAX u, lin, 0.0;\n" \
	/* toe: A * x^B */ \
	"POW v.x, u.x, kHableB.x;\n" \
	"POW v.y, u.y, kHableB.x;\n" \
	"POW v.z, u.z, kHableB.x;\n" \
	"MUL v, v, 1.1313708;\n" \
	/* straight middle: y0 + (x - x0), slope 1 */ \
	"ADD n, u, -0.05;\n" \
	/* shoulder: y1 + (1-y1)(x-x1)/((x-x1)+S) */ \
	"SUB d, u, 0.70;\n" \
	"ADD g, d, 0.35;\n" \
	"MAX g, g, 0.00001;\n" \
	"RCP a.x, g.x;\n" \
	"RCP a.y, g.y;\n" \
	"RCP a.z, g.z;\n" \
	"MUL h, d, 0.35;\n" \
	"MUL h, h, a;\n" \
	"ADD h, h, 0.65;\n" \
	/* pick the segment: below x1 take the middle, below x0 the toe */ \
	"SUB d, u, 0.70;\n" \
	"CMP t, d, n, h;\n" \
	"SUB d, u, 0.25;\n" \
	"CMP t, d, v, t;\n" \
	"MUL t, t, 1.2322275;\n"

#define HDR_ARB_CURVE_ACES2 \
	"MAX u, lin, 0.0;\n" \
	"ADD v, u, 0.9198583;\n" \
	"MAX v, v, 0.00001;\n" \
	"RCP a.x, v.x;\n" \
	"RCP a.y, v.y;\n" \
	"RCP a.z, v.z;\n" \
	"MUL t, u, a;\n" \
	"POW t.x, t.x, kAces2G.x;\n" \
	"POW t.y, t.y, kAces2G.x;\n" \
	"POW t.z, t.z, kAces2G.x;\n" \
	"MUL t, t, 1.0471038;\n" \
	/* flare: f*f / (f + t_1) */ \
	"MUL n, t, t;\n" \
	"ADD d, t, 0.04;\n" \
	"MAX d, d, 0.00001;\n" \
	"RCP a.x, d.x;\n" \
	"RCP a.y, d.y;\n" \
	"RCP a.z, d.z;\n" \
	"MUL t, n, a;\n" \
	"MUL t, t, 2.1854781;\n"

#define HDR_ARB_CURVE_JODIE \
	"DP3 g.x, lin, kLuma;\n" \
	"ADD u, lin, 1.0;\n" \
	"RCP a.x, u.x;\n" \
	"RCP a.y, u.y;\n" \
	"RCP a.z, u.z;\n" \
	"MUL v, lin, a;\n" \
	"ADD h.x, g.x, 1.0;\n" \
	"RCP h.x, h.x;\n" \
	"MUL u, lin, h.x;\n" \
	"MUL n, v, v;\n" \
	"SUB d, 1.0, v;\n" \
	"MUL t, u, d;\n" \
	"ADD t, t, n;\n" \
	"MUL t, t, 2.0;\n"

#define HDR_ARB_CURVE_ACESFIT \
	"DP3 u.x, lin, kAcesIn0;\n" \
	"DP3 u.y, lin, kAcesIn1;\n" \
	"DP3 u.z, lin, kAcesIn2;\n" \
	"ADD v, u, 0.0245786;\n" \
	"MUL v, v, u;\n" \
	"SUB v, v, 0.000090537;\n" \
	"MAD n, u, 0.983729, 0.4329510;\n" \
	"MAD n, n, u, 0.238081;\n" \
	"MAX n, n, 0.00001;\n" \
	"RCP a.x, n.x;\n" \
	"RCP a.y, n.y;\n" \
	"RCP a.z, n.z;\n" \
	"MUL u, v, a;\n" \
	"DP3 t.x, u, kAcesOut0;\n" \
	"DP3 t.y, u, kAcesOut1;\n" \
	"DP3 t.z, u, kAcesOut2;\n" \
	"MAX t, t, 0.0;\n" \
	"MUL t, t, 1.6152077;\n"

#define HDR_ARB_CURVE_NEUTRAL \
	"MIN g.x, lin.x, lin.y;\n" \
	"MIN g.x, g.x, lin.z;\n" \
	"MAD h.x, g.x, -6.25, 1.0;\n" \
	"MUL h.x, h.x, g.x;\n" \
	"SUB d.x, g.x, 0.08;\n" \
	"CMP h.x, d.x, h.x, 0.04;\n" \
	"SUB v, lin, h.x;\n" \
	"MAX g.y, v.x, v.y;\n" \
	"MAX g.y, g.y, v.z;\n" \
	"ADD n.x, g.y, 0.24;\n" \
	"SUB n.x, n.x, 0.76;\n" \
	"MAX n.x, n.x, 0.00001;\n" \
	"RCP n.x, n.x;\n" \
	"MAD n.x, n.x, -0.0576, 1.0;\n" \
	"MAX d.y, g.y, 0.00001;\n" \
	"RCP d.y, d.y;\n" \
	"MUL d.y, d.y, n.x;\n" \
	"MUL u, v, d.y;\n" \
	"SUB d.z, g.y, n.x;\n" \
	"MAD d.z, d.z, 0.15, 1.0;\n" \
	"MAX d.z, d.z, 0.00001;\n" \
	"RCP d.z, d.z;\n" \
	"SUB d.z, 1.0, d.z;\n" \
	"SUB d.w, 1.0, d.z;\n" \
	"MUL t, u, d.w;\n" \
	"MAD t, n.x, d.z, t;\n" \
	"SUB d.x, g.y, 0.76;\n" \
	"CMP t, d.x, v, t;\n" \
	"MUL t, t, 1.1506276;\n"

#define HDR_ARB_CURVE_AGX \
	"MAX u, lin, 0.0;\n" \
	"DP3 v.x, u, kAgxIn0;\n" \
	"DP3 v.y, u, kAgxIn1;\n" \
	"DP3 v.z, u, kAgxIn2;\n" \
	"MAX v, v, 0.0000000001;\n" \
	"LG2 n.x, v.x;\n" \
	"LG2 n.y, v.y;\n" \
	"LG2 n.z, v.z;\n" \
	"ADD n, n, 12.47393;\n" \
	"MUL n, n, 0.06060606;\n" \
	"MAX n, n, 0.0;\n" \
	"MIN n, n, 1.0;\n" \
	"MAD d, n, 15.5, -40.14;\n" \
	"MAD d, d, n, 31.96;\n" \
	"MAD d, d, n, -6.868;\n" \
	"MAD d, d, n, 0.4298;\n" \
	"MAD d, d, n, 0.1191;\n" \
	"MAD d, d, n, -0.00232;\n" \
	"DP3 u.x, d, kAgxOut0;\n" \
	"DP3 u.y, d, kAgxOut1;\n" \
	"DP3 u.z, d, kAgxOut2;\n" \
	"MAX u, u, 0.0;\n" \
	"POW t.x, u.x, kGamma22.x;\n" \
	"POW t.y, u.y, kGamma22.x;\n" \
	"POW t.z, u.z, kGamma22.x;\n" \
	"MUL t, t, 1.4491792;\n"

/* ACES 2.0, complete.  Unlike every other entry here this is not a tone
   curve but the whole output transform: it consumes the scene colour in
   lin and leaves finished display RGB there.  It therefore runs without
   the shoulder, and the gamut matrix that follows is fed identity rows
   for this mode because the transform has already landed in the display
   primaries. */
#define HDR_ARB_CURVE_ACES2FULL \
	ACES2_ARB_TRANSFORM_BODY \
	"MOV lin, v;\n"

#define HDR_ARB_COMPOSITE_TAIL \
	/* lin += bloomAmt * bl */ \
	"MAD lin, bl, program.env[2].y, lin;\n" \
	/* ---- expand(): t = clamp((v-K)*invK, 0, 1)
	   hi = K + (1-K)t + (E-1)t^2 + max(v-1,0), taken where v >= K */ \
	"SUB t, lin, program.env[1].y;\n" \
	"MUL t, t, program.env[1].z;\n" \
	"MAX t, t, 0.0;\n" \
	"MIN t, t, 1.0;\n" \
	"SUB u, 1.0, program.env[1].y;\n" \
	"MUL v, t, u;\n" \
	"ADD v, v, program.env[1].y;\n" \
	"SUB u, program.env[0].y, 1.0;\n" \
	"MUL a, t, t;\n" \
	"MAD v, a, u, v;\n" \
	"SUB a, lin, 1.0;\n" \
	"MAX a, a, 0.0;\n" \
	"ADD v, v, a;\n" \
	/* below the knee keep the input: CMP picks v when (lin - K) >= 0 */ \
	"SUB a, lin, program.env[1].y;\n" \
	"CMP e, a, lin, v;\n" \
	/* mode 2 drives the same curve from the peak channel and scales
	   uniformly.  peak in m.x, curve(peak) in m.y, ratio in m.z. */ \
	"MAX m.x, lin.x, lin.y;\n" \
	"MAX m.x, m.x, lin.z;\n" \
	"SUB t.x, m.x, program.env[1].y;\n" \
	"MUL t.x, t.x, program.env[1].z;\n" \
	"MAX t.x, t.x, 0.0;\n" \
	"MIN t.x, t.x, 1.0;\n" \
	"SUB u.x, 1.0, program.env[1].y;\n" \
	"MUL v.x, t.x, u.x;\n" \
	"ADD v.x, v.x, program.env[1].y;\n" \
	"SUB u.x, program.env[0].y, 1.0;\n" \
	"MUL a.x, t.x, t.x;\n" \
	"MAD v.x, a.x, u.x, v.x;\n" \
	"SUB a.x, m.x, 1.0;\n" \
	"MAX a.x, a.x, 0.0;\n" \
	"ADD v.x, v.x, a.x;\n" \
	"SUB a.x, m.x, program.env[1].y;\n" \
	"CMP m.y, a.x, m.x, v.x;\n" \
	"MAX m.z, m.x, 0.00001;\n" \
	"RCP m.z, m.z;\n" \
	"MUL m.w, m.y, m.z;\n" \
	"MUL r, lin, m.w;\n" \
	/* mode < 1.5 picks the per-channel form, else the peak form */ \
	"SUB a.x, program.env[1].x, 1.5;\n" \
	"CMP e, a.x, e, r;\n" \
	/* mode < 0.5, or no headroom at all, keeps the input untouched */ \
	"SUB a.x, program.env[1].x, 0.5;\n" \
	"CMP e, a.x, lin, e;\n" \
	"SUB a.x, program.env[0].y, 1.0001;\n" \
	"CMP lin, a.x, lin, e;\n" \
	/* ---- rolloff(), one curve, chosen when the program is built ---- */ \
	/* The curve is a build-time choice rather than a runtime select.
	   ARB has no branches, so a CMP chain over eight curves would
	   evaluate all eight for every pixel - transcendentals included -
	   and throw seven away.  The composite is rebuilt when the option
	   changes instead, which costs a program load on a menu toggle and
	   nothing per pixel.  This also drops the cost below what it was
	   with four curves chained. */ \
	/* the curve macro is spliced in here at build time */

/* The shoulder that carries a tone curve's output up into the display
   headroom.  Separate from the encode below because a complete output
   transform - ACES 2.0 - already decides its own top end, and putting
   this on top of one would compress a range that has already been
   compressed. */
#define HDR_ARB_COMPOSITE_SHOULDER \
	/* shoulder above paper white, carrying the curve out to H:
	   1 + e*A*slope/(e + A), taken where e > 0 */ \
	"SUB v, lin, 1.0;\n" \
	"MAX v, v, 0.0;\n" \
	"ADD u, v, program.env[7].x;\n" \
	"MAX u, u, 0.00001;\n" \
	"RCP a.x, u.x;\n" \
	"RCP a.y, u.y;\n" \
	"RCP a.z, u.z;\n" \
	"MUL u, v, program.env[7].x;\n" \
	"MUL u, u, program.env[7].y;\n" \
	"MAD u, u, a, 1.0;\n" \
	"SUB v, lin, 1.0;\n" \
	"CMP r, v, t, u;\n" \
	/* no headroom: plain min(v, 1) */ \
	"MIN t, lin, 1.0;\n" \
	"SUB v.x, program.env[0].y, 1.0001;\n" \
	"CMP lin, v.x, t, r;\n" \
	
#define HDR_ARB_COMPOSITE_TAIL2 \
	/* ---- gamut, scale, PQ ---- */ \
	"DP3 r.x, lin, program.env[4];\n" \
	"DP3 r.y, lin, program.env[5];\n" \
	"DP3 r.z, lin, program.env[6];\n" \
	"MUL_SAT y, r, program.env[0].x;\n" \
	"POW p.x, y.x, kPqA.x;\n" \
	"POW p.y, y.y, kPqA.x;\n" \
	"POW p.z, y.z, kPqA.x;\n" \
	"MAD t, p, 18.8515625, 0.8359375;\n" \
	"MAD u, p, 18.6875, 1.0;\n" \
	"RCP a.x, u.x;\n" \
	"RCP a.y, u.y;\n" \
	"RCP a.z, u.z;\n" \
	"MUL t, t, a;\n" \
	"POW e.x, t.x, kPqB.x;\n" \
	"POW e.y, t.y, kPqB.x;\n" \
	"POW e.z, t.z, kPqB.x;\n" \
	/* ---- interleaved gradient noise, per channel, rotated by frame ---- */ \
	"MUL n.x, fragment.position.x, 0.06711056;\n" \
	"MAD n.x, fragment.position.y, 0.00583715, n.x;\n" \
	"FRC n.x, n.x;\n" \
	"MUL n.x, n.x, 52.9829189;\n" \
	"FRC n.x, n.x;\n" \
	"MAD d.x, program.env[3].x, program.env[3].y, n.x;\n" \
	"ADD t.x, program.env[3].x, 1.0;\n" \
	"MAD d.y, t.x, program.env[3].y, n.x;\n" \
	"ADD t.x, program.env[3].x, 2.0;\n" \
	"MAD d.z, t.x, program.env[3].y, n.x;\n" \
	"FRC d, d;\n" \
	"SUB d, d, 0.5;\n" \
	"MAD_SAT e, d, program.env[3].z, e;\n" \
	"MOV e.w, 1.0;\n" \
	"MOV result.color, e;\n" \
	"END\n"

/* One band: the second bloom fetch is skipped entirely rather than
   multiplied by a zero weight.  The GLSL does this with a branch on a
   uniform; ARB has no branches, so it is two programs and the choice
   moves to bind time. */
/* The composite is assembled rather than declared, because the roll-off
   curve is chosen when the program is built.  Everything else is the
   same text as before; only the curve block varies. */
static const char *hdr_arb_curve_src( int mode ) {
	switch ( mode ) {
	case HDR_ROLLOFF_ACES:   return HDR_ARB_CURVE_ACES;
	case HDR_ROLLOFF_HEJL:   return HDR_ARB_CURVE_HEJL;
	case HDR_ROLLOFF_GT:     return HDR_ARB_CURVE_GT;
	case HDR_ROLLOFF_HABLE:  return HDR_ARB_CURVE_HABLE;
	case HDR_ROLLOFF_LOTTES: return HDR_ARB_CURVE_LOTTES;
	case HDR_ROLLOFF_DRAGO:  return HDR_ARB_CURVE_DRAGO;
	case HDR_ROLLOFF_UNREAL: return HDR_ARB_CURVE_UNREAL;
	case HDR_ROLLOFF_FILMICALU: return HDR_ARB_CURVE_FILMICALU;
	case HDR_ROLLOFF_JODIE:     return HDR_ARB_CURVE_JODIE;
	case HDR_ROLLOFF_ACESFIT:   return HDR_ARB_CURVE_ACESFIT;
	case HDR_ROLLOFF_NEUTRAL:   return HDR_ARB_CURVE_NEUTRAL;
	case HDR_ROLLOFF_AGX:       return HDR_ARB_CURVE_AGX;
	case HDR_ROLLOFF_ACES2FULL: return HDR_ARB_CURVE_ACES2FULL;
	case HDR_ROLLOFF_RPLAIN:    return HDR_ARB_CURVE_RPLAIN;
	case HDR_ROLLOFF_REXT:      return HDR_ARB_CURVE_REXT;
	case HDR_ROLLOFF_EXPO:      return HDR_ARB_CURVE_EXPO;
	case HDR_ROLLOFF_HABLE2017: return HDR_ARB_CURVE_HABLE2017;
	case HDR_ROLLOFF_ACES2:     return HDR_ARB_CURVE_ACES2;
	case HDR_ROLLOFF_TUMBLIN:   return HDR_ARB_CURVE_TUMBLIN;
	case HDR_ROLLOFF_WARD:      return HDR_ARB_CURVE_WARD;
	case HDR_ROLLOFF_SCHLICK:   return HDR_ARB_CURVE_SCHLICK;
	case HDR_ROLLOFF_DEVLIN:    return HDR_ARB_CURVE_DEVLIN;
	case HDR_ROLLOFF_FILMICLOG: return HDR_ARB_CURVE_FILMICLOG;
	default:                 return HDR_ARB_CURVE_REINHARD;
	}
}

static const char *hdr_arb_composite_src( int mode, bool twoBand ) {
	static char buf[2][16384];
	char *p = buf[twoBand ? 1 : 0];

	idStr::snPrintf( p, sizeof( buf[0] ), "%s%s%s%s%s%s%s%s",
			"!!ARBfp1.0\n"
			"OPTION ARB_precision_hint_nicest;\n",
			/* ACES 2.0 brings its own registers; every other curve uses
			 * the composite's, and charging them all for registers only
			 * one mode needs would push the rest toward the limits for
			 * nothing. */
			mode == HDR_ROLLOFF_ACES2FULL ? ACES2_ARB_DECLS : "",
			HDR_ARB_COMPOSITE_BODY
			"TEX bl, fragment.texcoord[0], texture[1], 2D;\n"
			"MUL bl, bl, program.env[2].z;\n",
			twoBand ? "TEX t, fragment.texcoord[0], texture[2], 2D;\n"
					  "MAD bl, t, program.env[2].w, bl;\n" : "",
			HDR_ARB_COMPOSITE_TAIL,
			hdr_arb_curve_src( mode ),
			mode == HDR_ROLLOFF_ACES2FULL ? "" : HDR_ARB_COMPOSITE_SHOULDER,
			HDR_ARB_COMPOSITE_TAIL2,
			"" );
	return p;
}

static const char *hdr_arb_bright_fs_src =
	"!!ARBfp1.0\n"
	"OPTION ARB_precision_hint_nicest;\n"
	/* env[0] = (threshold, knee, encodeScale, 1/(1 - threshold))
	   env[1] = (texelX, texelY, 0, 0)
	   The reciprocals are computed on the CPU: 1/(1-uThresh) and
	   1/(4*uKnee) are constant for the whole pass, and RCP here would
	   pay for them per fragment. */
	"PARAM p = program.env[0];\n"
	"PARAM q = program.env[1];\n"
	"PARAM texel = program.env[2];\n"
	"PARAM luma = { 0.2126, 0.7152, 0.0722, 0.0 };\n"
	"PARAM kSrgb = { 2.4, 2.4, 2.4, 2.4 };\n"
	"TEMP uv, s, lin, lum, sk, t, b, l;\n"
	/* four-tap box, matching the GLSL's tap positions */
	"MAD uv, texel, { -1.0, -1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX s, uv, texture[0], 2D;\n"
	"MAD uv, texel, {  1.0, -1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD s, s, t;\n"
	"MAD uv, texel, { -1.0,  1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD s, s, t;\n"
	"MAD uv, texel, {  1.0,  1.0, 0.0, 0.0 }, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"ADD s, s, t;\n"
	/* lin = pow(abs(0.25 * sum * encScale), 2.4) - abs() for the same
	   reason the GLSL uses it: POW with a negative base is undefined and
	   at least one driver returns nonsense rather than clamping. */
	"MUL s, s, 0.25;\n"
	"MUL s, s, p.z;\n"
	"ABS s, s;\n"
	"POW lin.x, s.x, kSrgb.x;\n"
	"POW lin.y, s.y, kSrgb.x;\n"
	"POW lin.z, s.z, kSrgb.x;\n"
	"DP3 lum.x, lin, luma;\n"
	/* sk = clamp(lum - thresh + knee, 0, 2*knee); sk = sk*sk * (1/(4*knee)) */
	"SUB sk.x, lum.x, p.x;\n"
	"ADD sk.x, sk.x, p.y;\n"
	"MAX sk.x, sk.x, 0.0;\n"
	"ADD t.x, p.y, p.y;\n"
	"MIN sk.x, sk.x, t.x;\n"
	"MUL sk.x, sk.x, sk.x;\n"
	"MUL sk.x, sk.x, q.y;\n"
	/* b = lin * (max(sk, lum - thresh) / max(lum, 1e-5)) * (1/(1 - thresh)) */
	"SUB t.x, lum.x, p.x;\n"
	"MAX t.x, sk.x, t.x;\n"
	"MAX l.x, lum.x, 0.00001;\n"
	"RCP l.x, l.x;\n"
	"MUL t.x, t.x, l.x;\n"
	"MUL t.x, t.x, p.w;\n"
	"MUL b, lin, t.x;\n"
	/* out = b / (1 + luminance(b)) */
	"DP3 l.x, b, luma;\n"
	"ADD l.x, l.x, 1.0;\n"
	"RCP l.x, l.x;\n"
	"MUL b, b, l.x;\n"
	"MOV b.w, 1.0;\n"
	"MOV result.color, b;\n"
	"END\n";

static const char *hdr_arb_blur_fs_src =
	"!!ARBfp1.0\n"
	"OPTION ARB_precision_hint_nicest;\n"
	/* env[0].xy = direction scaled by texel size, or (0,0) for a copy */
	"PARAM d = program.env[0];\n"
	"TEMP uv, c, t;\n"
	"TEX c, fragment.texcoord[0], texture[0], 2D;\n"
	"MUL c, c, 0.24458;\n"
	"MAD uv, d, 1.36268, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"MAD c, t, 0.31802, c;\n"
	"MAD uv, d, -1.36268, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"MAD c, t, 0.31802, c;\n"
	"MAD uv, d, 3.21159, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"MAD c, t, 0.05717, c;\n"
	"MAD uv, d, -3.21159, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"MAD c, t, 0.05717, c;\n"
	"MAD uv, d, 5.11234, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"MAD c, t, 0.00251, c;\n"
	"MAD uv, d, -5.11234, fragment.texcoord[0];\n"
	"TEX t, uv, texture[0], 2D;\n"
	"MAD c, t, 0.00251, c;\n"
	"MOV c.w, 1.0;\n"
	"MOV result.color, c;\n"
	"END\n";
#endif /* !HAVE_OPENGLES */

#ifdef HAVE_OPENGLES
static const char *hdr_bright_fs_src =
	"#ifdef GL_ES\nprecision highp float;\n#endif\n"
	"varying vec2 vUV;\n"
	"uniform sampler2D uScene;\n"
	"uniform float uThresh;\n"
	"uniform float uKnee;\n"
	/* Decode with a pure 2.4 power, matching RetroArch.

	   The frontend converts SDR core output for an HDR swapchain with
	   pow(abs(sdr.rgb), 2.4) - identically in hdr.frag and
	   hdr_sm5.hlsl.h - and re-encodes with 1/2.4. That is also what
	   BT.1886 specifies, so it is what an HDR display applies to the
	   24-bit signal this mode is meant to be compared against.

	   The inverse-sRGB curve that used to be here differs from it
	   almost entirely in the toe: sRGB is linear below 0.04045, a much
	   shallower approach to black than any power function, so decoded
	   shadows came out 18.5x too bright at code 0.02, 5.2x at 0.05 and
	   2.5x at 0.10, converging by mid grey (1.13x at 0.50) and exact at
	   1.0. Paper white is unaffected either way - only the shape below
	   it changes. On a game built around its shadows that was the most
	   visible remaining difference between 30-bit and 24-bit output,
	   and it made boom3 inconsistent with every other core the frontend
	   renders.

	   abs() rather than max(): parity with the frontend, and the FP16
	   epilogue already clamps its low side. */
	"vec3 srgbToLinear(vec3 c) {\n"
	"  return pow(abs(c), vec3(2.4));\n"
	"}\n"
	"uniform float uEncScale;\n"
	"uniform vec2 uTexel;\n"
	"void main() {\n"
	"  vec3 s0 = texture2D(uScene, vUV + uTexel * vec2(-1.0, -1.0)).rgb;\n"
	"  vec3 s1 = texture2D(uScene, vUV + uTexel * vec2( 1.0, -1.0)).rgb;\n"
	"  vec3 s2 = texture2D(uScene, vUV + uTexel * vec2(-1.0,  1.0)).rgb;\n"
	"  vec3 s3 = texture2D(uScene, vUV + uTexel * vec2( 1.0,  1.0)).rgb;\n"
	"  vec3 lin = srgbToLinear(0.25 * (s0 + s1 + s2 + s3) * uEncScale);\n"
	/* Soft knee instead of a hard threshold.

	   max(lin - t, 0) has a discontinuous derivative at t, so a
	   highlight drifting sub-pixel makes texels cross the threshold
	   and their contribution appears and vanishes between frames. On a
	   saturated source drifting an eighth of a pixel per frame, total
	   extracted energy swung 11% (r=5px) to 20% (r=6px) frame to
	   frame. That is the bloom crawling and pulsing on motion.

	   The knee ramps quadratically across +-uKnee around the
	   threshold, so a texel fades in rather than popping, and rejoins
	   max(l - t, 0) exactly above it. At the retuned operating point
	   the same test gives 6.9% / 8.4% / 2.4% against 11.1% / 20.4% /
	   3.4%, at matched total bloom energy.

	   Weighted on luma, not per channel, so the knee cannot shift hue
	   as a highlight fades in. */
	"  float lum = dot(lin, vec3(0.2126, 0.7152, 0.0722));\n"
	"  float sk = clamp(lum - uThresh + uKnee, 0.0, 2.0 * uKnee);\n"
	"  sk = sk * sk / (4.0 * uKnee);\n"
	"  vec3 b = lin * (max(sk, lum - uThresh) / max(lum, 1e-5)) / (1.0 - uThresh);\n"
	/* Reinhard firefly limiter. Keep it, and do not "simplify" it away
	   after noticing it never fires in 10-bit mode - that observation
	   is correct and the conclusion from it is wrong.

	   In 10-bit mode a scene texel saturates at 1.0, so one blazing
	   texel inside the 4x4 block the taps above average over decodes
	   to 0.022 against a threshold of 0.70. A single-texel firefly
	   cannot reach extraction at all there; the box filter already
	   killed it.

	   With an FP16/FP32 scene and unbounded blending there is no such
	   cap, and it fires exactly as intended: a texel at 50x paper
	   white extracts 2.95 with the limiter against 164 without, a 55x
	   suppression of a single sparkling pixel.

	   Removing it and raising the threshold to compensate also fails
	   on its own terms - energy matches but crawl on mid-size
	   highlights goes from 6.9%% to 20-50%%, because the limiter is
	   damping the same near-threshold swing the knee above targets. */
	"  float l = dot(b, vec3(0.2126, 0.7152, 0.0722));\n"
	"  gl_FragColor = vec4(b / (1.0 + l), 1.0);\n"
	"}\n";
#endif /* HAVE_OPENGLES */

/*
   Separable Gaussian, 5 bilinear fetches for a 9-tap kernel
   (offsets/weights are the classic linear-sampling optimization,
   renormalized to sum exactly 1 so a (0,0) direction acts as a pure
   downsample copy - which is how the wide band is seeded from the
   tight band without a dedicated copy program).
*/
/*
   Convolution bloom, downsample stage. The 13-tap filter from Jimenez's
   SIGGRAPH 2014 course notes (the one Call of Duty: Advanced Warfare
   used): a centre tap, a ring of four at +-1 texel and a ring of eight
   at +-2, weights summing to exactly 1. The overlapping inner quad is
   what keeps successive halvings from aliasing the way a plain box
   would - and aliasing at the top of the chain is what shows up as
   bloom crawling when the camera moves.

   uTexel is the SOURCE texel size, so the offsets are in source texels
   while the viewport is the half-size destination.
*/
#ifdef HAVE_OPENGLES
static const char *hdr_down_fs_src =
	"#ifdef GL_ES\nprecision highp float;\n#endif\n"
	"varying vec2 vUV;\n"
	"uniform sampler2D uScene;\n"
	"uniform vec2 uTexel;\n"
	"void main() {\n"
	"  vec3 a = texture2D(uScene, vUV + uTexel * vec2(-2.0,  2.0)).rgb;\n"
	"  vec3 b = texture2D(uScene, vUV + uTexel * vec2( 0.0,  2.0)).rgb;\n"
	"  vec3 c = texture2D(uScene, vUV + uTexel * vec2( 2.0,  2.0)).rgb;\n"
	"  vec3 d = texture2D(uScene, vUV + uTexel * vec2(-2.0,  0.0)).rgb;\n"
	"  vec3 e = texture2D(uScene, vUV).rgb;\n"
	"  vec3 f = texture2D(uScene, vUV + uTexel * vec2( 2.0,  0.0)).rgb;\n"
	"  vec3 g = texture2D(uScene, vUV + uTexel * vec2(-2.0, -2.0)).rgb;\n"
	"  vec3 h = texture2D(uScene, vUV + uTexel * vec2( 0.0, -2.0)).rgb;\n"
	"  vec3 i = texture2D(uScene, vUV + uTexel * vec2( 2.0, -2.0)).rgb;\n"
	"  vec3 j = texture2D(uScene, vUV + uTexel * vec2(-1.0,  1.0)).rgb;\n"
	"  vec3 k = texture2D(uScene, vUV + uTexel * vec2( 1.0,  1.0)).rgb;\n"
	"  vec3 l = texture2D(uScene, vUV + uTexel * vec2(-1.0, -1.0)).rgb;\n"
	"  vec3 m = texture2D(uScene, vUV + uTexel * vec2( 1.0, -1.0)).rgb;\n"
	"  vec3 o = e * 0.125;\n"
	"  o += (a + c + g + i) * 0.03125;\n"
	"  o += (b + d + f + h) * 0.0625;\n"
	"  o += (j + k + l + m) * 0.125;\n"
	"  gl_FragColor = vec4(o, 1.0);\n"
	"}\n";
#endif /* HAVE_OPENGLES */

/*
   Convolution bloom, upsample stage: a 3x3 tent, weights 1/2/1 2/4/2
   1/2/1 over 16, blended additively into the next larger level.

   A tent rather than the hardware's bilinear tap because a single
   bilinear fetch magnified 2x leaves the low-resolution grid visible as
   diamond-shaped creasing, and that creasing crawls under motion. The
   tent is what makes the accumulated pyramid read as one smooth falloff
   instead of a stack of resampled buffers.

   uRadius scales the tent in source texels. Above 1.0 the levels
   overlap more and the glow spreads wider and softer.
*/
#ifdef HAVE_OPENGLES
static const char *hdr_up_fs_src =
	"#ifdef GL_ES\nprecision highp float;\n#endif\n"
	"varying vec2 vUV;\n"
	"uniform sampler2D uScene;\n"
	"uniform vec2 uTexel;\n"
	"uniform float uRadius;\n"
	"void main() {\n"
	"  vec2 o = uTexel * uRadius;\n"
	"  vec3 s = texture2D(uScene, vUV + vec2(-o.x, -o.y)).rgb;\n"
	"  s += texture2D(uScene, vUV + vec2( 0.0, -o.y)).rgb * 2.0;\n"
	"  s += texture2D(uScene, vUV + vec2( o.x, -o.y)).rgb;\n"
	"  s += texture2D(uScene, vUV + vec2(-o.x,  0.0)).rgb * 2.0;\n"
	"  s += texture2D(uScene, vUV).rgb * 4.0;\n"
	"  s += texture2D(uScene, vUV + vec2( o.x,  0.0)).rgb * 2.0;\n"
	"  s += texture2D(uScene, vUV + vec2(-o.x,  o.y)).rgb;\n"
	"  s += texture2D(uScene, vUV + vec2( 0.0,  o.y)).rgb * 2.0;\n"
	"  s += texture2D(uScene, vUV + vec2( o.x,  o.y)).rgb;\n"
	"  gl_FragColor = vec4(s * 0.0625, 1.0);\n"
	"}\n";
#endif /* HAVE_OPENGLES */

#ifdef HAVE_OPENGLES
static const char *hdr_blur_fs_src =
	"#ifdef GL_ES\nprecision highp float;\n#endif\n"
	"varying vec2 vUV;\n"
	"uniform sampler2D uScene;\n"
	"uniform vec2 uDir;\n"   /* texel-size-scaled direction, or 0,0 for copy */
	/* 13-tap Gaussian (sigma 1.631 texels) as 7 bilinear fetches.

	   This was 9 taps, which truncates at 2.45 sigma - where the
	   kernel still carries 4.95% of its centre weight. Convolving
	   anything with a kernel that has a 4.95% discontinuity at its
	   edge leaves a step in the output at exactly that radius, and
	   the glow drops to hard zero past it.

	   In SDR that step is invisible: the source clamps at 1.0, so the
	   rim is at most 0.05. In HDR the source is many multiples of
	   paper white, so the same 4.95% is 0.25x paper white at a 5x
	   source and 2.5x at a 50x source - a bright, hard-edged ring.
	   That is why this reads as an HDR bloom artifact specifically.

	   Confirmed against a 4K capture: the glow around a steam vent
	   fell to exactly 0.0000 at 52 screen pixels from the source. The
	   outermost tap of the wide band sits at 3.23077 texels of a
	   1/16-res buffer, which at 3840 wide is 51.7 pixels.

	   Extending to 13 taps moves the cut to 3.68 sigma, where the
	   weight is 0.115% of centre, so the tail decays 0.44 -> 0.076 ->
	   0.009 -> 0.001 instead of 0.36 -> 0. Two extra fetches per pass
	   on quarter- and sixteenth-res buffers. Same sigma, so the glow
	   radius and total energy are unchanged; only the hard edge goes.

	   Note this does not make the falloff wide or gentle - measured,
	   the tail shape past 13 taps is the Gaussian's own steepness and
	   does not improve with more taps. Bloom that carries far past its
	   source needs more octaves, not a longer kernel. */
	"void main() {\n"
	"  vec3 c = texture2D(uScene, vUV).rgb * 0.24458;\n"
	"  c += texture2D(uScene, vUV + uDir * 1.36268).rgb * 0.31802;\n"
	"  c += texture2D(uScene, vUV - uDir * 1.36268).rgb * 0.31802;\n"
	"  c += texture2D(uScene, vUV + uDir * 3.21159).rgb * 0.05717;\n"
	"  c += texture2D(uScene, vUV - uDir * 3.21159).rgb * 0.05717;\n"
	"  c += texture2D(uScene, vUV + uDir * 5.11234).rgb * 0.00251;\n"
	"  c += texture2D(uScene, vUV - uDir * 5.11234).rgb * 0.00251;\n"
	"  gl_FragColor = vec4(c, 1.0);\n"
	"}\n";
#endif /* HAVE_OPENGLES */

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

/*
   Give up on the HDR pass for this session.

   Without this, a shader that fails to build leaves hdr_prog at 0 and
   hdr_ensure_target returning false, so hdr_bind_scene never binds the
   scene FBO and the engine draws straight into the frontend's HDR10
   framebuffer - with hdr_output_active still true, so the encoding fold
   and the specular boost stay on while nothing ever expands or
   PQ-encodes them. That is sRGB code values on a PQ swapchain, the
   exact washed-out-and-dim failure this module exists to prevent, plus
   a full shader recompile attempt every frame forever.

   Turning hdr_output_active off instead makes the fold and the boost
   identity, so the frame is at least internally consistent - still
   tone-shifted, because the pixel format was fixed at load and cannot
   change mid-session, but viewable and with a log line saying why. Same
   handling the FBO-incomplete path already had.
*/
static void hdr_fail( const char *why ) {
	if ( log_cb )
		log_cb( RETRO_LOG_ERROR,
				"[boom3] HDR: %s; disabling the HDR pass. Switch Color Format "
				"to 24-bit for correct colors.\n", why );
	hdr_output_active = false;
}

#ifndef HAVE_OPENGLES
/*
===========================================================================
ARB program loading for the HDR chain.

The composite and bloom passes are GLSL today, which means a desktop
build running the ARB2 renderer compiles GLSL for its post chain and ARB
assembly for everything else.  This is the loader for the ARB side of
that split; the programs themselves land in following commits, and the
GLSL sources stay until they do.

There is deliberately no GLSL fallback.  The ARB2 renderer cannot draw a
single interaction without GL_ARB_fragment_program, so a driver that
cannot give us an ARB program here has already failed something much
larger - and a fallback would only ever cover a mistake in our own
program text, while keeping GLSL compiled into every desktop build to do
it.  A refusal drops to the 24-bit path, which is the pre-existing,
well-travelled one.

The check that matters is not "did it compile".  ARB has two tiers of
limits: the virtual ones a program is validated against, and the native
ones the hardware can actually run.  A program over the native limits
loads cleanly, sets no error, and then executes in software - which on a
full-screen pass reads as the core hanging rather than as a bug.  So
GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB is queried explicitly and treated as
a hard failure.
===========================================================================
*/
static bool hdr_arb_available( void ) {
	return glConfig.ARBFragmentProgramAvailable
			&& glConfig.ARBVertexProgramAvailable
			&& qglProgramStringARB != NULL
			&& qglGenProgramsARB != NULL
			&& qglBindProgramARB != NULL
			&& qglProgramEnvParameter4fvARB != NULL;
}

/*
   Loads the whole ARB chain, once, and only reports ready when every
   program in it is up.

   This is one function rather than a load beside each pass because the
   first attempt at it was not: the composite loaded in one place and
   set the "ARB path is live" flag, the pyramid loaded in another and
   was guarded on that same flag being clear.  Whichever ran first
   locked the other out, so the flag said the chain was on ARB while
   three of its five programs were still zero.  Binding program 0 is
   not an error in GL, it just draws with no program - so nothing
   failed, nothing logged, and every live HDR option looked inert
   because the pass consuming their parameters was not the pass
   drawing.
*/
static bool hdr_arb_ready( void );

/* ---- ACES 2.0 resources ----
 *
 * The three hue tables live in one RGBA16 texture on unit 3, and the
 * transform's constants in env registers 10 upward.  Both are rebuilt
 * only when the peak luminance changes, which is a core option rather
 * than a per-frame quantity.
 */
/* Only the vector form of the env setter is resolved in this build, so
 * the four scalars go through a local. */
#define ACES2_ENV( reg, x, y, z, w ) do { \
		const float envv[4] = { (x), (y), (z), (w) }; \
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, (reg), envv ); \
	} while ( 0 )

static GLuint hdr_aces2_lut;
static float  hdr_aces2_peak;
static double hdr_aces2_scales[4];
static aces2Params_t *hdr_aces2_params;

static void hdr_aces2_free( void ) {
	if ( hdr_aces2_lut ) {
		glDeleteTextures( 1, &hdr_aces2_lut );
		hdr_aces2_lut = 0;
	}
	if ( hdr_aces2_params ) {
		free( hdr_aces2_params );
		hdr_aces2_params = NULL;
	}
	hdr_aces2_peak = 0.0f;
}

static bool hdr_aces2_build( float peakLuminance ) {
	/* Rec.2020 - the transform lands directly in the composite's output
	 * primaries, which is what lets the gamut matrix after it be fed
	 * identity rows instead of rotating an already-finished colour. */
	static const double rec2020[8] = {
		0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.3290 };
	static unsigned short lut[ACES2_LUT_WIDTH * 4];

	if ( hdr_aces2_lut && hdr_aces2_peak == peakLuminance ) {
		return true;
	}
	hdr_aces2_free();

	hdr_aces2_params = (aces2Params_t *)calloc( 1, sizeof( aces2Params_t ) );
	if ( !hdr_aces2_params ) {
		return false;
	}
	ACES2_Init( rec2020, peakLuminance, hdr_aces2_params );
	ACES2_PackHueTables( hdr_aces2_params, lut, hdr_aces2_scales );

	glGenTextures( 1, &hdr_aces2_lut );
	glActiveTexture( GL_TEXTURE3 );
	glBindTexture( GL_TEXTURE_2D, hdr_aces2_lut );
	while ( glGetError() != GL_NO_ERROR ) {
	}
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16, ACES2_LUT_WIDTH, 1, 0,
				  GL_RGBA, GL_UNSIGNED_SHORT, lut );
	if ( glGetError() != GL_NO_ERROR ) {
		/* no normalised 16-bit texture here - GLES in particular.  The
		 * caller falls back to the tone scale rather than rendering
		 * through a table that never arrived. */
		hdr_aces2_free();
		glActiveTexture( GL_TEXTURE0 );
		return false;
	}
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glActiveTexture( GL_TEXTURE0 );

	hdr_aces2_peak = peakLuminance;
	return true;
}

/* The appearance matrices are applied on the CPU as row-vector times
 * matrix, so DP3 wants their COLUMNS; the AP0 to AP1 pair are applied
 * the other way, so those want their rows.  Getting this backwards
 * costs a picture that looks merely wrong rather than broken. */
static void hdr_aces2_env_col( int reg, const double m[9], int col ) {
	ACES2_ENV( reg,
		(float)m[0*3+col], (float)m[1*3+col], (float)m[2*3+col], 0.0f );
}

static void hdr_aces2_env_row( int reg, const double m[9], int row ) {
	ACES2_ENV( reg,
		(float)m[row*3+0], (float)m[row*3+1], (float)m[row*3+2], 0.0f );
}

static void hdr_aces2_set_env( void ) {
	static const double AP0_TO_AP1[9] = {
		 1.4514393161, -0.2365107469, -0.2149285693,
		-0.0765537733,  1.1762296998, -0.0996759265,
		 0.0083161484, -0.0060324498,  0.9977163014 };
	static const double AP1_TO_AP0[9] = {
		 0.6954522414,  0.1406786965,  0.1638690622,
		 0.0447945634,  0.8596711185,  0.0955343182,
		-0.0055258826,  0.0040252103,  1.0015006723 };
	const aces2Params_t *p = hdr_aces2_params;
	int r;

	if ( !p ) {
		return;
	}
	for ( r = 0; r < 3; r++ ) {
		hdr_aces2_env_col( 10 + r, p->input.RGB_to_CAM16_c, r );
		hdr_aces2_env_col( 13 + r, p->input.cone_to_Aab, r );
		hdr_aces2_env_col( 16 + r, p->limit.Aab_to_cone, r );
		hdr_aces2_env_col( 19 + r, p->limit.CAM16_c_to_RGB, r );
		hdr_aces2_env_row( 29 + r, AP0_TO_AP1, r );
		hdr_aces2_env_row( 32 + r, AP1_TO_AP0, r );
	}
	ACES2_ENV( 22,
		(float)p->input.F_L_n, (float)p->input.cz, (float)p->input.inv_cz, (float)p->input.A_w_J );
	ACES2_ENV( 23,
		(float)p->limit.F_L_n, (float)p->limit.cz, (float)p->limit.inv_cz, (float)p->limit.A_w_J );
	ACES2_ENV( 24,
		(float)p->ts.m_2, (float)p->ts.s_2, (float)p->ts.g, (float)p->ts.t_1 );
	ACES2_ENV( 25,
		(float)p->chroma.limit_J_max, (float)p->chroma.model_gamma_inv,
		(float)p->chroma.sat, (float)p->chroma.sat_thr );
	ACES2_ENV( 26,
		(float)p->chroma.compr, (float)p->chroma.chroma_compress_scale,
		(float)p->gamut.mid_J, (float)p->gamut.focus_dist );
	ACES2_ENV( 27,
		(float)p->gamut.lower_hull_gamma_inv, (float)p->ts.forward_limit,
		/* half a texel, so the fetch lands on a texel centre */
		0.5f / (float)ACES2_LUT_WIDTH, 0.0f );
	ACES2_ENV( 28,
		(float)hdr_aces2_scales[0], (float)hdr_aces2_scales[1],
		(float)hdr_aces2_scales[2], (float)hdr_aces2_scales[3] );
}

static bool hdr_arb_load( GLenum target, const char *text, GLuint *out, const char *what ) {
	GLuint id = 0;
	GLint  ofs = -1;
	GLint  native = GL_TRUE;

	*out = 0;

	qglGenProgramsARB( 1, &id );
	if ( id == 0 ) {
		hdr_fail( "could not allocate an ARB program id" );
		return false;
	}

	qglBindProgramARB( target, id );
	qglGetError();
	qglProgramStringARB( target, GL_PROGRAM_FORMAT_ASCII_ARB,
			(GLsizei)strlen( text ), text );

	if ( qglGetError() == GL_INVALID_OPERATION ) {
		const GLubyte *err = qglGetString( GL_PROGRAM_ERROR_STRING_ARB );
		qglGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, &ofs );
		if ( log_cb ) {
			log_cb( RETRO_LOG_ERROR, "[boom3] HDR: %s rejected at %d: %s\n",
					what, (int)ofs, err ? (const char *)err : "(no message)" );
		}
		hdr_fail( "an ARB program did not load" );
		return false;
	}

	/* Loaded, no error - but that only means it fits the virtual limits. */
	glGetProgramivARB( target, GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB, &native );
	if ( native != GL_TRUE ) {
		GLint alu = 0, tex = 0, tmp = 0;
		glGetProgramivARB( target, GL_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB, &alu );
		glGetProgramivARB( target, GL_PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB, &tex );
		glGetProgramivARB( target, GL_PROGRAM_NATIVE_TEMPORARIES_ARB, &tmp );
		if ( log_cb ) {
			log_cb( RETRO_LOG_ERROR,
					"[boom3] HDR: %s exceeds this GPU's native limits "
					"(%d ALU, %d TEX, %d temporaries) and would run in "
					"software\n", what, (int)alu, (int)tex, (int)tmp );
		}
		hdr_fail( "an ARB program would not run in hardware" );
		return false;
	}

	*out = id;
	return true;
}

static bool hdr_arb_ready( void ) {
	if ( hdr_arb_pyramid && hdr_arb_comp_mode == hdr_rolloff_mode ) {
		return true;
	}

	if ( !hdr_arb_available() ) {
		/* Nothing to load onto and, on this build, no other chain to
		   fall back to - reporting ready would leave hdr_present
		   bailing on its first line every frame, which looks like the
		   scene rendering without its conversion pass: HUD and the
		   brightest highlights survive, everything mid-toned crushes
		   to black.  Say so instead. */
		hdr_fail( "no ARB program support on this context" );
		return false;
	}

	/* The curveless programs are loaded once. */
	if ( !hdr_arb_pyramid ) {
		if ( !hdr_arb_load( GL_VERTEX_PROGRAM_ARB, hdr_arb_vp_src,
					&hdr_arb_vp, "hdr vertex program" )
				|| !hdr_arb_load( GL_FRAGMENT_PROGRAM_ARB, hdr_arb_bright_fs_src,
					&hdr_arb_bright, "bloom bright-pass program" )
				|| !hdr_arb_load( GL_FRAGMENT_PROGRAM_ARB, hdr_arb_blur_fs_src,
					&hdr_arb_blur, "bloom blur program" )
				|| !hdr_arb_load( GL_FRAGMENT_PROGRAM_ARB, hdr_arb_down_fs_src,
					&hdr_arb_down, "bloom downsample program" )
				|| !hdr_arb_load( GL_FRAGMENT_PROGRAM_ARB, hdr_arb_up_fs_src,
					&hdr_arb_up, "bloom upsample program" ) ) {
			return false;
		}
	}

	/* The composite pair carries the roll-off curve, so it is rebuilt
	 * when the option changes.  The old pair is deleted first: without
	 * that, every toggle would strand two program objects, and the
	 * option is a menu item somebody will sit and cycle through. */
	if ( hdr_arb_comp_mode != hdr_rolloff_mode ) {
		if ( hdr_arb_comp1 ) {
			GLuint dead[2] = { hdr_arb_comp1, hdr_arb_comp2 };
			glDeleteProgramsARB( 2, dead );
			hdr_arb_comp1 = hdr_arb_comp2 = 0;
		}
		if ( !hdr_arb_load( GL_FRAGMENT_PROGRAM_ARB,
					hdr_arb_composite_src( hdr_rolloff_mode, false ),
					&hdr_arb_comp1, "hdr composite program (one band)" )
				|| !hdr_arb_load( GL_FRAGMENT_PROGRAM_ARB,
					hdr_arb_composite_src( hdr_rolloff_mode, true ),
					&hdr_arb_comp2, "hdr composite program (two bands)" ) ) {
			return false;
		}
		hdr_arb_comp_mode = hdr_rolloff_mode;
	}

	if ( !hdr_arb_pyramid && log_cb ) {
		log_cb( RETRO_LOG_INFO, "[boom3] HDR: post chain on ARB programs\n" );
	}
	hdr_arb_pyramid = true;
	return true;
}
#endif /* !HAVE_OPENGLES */


/* Levels in the convolution pyramid for a given scene size. Level 0 is
   1/4 res and each step halves. Stops before a level would drop under 4
   texels on either axis: upsampling from a 2x1 buffer contributes a flat
   wash rather than a glow, and the tent has nothing left to work with. */
static int hdr_conv_levels( int w, int h ) {
	int n = 0, bw = w / 4, bh = h / 4;
	while ( n < HDR_CONV_MAX && bw >= 4 && bh >= 4 ) {
		n++; bw >>= 1; bh >>= 1;
	}
	return n < 1 ? 1 : n;
}

/* (re)create GL objects for the current context and size; safe to call
   per frame, does work only on change. Context loss zeroes the ids. */
static bool hdr_ensure_target( int w, int h ) {
#ifndef HAVE_OPENGLES
	if ( hdr_rolloff_mode == HDR_ROLLOFF_ACES2FULL ) {
		/* Peak luminance the transform is solved for.  If the tables or
		 * the 16-bit texture cannot be built - GLES has no normalised
		 * 16-bit format - the mode falls back to the ACES 2.0 tone
		 * scale, which is a curve and needs neither. */
		if ( !hdr_aces2_build( hdr_aces2_wanted_peak > 0.0f ? hdr_aces2_wanted_peak : 1000.0f ) ) {
			if ( log_cb ) {
				log_cb( RETRO_LOG_WARN,
					"[boom3] HDR: ACES 2.0 tables unavailable, using the tone scale\n" );
			}
			hdr_rolloff_mode = HDR_ROLLOFF_ACES2;
		}
	}

	if ( !hdr_arb_ready() ) {
		return false;
	}
#endif
#ifdef HAVE_OPENGLES
	if ( hdr_prog == 0 ) {
		GLuint vs = hdr_compile( GL_VERTEX_SHADER, hdr_vs_src );
		GLuint fs = hdr_compile( GL_FRAGMENT_SHADER, hdr_fs_src );
		if ( !vs || !fs ) {
			if ( vs ) glDeleteShader( vs );
			if ( fs ) glDeleteShader( fs );
			hdr_fail( "composite shader did not compile" );
			return false;
		}
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
			hdr_fail( "composite program did not link" );
			return false;
		}
		hdr_loc_tex   = glGetUniformLocation( hdr_prog, "uScene" );
		hdr_loc_mat   = glGetUniformLocation( hdr_prog, "uGamut" );
		hdr_loc_parms = glGetUniformLocation( hdr_prog, "uParms" );
		hdr_loc_bloomT  = glGetUniformLocation( hdr_prog, "uBloomT" );
		hdr_loc_bloomW  = glGetUniformLocation( hdr_prog, "uBloomW" );
		hdr_loc_bloomAmt= glGetUniformLocation( hdr_prog, "uBloomAmt" );
		hdr_loc_bandW   = glGetUniformLocation( hdr_prog, "uBandW" );
		hdr_loc_encScale= glGetUniformLocation( hdr_prog, "uEncScale" );
		hdr_loc_frame   = glGetUniformLocation( hdr_prog, "uFrame" );
		hdr_loc_expand  = glGetUniformLocation( hdr_prog, "uExpand" );
	}
#endif /* HAVE_OPENGLES */
#ifdef HAVE_OPENGLES
	if ( hdr_prog_bright == 0 && !hdr_bloom_prog_bad ) {
		GLuint vs = hdr_compile( GL_VERTEX_SHADER, hdr_vs_src );
		GLuint fs = hdr_compile( GL_FRAGMENT_SHADER, hdr_bright_fs_src );
		GLuint fs2 = hdr_compile( GL_FRAGMENT_SHADER, hdr_blur_fs_src );
		if ( vs && fs && fs2 ) {
			GLint okB = 0, okL = 0;
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
			/*
			   Unlike hdr_prog these two never checked GL_LINK_STATUS.
			   On a link failure the ids are still non-zero, so
			   haveBloom stayed true, glGetUniformLocation returned -1,
			   and the bloom chain drew with an unlinked program -
			   undefined output composited into the frame at up to
			   1.7x amplitude. Bloom is optional, so a failure here
			   disables bloom rather than the whole HDR pass.
			*/
			glGetProgramiv( hdr_prog_bright, GL_LINK_STATUS, &okB );
			glGetProgramiv( hdr_prog_blur,   GL_LINK_STATUS, &okL );
			if ( !okB || !okL ) {
				glDeleteProgram( hdr_prog_bright );
				glDeleteProgram( hdr_prog_blur );
				hdr_prog_bright = hdr_prog_blur = 0;
				hdr_bloom_prog_bad = true;
				if ( log_cb )
					log_cb( RETRO_LOG_WARN, "[boom3] HDR: bloom program did not link; bloom disabled\n" );
			} else {
				hdr_bright_loc_thresh = glGetUniformLocation( hdr_prog_bright, "uThresh" );
				hdr_bright_loc_enc    = glGetUniformLocation( hdr_prog_bright, "uEncScale" );
				hdr_bright_loc_texel  = glGetUniformLocation( hdr_prog_bright, "uTexel" );
				hdr_bright_loc_knee   = glGetUniformLocation( hdr_prog_bright, "uKnee" );
				hdr_blur_loc_dir      = glGetUniformLocation( hdr_prog_blur, "uDir" );
			}
		} else {
			hdr_bloom_prog_bad = true;
			if ( log_cb )
				log_cb( RETRO_LOG_WARN, "[boom3] HDR: bloom shader did not compile; bloom disabled\n" );
		}
		if ( vs ) glDeleteShader( vs );
		if ( fs ) glDeleteShader( fs );
		if ( fs2 ) glDeleteShader( fs2 );
	}

	/* Convolution pyramid programs. Built alongside the two-band ones and
	   sharing their failure flag: if either fails the option cannot run,
	   and haveBloom already gates on hdr_bloom_prog_bad. */
	if ( hdr_prog_down == 0 && !hdr_bloom_prog_bad ) {
		GLuint vs  = hdr_compile( GL_VERTEX_SHADER, hdr_vs_src );
		GLuint fsd = hdr_compile( GL_FRAGMENT_SHADER, hdr_down_fs_src );
		GLuint fsu = hdr_compile( GL_FRAGMENT_SHADER, hdr_up_fs_src );
		if ( vs && fsd && fsu ) {
			GLint okD = 0, okU = 0;
			hdr_prog_down = glCreateProgram();
			glAttachShader( hdr_prog_down, vs );
			glAttachShader( hdr_prog_down, fsd );
			glBindAttribLocation( hdr_prog_down, 0, "aPos" );
			glLinkProgram( hdr_prog_down );
			hdr_prog_up = glCreateProgram();
			glAttachShader( hdr_prog_up, vs );
			glAttachShader( hdr_prog_up, fsu );
			glBindAttribLocation( hdr_prog_up, 0, "aPos" );
			glLinkProgram( hdr_prog_up );
			glGetProgramiv( hdr_prog_down, GL_LINK_STATUS, &okD );
			glGetProgramiv( hdr_prog_up,   GL_LINK_STATUS, &okU );
			if ( !okD || !okU ) {
				glDeleteProgram( hdr_prog_down );
				glDeleteProgram( hdr_prog_up );
				hdr_prog_down = hdr_prog_up = 0;
				hdr_bloom_prog_bad = true;
				if ( log_cb )
					log_cb( RETRO_LOG_WARN, "[boom3] HDR: convolution bloom program did not link; bloom disabled\n" );
			} else {
				hdr_down_loc_texel = glGetUniformLocation( hdr_prog_down, "uTexel" );
				hdr_up_loc_texel   = glGetUniformLocation( hdr_prog_up, "uTexel" );
				hdr_up_loc_radius  = glGetUniformLocation( hdr_prog_up, "uRadius" );
			}
		} else {
			hdr_bloom_prog_bad = true;
			if ( log_cb )
				log_cb( RETRO_LOG_WARN, "[boom3] HDR: convolution bloom shader did not compile; bloom disabled\n" );
		}
		if ( vs )  glDeleteShader( vs );
		if ( fsd ) glDeleteShader( fsd );
		if ( fsu ) glDeleteShader( fsu );
	}
#endif /* HAVE_OPENGLES */

	/* Everything from here down builds framebuffers and textures, which
	   both backends need.  It ended up inside the GLES-only region when
	   the GLSL program creation above it was gated: the desktop build
	   then had no scene target at all, hdr_fbo stayed zero, hdr_present
	   returned on its first line, and the screen was black with nothing
	   to log. */
	if ( hdr_fbo == 0 || w != hdr_w || h != hdr_h ) {
		if ( hdr_tex ) glDeleteTextures( 1, &hdr_tex );
		if ( hdr_rbo ) glDeleteRenderbuffers( 1, &hdr_rbo );
		if ( hdr_fbo ) glDeleteFramebuffers( 1, &hdr_fbo );
		glGenTextures( 1, &hdr_tex );
		glBindTexture( GL_TEXTURE_2D, hdr_tex );
		/*
		   Precision selection. RGB10_A2: 10-bit gamma-domain steps at
		   every additive light pass. RGBA16F: per-pass write
		   quantization effectively disappears (11-bit mantissa near
		   1.0, far denser near 0), and together with the unclamped
		   epilogue the accumulation ceiling disappears with it - no
		   encoding fold needed, no darks cost.
		*/
		if ( hdr_fp32_scene )
			glTexImage2D( GL_TEXTURE_2D, 0, 0x8814 /* GL_RGBA32F */, w, h, 0, GL_RGBA,
					GL_FLOAT, NULL );
		else if ( hdr_fp16_scene )
			glTexImage2D( GL_TEXTURE_2D, 0, 0x881A /* GL_RGBA16F */, w, h, 0, GL_RGBA,
					0x140B /* GL_HALF_FLOAT */, NULL );
		else
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB10_A2, w, h, 0, GL_RGBA,
					GL_UNSIGNED_INT_2_10_10_10_REV, NULL );
		/* the scene texture is sampled with bilinear taps by the bright
		   pass and 1:1 by the composite - LINEAR serves both */
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
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
			/* release what we just built: the ids are non-zero and
			   hdr_w/hdr_h are still stale, so leaving them behind
			   hands a later context-loss handler objects it will
			   either double-manage or leak outright */
			glBindFramebuffer( RARCH_GL_FRAMEBUFFER, 0 );
			glDeleteFramebuffers( 1, &hdr_fbo );
			glDeleteRenderbuffers( 1, &hdr_rbo );
			glDeleteTextures( 1, &hdr_tex );
			hdr_fbo = hdr_rbo = hdr_tex = 0;
			hdr_fail( "scene FBO incomplete" );
			return false;
		}
		/* bloom chain: [0,1] at 1/4 res, [2,3] at 1/16 */
		{
			/*
			   The bright pass writes LINEAR light here, so the target's
			   encoding matters more than its nominal bit depth.
			   RGB10_A2 is a UNORM: uniform absolute steps, therefore
			   poor relative precision exactly where bloom lives. After
			   the threshold subtract, the firefly clamp and two
			   Gaussians, typical values sit under 0.05, where 10-bit
			   linear leaves ~50 distinct levels - which the composite
			   then multiplies by up to 1.7 and adds, landing as
			   contouring in the haze around lamps.

			   R11F_G11F_B10F is the right target: same 32 bits per
			   texel as RGB10_A2, float encoding so the precision
			   follows the data, no alpha (the passes write 1.0 and
			   nothing reads it), core in GL 3.0 and GLES 3.0 and
			   colour-renderable in both.

			   Renderability is not guaranteed on every GLES3 driver
			   without EXT_color_buffer_float, so try it, check, and
			   fall back to the scene's own format - and if that fails
			   too, disable bloom rather than the whole HDR pass. These
			   four framebuffers previously had no completeness check at
			   all.
			*/
			GLenum tryInt[2], tryFmt[2], tryType[2];
			int attempt, i;

			tryInt[0]  = 0x8C3A;        /* GL_R11F_G11F_B10F */
			tryFmt[0]  = GL_RGB;
			tryType[0] = 0x140B;        /* GL_HALF_FLOAT */

			if ( hdr_fp32_scene ) {
				tryInt[1] = 0x8814; tryFmt[1] = GL_RGBA; tryType[1] = GL_FLOAT;
			} else if ( hdr_fp16_scene ) {
				tryInt[1] = 0x881A; tryFmt[1] = GL_RGBA; tryType[1] = 0x140B;
			} else {
				tryInt[1] = GL_RGB10_A2; tryFmt[1] = GL_RGBA;
				tryType[1] = GL_UNSIGNED_INT_2_10_10_10_REV;
			}

			hdr_bloom_tex_bad = true;
			for ( attempt = 0; attempt < 2 && hdr_bloom_tex_bad; attempt++ ) {
				bool allOk = true;
				for ( i = 0; i < 4; i++ ) {
					int bw = ( i < 2 ) ? w / 4 : w / 16;
					int bh = ( i < 2 ) ? h / 4 : h / 16;
					if ( bw < 1 ) bw = 1;
					if ( bh < 1 ) bh = 1;
					if ( hdr_bloom_tex[i] ) glDeleteTextures( 1, &hdr_bloom_tex[i] );
					if ( hdr_bloom_fbo[i] ) glDeleteFramebuffers( 1, &hdr_bloom_fbo[i] );
					hdr_bloom_tex[i] = hdr_bloom_fbo[i] = 0;
					glGenTextures( 1, &hdr_bloom_tex[i] );
					glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[i] );
					glTexImage2D( GL_TEXTURE_2D, 0, tryInt[attempt], bw, bh, 0,
							tryFmt[attempt], tryType[attempt], NULL );
					glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
					glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
					glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
					glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
					glGenFramebuffers( 1, &hdr_bloom_fbo[i] );
					glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[i] );
					glFramebufferTexture2D( RARCH_GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
							GL_TEXTURE_2D, hdr_bloom_tex[i], 0 );
					if ( glCheckFramebufferStatus( RARCH_GL_FRAMEBUFFER )
							!= GL_FRAMEBUFFER_COMPLETE ) {
						allOk = false;
						break;
					}
				}
				for ( i = 0; allOk && i < HDR_CONV_MAX; i++ ) {
					int bw = ( w / 4 ) >> i, bh = ( h / 4 ) >> i;
					if ( bw < 1 ) bw = 1;
					if ( bh < 1 ) bh = 1;
					if ( hdr_conv_tex[i] ) glDeleteTextures( 1, &hdr_conv_tex[i] );
					if ( hdr_conv_fbo[i] ) glDeleteFramebuffers( 1, &hdr_conv_fbo[i] );
					hdr_conv_tex[i] = hdr_conv_fbo[i] = 0;
					glGenTextures( 1, &hdr_conv_tex[i] );
					glBindTexture( GL_TEXTURE_2D, hdr_conv_tex[i] );
					glTexImage2D( GL_TEXTURE_2D, 0, tryInt[attempt], bw, bh, 0,
							tryFmt[attempt], tryType[attempt], NULL );
					glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
					glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
					glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
					glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
					glGenFramebuffers( 1, &hdr_conv_fbo[i] );
					glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_conv_fbo[i] );
					glFramebufferTexture2D( RARCH_GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
							GL_TEXTURE_2D, hdr_conv_tex[i], 0 );
					if ( glCheckFramebufferStatus( RARCH_GL_FRAMEBUFFER )
							!= GL_FRAMEBUFFER_COMPLETE ) {
						allOk = false;
						break;
					}
				}
				if ( allOk )
					hdr_bloom_tex_bad = false;
				else if ( log_cb )
					log_cb( RETRO_LOG_WARN, "[boom3] HDR: bloom target 0x%04X not renderable%s\n",
							(unsigned)tryInt[attempt],
							attempt == 0 ? ", falling back to the scene format" : "; bloom disabled" );
			}
			if ( hdr_bloom_tex_bad ) {
				for ( i = 0; i < 4; i++ ) {
					if ( hdr_bloom_tex[i] ) glDeleteTextures( 1, &hdr_bloom_tex[i] );
					if ( hdr_bloom_fbo[i] ) glDeleteFramebuffers( 1, &hdr_bloom_fbo[i] );
					hdr_bloom_tex[i] = hdr_bloom_fbo[i] = 0;
				}
				for ( i = 0; i < HDR_CONV_MAX; i++ ) {
					if ( hdr_conv_tex[i] ) glDeleteTextures( 1, &hdr_conv_tex[i] );
					if ( hdr_conv_fbo[i] ) glDeleteFramebuffers( 1, &hdr_conv_fbo[i] );
					hdr_conv_tex[i] = hdr_conv_fbo[i] = 0;
				}
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

/* ACES stretch for the current headroom.
 *
 * The curve's asymptote is (2.51/2.43) and it is normalised by aces(1)
 * so that diffuse white lands on paper white.  That pins its ceiling at
 * 1.285 x paper white no matter how much headroom the display has - at
 * H = 4 more than two thirds of the range is unreachable, which is why
 * a bright light could never look brighter than a moderately bright
 * one.  Reinhard did not have this problem; it is built from H.
 *
 * Scaling the input by x and renormalising by 1/aces(x) keeps unity at
 * v = 1 and moves the asymptote to (2.51/2.43)/aces(x).  Solving
 * aces(x) = (2.51/2.43)/H puts the asymptote exactly on H.  Monotonic
 * in x, so a bisection converges in a few dozen steps and this runs
 * once per present, not per pixel.
 */
/* Base curves, each normalised so paper white lands on paper white. */
static double hdr_curve_aces( double v ) {
	const double n = v * ( 2.51 * v + 0.03 ) / ( v * ( 2.43 * v + 0.59 ) + 0.14 );
	return n * 1.2440945;
}

static double hdr_curve_hejl( double v ) {
	/* Hejl and Burgess-Dawson.  The published form bakes in a display
	 * gamma, which this pipeline must not have - it works in linear
	 * light and encodes PQ at the end - so the 2.2 is undone here. */
	const double x = v > 0.004 ? v - 0.004 : 0.0;
	const double n = ( x * ( 6.2 * x + 0.5 ) ) / ( x * ( 6.2 * x + 1.7 ) + 0.06 );
	return pow( n, 2.2 ) * 1.4629683;
}

/* The 1990s perceptual operators.
 *
 * These were written to map scene luminance onto a CRT of known peak
 * brightness, with a viewer adaptation term.  This pipeline fixes paper
 * white instead, so the adaptation constants divide out and what is
 * left is the shape each model actually imposes.  That is worth stating
 * plainly rather than hiding: Tumblin-Rushmeier becomes a power law,
 * and Ward becomes the identity - its whole contribution was choosing a
 * linear scale factor, and normalisation chooses one for us.  Ward is
 * still worth having as the honest "no curve at all" entry.
 *
 * Ferwerda is deliberately absent.  Under a fixed adaptation its
 * threshold-versus-intensity model reduces to the same linear scaling
 * as Ward, so it would be a second name for an entry already here.
 */
/* GT's derived constants, and the factor that pins paper white.
 *
 * l0, S0, S1 and C2 all follow from the toe and the linear length, so
 * changing either moves the whole curve; the normalisation has to be
 * re-solved with them or paper white drifts as the sliders move. */
static void hdr_gt_params( double m, double l, double *l0, double *S0,
						   double *S1, double *C2 ) {
	*l0 = ( ( 1.0 - m ) * l );
	*S0 = m + *l0;
	*S1 = m + *l0;
	*C2 = 1.0 / ( 1.0 - *S1 );
}

static double hdr_curve_gt_at( double v, double m, double l ) {
	double l0, S0, S1, C2, t, w0, w1, w2, T, S, L;

	hdr_gt_params( m, l, &l0, &S0, &S1, &C2 );
	t = v / m;
	if ( t > 1.0 ) t = 1.0; else if ( t < 0.0 ) t = 0.0;
	w0 = 1.0 - ( t * t * ( 3.0 - 2.0 * t ) );
	w2 = ( v >= S0 ) ? 1.0 : 0.0;
	w1 = 1.0 - w0 - w2;
	T  = m * pow( ( v > 0.0 ? v : 0.0 ) / m, 1.33 );
	S  = 1.0 - ( 1.0 - S1 ) * exp( -C2 * ( v - S0 ) );
	L  = v;
	return T * w0 + L * w1 + S * w2;
}

static double hdr_gt_norm( void ) {
	const double f = hdr_curve_gt_at( 1.0, hdr_gt_toe, hdr_gt_shoulder );
	return f > 1e-6 ? 1.0 / f : 1.0;
}

static double hdr_curve_filmiclog( double v ) {
	/* Sobotka's Filmic approach: encode to log across the same exposure
	 * range Blender's Filmic uses - about -12.47 to +4.03 stops - and
	 * put a contrast sigmoid on the encoded value, pivoted where mid
	 * grey lands.  The 2.2 comes back out because the sigmoid produces
	 * a display-referred value and this pipeline is linear.
	 *
	 * Named for what it is.  Blender's Filmic look is defined by a
	 * baked lookup table shipped with its config, not by a closed form;
	 * without those coefficients this is the same construction with a
	 * sigmoid in place of the table, which is a different curve.
	 * Calling it "Filmic Blender" would be borrowing a name for
	 * something that does not match it. */
	const double mn = -12.473931188, mx = 4.026068811;
	const double t0 = 0.6060791;   /* where 0.18 encodes to */
	const double k  = 8.0;
	double t, sg;

	t = ( log( v > 1e-10 ? v : 1e-10 ) / log( 2.0 ) - mn ) / ( mx - mn );
	if ( t < 0.0 ) t = 0.0; else if ( t > 1.0 ) t = 1.0;
	sg = 1.0 / ( 1.0 + exp( -k * ( t - t0 ) ) );
	return pow( sg, 2.2 ) * 1.7852541;
}

static double hdr_curve_tumblin( double v ) {
	/* Exponent is gamma(Lwa)/gamma(Lda) at Lwa = 1, Lda = 20 cd/m2,
	 * which is 0.6075.  Everything else in the model is a constant
	 * scale and drops out. */
	const double x = v > 0.0 ? v : 0.0;
	return pow( x, 0.6075 );
}

static double hdr_curve_ward( double v ) {
	/* Linear, by construction. */
	return v > 0.0 ? v : 0.0;
}

static double hdr_curve_schlick( double v ) {
	/* Schlick's rational model with p = 2.3.  Self-normalising: f(1) is
	 * exactly 1 for any p, so no correction factor is needed. */
	const double x = v > 0.0 ? v : 0.0;
	return 2.3 * x / ( 2.3 * x - x + 1.0 );
}

static double hdr_curve_devlin( double v ) {
	/* Reinhard-Devlin's photoreceptor model, global and achromatic,
	 * with adaptation pinned at mid grey rather than at a measured
	 * frame average - this pipeline has no exposure stage to measure
	 * one.  sigma = 0.18^0.7. */
	const double x = v > 0.0 ? v : 0.0;
	return ( x / ( x + 0.3010864 ) ) * 1.3010864;
}

static double hdr_curve_rplain( double v ) {
	/* The textbook Reinhard, kept as the honest baseline: no knee, no
	 * white point, x/(1+x) with white pinned by the usual factor. */
	const double x = v > 0.0 ? v : 0.0;
	return ( x / ( 1.0 + x ) ) * 2.0;
}

static double hdr_curve_rext( double v ) {
	/* Reinhard with a white point, W = 4: the input that would have
	 * mapped to 1 before normalisation.  Unlike the plain form it keeps
	 * climbing past its nominal white rather than converging, so bright
	 * content stays separable. */
	const double x = v > 0.0 ? v : 0.0;
	return ( x * ( 1.0 + x / 16.0 ) / ( 1.0 + x ) ) * 1.8823529;
}

static double hdr_curve_expo( double v ) {
	/* 1 - exp(-x).  No shoulder to speak of, just a smooth approach -
	 * the simplest shape here and the cheapest to evaluate. */
	const double x = v > 0.0 ? v : 0.0;
	return ( 1.0 - exp( -x ) ) * 1.5819767;
}

static double hdr_curve_hable2017( double v ) {
	/* Hable's 2017 piecewise: a power toe, a straight middle, and a
	 * shoulder, joined so that value and slope match at both seams.
	 * Constants solved once from x0 = 0.25, y0 = 0.20, x1 = 0.70 and a
	 * unit slope through the middle, which is what makes the mid-tones
	 * linear - the property the piecewise form exists for. */
	const double x0 = 0.25, y0 = 0.20, x1 = 0.70, m = 1.0;
	const double A = 1.1313708, B = 1.25, y1 = 0.65, S = 0.35;
	const double x = v > 0.0 ? v : 0.0;
	double f;

	if ( x < x0 ) {
		f = A * pow( x, B );
	} else if ( x < x1 ) {
		f = y0 + m * ( x - x0 );
	} else {
		f = y1 + ( 1.0 - y1 ) * ( x - x1 ) / ( ( x - x1 ) + S );
	}
	return f * 1.2322275;
}

static double hdr_curve_aces2( double v ) {
	/* The ACES 2.0 forward tone scale at a 100 nit anchor, with its
	 * parameters solved once: m2 and s2 below come out of the published
	 * derivation for peak = reference = 100.
	 *
	 * The tone scale only.  ACES 2.0's chroma compression and gamut
	 * mapping work in JMh and are a different kind of change than a
	 * curve - not something to imply by the name, hence the label. */
	const double m2 = 1.0471038, s2 = 0.9198583, g = 1.15, t1 = 0.04;
	const double x = v > 0.0 ? v : 0.0;
	{
		const double f = m2 * pow( x / ( x + s2 ), g );
		return ( f * f / ( f + t1 ) ) * 2.1854781;
	}
}

static double hdr_curve_filmicalu( double v ) {
	/* Hejl's 2015 revision of the curve above.  Also display-referred,
	 * so the 2.2 comes back out.  Darkest mid-tones of the set at
	 * 0.092, with a short shoulder at 1.17x paper white. */
	const double x = v > 0.0 ? v : 0.0;
	const double va = 1.425 * x + 0.05;
	double f = ( ( x * va + 0.004 ) / ( ( x * va + 0.055 ) + 0.0491 ) ) - 0.0821;
	if ( f < 0.0 ) {
		f = 0.0;
	}
	return pow( f, 2.2 ) * 1.4132626;
}

static double hdr_curve_gt( double v ) {
	return hdr_curve_gt_at( v, hdr_gt_toe, hdr_gt_shoulder ) * hdr_gt_norm();
}

static double hdr_curve_hable( double v ) {
	/* Hable's Uncharted 2 filmic, published coefficients.  Linear in and
	 * out - no display gamma folded in, unlike Hejl and Unreal - so it
	 * needs no correction, only the usual normalisation by its own value
	 * at 1.0.  Mid grey lands at 0.221, close to GT, and it saturates at
	 * 4.23x paper white: the longest usable shoulder of the filmic
	 * curves here. */
	const double A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
	const double n = ( ( v * ( A * v + C * B ) + D * E ) / ( v * ( A * v + B ) + D * F ) ) - E / F;
	return n * 4.5319149;
}

static double hdr_curve_lottes( double v ) {
	/* Lottes' AMD curve with its published parameters: a = 1.6,
	 * d = 0.977, hdrMax = 8, midIn = 0.18, midOut = 0.267, from which b
	 * and c fall out as constants.
	 *
	 * Normalising it to pin paper white costs its mid-tones: mid grey
	 * arrives at 0.074 against a reference of 0.18.  That is not a bug
	 * in the fit, it is what the curve is - a compressor built to carry
	 * eight stops, rescaled so 1.0 lands on 1.0 - and it buys the
	 * longest highlight range of the set, 12.3x paper white.  Dark and
	 * contrasty by construction. */
	const double a = 1.6, d = 0.977;
	const double b = 1.0730397, c = 0.1674199;
	if ( v <= 0.0 ) {
		return 0.0;
	}
	return ( pow( v, a ) / ( pow( v, a * d ) * c + b ) ) * 1.2404596;
}

static double hdr_curve_drago( double v ) {
	/* Drago's logarithmic operator, Lwmax = 8, bias 0.85.  Used as
	 * published, without undoing a gamma: its output is already the
	 * shape this pipeline wants, and raising it to 2.2 crushes mid grey
	 * to 0.06.  As it stands mid grey lands at 0.277 - between GT and
	 * ACES - with a 7.95x shoulder. */
	const double Lwmax = 8.0, bias = 0.85;
	double e, den;
	if ( v <= 0.0 ) {
		return 0.0;
	}
	e = log( bias ) / log( 0.5 );
	den = log( 2.0 + 8.0 * pow( v / Lwmax, e ) );
	if ( den <= 0.0 ) {
		return 0.0;
	}
	return ( log( 1.0 + v ) / den ) / log10( 1.0 + Lwmax ) * 2.6616800;
}

static double hdr_curve_unreal( double v ) {
	/* Unreal 3's tonemapper, x/(x+0.155)*1.019.  Like Hejl it bakes in
	 * an approximate display gamma, so the 2.2 is undone before
	 * normalising, otherwise mid grey lands at 0.62.  Corrected it sits
	 * at 0.350 with a 1.373x shoulder - the shortest here after GT. */
	const double n = v / ( v + 0.155 ) * 1.019;
	return pow( n, 2.2 ) * 1.3173380;
}

/* Shoulder parameters for the selected curve.
 *
 * Every one of these curves flattens out well below the display's
 * headroom - ACES at 1.285x paper white, Hejl at 1.463x - and does so
 * whatever the panel can do, because the normalisation that pins paper
 * white also pins the asymptote.  So the curve is used up to paper
 * white and a shoulder carries everything above it out to H, joined at
 * the curve's own gradient there so there is no crease.
 *
 * A = (H-1)/slope and slope are what the shaders need; both are
 * measured from the curve rather than assumed, so adding a curve means
 * adding it to the switch and nothing else.
 */
static void hdr_shoulder_params( int mode, float H, float *A, float *slopeOut ) {
	const double d = 1e-4;
	double f1, f0, slope;

	switch ( mode ) {
	case HDR_ROLLOFF_HEJL:
		f1 = hdr_curve_hejl( 1.0 + d );
		f0 = hdr_curve_hejl( 1.0 - d );
		break;
	case HDR_ROLLOFF_GT:
		f1 = hdr_curve_gt( 1.0 + d );
		f0 = hdr_curve_gt( 1.0 - d );
		break;
	case HDR_ROLLOFF_FILMICLOG:
		f1 = hdr_curve_filmiclog( 1.0 + d );
		f0 = hdr_curve_filmiclog( 1.0 - d );
		break;
	case HDR_ROLLOFF_TUMBLIN:
		f1 = hdr_curve_tumblin( 1.0 + d );
		f0 = hdr_curve_tumblin( 1.0 - d );
		break;
	case HDR_ROLLOFF_WARD:
		f1 = hdr_curve_ward( 1.0 + d );
		f0 = hdr_curve_ward( 1.0 - d );
		break;
	case HDR_ROLLOFF_SCHLICK:
		f1 = hdr_curve_schlick( 1.0 + d );
		f0 = hdr_curve_schlick( 1.0 - d );
		break;
	case HDR_ROLLOFF_DEVLIN:
		f1 = hdr_curve_devlin( 1.0 + d );
		f0 = hdr_curve_devlin( 1.0 - d );
		break;
	case HDR_ROLLOFF_RPLAIN:
		f1 = hdr_curve_rplain( 1.0 + d );
		f0 = hdr_curve_rplain( 1.0 - d );
		break;
	case HDR_ROLLOFF_REXT:
		f1 = hdr_curve_rext( 1.0 + d );
		f0 = hdr_curve_rext( 1.0 - d );
		break;
	case HDR_ROLLOFF_EXPO:
		f1 = hdr_curve_expo( 1.0 + d );
		f0 = hdr_curve_expo( 1.0 - d );
		break;
	case HDR_ROLLOFF_HABLE2017:
		f1 = hdr_curve_hable2017( 1.0 + d );
		f0 = hdr_curve_hable2017( 1.0 - d );
		break;
	case HDR_ROLLOFF_ACES2:
		f1 = hdr_curve_aces2( 1.0 + d );
		f0 = hdr_curve_aces2( 1.0 - d );
		break;
	case HDR_ROLLOFF_FILMICALU:
		f1 = hdr_curve_filmicalu( 1.0 + d );
		f0 = hdr_curve_filmicalu( 1.0 - d );
		break;
	case HDR_ROLLOFF_HABLE:
		f1 = hdr_curve_hable( 1.0 + d );
		f0 = hdr_curve_hable( 1.0 - d );
		break;
	case HDR_ROLLOFF_LOTTES:
		f1 = hdr_curve_lottes( 1.0 + d );
		f0 = hdr_curve_lottes( 1.0 - d );
		break;
	case HDR_ROLLOFF_DRAGO:
		f1 = hdr_curve_drago( 1.0 + d );
		f0 = hdr_curve_drago( 1.0 - d );
		break;
	case HDR_ROLLOFF_UNREAL:
		f1 = hdr_curve_unreal( 1.0 + d );
		f0 = hdr_curve_unreal( 1.0 - d );
		break;
	default:
		f1 = hdr_curve_aces( 1.0 + d );
		f0 = hdr_curve_aces( 1.0 - d );
		break;
	}
	slope = ( f1 - f0 ) / ( 2.0 * d );

	if ( H <= 1.0001f ) {
		*A = 0.0f;	/* flat shoulder: nothing lives above paper white */
		*slopeOut = 0.0f;
		return;
	}
	*A = (float)( ( (double)H - 1.0 ) / slope );
	*slopeOut = (float)slope;
}

/* Is the chain this build actually uses loaded?
 *
 * These used to be written as direct tests of the GLSL program handles,
 * which was fine while GLSL was the only chain.  Once the desktop build
 * stopped compiling GLSL those handles are permanently zero there, so
 * the composite guard below refused every frame and the screen went
 * black with nothing logged - the pass was not failing, it was never
 * running.  Name the programs per build instead. */
#ifdef HAVE_OPENGLES
#define HDR_COMPOSITE_READY	( hdr_prog != 0 )
#define HDR_BLOOM_READY		( hdr_prog_bright != 0 && hdr_prog_blur != 0 )
#else
#define HDR_COMPOSITE_READY	( hdr_arb_vp != 0 && hdr_arb_comp1 != 0 && hdr_arb_comp2 != 0 )
#define HDR_BLOOM_READY		( hdr_arb_bright != 0 && hdr_arb_blur != 0 \
				  && hdr_arb_down != 0 && hdr_arb_up != 0 )
#endif

/* run the conversion pass from the scene target into dstFbo */
static void hdr_present( GLuint dstFbo ) {
	if ( !hdr_output_active || !HDR_COMPOSITE_READY || hdr_fbo == 0 )
		return;

	/* frontend HDR state, re-queried per present as the contract asks */
	float paperWhite = 200.0f, maxNits = 1000.0f;
	unsigned gamutMode = 0, outMode = 1;
	environ_cb( RETRO_ENVIRONMENT_GET_HDR_PAPER_WHITE_NITS, &paperWhite );
	environ_cb( RETRO_ENVIRONMENT_GET_HDR_MAX_NITS, &maxNits );
	/* the ACES 2.0 tables are solved for this, and rebuilt when it moves */
	hdr_aces2_wanted_peak = maxNits;
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

	/* Only the Expanded midpoint costs anything to build, and all four
	   change only when the user touches a frontend setting, so keep the
	   last one. */
	static float mat[9];
	static unsigned matMode = ~0u;
	int i;
	if ( gamutMode != matMode ) {
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
		matMode = gamutMode;
	}

	float H = maxNits / ( paperWhite > 1.0f ? paperWhite : 1.0f );
	if ( H < 1.0f )
		H = 1.0f;   /* the contract's zero-headroom clamp */

	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	glDisable( GL_BLEND );
	glDisable( GL_CULL_FACE );
	glDepthMask( GL_FALSE );
	/*
	   RB_SwapBuffers re-enables GL_SCISSOR_TEST immediately before
	   calling GLimp_SwapBuffers, and the scissor box holds whatever the
	   last view or 2D pass set. RB_SetGL2D puts it full-screen, which
	   covers the common case - but a frame that ends on a subview or
	   mirror scissor without a following 2D pass would have this
	   fullscreen composite clipped to that sub-rect, leaving stale
	   framebuffer content everywhere outside it. The colour mask is the
	   same hazard at lower probability.
	*/
	glDisable( GL_SCISSOR_TEST );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

	static const float triV[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 0, triV );

	int haveBloom = ( hdr_bloom_amount > 0.0f
			&& !hdr_bloom_prog_bad && !hdr_bloom_tex_bad
			&& HDR_BLOOM_READY );
	/* Level count for the convolution pyramid, 0 when the option is off.
	   Computed once: the chain below and the composite must agree, and
	   the composite's band weight is derived from it. */
	int convN = hdr_bloom_convolution ? hdr_conv_levels( hdr_w, hdr_h ) : 0;
	float bandW0, bandW1;
#ifndef HAVE_OPENGLES
	/* Unbind whatever program object is in use before enabling the ARB
	   targets.  This is not tidying: a program object in use overrides
	   the ARB program enables entirely, so with one bound - and the
	   frontend has its own bound around the core's frame - every pass
	   below draws with that program instead of ours, which is a black
	   screen rather than an error.  The GLSL path used to do this
	   implicitly by binding its own program per pass; when those binds
	   went, this had to become explicit and did not.
	   
	   Enabled once for the whole post chain and handed back at the end
	   of it, rather than per pass.  Bloom is optional and the composite
	   is not, so an enable that lived inside the bloom block would
	   leave the composite drawing with no program bound whenever bloom
	   was off. */
	glUseProgram( 0 );
	qglEnable( GL_VERTEX_PROGRAM_ARB );
	qglEnable( GL_FRAGMENT_PROGRAM_ARB );
	qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, hdr_arb_vp );
#endif

	if ( haveBloom ) {
		int qw = hdr_w / 4 < 1 ? 1 : hdr_w / 4,  qh = hdr_h / 4 < 1 ? 1 : hdr_h / 4;
		int sw = hdr_w / 16 < 1 ? 1 : hdr_w / 16, sh = hdr_h / 16 < 1 ? 1 : hdr_h / 16;
		glActiveTexture( GL_TEXTURE0 );
		/* bright pass: scene -> tight A, or pyramid level 0 (both 1/4 res) */
		glBindFramebuffer( RARCH_GL_FRAMEBUFFER, convN ? hdr_conv_fbo[0] : hdr_bloom_fbo[0] );
		glViewport( 0, 0, qw, qh );
#ifdef HAVE_OPENGLES
		glUseProgram( hdr_prog_bright );
#else
		qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, hdr_arb_bright );
#endif
		glBindTexture( GL_TEXTURE_2D, hdr_tex );
		/* Threshold and knee are tuned together: the knee lets
		   sub-threshold content contribute, so holding the knee fixed
		   and raising the threshold is what keeps total bloom energy
		   where it was. 0.70/0.25 measured within 1% of the old
		   0.6021 hard threshold on a drifting-highlight sweep, while
		   cutting frame-to-frame energy swing by 1.4x to 2.4x.
		   (0.6021 itself came from matching gamma code 0.8095 across
		   the inverse-sRGB to gamma-2.4 decode change.) */
#ifdef HAVE_OPENGLES
		glUniform1f( hdr_bright_loc_thresh, 0.70f );
		glUniform1f( hdr_bright_loc_knee, 0.25f );
		glUniform1f( hdr_bright_loc_enc, 1.0f / hdr_scene_encode_scale );
#else
		{
			/* 1/(1-thresh) and 1/(4*knee) are pass constants; computing
			   them here keeps two RCPs out of every fragment. */
			const float thresh = 0.70f, knee = 0.25f;
			const float p[4] = { thresh, knee, 1.0f / hdr_scene_encode_scale,
					1.0f / ( 1.0f - thresh ) };
			const float q[4] = { 0.0f, 1.0f / ( 4.0f * knee ), 0.0f, 0.0f };
			qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, p );
			qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 1, q );
		}
#endif
#ifdef HAVE_OPENGLES
		glUniform2f( hdr_bright_loc_texel, 1.0f / (float)hdr_w, 1.0f / (float)hdr_h );
#else
		{
			const float texel[4] = { 1.0f / (float)hdr_w, 1.0f / (float)hdr_h, 0.0f, 0.0f };
			qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 2, texel );
		}
#endif
		glDrawArrays( GL_TRIANGLES, 0, 3 );

		if ( convN ) {
			/*
			   Convolution bloom: build a mip pyramid down from 1/4, then
			   walk back up adding each level into the next larger one.
			   The accumulated level 0 is the scene convolved with the sum
			   of all the levels' kernels - a wide, heavy-tailed point
			   spread rather than the two discrete Gaussians of the
			   default path, which is what makes a glow read as carrying
			   far past its source instead of stopping at a visible edge.
			*/
			int i;
#ifdef HAVE_OPENGLES
			glUseProgram( hdr_prog_down );
#else
			qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, hdr_arb_down );
#endif
			for ( i = 1; i < convN; i++ ) {
				int pw = ( hdr_w / 4 ) >> ( i - 1 ), ph = ( hdr_h / 4 ) >> ( i - 1 );
				if ( pw < 1 ) pw = 1;
				if ( ph < 1 ) ph = 1;
				glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_conv_fbo[i] );
				glViewport( 0, 0, pw > 1 ? pw / 2 : 1, ph > 1 ? ph / 2 : 1 );
				glBindTexture( GL_TEXTURE_2D, hdr_conv_tex[i - 1] );
#ifdef HAVE_OPENGLES
				glUniform2f( hdr_down_loc_texel, 1.0f / (float)pw, 1.0f / (float)ph );
#else
				{
					const float texel[4] = { 1.0f / (float)pw, 1.0f / (float)ph, 0.0f, 0.0f };
					qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, texel );
				}
#endif
				glDrawArrays( GL_TRIANGLES, 0, 3 );
			}
			/* additive accumulation upward. Blend rather than ping-pong so
			   each level needs only one buffer; hdr_present disabled blend
			   on entry, so turn it off again afterwards. */
#ifdef HAVE_OPENGLES
			glUseProgram( hdr_prog_up );
#else
			qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, hdr_arb_up );
#endif
			glEnable( GL_BLEND );
			glBlendFunc( GL_ONE, GL_ONE );
#ifdef HAVE_OPENGLES
			glUniform1f( hdr_up_loc_radius, 1.0f );
#endif
			/* radius is 1.0 here, so env[0].xy is just the texel size:
			   the ARB pass takes the already-scaled offset rather than
			   multiplying per tap. */
			for ( i = convN - 1; i > 0; i-- ) {
				int pw = ( hdr_w / 4 ) >> i, ph = ( hdr_h / 4 ) >> i;
				int dw = ( hdr_w / 4 ) >> ( i - 1 ), dh = ( hdr_h / 4 ) >> ( i - 1 );
				if ( pw < 1 ) pw = 1;
				if ( ph < 1 ) ph = 1;
				if ( dw < 1 ) dw = 1;
				if ( dh < 1 ) dh = 1;
				glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_conv_fbo[i - 1] );
				glViewport( 0, 0, dw, dh );
				glBindTexture( GL_TEXTURE_2D, hdr_conv_tex[i] );
#ifdef HAVE_OPENGLES
				glUniform2f( hdr_up_loc_texel, 1.0f / (float)pw, 1.0f / (float)ph );
#else
				{
					const float texel[4] = { 1.0f / (float)pw, 1.0f / (float)ph, 0.0f, 0.0f };
					qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, texel );
				}
#endif
				glDrawArrays( GL_TRIANGLES, 0, 3 );
			}
			glDisable( GL_BLEND );
		} else {
		/* tight band blur: A -> B (H), B -> A (V) */
#ifdef HAVE_OPENGLES
		glUseProgram( hdr_prog_blur );
		glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[1] );
		glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[0] );
		glUniform2f( hdr_blur_loc_dir, 1.0f / qw, 0.0f );
		glDrawArrays( GL_TRIANGLES, 0, 3 );
		glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[0] );
		glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[1] );
		glUniform2f( hdr_blur_loc_dir, 0.0f, 1.0f / qh );
		glDrawArrays( GL_TRIANGLES, 0, 3 );
#else
		{
			const float dh[4] = { 1.0f / qw, 0.0f, 0.0f, 0.0f };
			const float dv[4] = { 0.0f, 1.0f / qh, 0.0f, 0.0f };
			qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, hdr_arb_blur );
			glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[1] );
			glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[0] );
			qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, dh );
			glDrawArrays( GL_TRIANGLES, 0, 3 );
			glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[0] );
			glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[1] );
			qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, dv );
			glDrawArrays( GL_TRIANGLES, 0, 3 );
		}
#endif
		/* wide band: the horizontal pass runs at 1/16 sampling the
		   already-blurred 1/4 band, so the 4x downsample rides along
		   with it. The previous seed was a dedicated draw through the
		   blur program with uDir = (0,0) - five bilinear fetches all
		   landing on the same texel, weights summing to 1, i.e. an
		   expensive copy. One draw and four fetches less per frame,
		   and nothing about the result changes except that the wide
		   band is no longer point-sampled at the downsample. */
		glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[3] );
		glViewport( 0, 0, sw, sh );
		glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[0] );
#ifdef HAVE_OPENGLES
		glUniform2f( hdr_blur_loc_dir, 1.0f / sw, 0.0f );
#else
		{
			const float dh[4] = { 1.0f / sw, 0.0f, 0.0f, 0.0f };
			qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, dh );
		}
#endif
		glDrawArrays( GL_TRIANGLES, 0, 3 );
		glBindFramebuffer( RARCH_GL_FRAMEBUFFER, hdr_bloom_fbo[2] );
		glBindTexture( GL_TEXTURE_2D, hdr_bloom_tex[3] );
#ifdef HAVE_OPENGLES
		glUniform2f( hdr_blur_loc_dir, 0.0f, 1.0f / sh );
#else
		{
			const float dv[4] = { 0.0f, 1.0f / sh, 0.0f, 0.0f };
			qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, dv );
		}
#endif
		glDrawArrays( GL_TRIANGLES, 0, 3 );
		}
	}


	glBindFramebuffer( RARCH_GL_FRAMEBUFFER, dstFbo );
	glViewport( 0, 0, hdr_w, hdr_h );

#ifdef HAVE_OPENGLES
	glUseProgram( hdr_prog );
#endif

	/*
	   Band weights, and with them the total bloom energy.

	   Every filter in both chains has weights summing to exactly 1, and
	   both chains are linear, so the integrated bloom a given extraction
	   produces is just the sum of the band weights - the ratio between
	   the two modes is a constant, independent of scene content. The
	   two-band path sums 0.22 + 0.14 = 0.36. The pyramid contributes one
	   unit-gain term per level, so dividing 0.36 by the level count
	   matches it exactly rather than by eye.

	   Convolution bloom still looks dimmer at the core of a highlight.
	   That is the trade: the same energy spread over a far wider point
	   spread means a lower peak and a longer tail.
	*/
	bandW0 = convN ? 0.36f / (float)convN : 0.22f;
	bandW1 = convN ? 0.0f : 0.14f;
	glActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, convN ? hdr_conv_tex[0] : hdr_bloom_tex[0] );
	glActiveTexture( GL_TEXTURE2 );
	glBindTexture( GL_TEXTURE_2D, convN ? hdr_conv_tex[0] : hdr_bloom_tex[2] );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, hdr_tex );
	/* wraps every 64 frames; the pattern only has to keep moving */
	const float frameN = (float)( hdr_frame_counter++ & 63u );

#ifdef HAVE_OPENGLES
	glUniform1i( hdr_loc_tex, 0 );
	glUniform1i( hdr_loc_bloomT, 1 );
	glUniform1i( hdr_loc_bloomW, 2 );
	glUniform1f( hdr_loc_bloomAmt, haveBloom ? hdr_bloom_amount : 0.0f );
	glUniform2f( hdr_loc_bandW, bandW0, bandW1 );
	glUniform1f( hdr_loc_encScale, 1.0f / hdr_scene_encode_scale );
	glUniform1f( hdr_loc_frame, frameN );
	glUniformMatrix3fv( hdr_loc_mat, 1, GL_FALSE, mat );
	glUniform4f( hdr_loc_parms, paperWhite / 10000.0f, H,
			(float)hdr_rolloff_mode, 0.75f /* Reinhard knee */ );
	/* Same 0.75 knee as the Reinhard roll-off: expansion starts where
	 * the diffuse range ends, so the two meet at the same place. */
	glUniform2f( hdr_loc_expand, (float)hdr_expand_mode, 0.75f );
	{
		float as, an;
		hdr_shoulder_params( hdr_rolloff_mode, H, &as, &an );
		{
			double l0, S0, S1, C2;
			hdr_gt_params( hdr_gt_toe, hdr_gt_shoulder, &l0, &S0, &S1, &C2 );
			glUniform3f( hdr_loc_aces, as, an, (float)hdr_gt_norm() );
			glUniform4f( hdr_loc_gt, hdr_gt_toe, (float)S0,
					(float)( 1.0 - S1 ), (float)C2 );
		}
	}
#endif

#ifndef HAVE_OPENGLES
	{
		/* The band-weight branch the GLSL took on a uniform is a choice
		   of program here, so the second bloom fetch is still skipped
		   rather than multiplied by zero. */
		const float eknee = 0.75f;
		const float p0[4] = { paperWhite / 10000.0f, H,
				(float)hdr_rolloff_mode, 0.75f };
		const float p1[4] = { (float)hdr_expand_mode, eknee,
				1.0f / ( 1.0f - eknee < 1e-4f ? 1e-4f : 1.0f - eknee ), 0.0f };
		const float p2[4] = { 1.0f / hdr_scene_encode_scale,
				haveBloom ? hdr_bloom_amount : 0.0f, bandW0, bandW1 };
		const float p3[4] = { frameN * 3.0f, 0.6180339887f, 1.0f / 1023.0f, 0.0f };
		/* rows, from the column-major matrix the GLSL upload uses */
		/* ACES 2.0 lands in the output primaries itself, so the gamut
		 * stage that follows it is fed identity rows.  Rotating an
		 * already-finished colour a second time is the failure this
		 * avoids, and it would look like a colour cast rather than
		 * anything obviously broken. */
		const bool aces2 = ( hdr_rolloff_mode == HDR_ROLLOFF_ACES2FULL && hdr_aces2_lut != 0 );
		const float r0[4] = { aces2 ? 1.0f : mat[0], aces2 ? 0.0f : mat[3], aces2 ? 0.0f : mat[6], 0.0f };
		const float r1[4] = { aces2 ? 0.0f : mat[1], aces2 ? 1.0f : mat[4], aces2 ? 0.0f : mat[7], 0.0f };
		const float r2[4] = { aces2 ? 0.0f : mat[2], aces2 ? 0.0f : mat[5], aces2 ? 1.0f : mat[8], 0.0f };
		qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB,
				bandW1 > 0.0f ? hdr_arb_comp2 : hdr_arb_comp1 );
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, p0 );
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 1, p1 );
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 2, p2 );
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 3, p3 );
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 4, r0 );
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 5, r1 );
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 6, r2 );
		if ( aces2 ) {
			hdr_aces2_set_env();
			glActiveTexture( GL_TEXTURE3 );
			glBindTexture( GL_TEXTURE_2D, hdr_aces2_lut );
			glActiveTexture( GL_TEXTURE0 );
		}
		{
			float as, an, p7[4];
			hdr_shoulder_params( hdr_rolloff_mode, H, &as, &an );
			p7[0] = as; p7[1] = an; p7[2] = 0.0f; p7[3] = 0.0f;
			qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 7, p7 );
			{
				double l0, S0, S1, C2;
				float p8[4], p9[4];
				hdr_gt_params( hdr_gt_toe, hdr_gt_shoulder, &l0, &S0, &S1, &C2 );
				p8[0] = hdr_gt_toe;      p8[1] = (float)S0;
				p8[2] = (float)( 1.0 - S1 ); p8[3] = (float)C2;
				p9[0] = 1.0f / hdr_gt_toe;   p9[1] = (float)hdr_gt_norm();
				p9[2] = 0.0f;                p9[3] = 0.0f;
				qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 8, p8 );
				qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 9, p9 );
			}
		}
	}
#endif

	glDrawArrays( GL_TRIANGLES, 0, 3 );

#ifndef HAVE_OPENGLES
	/* Hand the pipeline back: while an ARB target is enabled it
	   overrides any bound program object, so leaving it on would take
	   the game's own rendering next frame with it. */
	qglDisable( GL_FRAGMENT_PROGRAM_ARB );
	qglDisable( GL_VERTEX_PROGRAM_ARB );
#endif
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

/*
   Scoped owner for retro_savestate_active.

   idCommonLocal::Frame() wraps its whole body in catch( idException & ),
   which is what makes retro_run survive an ERP_DROP. The savestate
   entry points do not go through Frame(): they call sessLocal.SaveGame
   and sessLocal.LoadGame directly, so nothing was catching an
   idException thrown underneath them.

   That is not a theoretical path. ExecuteMapChange calls
   common->Error( "couldn't load %s" ) when the map named in the state
   is missing, and common->Error throws for ERP_DROP. A state carried
   between installs, mods, or core versions - which the format
   deliberately allows to be version- and platform-dependent - lands
   exactly there.

   The consequences were both bad. The flag stayed true forever, and
   every user of it fails closed: UpdateScreen returns immediately, so
   no frame is ever rendered again and the HDR present pass never runs.
   A permanently black screen from one bad state. And the exception
   unwound out of retro_unserialize into the frontend, which is C and
   has no handler for it - undefined behaviour at the ABI boundary.

   The destructor restores the flag on every exit path, and the callers
   below stop anything escaping into the frontend.
*/
class RetroSaveStateScope {
public:
	RetroSaveStateScope()  { retro_savestate_active = true; }
	~RetroSaveStateScope() { retro_savestate_active = false; }
};

/* The built state lives in the memory file it was serialized into, and
 * retro_serialize copies from there straight to the frontend's buffer.
 * It used to be copied out into a byte list first, so every state cost
 * two full-size copies instead of one - a multi-megabyte memcpy per
 * build, and under run-ahead that is per frame.  idFile_Memory owns the
 * buffer for as long as it lives, so the file is what persists. */
static idFile_Memory retro_state_file( RETRO_STATE_NAME ".save" );
static int retro_state_len;
static int retro_state_cache_tic = -1;

static bool RetroBuildState(void)
{
	extern volatile int com_ticNumber;

	/* mapSpawned gates the cache, not the other way round. A cache entry
	 * describes a spawned map; if there is no longer one, the entry is
	 * stale no matter what tic it carries, and serving it would hand the
	 * frontend a state for a session that has ended. */
	if (!sessLocal.mapSpawned) {
		if (log_cb) log_cb(RETRO_LOG_INFO, "[boom3] state: refused, mapSpawned=false\n");
		return false;
	}

	if (retro_state_cache_tic == com_ticNumber && retro_state_len > 0)
		return true;	// still current: state can only change on a tic

	// serialize straight into memory: no disk I/O anywhere in this path
	/* Reuse the file, keeping its allocation: Clear(false) rewinds it
	 * without giving the buffer back, so a steady-state build neither
	 * allocates nor grows. */
	idFile_Memory &mem = retro_state_file;
	const int prevLen = retro_state_len;
	retro_state_len = 0;
	mem.Clear(false);
	if (prevLen > 0)
		mem.SetGranularity(prevLen + 65536);
	int stT0 = Core_Milliseconds();
	bool ok = false;
	try {
		RetroSaveStateScope guard;
		ok = sessLocal.SaveGame(RETRO_STATE_NAME, true, NULL, &mem);
	} catch ( idException &e ) {
		if (log_cb) log_cb(RETRO_LOG_ERROR, "[boom3] state: SaveGame threw: %s\n", e.error);
		return false;
	} catch ( ... ) {
		if (log_cb) log_cb(RETRO_LOG_ERROR, "[boom3] state: SaveGame threw\n");
		return false;
	}
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

	retro_state_len = mem.Length();
	retro_state_cache_tic = com_ticNumber;
	return true;
}

size_t retro_serialize_size(void)
{
	if (!RetroBuildState())
		return 0;
	return (size_t)retro_state_len;
}

bool retro_serialize(void *data_, size_t size)
{
	if (!RetroBuildState())
		return false;
	if (size < (size_t)retro_state_len)
		return false;
	memcpy(data_, retro_state_file.GetDataPtr(), retro_state_len);
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

	/* Invalidate before the restore rather than after it. The moment we
	 * commit to loading, the cache describes a game state that is being
	 * torn down, and that is true whether LoadGame succeeds, returns
	 * false, or throws. Doing it on the success path only left the throw
	 * path holding a cache still stamped with the current tic - and
	 * RetroBuildState's cache-hit check would then hand the frontend the
	 * pre-restore state back, after the engine had already dropped to the
	 * menu. */
	retro_state_cache_tic = -1;

	int stT0 = Core_Milliseconds();
	bool ok = false;
	try {
		RetroSaveStateScope guard;
		ok = sessLocal.LoadGame(RETRO_STATE_NAME, mem);
	} catch ( idException &e ) {
		/* LoadGame owns mem's lifetime through savegameFile and closes it
		   on every exit path it takes, including the error one, so it is
		   not ours to free here. */
		if (log_cb) log_cb(RETRO_LOG_ERROR, "[boom3] state restore: LoadGame threw: %s\n", e.error);
		return false;
	} catch ( ... ) {
		if (log_cb) log_cb(RETRO_LOG_ERROR, "[boom3] state restore: LoadGame threw\n");
		return false;
	}
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
      { "Gamepad Modern", RETRO_DEVICE_MODERN },
      { "RetroKeyboard/RetroMouse", RETRO_DEVICE_KEYBOARD }
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

   vfs_hybrid_init( environ_cb, log_cb );

   libretro_set_core_options(environ_cb,
         &libretro_supports_option_categories);
   cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);

   struct retro_perf_callback perf;
   if (environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf))
      perf_get_time_usec = perf.get_time_usec;
}
