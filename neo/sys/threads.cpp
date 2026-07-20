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
#include "framework/Common.h"

#include "sys/sys_public.h"


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
typedef DWORD sys_threadID_t;
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_t sys_threadID_t;
#endif

#ifdef _WIN32
static CRITICAL_SECTION	mutex[MAX_CRITICAL_SECTIONS];
static HANDLE			cond[MAX_TRIGGER_EVENTS];
static bool				signaled[MAX_TRIGGER_EVENTS] = { };
static bool				waiting[MAX_TRIGGER_EVENTS] = { };
#else
static pthread_mutex_t	mutex[MAX_CRITICAL_SECTIONS];
static pthread_cond_t	cond[MAX_TRIGGER_EVENTS];
static bool				signaled[MAX_TRIGGER_EVENTS] = { };
static bool				waiting[MAX_TRIGGER_EVENTS] = { };
#endif

static xthreadInfo	*thread[MAX_THREADS] = { };
static size_t		thread_count = 0;

static bool mainThreadIDset = false;
static sys_threadID_t mainThreadID;

static sys_threadID_t GetCurrentThreadID() {
#ifdef _WIN32
	return GetCurrentThreadId();
#else
	return pthread_self();
#endif
}

/*
==============
Sys_Sleep
==============
*/
void Sys_Sleep(int msec) {
#ifdef _WIN32
	Sleep(msec);
#else
	usleep(msec * 1000);
#endif
}

/*
==================
Sys_InitThreads
==================
*/
void Sys_InitThreads() {
	mainThreadID = GetCurrentThreadID();
	mainThreadIDset = true;

	// critical sections
	for (int i = 0; i < MAX_CRITICAL_SECTIONS; i++) {
#ifdef _WIN32
		InitializeCriticalSection(&mutex[i]);
#else
		if (pthread_mutex_init(&mutex[i], NULL) != 0) {
			Sys_Printf("ERROR: pthread_mutex_init failed\n");
			return;
		}
#endif
	}

	// events
	for (int i = 0; i < MAX_TRIGGER_EVENTS; i++) {
#ifdef _WIN32
		cond[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (!cond[i]) {
			Sys_Printf("ERROR: CreateEvent failed\n");
			return;
		}
#else
		if (pthread_cond_init(&cond[i], NULL) != 0) {
			Sys_Printf("ERROR: pthread_cond_init failed\n");
			return;
		}
#endif

		signaled[i] = false;
		waiting[i] = false;
	}

	// threads
	for (int i = 0; i < MAX_THREADS; i++)
		thread[i] = NULL;

	thread_count = 0;
}

/*
==================
Sys_ShutdownThreads
==================
*/
void Sys_ShutdownThreads() {
	// threads
	for (int i = 0; i < MAX_THREADS; i++) {
		if (!thread[i])
			continue;

		Sys_Printf("WARNING: Thread '%s' still running\n", thread[i]->name);
		thread[i] = NULL;
	}

	// events
	for (int i = 0; i < MAX_TRIGGER_EVENTS; i++) {
#ifdef _WIN32
		CloseHandle(cond[i]);
		cond[i] = NULL;
#else
		pthread_cond_destroy(&cond[i]);
#endif
		signaled[i] = false;
		waiting[i] = false;
	}

	// critical sections
	for (int i = 0; i < MAX_CRITICAL_SECTIONS; i++) {
#ifdef _WIN32
		DeleteCriticalSection(&mutex[i]);
#else
		pthread_mutex_destroy(&mutex[i]);
#endif
	}
}

/*
==================
Sys_EnterCriticalSection
==================
*/
void Sys_EnterCriticalSection(int index) {
	assert(index >= 0 && index < MAX_CRITICAL_SECTIONS);

#ifdef _WIN32
	EnterCriticalSection(&mutex[index]);
#else
	if (pthread_mutex_lock(&mutex[index]) != 0)
		common->Error("ERROR: pthread_mutex_lock failed\n");
#endif
}

/*
==================
Sys_LeaveCriticalSection
==================
*/
void Sys_LeaveCriticalSection(int index) {
	assert(index >= 0 && index < MAX_CRITICAL_SECTIONS);

#ifdef _WIN32
	LeaveCriticalSection(&mutex[index]);
#else
	if (pthread_mutex_unlock(&mutex[index]) != 0)
		common->Error("ERROR: pthread_mutex_unlock failed\n");
#endif
}

/*
======================================================
wait and trigger events
we use a single lock to manipulate the conditions, CRITICAL_SECTION_SYS

the semantics match the win32 version. signals raised while no one is waiting stay raised until a wait happens (which then does a simple pass-through)

NOTE: we use the same mutex for all the events. I don't think this would become much of a problem
cond_wait unlocks atomically with setting the wait condition, and locks it back before exiting the function
the potential for time wasting lock waits is very low
======================================================
*/

/*
==================
Sys_WaitForEvent
==================
*/
void Sys_WaitForEvent(int index) {
	assert(index >= 0 && index < MAX_TRIGGER_EVENTS);

	Sys_EnterCriticalSection(CRITICAL_SECTION_SYS);

	assert(!waiting[index]);	// WaitForEvent from multiple threads? that wouldn't be good
	if (signaled[index]) {
		// emulate windows behaviour: signal has been raised already. clear and keep going
		signaled[index] = false;
	} else {
		waiting[index] = true;
#ifdef _WIN32
		Sys_LeaveCriticalSection(CRITICAL_SECTION_SYS);
		WaitForSingleObject(cond[index], INFINITE);
		Sys_EnterCriticalSection(CRITICAL_SECTION_SYS);
#else
		if (pthread_cond_wait(&cond[index], &mutex[CRITICAL_SECTION_SYS]) != 0)
			common->Error("ERROR: pthread_cond_wait failed\n");
#endif
		waiting[index] = false;
	}

	Sys_LeaveCriticalSection(CRITICAL_SECTION_SYS);
}

/*
==================
Sys_TriggerEvent
==================
*/
void Sys_TriggerEvent(int index) {
	assert(index >= 0 && index < MAX_TRIGGER_EVENTS);

	Sys_EnterCriticalSection(CRITICAL_SECTION_SYS);

	if (waiting[index]) {
#ifdef _WIN32
		SetEvent(cond[index]);
#else
		if (pthread_cond_signal(&cond[index]) != 0)
			common->Error("ERROR: pthread_cond_signal failed\n");
#endif
	} else {
		// emulate windows behaviour: if no thread is waiting, leave the signal on so next wait keeps going
		signaled[index] = true;
	}

	Sys_LeaveCriticalSection(CRITICAL_SECTION_SYS);
}

/*
==================
Sys_CreateThread
==================
*/

#ifdef _WIN32
struct win32_thread_args {
	xthread_t function;
	void *parms;
};

static unsigned __stdcall win32_thread_wrapper(void *arg) {
	win32_thread_args *args = (win32_thread_args *)arg;
	args->function(args->parms);
	delete args;
	return 0;
}
#endif

void Sys_CreateThread(xthread_t function, void *parms, xthreadInfo& info, const char *name) {
	Sys_EnterCriticalSection();

#ifdef _WIN32
	win32_thread_args *args = new win32_thread_args;
	args->function = function;
	args->parms = parms;
	HANDLE t = (HANDLE)_beginthreadex(NULL, 0, win32_thread_wrapper, args, 0, NULL);
	if (!t) {
		common->Error("ERROR: _beginthreadex for '%s' failed\n", name);
		delete args;
		Sys_LeaveCriticalSection();
		return;
	}
	info.name = name;
	info.threadHandle = (uintptr_t)t;
	info.threadId = GetThreadId(t);
#else
	pthread_t t;
	if (pthread_create(&t, NULL, (void*(*)(void*))function, parms) != 0) {
		common->Error("ERROR: pthread_create for '%s' failed\n", name);
		Sys_LeaveCriticalSection();
		return;
	}

	info.name = name;
	info.threadHandle = (uintptr_t)t;
	info.threadId = (uintptr_t)t;
#endif

	if (thread_count < MAX_THREADS)
		thread[thread_count++] = &info;
	else
		common->DPrintf("WARNING: MAX_THREADS reached\n");

	Sys_LeaveCriticalSection();
}

/*
==================
Sys_DestroyThread
==================
*/
void Sys_DestroyThread(xthreadInfo& info) {
	assert(info.threadHandle);

#ifdef _WIN32
	WaitForSingleObject((HANDLE)(uintptr_t)info.threadHandle, INFINITE);
	CloseHandle((HANDLE)(uintptr_t)info.threadHandle);
#else
	pthread_join((pthread_t)info.threadHandle, NULL);
#endif

	info.name = NULL;
	info.threadHandle = 0;
	info.threadId = 0;

	Sys_EnterCriticalSection();

	for (int i = 0; i < thread_count; i++) {
		if (&info == thread[i]) {
			thread[i] = NULL;

			int j;
			for (j = i + 1; j < thread_count; j++)
				thread[j - 1] = thread[j];

			thread[j - 1] = NULL;
			thread_count--;

			break;
		}
	}

	Sys_LeaveCriticalSection( );
}

/*
==================
Sys_GetThreadName
find the name of the calling thread
==================
*/
const char *Sys_GetThreadName(int *index) {
	const char *name;

	Sys_EnterCriticalSection();

	sys_threadID_t id = GetCurrentThreadID();

	for (int i = 0; i < thread_count; i++) {
#ifdef _WIN32
		if (id == (DWORD)thread[i]->threadId) {
#else
		if (pthread_equal(id, (pthread_t)thread[i]->threadHandle)) {
#endif
			if (index)
				*index = i;

			name = thread[i]->name;

			Sys_LeaveCriticalSection();

			return name;
		}
	}

	if (index)
		*index = -1;

	Sys_LeaveCriticalSection();

	return "main";
}


/*
==================
Sys_IsMainThread
returns true if the current thread is the main thread
==================
*/
bool Sys_IsMainThread() {
	if ( mainThreadIDset ) {
#ifdef _WIN32
		return GetCurrentThreadID() == mainThreadID;
#else
		return pthread_equal(GetCurrentThreadID(), mainThreadID);
#endif
	}
	// if this is called before mainThreadID is set, we haven't created
	// any threads yet so it should be the main thread
	return true;
}

