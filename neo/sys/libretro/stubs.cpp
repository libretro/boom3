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

double Sys_MillisecondsPrecise() {
    if (perf_get_time_usec)
        return (double)perf_get_time_usec() / 1000.0;
    // fallback implementation if the performance interface isn't available
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    static bool init = false;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = true;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (now.QuadPart * 1000.0) / freq.QuadPart;

#elif defined(__APPLE__)
    static mach_timebase_info_data_t timebase;
    static uint64_t start = 0;
    if (start == 0) {
        mach_timebase_info(&timebase);
        start = mach_absolute_time();
    }
    uint64_t now = mach_absolute_time() - start;
    double ms = (double)now * timebase.numer / timebase.denom / 1e6;
    return ms;

#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
#endif
}

void Sys_SleepUntilPrecise(double targetTime) {
#if 0
    double now;
    do {
        if (perf_get_time_usec)
            now = perf_get_time_usec() / 1000.0;
        else {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            now = (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
        }
        if (now < targetTime) {
            // spin or short sleep
            struct timespec req = { 0, 100000 }; // 0.1ms
            nanosleep(&req, NULL);
        }
    } while (now < targetTime);
#endif
    (void)targetTime;
}

// ---- ImGui hooks ----
namespace D3 {
namespace ImGuiHooks {
    void NewFrame() {}
    void EndFrame() {}
    void Shutdown() {}
}}

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

bool GLimp_Init(glimpParms_t parms) {
    GLimp_UpdateWindowSize();
    return true;
}

bool GLimp_SetScreenParms(glimpParms_t parms) {
    return true;
}

void GLimp_SetGamma(unsigned short red[256],
                    unsigned short green[256],
                    unsigned short blue[256]) {}

void GLimp_ResetGamma() {}

bool GLimp_SetWindowResizable(bool resizable) {
    return true;
}

bool GLimp_SetSwapInterval(int interval) {
    return true;
}

int GLimp_GetSwapInterval() {
    return 0;
}

float GLimp_GetDisplayRefresh() {
    return 60.0f; // libretro controls timing
}

void GLimp_GrabInput(int flags) {}

// Context activation - libretro manages the GL context
void GLimp_DeactivateContext() {}

void GLimp_ActivateContext() {}
