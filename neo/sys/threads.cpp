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

#include "sys/platform.h"

// libretro-common headers go before the idlib ones (see File.cpp)
#include <rthreads/rthreads.h>

#include "framework/Common.h"

#include "sys/sys_public.h"

/*
==================
Sys_CreateThread

Leaves info.threadHandle at 0 if the thread could not be started.
==================
*/
void Sys_CreateThread( xthread_t function, void *parms, xthreadInfo &info, const char *name ) {
	sthread_t *t = sthread_create( function, parms );

	if ( !t ) {
		common->Warning( "Sys_CreateThread: could not start '%s'", name );
		return;
	}

	info.name = name;
	info.threadHandle = (uintptr_t)t;
}

/*
==================
Sys_DestroyThread

Waits for the thread to return; telling it to is the caller's job.
==================
*/
void Sys_DestroyThread( xthreadInfo &info ) {
	if ( !info.threadHandle ) {
		return;
	}

	sthread_join( (sthread_t *)info.threadHandle );

	info.name = NULL;
	info.threadHandle = 0;
}
