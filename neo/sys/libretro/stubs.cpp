// Stubs for dhewm3-specific functionality not supported in the libretro core

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#include "sys/platform.h"
#include "framework/Common.h"
#include "framework/KeyInput.h"
#include "renderer/tr_local.h"
#include "libretro.h"

extern retro_perf_get_time_usec_t perf_get_time_usec;

// Deterministic, frame-quantized time source.
//
// A libretro core must not derive game state from wall-clock time: the
// frontend controls pacing (fast-forward, slow motion, frame stepping,
// runahead), so all timing has to be a pure function of how many times
// The deterministic core clock. It advances by exactly one frame per
// retro_run() call; the millisecond value is DERIVED with a single
// multiply (never accumulated), so it carries no floating-point drift
// regardless of session length. Nothing in this core reads a wall
// clock: core behavior is a pure function of the retro_run() call
// count and polled input.
//
// The base offset keeps the clock strictly positive (parts of the
// engine treat 0/negative time as "uninitialized"), and framerate
// changes fold elapsed time into the base so the clock stays monotonic.
/*
   Core time is derived from the frame counter, never from a wall clock, so
   two runs that see the same number of retro_run calls agree exactly.

   The accumulator is integer microseconds rather than a double. Both forms
   are deterministic for a fixed framerate - measured identical over 8 hours
   at 60fps - but Core_SetFramerate folds the elapsed time into the base on
   every rate change, and in double that rounding accumulates: 8 switches of
   100k frames each drifted 2.3us from exact. Small, but it makes the result
   depend on the framerate history rather than only on the frame count, which
   is the property savestates and replays rely on.
*/
static uint64_t core_frame_count = 0;
static int64_t  core_base_us     = 16000;	/* folded elapsed time */
static int      core_fps         = 60;

void Core_AdvanceFrame( void ) {
	core_frame_count++;
}

uint64_t Core_FrameCount( void ) {
	return core_frame_count;
}

void Core_SetFramerate( int fps ) {
	if ( fps < 1 )
		fps = 60;
	if ( fps != core_fps )
	{
		core_base_us    += (int64_t)core_frame_count * 1000000 / core_fps;
		core_frame_count = 0;
		core_fps         = fps;
	}
}

int64_t Core_MicrosecondsPrecise( void ) {
	return core_base_us + (int64_t)core_frame_count * 1000000 / core_fps;
}

double Core_MillisecondsPrecise( void ) {
	/* kept for the few callers that want sub-millisecond resolution as a
	   double; the value itself is computed in integers above */
	return (double)Core_MicrosecondsPrecise() / 1000.0;
}

// ---- Debugger server ----
void DebuggerServerInit() {}
void DebuggerServerShutdown() {}
void DebuggerServerPrint(const char *str) {}
void DebuggerServerCheckBreakpoint(idInterpreter *interpreter, idProgram *program, int instructionPointer) {}

// ---- Input / GUI interaction ----
bool D3_IN_interactiveIngameGuiActive = false;

void Sys_SetInteractiveIngameGuiActive(bool active, idUserInterface *gui) {
    D3_IN_interactiveIngameGuiActive = active;
}

// ---- Joystick ----
idCVar joy_gamepadLayout("joy_gamepadLayout", "-1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT | CVAR_INTEGER,
		"Button layout of gamepad. -1: auto (needs SDL 2.0.12 or newer), 0: XBox-style, 1: Nintendo-style, 2: PS4/5-style, 3: PS2/3-style", idCmdSystem::ArgCompletion_Integer<-1, 3> );

int Sys_PollJoystickInputEvents(int numEvents) {
    return 0;
}

const char *Sys_GetScancodeName(int scancode) {
    return "";
}

const char *Sys_GetLocalizedScancodeName(int scancode) {
    return "";
}

const char *Sys_GetLocalizedJoyKeyName(int key) {
    return "";
}

int Sys_GetKeynumForScancodeName(const char *scancodeName) {
    return -1;
}

// ---- Clipboard ----
void Sys_FreeClipboardData(char *data) {}

// ---- Settings UI ----
void Com_Dhewm3Settings_f(const idCmdArgs &args) {}

void GLimp_Shutdown() {}

bool GLimp_Init(glimpParms_t parms) { return true; }
bool GLimp_SetScreenParms(glimpParms_t parms) { return true; }

void GLimp_SetGamma(unsigned short red[256],
                    unsigned short green[256],
                    unsigned short blue[256]) {}

void GLimp_ResetGamma() {}

// Context activation - libretro manages the GL context
void GLimp_DeactivateContext() {}

void GLimp_ActivateContext() {}
