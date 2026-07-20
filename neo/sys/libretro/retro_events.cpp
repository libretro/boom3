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
along with Doom 3 Source Code.	If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/



#include "sys/platform.h"
#include "idlib/containers/List.h"
#include "idlib/Heap.h"
#include "framework/Common.h"
#include "framework/KeyInput.h"
#include "framework/Session.h"
#include "framework/Session_local.h"
#include "renderer/RenderSystem.h"
#include "renderer/tr_local.h"
#include "ui/DeviceContext.h"
#include "ui/UserInterface.h"

#include "sys/sys_public.h"

const char *kbdNames[] = {
	"english", "french", "german", "italian", "spanish", "turkish", "norwegian", "brazilian", NULL
};

idCVar in_kbd("in_kbd", "english", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "keyboard layout", kbdNames, idCmdSystem::ArgCompletion_String<kbdNames> );
extern idCVar r_scaleMenusTo43; // DG: for the "scale menus to 4:3" hack

struct kbd_poll_t {
	int key;
	bool state;

	kbd_poll_t() {
	}

	kbd_poll_t(int k, bool s) {
		key = k;
		state = s;
	}
};

struct mouse_poll_t {
	int action;
	int value;

	mouse_poll_t() {
	}

	mouse_poll_t(int a, int v) {
		action = a;
		value = v;
	}
};

static idList<kbd_poll_t> kbd_polls;
static idList<mouse_poll_t> mouse_polls;

// all the menus are now 4:3, so we got a different origin
// 960x720 is the 4:3 resolution we get

static int touch_w = 1280;
static int touch_h = 720;
static int menu_w = 960;

/*
=================
Sys_InitInput
=================
*/
void Sys_InitInput() {
	kbd_polls.SetGranularity(64);
	mouse_polls.SetGranularity(64);

	touch_w = glConfig.vidWidth;
	touch_h = glConfig.vidHeight;
	menu_w = (int)((double)touch_h / 3.0 * 4.0);

	in_kbd.SetModified();
}

/*
=================
Sys_ShutdownInput
=================
*/
void Sys_ShutdownInput() {
	kbd_polls.Clear();
	mouse_polls.Clear();
}

void Sys_ShowWindow( bool show ) {
}

bool Sys_IsWindowVisible( void ) {
	return true;
}

void Conbuf_AppendText( const char *pMsg )
{
	// This was a leftover of the Win32 console window: it reformatted the
	// message into a 32KB stack buffer whose only consumers were commented
	// out (dead SendMessage calls), and its bounds check was off by one -
	// the terminating '*b = 0' could write one byte past the buffer when a
	// two-byte "\r\n" expansion landed exactly at the end. The libretro core
	// has no console window; engine output already goes through Sys_Printf.
	(void)pMsg;
}

/*
===============
Sys_GetConsoleKey
===============
*/
unsigned char Sys_GetConsoleKey(bool shifted) {
	static unsigned char keys[2] = { '`', '~' };

	if (in_kbd.IsModified()) {
		idStr lang = in_kbd.GetString();

		if (lang.Length()) {
			if (!lang.Icmp("french")) {
				keys[0] = '<';
				keys[1] = '>';
			} else if (!lang.Icmp("german")) {
				keys[0] = '^';
				keys[1] = 176; // °
			} else if (!lang.Icmp("italian")) {
				keys[0] = '\\';
				keys[1] = '|';
			} else if (!lang.Icmp("spanish")) {
				keys[0] = 186; // º
				keys[1] = 170; // ª
			} else if (!lang.Icmp("turkish")) {
				keys[0] = '"';
				keys[1] = 233; // é
			} else if (!lang.Icmp("norwegian")) {
				keys[0] = 124; // |
				keys[1] = 167; // §
			} else if (!lang.Icmp("brazilian")) {
				keys[0] = '\'';
				keys[1] = '"';
			}
		}

		in_kbd.ClearModified();
	}

	return shifted ? keys[1] : keys[0];
}

/*
===============
Sys_MapCharForKey
===============
*/
unsigned char Sys_MapCharForKey(int key) {
	return key & 0xff;
}

extern void Sys_SetKeys();
extern void Sys_SetMouse();
struct simulated {
	int key;
	int val;
};

struct simulated2 {
	int x;
	int y;
};

#define SKEYS_LENGTH 32

simulated schar_buf[SKEYS_LENGTH];
simulated skeys[SKEYS_LENGTH];
simulated2 smouse[SKEYS_LENGTH];
uint8_t snum = 0;
uint8_t snum2 = 0;
uint8_t schar_num = 0;
// FIFO read cursors: events must be delivered in arrival order (the old
// drain popped from the tail, reversing same-frame ordering - e.g. the
// wheel's press+release pulse arrived as release-then-press and was eaten)
static uint8_t sread = 0;
static uint8_t s2read = 0;
static uint8_t scread = 0;

// drop any queued-but-undelivered events (used when the game is unloaded)
void LibRetro_ResetInputQueues(void) {
	snum = 0;
	snum2 = 0;
	schar_num = 0;
	sread = s2read = scread = 0;
}

void Char_Event(int c) {
    if (schar_num >= SKEYS_LENGTH) return;
    schar_buf[schar_num].key = c;
    schar_buf[schar_num].val = 0;
    schar_num++;
}

void Key_Event(int key, int val) {
	if (snum >= SKEYS_LENGTH) return;
	skeys[snum].key = key;
	skeys[snum].val = val;
	snum++;
}

void Mouse_Event(int x, int y) {
	// was checking snum (the *key* queue) - once more than 32 mouse events
	// arrived in a frame this wrote past the end of smouse[]
	if (snum2 >= SKEYS_LENGTH) return;
	smouse[snum2].x = x;
	smouse[snum2].y = y;
	snum2++;
}

/*
================
Sys_GetEvent
================
*/
static const sysEvent_t res_none = { SE_NONE, 0, 0, 0, NULL };
sysEvent_t Sys_GetEvent() {
	sysEvent_t res = { };
	static bool polled_this_pass = false;

	// Poll the frontend exactly once per drain pass. The old code returned
	// SE_NONE right after polling, which ended the event loop with the
	// freshly queued events still pending - every input arrived one frame
	// late. Now we poll and immediately fall through to draining, in FIFO
	// (arrival) order.
	if (sread >= snum && s2read >= snum2 && scread >= schar_num) {
		snum = snum2 = schar_num = 0;
		sread = s2read = scread = 0;
		if (polled_this_pass) {
			// queues fully drained after a poll: end of this pass
			polled_this_pass = false;
			return res_none;
		}
		Sys_SetKeys();
		Sys_SetMouse();
		polled_this_pass = true;
	}

	if (s2read < snum2) {
		res.evType = SE_MOUSE;
		res.evValue = smouse[s2read].x;
		res.evValue2 = smouse[s2read].y;

		mouse_polls.Append(mouse_poll_t(M_DELTAX, smouse[s2read].x));
		mouse_polls.Append(mouse_poll_t(M_DELTAY, smouse[s2read].y));

		s2read++;
		return res;
	}

	if (sread < snum) {
		res.evType = SE_KEY;
		res.evValue = skeys[sread].key;
		res.evValue2 = skeys[sread].val;

		kbd_polls.Append(kbd_poll_t(skeys[sread].key, skeys[sread].val));

		sread++;
		return res;
	}

	// drain char events after key/mouse
	if (scread < schar_num) {
		res.evType  = SE_CHAR;
		res.evValue = schar_buf[scread].key;
		scread++;
		return res;
	}

	polled_this_pass = false;
	return res_none;
}

/*
================
Sys_ClearEvents
================
*/
void Sys_ClearEvents() {
	kbd_polls.SetNum(0, false);
	mouse_polls.SetNum(0, false);
}

/*
================
Sys_GenerateEvents
================
*/
void Sys_GenerateEvents(void) {
	// called for its side effects; the returned line is not consumed here
	(void)Sys_ConsoleInput();
}

/*
================
Sys_PollKeyboardInputEvents
================
*/
int Sys_PollKeyboardInputEvents(void) {
	return kbd_polls.Num();
}

/*
================
Sys_ReturnKeyboardInputEvent
================
*/
int Sys_ReturnKeyboardInputEvent(const int n, int &key, bool &state) {
	if (n >= kbd_polls.Num())
		return 0;

	key = kbd_polls[n].key;
	state = kbd_polls[n].state;
	return 1;
}

/*
================
Sys_EndKeyboardInputEvents
================
*/
void Sys_EndKeyboardInputEvents() {
	kbd_polls.SetNum(0, false);
}

/*
================
Sys_PollMouseInputEvents
================
*/
int Sys_PollMouseInputEvents() {
	return mouse_polls.Num();
}

/*
================
Sys_ReturnMouseInputEvent
================
*/
int	Sys_ReturnMouseInputEvent(const int n, int &action, int &value) {
	if (n >= mouse_polls.Num())
		return 0;

	action = mouse_polls[n].action;
	value = mouse_polls[n].value;
	return 1;
}

/*
================
Sys_EndMouseInputEvents
================
*/
void Sys_EndMouseInputEvents() {
	mouse_polls.SetNum(0, false);
}
