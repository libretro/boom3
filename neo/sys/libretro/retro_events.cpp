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

struct joystick_poll_t
{
	int axis;
	int value;
	
	joystick_poll_t()
	{
	}
	
	joystick_poll_t( int a, int v )
	{
		axis = a;
		value = v;
	}
};

static idList<joystick_poll_t> joystick_polls;
static idList<kbd_poll_t> kbd_polls;
static idList<mouse_poll_t> mouse_polls;

static void PushConsoleEvent(const char *s) {
	char *b;
	size_t len;

	len = strlen(s) + 1;
	b = (char *)Mem_Alloc(len);
	strcpy(b, s);
}

static inline byte JoyToKey(int button) {
    static const int keymap[] = {
        /* KEY_A      */ K_ENTER,
        /* KEY_B      */ K_BACKSPACE,
        /* KEY_X      */ K_ALT,
        /* KEY_Y      */ K_CTRL,
        /* KEY_LSTICK */ K_JOY_BTN_LSTICK,
        /* KEY_RSTICK */ K_JOY_BTN_RSTICK,
        /* KEY_L      */ K_SHIFT,
        /* KEY_R      */ K_DEL,
        /* KEY_ZL     */ K_MOUSE2,
        /* KEY_ZR     */ K_MOUSE1,
        /* KEY_PLUS   */ K_ESCAPE,
        /* KEY_MINUS  */ K_TAB,
        /* KEY_DLEFT  */ K_LEFTARROW,
        /* KEY_DUP    */ K_UPARROW,
        /* KEY_DRIGHT */ K_RIGHTARROW,
        /* KEY_DDOWN  */ K_DOWNARROW,
    };

    if (button < 0 || button > 15) return 0;
    return keymap[button];
}

static int joy_mouse[2] = { 0 };
static int joy_mouse_prev[2] = { 0 };

static float touch_pos[2] = { 0 };
static float touch_pos_prev[2] = { 0 };
static bool touch_pressed = false;
static bool touch_pressed_prev = false;

// all the menus are now 4:3, so we got a different origin
// 960x720 is the 4:3 resolution we get

static int touch_w = 1280;
static int touch_h = 720;
static int menu_w = 960;
static int menu_ox = (1280 - 960) / 2;

extern idSession *session;
extern idSessionLocal sessLocal;

/*
=================
Sys_InitInput
=================
*/
void Sys_InitInput() {
	kbd_polls.SetGranularity(64);
	mouse_polls.SetGranularity(64);
	joystick_polls.SetGranularity(64);

	touch_w = glConfig.vidWidth;
	touch_h = glConfig.vidHeight;
	menu_w = (int)((double)touch_h / 3.0 * 4.0);
	menu_ox = (touch_w - menu_w) / 2;

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
	joystick_polls.Clear();
}

void Sys_ShowWindow( bool show ) {
}

bool Sys_IsWindowVisible( void ) {
	return true;
}

void Conbuf_AppendText( const char *pMsg )
{
#define CONSOLE_BUFFER_SIZE		16384

	char buffer[CONSOLE_BUFFER_SIZE*2];
	char *b = buffer;
	const char *msg;
	int bufLen;
	int i = 0;
	static unsigned long s_totalChars;

	//
	// if the message is REALLY long, use just the last portion of it
	//
	if ( strlen( pMsg ) > CONSOLE_BUFFER_SIZE - 1 )	{
		msg = pMsg + strlen( pMsg ) - CONSOLE_BUFFER_SIZE + 1;
	} else {
		msg = pMsg;
	}

	//
	// copy into an intermediate buffer
	//
	while ( msg[i] && ( ( b - buffer ) < sizeof( buffer ) - 1 ) ) {
		if ( msg[i] == '\n' && msg[i+1] == '\r' ) {
			b[0] = '\r';
			b[1] = '\n';
			b += 2;
			i++;
		} else if ( msg[i] == '\r' ) {
			b[0] = '\r';
			b[1] = '\n';
			b += 2;
		} else if ( msg[i] == '\n' ) {
			b[0] = '\r';
			b[1] = '\n';
			b += 2;
		} else if ( idStr::IsColor( &msg[i] ) ) {
			i++;
		} else {
			*b= msg[i];
			b++;
		}
		i++;
	}
	*b = 0;
	bufLen = b - buffer;

	s_totalChars += bufLen;
/*
	//
	// replace selection instead of appending if we're overflowing
	//
	if ( s_totalChars > 0x7000 ) {
		SendMessage( s_wcd.hwndBuffer, EM_SETSEL, 0, -1 );
		s_totalChars = bufLen;
	}

	//
	// put this text into the windows console
	//
	SendMessage( s_wcd.hwndBuffer, EM_LINESCROLL, 0, 0xffff );
	SendMessage( s_wcd.hwndBuffer, EM_SCROLLCARET, 0, 0 );
	SendMessage( s_wcd.hwndBuffer, EM_REPLACESEL, 0, (LPARAM) buffer );*/
}

/*
===========
Sys_InitScanTable
===========
*/
void Sys_InitScanTable() {
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

/*
===============
Sys_GrabMouseCursor
===============
*/
void Sys_GrabMouseCursor(bool grabIt) {
	int flags;

	if (grabIt)
		flags = GRAB_GRABMOUSE | GRAB_HIDECURSOR | GRAB_ENABLETEXTINPUT;
	else
		flags = GRAB_ENABLETEXTINPUT;

	GLimp_GrabInput(flags);
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
	if (snum >= SKEYS_LENGTH) return;
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
	byte key;
	
	if (snum == 0 && snum2 == 0) {
		Sys_SetKeys();
		Sys_SetMouse();
	} else if (snum2 == 0) {
		snum--;
		
		res.evType = SE_KEY;
		res.evValue = skeys[snum].key;
		res.evValue2 = skeys[snum].val;

		kbd_polls.Append(kbd_poll_t(skeys[snum].key, skeys[snum].val));
		
		return res;
	} else {
		snum2--;
		
		res.evType = SE_MOUSE;
		res.evValue = smouse[snum2].x;
		res.evValue2 = smouse[snum2].y;

		mouse_polls.Append(mouse_poll_t(M_DELTAX, smouse[snum2].x));
		mouse_polls.Append(mouse_poll_t(M_DELTAY, smouse[snum2].y));
		
		return res;
	}
	
	// drain char events after key/mouse
	if (schar_num > 0) {
		schar_num--;
		res.evType  = SE_CHAR;
		res.evValue = schar_buf[schar_num].key;
		return res;
	}

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
	joystick_polls.SetNum(0, false);
}

/*
================
Sys_GenerateEvents
================
*/
void Sys_GenerateEvents() {
	char *s = Sys_ConsoleInput();
}

/*
================
Sys_PollKeyboardInputEvents
================
*/
int Sys_PollKeyboardInputEvents() {
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
Sys_PollJoystickInputEvents
================
*/
int Sys_PollJoystickInputEvents() {
	return joystick_polls.Num();
}

/*
================
Sys_ReturnJoystickInputEvent
================
*/
int Sys_ReturnJoystickInputEvent(const int n, int &axis, int &value) {
	if (n >= joystick_polls.Num())
		return 0;

	axis = joystick_polls[n].axis;
	value = joystick_polls[n].value;
	return 1;
}

/*
================
Sys_EndJoystickInputEvents
================
*/
void Sys_EndJoystickInputEvents() {
	joystick_polls.SetNum(0, false);
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
