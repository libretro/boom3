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
#include "framework/Licensee.h"
#include "framework/FileSystem.h"
#include "framework/KeyInput.h"
#include "renderer/ModelManager.h"
#include "renderer/tr_local.h"
#include "sys/libretro/retro_public.h"
#include "sys/sys_local.h"
#include "sound/snd_local.h"

#include "libretro_core_options.h"

#include <locale.h>

#include <glsm/glsm.h>

#ifdef HAVE_OPENAL
#include <AL/alc.h>
#include <AL/alext.h>
#endif

static LPALCRENDERSAMPLESSOFT d3_alcRenderSamplesSOFT = NULL;

#define RETRO_AUDIO_BUFFER_SIZE 2048
#define SAMPLE_RATE   	44100
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

volatile bool flushed = false;

extern struct retro_hw_render_callback hw_render;

static retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
retro_audio_sample_batch_t audio_batch_cb;
retro_environment_t environ_cb;
static retro_input_poll_t poll_cb;
static retro_input_state_t input_cb;
static struct retro_rumble_interface rumble;
retro_perf_get_time_usec_t perf_get_time_usec = NULL;
static bool libretro_supports_bitmasks = false;
static bool needs_gl_reinit = false;

static void audio_callback(void);

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
int16_t old_ret;

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

extern idCVar com_asyncSound;

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

static void update_variables(bool startup)
{
	struct retro_variable var;
	
	var.key = "doom_framerate";
	var.value = NULL;
	
	if (startup)
	{
		if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
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
	}
	
	var.key = "doom_resolution";
	var.value = NULL;
	
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && !initial_resolution_set)
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
	
	// We need setup sequence to be finished to change Cvar values
	if (!startup) {
		var.key = "doom_fps";
		var.value = NULL;

		if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		{
			extern idCVar com_showFPS;
			com_showFPS.SetBool(strcmp(var.value, "disabled"));
		}
		
	}

	var.key = "doom_mouse_sensitivity";
	var.value = NULL;

	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		mouse_sensitivity = (float)atof(var.value);

	/* Quality preset override */
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

   strncpy(buf, path, size - 1);
   buf[size - 1] = '\0';

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
	if (!first_boot) {
		// full screen / window toggle
        GLimp_UpdateWindowSize();
        R_ReinitOpenGL();
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
#ifndef _WIN32
int Sys_GetSystemRam( void ) {
#ifdef __linux__
	long	count, page_size;
	int		mb;

	count = sysconf( _SC_PHYS_PAGES );
	if ( count == -1 ) {
		common->Printf( "GetSystemRam: sysconf _SC_PHYS_PAGES failed\n" );
		return 512;
	}
	page_size = sysconf( _SC_PAGE_SIZE );
	if ( page_size == -1 ) {
		common->Printf( "GetSystemRam: sysconf _SC_PAGE_SIZE failed\n" );
		return 512;
	}
	mb= (int)( (double)count * (double)page_size / ( 1024 * 1024 ) );
	// round to the nearest 16Mb
	mb = ( mb + 8 ) & ~15;
	return mb;
#else
	return 1024;
#endif
}
#endif

/*
==================
Sys_DoStartProcess
if we don't fork, this function never returns
the no-fork lets you keep the terminal when you're about to spawn an installer

if the command contains spaces, system() is used. Otherwise the more straightforward execl ( system() blows though )
==================
*/
void Sys_DoStartProcess( const char *exeName, bool dofork ) {
	printf( "Sys_DoStartProcess: unimplemented\n" );
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

#ifdef _D3XP
static int fake_argc = 3;
static char *fake_argv[] = {
  (char *)"+set",
  (char *)"fs_game",
  (char *)"d3xp",
  nullptr
};
#else
static int fake_argc = 0;
static char *fake_argv[] = {
  nullptr
};
#endif

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

   strncpy(buf, base, size - 1);
   buf[size - 1] = '\0';

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
			int16_t ret    = 0;
			if (libretro_supports_bitmasks)
				ret = input_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
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
					Key_Event(K_JOY_TRIGGER2, 1);
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

		// Mouse buttons
		static const struct { unsigned retro_id; int doom_key; } mouse_buttons[] = {
			{ RETRO_DEVICE_ID_MOUSE_LEFT,      K_MOUSE1 },
			{ RETRO_DEVICE_ID_MOUSE_RIGHT,     K_MOUSE2 },
			{ RETRO_DEVICE_ID_MOUSE_MIDDLE,    K_MOUSE3 },
			{ RETRO_DEVICE_ID_MOUSE_WHEELUP,   K_MOUSE4 },
			{ RETRO_DEVICE_ID_MOUSE_WHEELDOWN, K_MOUSE5 },
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
	}
}

void Sys_SetMouse() {
    int rsx, rsy;
    int slowdown = 1024 * (framerate / 60.0f);

    // Always read physical mouse delta regardless of device mode
    {
        int dx = input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
        int dy = invert_y_axis * input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
        dx = (int)(dx * mouse_sensitivity);
        dy = (int)(dy * mouse_sensitivity);
        if (dx || dy)
            Mouse_Event(dx, dy);
    }

    if (doom_devices[0] == RETRO_DEVICE_KEYBOARD)
        return;

    // Right stick look for gamepad modes
    rsx = input_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
        RETRO_DEVICE_ID_ANALOG_X);
    rsy = invert_y_axis * input_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
        RETRO_DEVICE_ID_ANALOG_Y);

    if (rsx > analog_deadzone || rsx < -analog_deadzone) {
        if (rsx > analog_deadzone) rsx = rsx - analog_deadzone;
        if (rsx < -analog_deadzone) rsx = rsx + analog_deadzone;
    } else rsx = 0;
    if (rsy > analog_deadzone || rsy < -analog_deadzone) {
        if (rsy > analog_deadzone) rsy = rsy - analog_deadzone;
        if (rsy < -analog_deadzone) rsy = rsy + analog_deadzone;
    } else rsy = 0;

    Mouse_Event(rsx / slowdown, rsy / slowdown);
}

#define SOUND_BUFFER_SAMPLES (MIXBUFFER_SAMPLES * 4)
#define MAX_CHANNELS 2
static float snd_float_buf[SOUND_BUFFER_SAMPLES];
static int snd_buf_write = 0;
static int snd_buf_read  = 0;
static uint64_t sampletime = 0;
int idx = 0;

static void audio_callback(void)
{
	if (first_boot) return;

#ifdef HAVE_OPENAL
	ALsizei samples = (ALsizei)(SAMPLE_RATE / framerate);

    if (!d3_alcRenderSamplesSOFT && soundSystemLocal.openalDevice) {
        d3_alcRenderSamplesSOFT = (LPALCRENDERSAMPLESSOFT)
            alcGetProcAddress(soundSystemLocal.openalDevice, "alcRenderSamplesSOFT");
    }

    if (soundSystemLocal.isInitialized && d3_alcRenderSamplesSOFT && soundSystemLocal.openalDevice) {
        d3_alcRenderSamplesSOFT(soundSystemLocal.openalDevice, mixed_buffer[idx], samples);
        audio_batch_cb(mixed_buffer[idx], samples);
    } else {
        int16_t silence[RETRO_AUDIO_BUFFER_SIZE * 2] = {};
        audio_batch_cb(silence, samples);
    }

	idx = (idx + 1) % 2;
#else
    int samples_per_frame = (int)(SAMPLE_RATE / framerate);
    int total = samples_per_frame * MAX_CHANNELS;

    // only mix when we need more data
    int available = snd_buf_write - snd_buf_read;
    while (available < total) {
        float tmp[MIXBUFFER_SAMPLES * MAX_CHANNELS] = {};
        Sys_EnterCriticalSection();
        soundSystem->AsyncMix(sampletime, tmp);
        Sys_LeaveCriticalSection();
        sampletime += MIXBUFFER_SAMPLES;
        for (int i = 0; i < MIXBUFFER_SAMPLES * MAX_CHANNELS; i++) {
            snd_float_buf[snd_buf_write % SOUND_BUFFER_SAMPLES] = tmp[i];
            snd_buf_write++;
        }
        available = snd_buf_write - snd_buf_read;
    }

    static int16_t out[4096 * MAX_CHANNELS];
    static float tmp_out[4096 * MAX_CHANNELS];
    for (int i = 0; i < total; i++) {
        tmp_out[i] = snd_float_buf[snd_buf_read % SOUND_BUFFER_SAMPLES];
        snd_buf_read++;
    }
    SIMDProcessor->MixedSoundToSamples(out, tmp_out, total);
    audio_batch_cb(out, samples_per_frame);
#endif
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
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
	{
		if (log_cb)
			log_cb(RETRO_LOG_INFO, "XRGB8888 is not supported.\n");
		return false;
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
	
	int i;
	char *path_lower;
#if defined(_WIN32)
	char slash = '\\';
#else
	char slash = '/';
#endif
	bool use_external_savedir = false;
	const char *base_save_dir = NULL;
//	struct retro_keyboard_callback cb = { keyboard_cb };

	if (!info)
		return false;
	
	path_lower = strdup(info->path);
	
	for (i=0; path_lower[i]; ++i)
		path_lower[i] = tolower(path_lower[i]);
	
//	environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &cb);

	update_variables(true);
	
	extract_directory(g_rom_dir, info->path, sizeof(g_rom_dir));
	
	snprintf(g_pak_path, sizeof(g_pak_path), "%s", info->path);
	
	if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &base_save_dir) && base_save_dir)
	{
		if (strlen(base_save_dir) > 0)
		{
			// Get game 'name' (i.e. subdirectory)
			char game_name[1024];
			extract_basename(game_name, g_rom_dir, sizeof(game_name));
			
			// > Build final save path
			snprintf(g_save_dir, sizeof(g_save_dir), "%s%c%s", base_save_dir, slash, game_name);
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
   if (!libretro_shared_context)
      glsm_ctl(GLSM_CTL_STATE_BIND, NULL);

	if (first_boot) {
		network_init();
		//com_asyncSound.SetInteger(0);
		common->Init( fake_argc, fake_argv );
		first_boot = false;
		update_variables(false);
		gp_layout_set_bind(pending_layout);
	}
	
	bool updated = false;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
		update_variables(false);
	
	common->Frame();

   if (!libretro_shared_context)
      glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL);
	
	audio_process();
	audio_callback();
}

/*
===================
GLimp_SwapBuffers
===================
*/
void GLimp_SwapBuffers() {
   if (!libretro_shared_context)
      glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL);
	video_cb(RETRO_HW_FRAME_BUFFER_VALID, scr_width, scr_height, 0);
   if (!libretro_shared_context)
      glsm_ctl(GLSM_CTL_STATE_BIND, NULL);
	glBindFramebuffer(RARCH_GL_FRAMEBUFFER, hw_render.get_current_framebuffer());
}

void GLimp_UpdateWindowSize()
{
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
	if (common)
        common->Shutdown();
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

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data_, size_t size)
{
   return false;
}

bool retro_unserialize(const void *data_, size_t size)
{
   return false;
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
      { port_1, 4 },
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
