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
#include "renderer/tr_local.h"
#ifdef BOOM3_VULKAN_RENDERER
#include "renderer/vk/vk_backend.h"
#endif

#include "renderer/VertexCache.h"

#ifdef HAVE_OPENGLES
#include "renderer/gles_compat.h"
#endif

static const int	FRAME_MEMORY_BYTES = 0x200000;
static const int	EXPAND_HEADERS = 1024;

// the streaming tier: each frame's share of the ring, and the size of the
// shared buffers static data is packed into
static const int	STREAM_FRAME_BYTES = 0x800000;
static const int	ARENA_BYTES = 0x1000000;

#ifndef GL_COPY_READ_BUFFER
#define GL_COPY_READ_BUFFER			0x8F36
#endif
#ifndef GL_COPY_WRITE_BUFFER
#define GL_COPY_WRITE_BUFFER		0x8F37
#endif
#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT		0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT			0x0080
#endif
#ifndef GL_MAP_INVALIDATE_RANGE_BIT
#define GL_MAP_INVALIDATE_RANGE_BIT	0x0004
#endif
#ifndef GL_MAP_UNSYNCHRONIZED_BIT
#define GL_MAP_UNSYNCHRONIZED_BIT	0x0020
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE	0x9117
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT	0x00000001
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED			0x911A
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED		0x911C
#endif

idCVar idVertexCache::r_showVertexCache( "r_showVertexCache", "0", CVAR_INTEGER|CVAR_RENDERER, "" );
idCVar idVertexCache::r_vertexBufferMegs( "r_vertexBufferMegs", "32", CVAR_INTEGER|CVAR_RENDERER, "" );

idVertexCache		vertexCache;

/*
==============
R_ListVertexCache_f
==============
*/
static void R_ListVertexCache_f( const idCmdArgs &args ) {
	vertexCache.List();
}

/*
==============
idVertexCache::ActuallyFree
==============
*/
void idVertexCache::ActuallyFree( vertCache_t *block ) {
	if (!block) {
		common->Error( "idVertexCache Free: NULL pointer" );
	}

	if ( block->user ) {
		// let the owner know we have purged it
		*block->user = NULL;
		block->user = NULL;
	}

	// temp blocks are in a shared space that won't be freed
	if ( block->tag != TAG_TEMP ) {
		staticAllocTotal -= block->size;
		staticCountTotal--;

		if ( block->arena >= 0 ) {
			// the range goes back to its buffer; the header keeps its own
			range_alloc_free( &arenas[block->arena].ranges, (unsigned int)block->offset, (unsigned int)block->size );
			block->arena = -1;
			block->vbo = block->ownVbo;
			block->offset = 0;
#ifdef BOOM3_VULKAN_RENDERER
		} else if ( vulkan && block->vbo ) {
			vkb_static_free( block->vbo, (unsigned)block->offset, (size_t)block->size );
			block->vbo = 0;
			block->offset = 0;
#endif
		} else if ( block->vbo ) {
		} else if ( block->virtMem ) {
			Mem_Free( block->virtMem );
			block->virtMem = NULL;
		}
	}
	block->tag = TAG_FREE;		// mark as free

	// unlink stick it back on the free list
	block->next->prev = block->prev;
	block->prev->next = block->next;

	// stick it on the front of the free list so it will be reused immediately
	block->next = freeStaticHeaders.next;
	block->prev = &freeStaticHeaders;

	block->next->prev = block;
	block->prev->next = block;
}

/*
==============
idVertexCache::Position

this will be a real pointer with virtual memory,
but it will be an int offset cast to a pointer with
ARB_vertex_buffer_object

The ARB_vertex_buffer_object will be bound
==============
*/
void *idVertexCache::Position( vertCache_t *buffer ) {
	if ( !buffer || buffer->tag == TAG_FREE ) {
		common->FatalError( "idVertexCache::Position: bad vertCache_t (while drawing: <unknown>)");
	}

	// the ARB vertex object just uses an offset
	if ( buffer->vbo ) {
		if ( r_showVertexCache.GetInteger() == 2 ) {
			if ( buffer->tag == TAG_TEMP ) {
				common->Printf( "GL_ARRAY_BUFFER_ARB = %i + %i (%i bytes)\n", buffer->vbo, (int)buffer->offset, buffer->size );
			} else {
				common->Printf( "GL_ARRAY_BUFFER_ARB = %i (%i bytes)\n", buffer->vbo, buffer->size );
			}
		}
		if ( buffer->indexBuffer ) {
			BindElement( buffer->vbo );
		} else {
			BindArray( buffer->vbo );
		}
		return (void *)buffer->offset;
	}

	// virtual memory is a real pointer
	return (void *)((byte *)buffer->virtMem + buffer->offset);
}

void idVertexCache::UnbindIndex() {
	BindElement( 0 );
}

/*
==============
idVertexCache bindings

Every draw sets its attribute pointers through Position(), several per
draw and all in the same buffer, so the binds that change nothing are
skipped.  Uploads (Alloc, AllocFrameTemp) bind unconditionally: they run
in the frontend, where the libretro frontend's state binding may have
moved the bindings since the backend last ran.
==============
*/
void idVertexCache::InvalidateBindings() {
	boundArray = UNKNOWN_BINDING;
	boundElement = UNKNOWN_BINDING;
}

void idVertexCache::BindArray( GLuint vbo ) {
	if ( boundArray != vbo ) {
		qglBindBufferARB( GL_ARRAY_BUFFER_ARB, vbo );
		boundArray = vbo;
	}
}

void idVertexCache::BindElement( GLuint vbo ) {
	if ( boundElement != vbo ) {
		qglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, vbo );
		boundElement = vbo;
	}
}

void idVertexCache::ForceBindArray( GLuint vbo ) {
	qglBindBufferARB( GL_ARRAY_BUFFER_ARB, vbo );
	boundArray = vbo;
}

void idVertexCache::ForceBindElement( GLuint vbo ) {
	qglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, vbo );
	boundElement = vbo;
}


//================================================================================

/*
===========
idVertexCache::Init
===========
*/
void idVertexCache::Init() {
	cmdSystem->AddCommand( "listVertexCache", R_ListVertexCache_f, CMD_FL_RENDERER, "lists vertex cache" );

	if ( r_vertexBufferMegs.GetInteger() < 8 ) {
		r_vertexBufferMegs.SetInteger( 8 );
	}

	virtualMemory = false;
	InvalidateBindings();

	// use ARB_vertex_buffer_object unless explicitly disabled
	if( r_useVertexBuffers.GetInteger() && glConfig.ARBVertexBufferObjectAvailable ) {
		common->Printf( "using ARB_vertex_buffer_object memory\n" );
		/* Indexes go in buffer objects too: the GLES backends draw stencil
		 * shadows only from an index buffer, and on every backend a
		 * client-side index array is copied by the driver on each draw.
		 * Set here, after the config ran, so an old archived 0 cannot
		 * turn it back off; the console still can, for debugging. */
		r_useIndexBuffers.SetBool( true );
	} else {
		virtualMemory = true;
		r_useIndexBuffers.SetBool( false );
		common->Printf( "WARNING: vertex array range in virtual memory (SLOW)\n" );
	}

	// initialize the cache memory blocks
	freeStaticHeaders.next = freeStaticHeaders.prev = &freeStaticHeaders;
	staticHeaders.next = staticHeaders.prev = &staticHeaders;
	freeDynamicHeaders.next = freeDynamicHeaders.prev = &freeDynamicHeaders;
	dynamicHeaders.next = dynamicHeaders.prev = &dynamicHeaders;
	deferredFreeList.next = deferredFreeList.prev = &deferredFreeList;

	// set up the dynamic frame memory
	frameBytes = FRAME_MEMORY_BYTES;
	staticAllocTotal = 0;

#ifdef BOOM3_VULKAN_RENDERER
	vulkan = R_VulkanRenderer();
	if ( vulkan ) {
		// the device's memory: nothing of GL here
		virtualMemory = false;
		streaming = false;
		tempBuffers[0] = tempBuffers[1] = NULL;
		EndFrame();
		return;
	}
#endif
	InitStreaming();
	if ( streaming ) {
		// the frame's data goes through the ring, not the temp buffers
		frameBytes = ringSegmentBytes;
		tempBuffers[0] = tempBuffers[1] = NULL;
		EndFrame();
		return;
	}

	byte	*junk = (byte *)Mem_Alloc( frameBytes );
	for ( int i = 0 ; i < NUM_VERTEX_FRAMES ; i++ ) {
		allocatingTempBuffer = true;	// force the alloc to use GL_STREAM_DRAW_ARB
		Alloc( junk, frameBytes, &tempBuffers[i] );
		allocatingTempBuffer = false;
		tempBuffers[i]->tag = TAG_FIXED;
		// unlink these from the static list, so they won't ever get purged
		tempBuffers[i]->next->prev = tempBuffers[i]->prev;
		tempBuffers[i]->prev->next = tempBuffers[i]->next;
	}
	Mem_Free( junk );

	EndFrame();
}

/*
===========
idVertexCache::PurgeAll

Used when toggling vertex programs on or off, because
the cached data isn't valid
===========
*/
void idVertexCache::PurgeAll() {
	while( staticHeaders.next != &staticHeaders ) {
		ActuallyFree( staticHeaders.next );
	}
}

/*
===========
idVertexCache::Shutdown
===========
*/
void idVertexCache::Shutdown() {
//	PurgeAll();	// !@#: also purge the temp buffers

	headerAllocator.Shutdown();
}

/*
===========
idVertexCache::Alloc
===========
*/
void idVertexCache::Alloc( void *data, int size, vertCache_t **buffer, bool indexBuffer ) {
	vertCache_t	*block;

	if ( size <= 0 ) {
		common->Error( "idVertexCache::Alloc: size = %i\n", size );
	}

	// if we can't find anything, it will be NULL
	*buffer = NULL;

	// if we don't have any remaining unused headers, allocate some more
	if ( freeStaticHeaders.next == &freeStaticHeaders ) {

		for ( int i = 0; i < EXPAND_HEADERS; i++ ) {
			block = headerAllocator.Alloc();
			block->next = freeStaticHeaders.next;
			block->prev = &freeStaticHeaders;
			block->next->prev = block;
			block->prev->next = block;

			block->vbo = 0;
			if( !virtualMemory && !vulkan ) {
				qglGenBuffersARB( 1, & block->vbo );
			}
			block->ownVbo = block->vbo;
			block->arena = -1;
		}
	}

	// move it from the freeStaticHeaders list to the staticHeaders list
	block = freeStaticHeaders.next;
	block->next->prev = block->prev;
	block->prev->next = block->next;
	block->next = staticHeaders.next;
	block->prev = &staticHeaders;
	block->next->prev = block;
	block->prev->next = block;

	block->size = size;
	block->offset = 0;
	block->tag = TAG_USED;

	// save data for debugging
	staticAllocThisFrame += block->size;
	staticCountThisFrame++;
	staticCountTotal++;
	staticAllocTotal += block->size;

	// this will be set to zero when it is purged
	block->user = buffer;
	*buffer = block;

	// allocation doesn't imply used-for-drawing, because at level
	// load time lots of things may be created, but they aren't
	// referenced by the GPU yet, and can be purged if needed.
	block->frameUsed = currentFrame - NUM_VERTEX_FRAMES;

	block->indexBuffer = indexBuffer;
	block->indexType = indexBuffer ? GL_INDEX_TYPE : 0;

	// the streaming tier packs it into a shared buffer when it fits one
	if ( streaming && !allocatingTempBuffer && ArenaAlloc( block, data, size, indexBuffer ) ) {
		return;
	}
#ifdef BOOM3_VULKAN_RENDERER
	if ( vulkan ) {
		if ( !VulkanAlloc( block, data, size, indexBuffer ) ) {
			block->virtMem = Mem_Alloc( size );
			SIMDProcessor->Memcpy( block->virtMem, data, size );
		}
		return;
	}
#endif

	// copy the data
	if ( block->vbo ) {
		if ( indexBuffer ) {
			/* Indexes that all fit in 16 bits are stored in 16: half the
			 * memory and half the fetch bandwidth for the same triangles,
			 * which is nearly every surface.  The scan is once per upload. */
			const glIndex_t *src = (const glIndex_t *)data;
			const int count = size / (int)sizeof( glIndex_t );
			bool small = ( size % (int)sizeof( glIndex_t ) ) == 0;
			int i;
			for ( i = 0; i < count && small; i++ ) {
				small = (unsigned int)src[i] <= 0xffffu;
			}
			ForceBindElement( block->vbo );
			if ( small ) {
				unsigned short *dst = (unsigned short *)Mem_Alloc( count * sizeof( unsigned short ) );
				for ( i = 0; i < count; i++ ) {
					dst[i] = (unsigned short)src[i];
				}
				qglBufferDataARB( GL_ELEMENT_ARRAY_BUFFER_ARB, (GLsizeiptrARB)( count * sizeof( unsigned short ) ), dst, GL_STATIC_DRAW_ARB );
				Mem_Free( dst );
				block->indexType = GL_UNSIGNED_SHORT;
			} else {
				qglBufferDataARB( GL_ELEMENT_ARRAY_BUFFER_ARB, (GLsizeiptrARB)size, data, GL_STATIC_DRAW_ARB );
			}
		} else {
			ForceBindArray( block->vbo );
			if ( allocatingTempBuffer ) {
				qglBufferDataARB( GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)size, data, GL_STREAM_DRAW_ARB );
			} else {
				qglBufferDataARB( GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)size, data, GL_STATIC_DRAW_ARB );
			}
		}
	} else {
		block->virtMem = Mem_Alloc( size );
		SIMDProcessor->Memcpy( block->virtMem, data, size );
	}
}

/*
===========
idVertexCache::AllocStream
===========
*/
void idVertexCache::AllocStream( void *data, int size, vertCache_t **buffer ) {
	allocatingTempBuffer = true;
	Alloc( data, size, buffer );
	allocatingTempBuffer = false;
}

/*
===========
idVertexCache::Touch
===========
*/
void idVertexCache::Touch( vertCache_t *block ) {
	if ( !block ) {
		common->Error( "idVertexCache Touch: NULL pointer" );
	}

	if ( block->tag == TAG_FREE ) {
		common->FatalError( "idVertexCache Touch: freed pointer" );
	}
	if ( block->tag == TAG_TEMP ) {
		common->FatalError( "idVertexCache Touch: temporary pointer" );
	}

	block->frameUsed = currentFrame;

	// move to the head of the LRU list
	block->next->prev = block->prev;
	block->prev->next = block->next;

	block->next = staticHeaders.next;
	block->prev = &staticHeaders;
	staticHeaders.next->prev = block;
	staticHeaders.next = block;
}

/*
===========
idVertexCache::Free
===========
*/
void idVertexCache::Free( vertCache_t *block ) {
	if (!block) {
		return;
	}

	if ( block->tag == TAG_FREE ) {
		common->FatalError( "idVertexCache Free: freed pointer" );
	}
	if ( block->tag == TAG_TEMP ) {
		common->FatalError( "idVertexCache Free: temporary pointer" );
	}

	// this block still can't be purged until the frame count has expired,
	// but it won't need to clear a user pointer when it is
	block->user = NULL;

	block->next->prev = block->prev;
	block->prev->next = block->next;

	block->next = deferredFreeList.next;
	block->prev = &deferredFreeList;
	deferredFreeList.next->prev = block;
	deferredFreeList.next = block;
}

/*
===========
idVertexCache::AllocFrameTemp

A frame temp allocation must never be allowed to fail due to overflow.
We can't simply sync with the GPU and overwrite what we have, because
there may still be future references to dynamically created surfaces.
===========
*/
vertCache_t	*idVertexCache::AllocFrameTemp( void *data, int size ) {
	vertCache_t	*block;

	if ( size <= 0 ) {
		common->Error( "idVertexCache::AllocFrameTemp: size = %i\n", size );
	}

	// in the ring a vertex block starts at a multiple of every stride, for
	// base-vertex draws
	const int ringBytes = streaming ? ( ( size + VERTEX_ALIGN - 1 ) / VERTEX_ALIGN ) * VERTEX_ALIGN : size;
	if ( !vulkan && ( streaming ? ringUsed + ringBytes > ringSegmentBytes : dynamicAllocThisFrame + size > frameBytes ) ) {
		// if we don't have enough room in the temp block, allocate a static block,
		// but immediately free it so it will get freed at the next frame
		tempOverflow = true;
		if ( streaming ) {
			AllocStream( data, size, &block );
		} else {
			Alloc( data, size, &block );
		}
		Free( block);
		return block;
	}

#ifdef BOOM3_VULKAN_RENDERER
	void *ringPtr = NULL;
	unsigned ringBuf = 0, ringOff = 0;
	if ( vulkan ) {
		ringPtr = vkb_ring_alloc( (size_t)size, VERTEX_ALIGN, &ringBuf, &ringOff );
		if ( !ringPtr ) {
			tempOverflow = true;
			Alloc( data, size, &block );
			Free( block );
			return block;
		}
	}
#endif

	// this data is just going on the shared dynamic list

	// if we don't have any remaining unused headers, allocate some more
	if ( freeDynamicHeaders.next == &freeDynamicHeaders ) {

		for ( int i = 0; i < EXPAND_HEADERS; i++ ) {
			block = headerAllocator.Alloc();
			block->next = freeDynamicHeaders.next;
			block->prev = &freeDynamicHeaders;
			block->next->prev = block;
			block->prev->next = block;
			block->ownVbo = 0;
			block->arena = -1;
		}
	}

	// move it from the freeDynamicHeaders list to the dynamicHeaders list
	block = freeDynamicHeaders.next;
	block->next->prev = block->prev;
	block->prev->next = block->next;
	block->next = dynamicHeaders.next;
	block->prev = &dynamicHeaders;
	block->next->prev = block;
	block->prev->next = block;

	block->size = size;
	block->tag = TAG_TEMP;
	block->indexBuffer = false;
	block->indexType = 0;
	block->offset = dynamicAllocThisFrame;
	dynamicAllocThisFrame += block->size;
	dynamicCountThisFrame++;
	block->user = NULL;
	block->frameUsed = 0;

	if ( streaming ) {
		block->virtMem = NULL;
		block->vbo = ringBuffer;
		block->offset = ringSegment * ringSegmentBytes + ringUsed;
		ringUsed += ringBytes;
		SIMDProcessor->Memcpy( RingMemory( (int)block->offset, size ), data, size );
		RingRelease();
		return block;
	}
#ifdef BOOM3_VULKAN_RENDERER
	if ( vulkan ) {
		block->virtMem = NULL;
		block->vbo = ringBuf;
		block->offset = ringOff;
		SIMDProcessor->Memcpy( ringPtr, data, size );
		return block;
	}
#endif

	// copy the data
	block->virtMem = tempBuffers[listNum]->virtMem;
	block->vbo = tempBuffers[listNum]->vbo;

	if ( block->vbo ) {
		ForceBindArray( block->vbo );
		qglBufferSubDataARB( GL_ARRAY_BUFFER_ARB, block->offset, (GLsizeiptrARB)size, data );
	} else {
		SIMDProcessor->Memcpy( (byte *)block->virtMem + block->offset, data, size );
	}

	return block;
}

/*
===========
idVertexCache::EndFrame
===========
*/
void idVertexCache::EndFrame() {
	// display debug information
	if ( r_showVertexCache.GetBool() ) {
		int	staticUseCount = 0;
		int staticUseSize = 0;

		for ( vertCache_t *block = staticHeaders.next ; block != &staticHeaders ; block = block->next ) {
			if ( block->frameUsed == currentFrame ) {
				staticUseCount++;
				staticUseSize += block->size;
			}
		}

		const char *frameOverflow = tempOverflow ? "(OVERFLOW)" : "";

		common->Printf( "vertex dynamic:%i=%ik%s, static alloc:%i=%ik used:%i=%ik total:%i=%ik\n",
			dynamicCountThisFrame, dynamicAllocThisFrame/1024, frameOverflow,
			staticCountThisFrame, staticAllocThisFrame/1024,
			staticUseCount, staticUseSize/1024,
			staticCountTotal, staticAllocTotal/1024 );
	}

	currentFrame = tr.frameCount;
	listNum = currentFrame % NUM_VERTEX_FRAMES;

	if ( streaming ) {
		RingEndFrame();
		ForceBindArray( 0 );
		ForceBindElement( 0 );
	} else if ( vulkan ) {
		// the device's ring turns with its frames
	} else if( !virtualMemory ) {
		/* The temp buffer this frame streams into was last drawn from
		 * NUM_VERTEX_FRAMES frames ago and may still be in flight: orphan
		 * its storage before the first upload, so the driver hands back
		 * fresh memory instead of syncing or copying under the
		 * glBufferSubData calls.  Only the old contents are dropped, and
		 * nothing reads them again. */
		ForceBindArray( tempBuffers[listNum]->vbo );
		qglBufferDataARB( GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)frameBytes, NULL, GL_STREAM_DRAW_ARB );

		// unbind vertex buffers so normal virtual memory will be used in case
		// r_useVertexBuffers / r_useIndexBuffers
		ForceBindArray( 0 );
		ForceBindElement( 0 );
	}
	staticAllocThisFrame = 0;
	staticCountThisFrame = 0;
	dynamicAllocThisFrame = 0;
	dynamicCountThisFrame = 0;
	tempOverflow = false;

	// free all the deferred free headers
	while( deferredFreeList.next != &deferredFreeList ) {
		ActuallyFree( deferredFreeList.next );
	}

	// free all the frame temp headers
	vertCache_t	*block = dynamicHeaders.next;
	if ( block != &dynamicHeaders ) {
		block->prev = &freeDynamicHeaders;
		dynamicHeaders.prev->next = freeDynamicHeaders.next;
		freeDynamicHeaders.next->prev = dynamicHeaders.prev;
		freeDynamicHeaders.next = block;

		dynamicHeaders.next = dynamicHeaders.prev = &dynamicHeaders;
	}
}

/*
=============
idVertexCache::List
=============
*/
void idVertexCache::List( void ) {
	int	numActive = 0;

	vertCache_t *block;
	for ( block = staticHeaders.next ; block != &staticHeaders ; block = block->next)
		numActive++;

	int	numFreeStaticHeaders = 0;
	for ( block = freeStaticHeaders.next ; block != &freeStaticHeaders ; block = block->next ) {
		numFreeStaticHeaders++;
	}

	int	numFreeDynamicHeaders = 0;
	for ( block = freeDynamicHeaders.next ; block != &freeDynamicHeaders ; block = block->next ) {
		numFreeDynamicHeaders++;
	}

	common->Printf( "%i megs working set\n", r_vertexBufferMegs.GetInteger() );
	common->Printf( "%i dynamic temp buffers of %ik\n", NUM_VERTEX_FRAMES, frameBytes / 1024 );
	common->Printf( "%5i active static headers\n", numActive );
	common->Printf( "%5i free static headers\n", numFreeStaticHeaders );
	common->Printf( "%5i free dynamic headers\n", numFreeDynamicHeaders );

	if ( streaming ) {
		int i;
		common->Printf( "Ring: %i segments of %ik, %s\n", RING_SEGMENTS, ringSegmentBytes / 1024,
			ringMemory ? "persistently mapped" : "mapped per write" );
		for ( i = 0; i < numArenas; i++ ) {
			common->Printf( "Shared %s buffer %i: %ik of %ik used\n", arenas[i].indexes ? "index" : "vertex",
				i, arenas[i].ranges.used / 1024, arenas[i].ranges.capacity / 1024 );
		}
	}
	if ( !virtualMemory  ) {
		common->Printf( "Vertex cache is in ARB_vertex_buffer_object memory (FAST).\n");
	} else {
		common->Printf( "Vertex cache is in virtual memory (SLOW)\n" );
	}

	if ( r_useIndexBuffers.GetBool() ) {
		common->Printf( "Index buffers are accelerated.\n" );
	} else {
		common->Printf( "Index buffers are not used.\n" );
	}
}

/*
=============
idVertexCache::IsFast

just for gfxinfo printing
=============
*/
bool idVertexCache::IsFast() {
	if ( virtualMemory ) {
		return false;
	}
	return true;
}

/*
==============================================================================

The streaming tier (the ES 3.1 / GL 4.3 renderer)

Static data is packed into a few large shared buffers, so the surfaces of a
pass mostly share one buffer: their attribute pointers are set once and each
draws with a base vertex.  Uploads into a shared buffer go through the ring
and a GPU-side copy, which the driver orders after the draws already queued
from that buffer, so the CPU never waits on it and nothing duplicates it.

Every frame's temporary data is written straight into one ring buffer,
persistently mapped where the context allows.  The ring holds
RING_SEGMENTS frames; a fence at the end of each frame tells when the GPU
is done with its segment, which the frontend then reuses.

==============================================================================
*/

/*
==============
idVertexCache::InitStreaming
==============
*/
void idVertexCache::InitStreaming() {
	int i;

	// what the previous context had died with it
	streaming = false;
	numArenas = 0;
	ringBuffer = 0;
	ringMemory = NULL;
	ringSegment = 0;
	ringUsed = 0;
	ringWritable = true;
	ringPendingBytes = 0;
	ringPendingMapped = false;
	for ( i = 0; i < RING_SEGMENTS; i++ ) {
		ringFences[i] = NULL;
		ringFenceFailed[i] = false;
	}
	for ( i = 0; i < MAX_ARENAS; i++ ) {
		range_alloc_deinit( &arenas[i].ranges );
		arenas[i].buffer = 0;
	}

	if ( virtualMemory || glConfig.renderer != R_RENDERER_ES31 ) {
		return;
	}

	// segments start at a multiple of VERTEX_ALIGN and of 256, so the
	// blocks in them keep both alignments
	ringSegmentBytes = ( STREAM_FRAME_BYTES / ( VERTEX_ALIGN * 16 ) ) * ( VERTEX_ALIGN * 16 );
	arenaBytes = ARENA_BYTES;
	const int total = ringSegmentBytes * RING_SEGMENTS;

	qglGenBuffersARB( 1, &ringBuffer );
	qglBindBufferARB( GL_COPY_WRITE_BUFFER, ringBuffer );
	if ( glCaps.bufferStorage ) {
		const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
		glCaps.BufferStorage( GL_COPY_WRITE_BUFFER, total, NULL, flags );
		ringMemory = (byte *)glCaps.MapBufferRange( GL_COPY_WRITE_BUFFER, 0, total, flags );
		if ( !ringMemory ) {
			// immutable now: start over with a buffer that is mapped per write
			qglBindBufferARB( GL_COPY_WRITE_BUFFER, 0 );
			qglDeleteBuffersARB( 1, &ringBuffer );
			qglGenBuffersARB( 1, &ringBuffer );
			qglBindBufferARB( GL_COPY_WRITE_BUFFER, ringBuffer );
		}
	}
	if ( !ringMemory ) {
		qglBufferDataARB( GL_COPY_WRITE_BUFFER, total, NULL, GL_STREAM_DRAW_ARB );
	}
	qglBindBufferARB( GL_COPY_WRITE_BUFFER, 0 );

	streaming = true;
	common->Printf( "vertex cache: %i x %ik %s ring, static data in %ik shared buffers%s\n",
		RING_SEGMENTS, ringSegmentBytes / 1024, ringMemory ? "persistent" : "mapped",
		arenaBytes / 1024, glCaps.baseVertex ? ", base-vertex draws" : "" );
}

/*
==============
idVertexCache::RingMemory

Where to write size bytes at offset in the ring, until RingRelease.
==============
*/
void *idVertexCache::RingMemory( int offset, int size ) {
	if ( ringMemory ) {
		return ringMemory + offset;
	}
	ringPendingOffset = offset;
	ringPendingBytes = size;
	qglBindBufferARB( GL_COPY_WRITE_BUFFER, ringBuffer );
	void *p = glCaps.MapBufferRange( GL_COPY_WRITE_BUFFER, offset, size,
		GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
	ringPendingMapped = p != NULL;
	if ( p ) {
		return p;
	}
	// a driver that cannot map gets the bytes uploaded at release
	if ( ringScratchBytes < size ) {
		Mem_Free( ringScratch );
		ringScratchBytes = size;
		ringScratch = (byte *)Mem_Alloc( ringScratchBytes );
	}
	return ringScratch;
}

/*
==============
idVertexCache::RingRelease
==============
*/
void idVertexCache::RingRelease() {
	if ( ringMemory || !ringPendingBytes ) {
		return;
	}
	if ( ringPendingMapped ) {
		glCaps.UnmapBuffer( GL_COPY_WRITE_BUFFER );
	} else {
		qglBufferSubDataARB( GL_COPY_WRITE_BUFFER, ringPendingOffset, ringPendingBytes, ringScratch );
	}
	qglBindBufferARB( GL_COPY_WRITE_BUFFER, 0 );
	ringPendingBytes = 0;
	ringPendingMapped = false;
}

/*
==============
idVertexCache::RingEndFrame

Fence this frame and select a completed segment. With none ready,
the existing overflow uploads keep writes out of the live ring.
==============
*/
void idVertexCache::RingEndFrame() {
	if ( ringWritable ) {
		ringFences[ringSegment] = glCaps.FenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
		ringFenceFailed[ringSegment] = ringFences[ringSegment] == NULL;
	}

	ringWritable = false;
	ringUsed = ringSegmentBytes;
	for ( int n = 1; n <= RING_SEGMENTS; n++ ) {
		const int next = ( ringSegment + n ) % RING_SEGMENTS;
		if ( ringFenceFailed[next] ) {
			continue;
		}
		gl_caps_sync_t fence = ringFences[next];
		if ( fence ) {
			const GLenum r = glCaps.ClientWaitSync( fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0 );
			if ( r != GL_ALREADY_SIGNALED && r != GL_CONDITION_SATISFIED ) {
				continue;
			}
			glCaps.DeleteSync( fence );
			ringFences[next] = NULL;
		}
		ringSegment = next;
		ringUsed = 0;
		ringWritable = true;
		return;
	}
}

/*
==============
idVertexCache::RingReserve
==============
*/
void *idVertexCache::RingReserve( int size, int align, GLuint *buffer, int *offset ) {
	if ( !ringMemory ) {
		return NULL;
	}
	const int base = ringSegment * ringSegmentBytes;
	const int at = ( ( base + ringUsed + align - 1 ) / align ) * align;
	if ( at + size > base + ringSegmentBytes ) {
		return NULL;
	}
	// the next vertex block keeps its alignment
	ringUsed = ( ( at + size - base + VERTEX_ALIGN - 1 ) / VERTEX_ALIGN ) * VERTEX_ALIGN;
	if ( ringUsed > ringSegmentBytes ) {
		ringUsed = ringSegmentBytes;
	}
	*buffer = ringBuffer;
	*offset = at;
	return ringMemory + at;
}

/*
==============
idVertexCache::BaseVertex
==============
*/
int idVertexCache::BaseVertex( vertCache_t *buffer, int stride ) {
	if ( !streaming || !glCaps.baseVertex || !buffer || !buffer->vbo
			|| buffer->indexBuffer || ( buffer->offset % stride ) != 0 ) {
		return -1;
	}
	BindArray( buffer->vbo );
	return (int)( buffer->offset / stride );
}

/*
==============
idVertexCache::ArenaUpload

data into buffer at offset, as 16-bit indexes when shortIndexes (data then
holds size bytes of glIndex_t).  Through the ring and a GPU copy when the
frame's segment has room; at level load it often has not, and nothing draws
from the buffer yet, so a direct upload costs nothing there.
==============
*/
void idVertexCache::ArenaUpload( GLuint buffer, int offset, const void *data, int size, bool shortIndexes ) {
	const int count = size / (int)sizeof( glIndex_t );
	const int bytes = shortIndexes ? count * (int)sizeof( unsigned short ) : size;
	const glIndex_t *src = (const glIndex_t *)data;
	int i;

	if ( ringUsed + bytes <= ringSegmentBytes ) {
		const int at = ringSegment * ringSegmentBytes + ringUsed;
		// keep the ring's vertex blocks at their alignment
		ringUsed += ( ( bytes + VERTEX_ALIGN - 1 ) / VERTEX_ALIGN ) * VERTEX_ALIGN;
		if ( ringUsed > ringSegmentBytes ) {
			ringUsed = ringSegmentBytes;
		}
		void *dst = RingMemory( at, bytes );
		if ( shortIndexes ) {
			unsigned short *d = (unsigned short *)dst;
			for ( i = 0; i < count; i++ ) {
				d[i] = (unsigned short)src[i];
			}
		} else {
			SIMDProcessor->Memcpy( dst, data, bytes );
		}
		RingRelease();
		qglBindBufferARB( GL_COPY_READ_BUFFER, ringBuffer );
		qglBindBufferARB( GL_COPY_WRITE_BUFFER, buffer );
		glCaps.CopyBufferSubData( GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, at, offset, bytes );
		qglBindBufferARB( GL_COPY_READ_BUFFER, 0 );
		qglBindBufferARB( GL_COPY_WRITE_BUFFER, 0 );
		return;
	}

	qglBindBufferARB( GL_COPY_WRITE_BUFFER, buffer );
	if ( shortIndexes ) {
		unsigned short *d = (unsigned short *)Mem_Alloc( bytes );
		for ( i = 0; i < count; i++ ) {
			d[i] = (unsigned short)src[i];
		}
		qglBufferSubDataARB( GL_COPY_WRITE_BUFFER, offset, bytes, d );
		Mem_Free( d );
	} else {
		qglBufferSubDataARB( GL_COPY_WRITE_BUFFER, offset, bytes, data );
	}
	qglBindBufferARB( GL_COPY_WRITE_BUFFER, 0 );
}

/*
==============
idVertexCache::ArenaAlloc

Puts a static block in a shared buffer; false when it is too large for one
or no buffer can be had, and it gets its own.
==============
*/
bool idVertexCache::ArenaAlloc( vertCache_t *block, const void *data, int size, bool indexBuffer ) {
	bool shortIndexes = false;
	int bytes, align, i;
	unsigned int at = 0;

	if ( indexBuffer ) {
		const glIndex_t *src = (const glIndex_t *)data;
		const int count = size / (int)sizeof( glIndex_t );
		shortIndexes = ( size % (int)sizeof( glIndex_t ) ) == 0;
		for ( i = 0; i < count && shortIndexes; i++ ) {
			shortIndexes = (unsigned int)src[i] <= 0xffffu;
		}
		bytes = shortIndexes ? count * (int)sizeof( unsigned short ) : size;
		bytes = ( bytes + 3 ) & ~3;
		align = 4;
	} else {
		bytes = ( ( size + VERTEX_ALIGN - 1 ) / VERTEX_ALIGN ) * VERTEX_ALIGN;
		align = VERTEX_ALIGN;
	}
	if ( bytes > arenaBytes / 2 ) {
		return false;
	}

	for ( i = 0; i < numArenas; i++ ) {
		if ( arenas[i].indexes == indexBuffer
				&& range_alloc_alloc( &arenas[i].ranges, (unsigned int)bytes, (unsigned int)align, &at ) ) {
			break;
		}
	}
	if ( i == numArenas ) {
		if ( numArenas == MAX_ARENAS ) {
			return false;
		}
		arena_t *a = &arenas[numArenas];
		if ( !range_alloc_init( &a->ranges, (unsigned int)arenaBytes ) ) {
			return false;
		}
		qglGenBuffersARB( 1, &a->buffer );
		qglBindBufferARB( GL_COPY_WRITE_BUFFER, a->buffer );
		qglBufferDataARB( GL_COPY_WRITE_BUFFER, arenaBytes, NULL, GL_STATIC_DRAW_ARB );
		qglBindBufferARB( GL_COPY_WRITE_BUFFER, 0 );
		a->indexes = indexBuffer;
		numArenas++;
		if ( !range_alloc_alloc( &a->ranges, (unsigned int)bytes, (unsigned int)align, &at ) ) {
			return false;
		}
	}

	ArenaUpload( arenas[i].buffer, (int)at, data, size, shortIndexes );

	staticAllocTotal += bytes - block->size;
	block->size = bytes;
	block->arena = i;
	block->vbo = arenas[i].buffer;
	block->offset = at;
	if ( shortIndexes ) {
		block->indexType = GL_UNSIGNED_SHORT;
	}
	return true;
}

#ifdef BOOM3_VULKAN_RENDERER
/*
==============
idVertexCache::VulkanAlloc

A static block into the device's shared buffers, indexes in 16 bits when
they fit; false when the device has no room.
==============
*/
bool idVertexCache::VulkanAlloc( vertCache_t *block, const void *data, int size, bool indexBuffer ) {
	unsigned buffer = 0, offset = 0;
	int bytes = size;
	bool shortIndexes = false;
	if ( indexBuffer ) {
		const glIndex_t *src = (const glIndex_t *)data;
		const int count = size / (int)sizeof( glIndex_t );
		int i;
		shortIndexes = ( size % (int)sizeof( glIndex_t ) ) == 0;
		for ( i = 0; i < count && shortIndexes; i++ ) {
			shortIndexes = (unsigned int)src[i] <= 0xffffu;
		}
		if ( shortIndexes ) {
			bytes = count * (int)sizeof( unsigned short );
		}
		bytes = ( bytes + 3 ) & ~3;
	} else {
		bytes = ( ( size + VERTEX_ALIGN - 1 ) / VERTEX_ALIGN ) * VERTEX_ALIGN;
	}
	if ( !vkb_static_alloc( (size_t)bytes, indexBuffer, &buffer, &offset ) ) {
		return false;
	}
	if ( shortIndexes ) {
		const glIndex_t *src = (const glIndex_t *)data;
		const int count = size / (int)sizeof( glIndex_t );
		unsigned short *dst = (unsigned short *)Mem_Alloc( bytes );
		int i;
		for ( i = 0; i < count; i++ ) {
			dst[i] = (unsigned short)src[i];
		}
		vkb_static_upload( buffer, offset, dst, (size_t)( count * (int)sizeof( unsigned short ) ) );
		Mem_Free( dst );
		block->indexType = GL_UNSIGNED_SHORT;
	} else {
		vkb_static_upload( buffer, offset, data, (size_t)size );
	}
	staticAllocTotal += bytes - block->size;
	block->size = bytes;
	block->vbo = buffer;
	block->offset = offset;
	block->virtMem = NULL;
	return true;
}
#endif
