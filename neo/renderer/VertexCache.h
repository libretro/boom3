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

#include "framework/CVarSystem.h"
#include "renderer/qgl.h"
#include "renderer/gl_caps.h"
#include "renderer/range_alloc.h"

// vertex cache calls should only be made by the front end

const int NUM_VERTEX_FRAMES = 2;

typedef enum {
	TAG_FREE,
	TAG_USED,
	TAG_FIXED,		// for the temp buffers
	TAG_TEMP		// in frame temp area, not static area
} vertBlockTag_t;

typedef struct vertCache_s {
	GLuint			vbo;
	GLuint			ownVbo;				// the header's own buffer; vbo names a shared one while the block lives in it
	int				arena;				// the shared buffer the block lives in, or -1
	void			*virtMem;			// only one of vbo / virtMem will be set
	bool			indexBuffer;		// holds indexes instead of vertexes
	GLenum			indexType;			// GL_UNSIGNED_SHORT or GL_UNSIGNED_INT for an index buffer

	intptr_t		offset;
	int				size;				// may be larger than the amount asked for, due
										// to round up and minimum fragment sizes
	int				tag;				// a tag of 0 is a free block
	struct vertCache_s	**	user;				// will be set to zero when purged
	struct vertCache_s *next, *prev;	// may be on the static list or one of the frame lists
	int				frameUsed;			// it can't be purged if near the current frame
} vertCache_t;


class idVertexCache {
public:
	void			Init();
	void			Shutdown();

	// just for gfxinfo printing
	bool			IsFast();

	// called when vertex programs are enabled or disabled, because
	// the cached data is no longer valid
	void			PurgeAll();

	// Tries to allocate space for the given data in fast vertex
	// memory, and copies it over.
	// Alloc does NOT do a touch, which allows purging of things
	// created at level load time even if a frame hasn't passed yet.
	// These allocations can be purged, which will zero the pointer.
	void			Alloc( void *data, int bytes, vertCache_t **buffer, bool indexBuffer = false );

	// Alloc for vertexes rewritten whenever their surface is - a skinned
	// mesh's - so the driver keeps them where the CPU writes cheaply.
	void			AllocStream( void *data, int bytes, vertCache_t **buffer );

	// This will be a real pointer with virtual memory,
	// but it will be an int offset cast to a pointer of ARB_vertex_buffer_object
	void *			Position( vertCache_t *buffer );

	// if r_useIndexBuffers is enabled, but you need to draw something without
	// an indexCache, this must be called to reset GL_ELEMENT_ARRAY_BUFFER_ARB
	void			UnbindIndex();

	// the type to draw an index buffer's indexes with: an index buffer whose
	// indexes all fit in 16 bits holds them in 16 bits
	GLenum			IndexType( const vertCache_t *buffer ) const { return buffer->indexType; }

	// Position() skips binding a buffer that is already bound; the backend
	// calls this when it starts, as the frontend and the libretro frontend
	// may have bound others since
	void			InvalidateBindings();

	// automatically freed at the end of the next frame
	// used for specular texture coordinates and gui drawing, which
	// will change every frame.
	// will return NULL if the vertex cache is completely full
	// As with Position(), this may not actually be a pointer you can access.
	vertCache_t	*	AllocFrameTemp( void *data, int bytes );

	// notes that a buffer is used this frame, so it can't be purged
	// out from under the GPU
	void			Touch( vertCache_t *buffer );

	// this block won't have to zero a buffer pointer when it is purged,
	// but it must still wait for the frames to pass, in case the GPU
	// is still referencing it
	void			Free( vertCache_t *buffer );

	// updates the counter for determining which temp space to use
	// and which blocks can be purged
	// Also prints debugging info when enabled
	void			EndFrame();

	// listVertexCache calls this
	void			List();

	// The ES 3.1 / GL 4.3 renderer's buffers: static data packed into a few
	// large buffers, every frame's data streamed through one ring.
	bool			Streaming() const { return streaming; }

	// the buffer GL_ARRAY_BUFFER holds, or UNKNOWN_BINDING when that is
	// not known (InvalidateBindings)
	GLuint			BoundArray() const { return boundArray; }
	static const GLuint UNKNOWN_BINDING = 0xffffffffu;

	// The base vertex to draw a vertex block with, its vertexes being
	// stride bytes apart: the attribute pointers then start at the buffer,
	// which is bound, so surfaces in the same buffer share them.  -1 when
	// the block has to be drawn through Position() instead.
	int				BaseVertex( vertCache_t *buffer, int stride );

	// size bytes of the frame's ring at a multiple of align, written
	// through the pointer and read by the GPU as they are; buffer and offset
	// name them for GL.  NULL when the ring is full or not persistently
	// mapped, which the caller takes as "draw without the ring".
	void *			RingReserve( int size, int align, GLuint *buffer, int *offset );

private:
	static const int RING_SEGMENTS = 3;		// frames the ring holds before overflow uploads
	static const int MAX_ARENAS = 64;
	// a vertex block in a shared buffer starts at a multiple of every stride
	// the backends draw vertexes with (idDrawVert's 60, shadowCache_t's 16)
	static const int VERTEX_ALIGN = 240;

	typedef struct arena_s {
		range_alloc_t	ranges;
		GLuint			buffer;
		bool			indexes;
	} arena_t;

	void			InitStreaming();
	bool			ArenaAlloc( vertCache_t *block, const void *data, int size, bool indexBuffer );
	void			ArenaUpload( GLuint buffer, int offset, const void *data, int size, bool shortIndexes );
	void *			RingMemory( int offset, int size );
	void			RingRelease();
	void			RingEndFrame();

	void			InitMemoryBlocks( int size );
	void			ActuallyFree( vertCache_t *block );
	// bind, skipping it when the cache says the buffer is already bound
	void			BindArray( GLuint vbo );
	void			BindElement( GLuint vbo );
	// bind and record, for uploads made outside the backend
	void			ForceBindArray( GLuint vbo );
	void			ForceBindElement( GLuint vbo );

	GLuint			boundArray;				// GL_ARRAY_BUFFER, or UNKNOWN_BINDING
	GLuint			boundElement;			// GL_ELEMENT_ARRAY_BUFFER, or UNKNOWN_BINDING

	static idCVar	r_showVertexCache;
	static idCVar	r_vertexBufferMegs;

	int				staticCountTotal;
	int				staticAllocTotal;		// for end of frame purging

	int				staticAllocThisFrame;	// debug counter
	int				staticCountThisFrame;
	int				dynamicAllocThisFrame;
	int				dynamicCountThisFrame;

	int				currentFrame;			// for purgable block tracking
	int				listNum;				// currentFrame % NUM_VERTEX_FRAMES, determines which tempBuffers to use

	bool			virtualMemory;			// not fast stuff

	bool			allocatingTempBuffer;	// force GL_STREAM_DRAW_ARB

	vertCache_t		*tempBuffers[NUM_VERTEX_FRAMES];		// allocated at startup
	bool			tempOverflow;			// had to alloc a temp in static memory

	idBlockAlloc<vertCache_t,1024>	headerAllocator;

	vertCache_t		freeStaticHeaders;		// head of doubly linked list
	vertCache_t		freeDynamicHeaders;		// head of doubly linked list
	vertCache_t		dynamicHeaders;			// head of doubly linked list
	vertCache_t		deferredFreeList;		// head of doubly linked list
	vertCache_t		staticHeaders;			// head of doubly linked list in MRU order,
											// staticHeaders.next is most recently used

	int				frameBytes;				// for each of NUM_VERTEX_FRAMES frames

	arena_t			arenas[MAX_ARENAS];
	gl_caps_sync_t	ringFences[RING_SEGMENTS];
	bool			ringFenceFailed[RING_SEGMENTS];
	bool			ringWritable;
	byte *			ringMemory;				// the ring's persistent mapping, or NULL
	byte *			ringScratch;			// where RingMemory writes when the ring cannot be mapped
	int				ringScratchBytes;
	int				ringPendingOffset;		// what RingRelease has to put in place
	int				ringPendingBytes;
	bool			ringPendingMapped;		// mapped, rather than in ringScratch
	int				numArenas;
	int				arenaBytes;				// size of each shared buffer
	GLuint			ringBuffer;
	int				ringSegmentBytes;
	int				ringSegment;			// the segment this frame writes
	int				ringUsed;				// bytes of it handed out
	bool			streaming;
	// the Vulkan renderer's memory: the device's shared buffers and its
	// frame ring, through vk_backend.h
	bool			vulkan;
	bool			VulkanAlloc( vertCache_t *block, const void *data, int size, bool indexBuffer );
};

extern	idVertexCache	vertexCache;
