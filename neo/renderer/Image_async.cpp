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

===========================================================================
*/

#include "sys/platform.h"

// libretro-common C headers must come before the idlib headers below:
// idlib/Str.h installs macros such as `#define strcmp idStr::Cmp`, which
// otherwise corrupt the standard <cstring> that these headers pull in
// (breaks the libc++ `using ::strcmp;` on macOS/Xcode). Matches File.cpp.
#include "retro_spsc.h"

#include "framework/Common.h"
#include "framework/Session.h"

#include "renderer/tr_local.h"

/*
================================================================================

Asynchronous image loading

EndLevelLoad's media phase loads a few thousand images. Each load is a CPU
decode (R_LoadImageProgram + MD4 hash) followed by a GL upload. The decode
dominates and does not touch GL; the upload must run on the context thread.

This overlaps the two: a single worker thread runs the decode half
(idImage::DecodeImageData) for image N+1 while the main thread runs the
upload half (idImage::UploadImageData) for image N.

Communication is over two lock-free single-producer/single-consumer queues
(retro_spsc), so the cross-thread hand-off takes no mutex. The only shared
lock is the heap lock (Mem_EnableLock), enabled for the worker's lifetime,
which serialises the decoders' R_StaticAlloc scratch against main-thread
allocations. GL work stays entirely on the main thread.

Buffer ownership crosses the queues and is never shared concurrently: the
decoder allocates out.pic (R_StaticAlloc) on the worker, the main thread
frees it in UploadImageData. Because retro_spsc hands the record from one
thread to the other with a release/acquire barrier, that transfer is safe.

Fixed decode-was-fully-handled case: DecodeImageData returns false when the
image needs no upload (precompressed hit, or a failure that already called
MakeDefault). Those records come back with needUpload = false and the main
thread skips the upload.

Disabled by default; image_asyncLoad selects the classic synchronous path.
================================================================================
*/

// request: main -> worker
typedef struct {
	idImage *		image;
} asyncLoadReq_t;

// result: worker -> main
typedef struct {
	idImage *					image;
	idImage::decodedImageData_t	data;
	bool						decoded;	// false: decode failed (main does MakeDefault)
} asyncLoadRes_t;

// bounded number of in-flight decodes; keeps peak scratch memory in check
#define ASYNC_MAX_INFLIGHT	16

typedef struct {
	retro_spsc_t		reqQ;			// main -> worker
	retro_spsc_t		resQ;			// worker -> main
	retro_atomic_int_t	stop;			// main sets; worker polls
	xthreadInfo			thread;
	bool				running;
} asyncLoadState_t;

static asyncLoadState_t	async;

/*
====================
R_AsyncDecodeWorker

Worker thread: pop decode requests, run the CPU decode, push results back.
Runs until stop is set AND the request queue is drained.
====================
*/
static int R_AsyncDecodeWorker( void *parm ) {
	asyncLoadState_t *s = (asyncLoadState_t *)parm;

	for ( ; ; ) {
		asyncLoadReq_t req;
		if ( retro_spsc_read_avail( &s->reqQ ) >= sizeof( req ) ) {
			retro_spsc_read( &s->reqQ, &req, sizeof( req ) );

			// main may be blocked waiting for room in the request queue
			Sys_TriggerEvent( TRIGGER_EVENT_ASYNC_MAIN );

			asyncLoadRes_t res;
			res.image = req.image;
			res.decoded = req.image->DecodeImageDataWorker( res.data );

			// Block until there is room in the result queue (main drains
			// it) - but honour stop while blocked. If the main thread
			// left this loop early, nothing will ever drain resQ again,
			// and waiting unconditionally would turn a teardown into a
			// hang inside Sys_DestroyThread. Drop the decode we are
			// holding rather than block: main is no longer going to
			// upload it, and it owns no other reference.
			while ( retro_spsc_write_avail( &s->resQ ) < sizeof( res ) ) {
				if ( retro_atomic_load_acquire_int( &s->stop ) ) {
					if ( res.decoded && res.data.pic ) {
						R_StaticFree( res.data.pic );
						res.data.pic = NULL;
					}
					return 0;
				}
				Sys_WaitForEvent( TRIGGER_EVENT_ASYNC_WORKER );
			}
			retro_spsc_write( &s->resQ, &res, sizeof( res ) );
			Sys_TriggerEvent( TRIGGER_EVENT_ASYNC_MAIN );
			continue;
		}

		if ( retro_atomic_load_acquire_int( &s->stop ) ) {
			// stop requested and no more requests pending
			break;
		}

		// nothing to do: sleep until main submits or sets stop
		Sys_WaitForEvent( TRIGGER_EVENT_ASYNC_WORKER );
	}
	return 0;
}

/*
====================
R_AsyncLoadShutdown

Teardown for the decode worker, as a destructor so it runs on every exit
path from R_AsyncLoadImages - including the one nobody was thinking
about.

The body of that function calls into idImage::UploadImageData, which
reaches R_CreateImage, which calls common->Error( "R_CreateImage: not a
power of 2 image" ) among others. common->Error throws idException for
ERP_DROP, and idCommonLocal::Frame() catches it several frames up the
stack - so the throw is a normal, survivable event that skipped every
line below the loop.

What that left behind: the worker never told to stop and never reaped,
still running and blocked in Sys_WaitForEvent; Mem_EnableLock stuck on
for the rest of the session, putting a mutex on every allocation the
engine makes; and both queue buffers leaked.

The corruption came on the next map load. R_AsyncLoadImages would
retro_spsc_init the same file-static queues while the orphaned worker
was still polling them, so two workers shared one single-producer/
single-consumer ring. That invariant is the whole basis for the
lock-free hand-off: break it and records are lost or read twice, which
means decoded buffers double-freed in UploadImageData.

Also drains anything still sitting in the result queue. Those records
hold R_StaticAlloc'd pixels that only the main thread's upload frees.
====================
*/
class AsyncLoadShutdown {
public:
	AsyncLoadShutdown( asyncLoadState_t *s ) : st( s ) { }
	~AsyncLoadShutdown() {
		retro_atomic_store_release_int( &st->stop, 1 );
		Sys_TriggerEvent( TRIGGER_EVENT_ASYNC_WORKER );
		Sys_DestroyThread( st->thread );
		st->running = false;

		// the worker is joined, so this is now single-threaded: free any
		// decodes it finished that were never uploaded
		asyncLoadRes_t res;
		while ( retro_spsc_read_avail( &st->resQ ) >= sizeof( res ) ) {
			retro_spsc_read( &st->resQ, &res, sizeof( res ) );
			if ( res.decoded && res.data.pic ) {
				R_StaticFree( res.data.pic );
			}
		}

		Mem_EnableLock( false );
		retro_spsc_free( &st->reqQ );
		retro_spsc_free( &st->resQ );
	}
private:
	asyncLoadState_t *st;
};

/*
====================
R_AsyncLoadImages

Loads every 2D image in 'list' using the decode worker, overlapping decode
with GL upload. Non-2D images (cube maps, partial images) are not eligible
and must be handled by the caller on the synchronous path.

'list' holds only images that need loading and are known to be plain 2D
file images. The caller has already filtered generatorFunction/partial/
cube images out.
====================
*/
void R_AsyncLoadImages( idImage **list, int count ) {
	if ( count <= 0 ) {
		return;
	}

	// size each queue for the in-flight bound
	if ( !retro_spsc_init( &async.reqQ, ASYNC_MAX_INFLIGHT * sizeof( asyncLoadReq_t ) * 2 ) ) {
		// allocation failed: fall back to synchronous
		for ( int i = 0; i < count; i++ ) {
			list[ i ]->ActuallyLoadImage( true, false );
		}
		return;
	}
	if ( !retro_spsc_init( &async.resQ, ASYNC_MAX_INFLIGHT * sizeof( asyncLoadRes_t ) * 2 ) ) {
		retro_spsc_free( &async.reqQ );
		for ( int i = 0; i < count; i++ ) {
			list[ i ]->ActuallyLoadImage( true, false );
		}
		return;
	}

	retro_atomic_int_init( &async.stop, 0 );

	// the worker allocates decode scratch from the shared heap; serialise it
	Mem_EnableLock( true );

	Sys_CreateThread( R_AsyncDecodeWorker, &async, async.thread, "imageDecode" );
	async.running = true;

	// from here on every exit path, normal or thrown, runs the teardown
	AsyncLoadShutdown shutdown( &async );

	int submitted = 0;
	int completed = 0;
	int pacifier = 0;

	// A precompressed (.dds) hit uploads on the main thread inside
	// CheckPrecompressedImage, so it can't go to the worker. Resolve each
	// image's precompressed status on the main thread as we submit; images
	// that were fully handled that way are marked done immediately and never
	// enter the queue.
	while ( completed < count ) {
		// submit as many requests as the in-flight bound and queue allow
		while ( submitted < count &&
				( submitted - completed ) < ASYNC_MAX_INFLIGHT ) {
			idImage *img = list[ submitted ];

			// main-thread precompressed check (does GL if it hits)
			if ( globalImages->image_usePrecompressedTextures.GetBool() &&
					img->CheckPrecompressedImage( true ) ) {
				submitted++;
				completed++;
				if ( ( ++pacifier & 15 ) == 0 ) {
					session->PacifierUpdate();
				}
				continue;
			}

			// needs a real decode: hand to the worker (block only if the
			// request queue is momentarily full)
			if ( retro_spsc_write_avail( &async.reqQ ) < sizeof( asyncLoadReq_t ) ) {
				break;
			}
			asyncLoadReq_t req;
			req.image = img;
			retro_spsc_write( &async.reqQ, &req, sizeof( req ) );
			submitted++;
			Sys_TriggerEvent( TRIGGER_EVENT_ASYNC_WORKER );
		}

		// drain any finished decodes and upload them (GL, main thread)
		while ( retro_spsc_read_avail( &async.resQ ) >= sizeof( asyncLoadRes_t ) ) {
			asyncLoadRes_t res;
			retro_spsc_read( &async.resQ, &res, sizeof( res ) );
			if ( res.decoded ) {
				res.image->UploadImageData( res.data );
			} else {
				// decode failed - report and substitute the default image
				common->Warning( "Couldn't load image: %s", res.image->imgName.c_str() );
				res.image->MakeDefault();
			}
			completed++;

			// freed a result slot: the worker may be blocked on a full resQ
			Sys_TriggerEvent( TRIGGER_EVENT_ASYNC_WORKER );

			if ( ( ++pacifier & 15 ) == 0 ) {
				session->PacifierUpdate();
			}
		}

		if ( completed < count ) {
			// nothing ready: sleep until the worker posts a result or takes a
			// request. Both paths trigger ASYNC_MAIN, and the trigger is
			// sticky, so a signal raised between the drain above and this
			// wait is not lost.
			Sys_WaitForEvent( TRIGGER_EVENT_ASYNC_MAIN );
		}
	}
}
