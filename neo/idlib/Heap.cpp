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

#include "idlib/Heap.h"

#include "rthreads/rthreads.h"

//===============================================================
//
//	Optional heap lock
//
//	Mem_Alloc/Mem_Free are single-threaded throughout normal engine
//	operation. The async image-load worker is the one exception: it runs
//	image decoders that allocate scratch from the same global heap while
//	the main thread may also be allocating. To keep those two threads from
//	corrupting the heap's intrusive free lists, allocation can be guarded
//	by a lock.
//
//	The lock is DISABLED by default and only enabled (Mem_EnableLock) for
//	the duration a worker thread is live. While disabled, the guard is a
//	single predictable branch on a bool, so the common single-threaded
//	path is unchanged (and byte-for-byte identical in behavior).
//
//===============================================================

static slock_t *	mem_lock		= NULL;
static bool			mem_lockEnabled	= false;

void Mem_EnableLock( bool enable ) {
	if ( enable && !mem_lock ) {
		mem_lock = slock_new();
	}
	// only actually gate on the lock if we have one
	mem_lockEnabled = ( enable && mem_lock != NULL );
}

ID_INLINE static void Mem_Lock( void ) {
	if ( mem_lockEnabled ) {
		slock_lock( mem_lock );
	}
}

ID_INLINE static void Mem_Unlock( void ) {
	if ( mem_lockEnabled ) {
		slock_unlock( mem_lock );
	}
}

//===============================================================
//
//	idHeap
//
//===============================================================

#define SMALL_HEADER_SIZE		( (intptr_t) ( sizeof( byte ) + sizeof( byte ) ) )
#define MEDIUM_HEADER_SIZE		( (intptr_t) ( sizeof( mediumHeapEntry_s ) + sizeof( byte ) ) )
#define LARGE_HEADER_SIZE		( (intptr_t) ( sizeof( dword * ) + sizeof( byte ) ) )

#define ALIGN_SIZE( bytes )		( ( (bytes) + ALIGN - 1 ) & ~(ALIGN - 1) )
#define SMALL_ALIGN( bytes )	( ALIGN_SIZE( (bytes) + SMALL_HEADER_SIZE ) - SMALL_HEADER_SIZE )
#define MEDIUM_SMALLEST_SIZE	( ALIGN_SIZE( 256 ) + ALIGN_SIZE( MEDIUM_HEADER_SIZE ) )


class idHeap {

public:
					idHeap( void );
					~idHeap( void );				// frees all associated data
	void			Init( void );					// initialize
	void *			Allocate( const dword bytes );	// allocate memory
	void			Free( void *p );				// free memory
	void *			Allocate16( const dword bytes );// allocate 16 byte aligned memory
	void			Free16( void *p );				// free 16 byte aligned memory
	dword			Msize( void *p );				// return size of data block
	void			Dump( void  );

	void			AllocDefragBlock( void );		// hack for huge renderbumps

private:

	enum {
		ALIGN = 8									// memory alignment in bytes
	};

	enum {
		INVALID_ALLOC	= 0xdd,
		SMALL_ALLOC		= 0xaa,						// small allocation
		MEDIUM_ALLOC	= 0xbb,						// medium allocaction
		LARGE_ALLOC		= 0xcc						// large allocaction
	};

	struct page_s {									// allocation page
		void *				data;					// data pointer to allocated memory
		dword				dataSize;				// number of bytes of memory 'data' points to
		page_s *			next;					// next free page in same page manager
		page_s *			prev;					// used only when allocated
		dword				largestFree;			// this data used by the medium-size heap manager
		void *				firstFree;				// pointer to first free entry
	};

	struct mediumHeapEntry_s {
		page_s *			page;					// pointer to page
		dword				size;					// size of block
		mediumHeapEntry_s *	prev;					// previous block
		mediumHeapEntry_s *	next;					// next block
		mediumHeapEntry_s *	prevFree;				// previous free block
		mediumHeapEntry_s *	nextFree;				// next free block
		dword				freeBlock;				// non-zero if free block
	};

	// variables
	void *			smallFirstFree[256/ALIGN+1];	// small heap allocator lists (for allocs of 1-255 bytes)
	page_s *		smallCurPage;					// current page for small allocations
	dword			smallCurPageOffset;				// byte offset in current page
	page_s *		smallFirstUsedPage;				// first used page of the small heap manager

	page_s *		mediumFirstFreePage;			// first partially free page
	page_s *		mediumLastFreePage;				// last partially free page
	page_s *		mediumFirstUsedPage;			// completely used page

	page_s *		largeFirstUsedPage;				// first page used by the large heap manager

	page_s *		swapPage;

	dword			pagesAllocated;					// number of pages currently allocated
	dword			pageSize;						// size of one alloc page in bytes

	dword			pageRequests;					// page requests
	dword			OSAllocs;						// number of allocs made to the OS

	int				c_heapAllocRunningCount;

	void			*defragBlock;					// a single huge block that can be allocated
													// at startup, then freed when needed

	// methods
	page_s *		AllocatePage( dword bytes );	// allocate page from the OS
	void			FreePage( idHeap::page_s *p );	// free an OS allocated page

	void *			SmallAllocate( dword bytes );	// allocate memory (1-255 bytes) from small heap manager
	void			SmallFree( void *ptr );			// free memory allocated by small heap manager

	void *			MediumAllocateFromPage( idHeap::page_s *p, dword sizeNeeded );
	void *			MediumAllocate( dword bytes );	// allocate memory (256-32768 bytes) from medium heap manager
	void			MediumFree( void *ptr );		// free memory allocated by medium heap manager

	void *			LargeAllocate( dword bytes );	// allocate large block from OS directly
	void			LargeFree( void *ptr );			// free memory allocated by large heap manager

	void			ReleaseSwappedPages( void );
	void			FreePageReal( idHeap::page_s *p );
};


/*
================
idHeap::Init
================
*/
void idHeap::Init () {
	OSAllocs			= 0;
	pageRequests		= 0;
	pageSize			= 65536 - sizeof( idHeap::page_s );
	pagesAllocated		= 0;								// reset page allocation counter

	largeFirstUsedPage	= NULL;								// init large heap manager
	swapPage			= NULL;

	memset( smallFirstFree, 0, sizeof(smallFirstFree) );	// init small heap manager
	smallFirstUsedPage	= NULL;
	smallCurPage		= AllocatePage( pageSize );
	assert( smallCurPage );
	smallCurPageOffset	= SMALL_ALIGN( 0 );

	defragBlock = NULL;

	mediumFirstFreePage	= NULL;								// init medium heap manager
	mediumLastFreePage	= NULL;
	mediumFirstUsedPage	= NULL;

	c_heapAllocRunningCount = 0;
}

/*
================
idHeap::idHeap
================
*/
idHeap::idHeap( void ) {
	Init();
}

/*
================
idHeap::~idHeap

  returns all allocated memory back to OS
================
*/
idHeap::~idHeap( void ) {

	idHeap::page_s	*p;

	if ( smallCurPage ) {
		FreePage( smallCurPage );			// free small-heap current allocation page
	}
	p = smallFirstUsedPage;					// free small-heap allocated pages
	while( p ) {
		idHeap::page_s *next = p->next;
		FreePage( p );
		p= next;
	}

	p = largeFirstUsedPage;					// free large-heap allocated pages
	while( p ) {
		idHeap::page_s *next = p->next;
		FreePage( p );
		p = next;
	}

	p = mediumFirstFreePage;				// free medium-heap allocated pages
	while( p ) {
		idHeap::page_s *next = p->next;
		FreePage( p );
		p = next;
	}

	p = mediumFirstUsedPage;				// free medium-heap allocated completely used pages
	while( p ) {
		idHeap::page_s *next = p->next;
		FreePage( p );
		p = next;
	}

	ReleaseSwappedPages();

	if ( defragBlock ) {
		free( defragBlock );
	}

	assert( pagesAllocated == 0 );
}

/*
================
idHeap::AllocDefragBlock
================
*/
void idHeap::AllocDefragBlock( void ) {
	int		size = 0x40000000;

	if ( defragBlock ) {
		return;
	}
	while( 1 ) {
		defragBlock = malloc( size );
		if ( defragBlock ) {
			break;
		}
		size >>= 1;
	}
	idLib::common->Printf( "Allocated a %i mb defrag block\n", size / (1024*1024) );
}

/*
================
idHeap::Allocate
================
*/
void *idHeap::Allocate( const dword bytes ) {
	if ( !bytes )
		return NULL;
	c_heapAllocRunningCount++;
	if ( !(bytes & ~255) )
		return SmallAllocate( bytes );
	if ( !(bytes & ~32767) )
		return MediumAllocate( bytes );
	return LargeAllocate( bytes );
}

/*
================
idHeap::Free
================
*/
void idHeap::Free( void *p ) {
	if ( !p )
		return;
	c_heapAllocRunningCount--;

	switch( ((byte *)(p))[-1] ) {
		case SMALL_ALLOC: {
			SmallFree( p );
			break;
		}
		case MEDIUM_ALLOC: {
			MediumFree( p );
			break;
		}
		case LARGE_ALLOC: {
			LargeFree( p );
			break;
		}
		default: {
			idLib::common->FatalError( "idHeap::Free: invalid memory block" );
			break;
		}
	}
}

/*
================
idHeap::Allocate16
================
*/
void *idHeap::Allocate16( const dword bytes ) {
	byte *ptr, *alignedPtr;

	ptr = (byte *) malloc( bytes + 16 + sizeof(intptr_t) );
	if ( !ptr ) {
		if ( defragBlock ) {
			idLib::common->Printf( "Freeing defragBlock on alloc of %i.\n", bytes );
			free( defragBlock );
			defragBlock = NULL;
			ptr = (byte *) malloc( bytes + 16 + sizeof(intptr_t) );
			AllocDefragBlock();
		}
		if ( !ptr ) {
			common->FatalError( "malloc failure for %i", bytes );
		}
	}
	alignedPtr = (byte *) ( ( ( (intptr_t) ptr ) + 15) & ~15 );
	if ( alignedPtr - ptr < sizeof(intptr_t) ) {
		alignedPtr += 16;
	}
	*((intptr_t *)(alignedPtr - sizeof(intptr_t))) = (intptr_t) ptr;
	return (void *) alignedPtr;
}

/*
================
idHeap::Free16
================
*/
void idHeap::Free16( void *p ) {
	free( (void *) *((intptr_t *) (( (byte *) p ) - sizeof(intptr_t))) );
}

/*
================
idHeap::Msize

  returns size of allocated memory block
  p	= pointer to memory block
  Notes:	size may not be the same as the size in the original
			allocation request (due to block alignment reasons).
================
*/
dword idHeap::Msize( void *p ) {
	if ( !p )
		return 0;

	switch( ((byte *)(p))[-1] )
	{
		case SMALL_ALLOC:
			return SMALL_ALIGN( ((byte *)(p))[-SMALL_HEADER_SIZE] * ALIGN );
		case MEDIUM_ALLOC:
			return ((mediumHeapEntry_s *)(((byte *)(p)) - ALIGN_SIZE( MEDIUM_HEADER_SIZE )))->size - ALIGN_SIZE( MEDIUM_HEADER_SIZE );
		case LARGE_ALLOC:
			return ((idHeap::page_s*)(*((intptr_t *)(((byte *)p) - ALIGN_SIZE( LARGE_HEADER_SIZE )))))->dataSize - ALIGN_SIZE( LARGE_HEADER_SIZE );
		default:
			idLib::common->FatalError( "idHeap::Msize: invalid memory block" );
			break;
	}

	return 0;
}

/*
================
idHeap::Dump

  dump contents of the heap
================
*/
void idHeap::Dump( void ) {
	idHeap::page_s	*pg;

	for ( pg = smallFirstUsedPage; pg; pg = pg->next ) {
		idLib::common->Printf( "%p  bytes %-8d  (in use by small heap)\n", pg->data, pg->dataSize);
	}

	if ( smallCurPage ) {
		pg = smallCurPage;
		idLib::common->Printf( "%p  bytes %-8d  (small heap active page)\n", pg->data, pg->dataSize );
	}

	for ( pg = mediumFirstUsedPage; pg; pg = pg->next ) {
		idLib::common->Printf( "%p  bytes %-8d  (completely used by medium heap)\n", pg->data, pg->dataSize );
	}

	for ( pg = mediumFirstFreePage; pg; pg = pg->next ) {
		idLib::common->Printf( "%p  bytes %-8d  (partially used by medium heap)\n", pg->data, pg->dataSize );
	}

	for ( pg = largeFirstUsedPage; pg; pg = pg->next ) {
		idLib::common->Printf( "%p  bytes %-8d  (fully used by large heap)\n", pg->data, pg->dataSize );
	}

	idLib::common->Printf( "pages allocated : %d\n", pagesAllocated );
}

/*
================
idHeap::FreePageReal

  frees page to be used by the OS
  p	= page to free
================
*/
void idHeap::FreePageReal( idHeap::page_s *p ) {
	assert( p );
	::free( p );
}

/*
================
idHeap::ReleaseSwappedPages

  releases the swap page to OS
================
*/
void idHeap::ReleaseSwappedPages () {
	if ( swapPage ) {
		FreePageReal( swapPage );
	}
	swapPage = NULL;
}

/*
================
idHeap::AllocatePage

  allocates memory from the OS
  bytes	= page size in bytes
  returns pointer to page
================
*/
idHeap::page_s* idHeap::AllocatePage( dword bytes ) {
	idHeap::page_s*	p;

	pageRequests++;

	if ( swapPage && swapPage->dataSize == bytes ) {			// if we've got a swap page somewhere
		p			= swapPage;
		swapPage	= NULL;
	}
	else {
		dword size;

		size = bytes + sizeof(idHeap::page_s);

		p = (idHeap::page_s *) ::malloc( size + ALIGN - 1 );
		if ( !p ) {
			if ( defragBlock ) {
				idLib::common->Printf( "Freeing defragBlock on alloc of %i.\n", size + ALIGN - 1 );
				free( defragBlock );
				defragBlock = NULL;
				p = (idHeap::page_s *) ::malloc( size + ALIGN - 1 );
				AllocDefragBlock();
			}
			if ( !p ) {
				common->FatalError( "malloc failure for %i", bytes );
			}
		}

		p->data		= (void *) ALIGN_SIZE( (intptr_t)((byte *)(p)) + sizeof( idHeap::page_s ) );
		p->dataSize	= size - sizeof(idHeap::page_s);
		p->firstFree = NULL;
		p->largestFree = 0;
		OSAllocs++;
	}

	p->prev = NULL;
	p->next = NULL;

	pagesAllocated++;

	return p;
}

/*
================
idHeap::FreePage

  frees a page back to the operating system
  p	= pointer to page
================
*/
void idHeap::FreePage( idHeap::page_s *p ) {
	assert( p );

	if ( p->dataSize == pageSize && !swapPage ) {			// add to swap list?
		swapPage = p;
	}
	else {
		FreePageReal( p );
	}

	pagesAllocated--;
}

//===============================================================
//
//	small heap code
//
//===============================================================

/*
================
idHeap::SmallAllocate

  allocate memory (1-255 bytes) from the small heap manager
  bytes = number of bytes to allocate
  returns pointer to allocated memory
================
*/
void *idHeap::SmallAllocate( dword bytes ) {
	// we need the at least sizeof( dword ) bytes for the free list
	if ( bytes < sizeof( intptr_t ) ) {
		bytes = sizeof( intptr_t );
	}

	// increase the number of bytes if necessary to make sure the next small allocation is aligned
	bytes = SMALL_ALIGN( bytes );

	byte *smallBlock = (byte *)(smallFirstFree[bytes / ALIGN]);
	if ( smallBlock ) {
		intptr_t *link = (intptr_t *)(smallBlock + SMALL_HEADER_SIZE);
		smallBlock[1] = SMALL_ALLOC;					// allocation identifier
		smallFirstFree[bytes / ALIGN] = (void *)(*link);
		return (void *)(link);
	}

	dword bytesLeft = (size_t)(pageSize) - smallCurPageOffset;
	// if we need to allocate a new page
	if ( bytes >= bytesLeft ) {

		smallCurPage->next	= smallFirstUsedPage;
		smallFirstUsedPage	= smallCurPage;
		smallCurPage		= AllocatePage( pageSize );
		if ( !smallCurPage ) {
			return NULL;
		}
		// make sure the first allocation is aligned
		smallCurPageOffset	= SMALL_ALIGN( 0 );
	}

	smallBlock			= ((byte *)smallCurPage->data) + smallCurPageOffset;
	smallBlock[0]		= (byte)(bytes / ALIGN);		// write # of bytes/ALIGN
	smallBlock[1]		= SMALL_ALLOC;					// allocation identifier
	smallCurPageOffset  += bytes + SMALL_HEADER_SIZE;	// increase the offset on the current page
	return ( smallBlock + SMALL_HEADER_SIZE );			// skip the first two bytes
}

/*
================
idHeap::SmallFree

  frees a block of memory allocated by SmallAllocate() call
  data = pointer to block of memory
================
*/
void idHeap::SmallFree( void *ptr ) {
	((byte *)(ptr))[-1] = INVALID_ALLOC;

	byte *d = ( (byte *)ptr ) - SMALL_HEADER_SIZE;
	intptr_t *link = (intptr_t *)ptr;
	// index into the table with free small memory blocks
	dword ix = *d;

	// check if the index is correct
	if ( ix > (256 / ALIGN) ) {
		idLib::common->FatalError( "SmallFree: invalid memory block" );
	}

	*link = (intptr_t)smallFirstFree[ix];	// write next index
	smallFirstFree[ix] = (void *)d;		// link
}

//===============================================================
//
//	medium heap code
//
//	Medium-heap allocated pages not returned to OS until heap destructor
//	called (re-used instead on subsequent medium-size malloc requests).
//
//===============================================================

/*
================
idHeap::MediumAllocateFromPage

  performs allocation using the medium heap manager from a given page
  p				= page
  sizeNeeded	= # of bytes needed
  returns pointer to allocated memory
================
*/
void *idHeap::MediumAllocateFromPage( idHeap::page_s *p, dword sizeNeeded ) {

	mediumHeapEntry_s	*best,*nw = NULL;
	byte				*ret;

	best = (mediumHeapEntry_s *)(p->firstFree);			// first block is largest

	assert( best );
	assert( best->size == p->largestFree );
	assert( best->size >= sizeNeeded );

	// if we can allocate another block from this page after allocating sizeNeeded bytes
	if ( best->size >= (dword)( sizeNeeded + MEDIUM_SMALLEST_SIZE ) ) {
		nw = (mediumHeapEntry_s *)((byte *)best + best->size - sizeNeeded);
		nw->page		= p;
		nw->prev		= best;
		nw->next		= best->next;
		nw->prevFree	= NULL;
		nw->nextFree	= NULL;
		nw->size		= sizeNeeded;
		nw->freeBlock	= 0;			// used block
		if ( best->next ) {
			best->next->prev = nw;
		}
		best->next	= nw;
		best->size	-= sizeNeeded;

		p->largestFree = best->size;
	}
	else {
		if ( best->prevFree ) {
			best->prevFree->nextFree = best->nextFree;
		}
		else {
			p->firstFree = (void *)best->nextFree;
		}
		if ( best->nextFree ) {
			best->nextFree->prevFree = best->prevFree;
		}

		best->prevFree  = NULL;
		best->nextFree  = NULL;
		best->freeBlock = 0;			// used block
		nw = best;

		p->largestFree = 0;
	}

	ret		= (byte *)(nw) + ALIGN_SIZE( MEDIUM_HEADER_SIZE );
	ret[-1] = MEDIUM_ALLOC;		// allocation identifier

	return (void *)(ret);
}

/*
================
idHeap::MediumAllocate

  allocate memory (256-32768 bytes) from medium heap manager
  bytes	= number of bytes to allocate
  returns pointer to allocated memory
================
*/
void *idHeap::MediumAllocate( dword bytes ) {
	idHeap::page_s		*p;
	void				*data;

	dword sizeNeeded = ALIGN_SIZE( bytes ) + ALIGN_SIZE( MEDIUM_HEADER_SIZE );

	// find first page with enough space
	for ( p = mediumFirstFreePage; p; p = p->next ) {
		if ( p->largestFree >= sizeNeeded ) {
			break;
		}
	}

	if ( !p ) {								// need to allocate new page?
		p = AllocatePage( pageSize );
		if ( !p ) {
			return NULL;					// malloc failure!
		}
		p->prev		= NULL;
		p->next		= mediumFirstFreePage;
		if (p->next) {
			p->next->prev = p;
		}
		else {
			mediumLastFreePage	= p;
		}

		mediumFirstFreePage		= p;

		p->largestFree	= pageSize;
		p->firstFree	= (void *)p->data;

		mediumHeapEntry_s *e;
		e				= (mediumHeapEntry_s *)(p->firstFree);
		e->page			= p;
		// make sure ((byte *)e + e->size) is aligned
		e->size			= pageSize & ~(ALIGN - 1);
		e->prev			= NULL;
		e->next			= NULL;
		e->prevFree		= NULL;
		e->nextFree		= NULL;
		e->freeBlock	= 1;
	}

	data = MediumAllocateFromPage( p, sizeNeeded );		// allocate data from page

	// if the page can no longer serve memory, move it away from free list
	// (so that it won't slow down the later alloc queries)
	// this modification speeds up the pageWalk from O(N) to O(sqrt(N))
	// a call to free may swap this page back to the free list

	if ( p->largestFree < MEDIUM_SMALLEST_SIZE ) {
		if ( p == mediumLastFreePage ) {
			mediumLastFreePage = p->prev;
		}

		if ( p == mediumFirstFreePage ) {
			mediumFirstFreePage = p->next;
		}

		if ( p->prev ) {
			p->prev->next = p->next;
		}
		if ( p->next ) {
			p->next->prev = p->prev;
		}

		// link to "completely used" list
		p->prev = NULL;
		p->next = mediumFirstUsedPage;
		if ( p->next ) {
			p->next->prev = p;
		}
		mediumFirstUsedPage = p;
		return data;
	}

	// re-order linked list (so that next malloc query starts from current
	// matching block) -- this speeds up both the page walks and block walks

	if ( p != mediumFirstFreePage ) {
		assert( mediumLastFreePage );
		assert( mediumFirstFreePage );
		assert( p->prev);

		mediumLastFreePage->next	= mediumFirstFreePage;
		mediumFirstFreePage->prev	= mediumLastFreePage;
		mediumLastFreePage			= p->prev;
		p->prev->next				= NULL;
		p->prev						= NULL;
		mediumFirstFreePage			= p;
	}

	return data;
}

/*
================
idHeap::MediumFree

  frees a block allocated by the medium heap manager
  ptr	= pointer to data block
================
*/
void idHeap::MediumFree( void *ptr ) {
	((byte *)(ptr))[-1] = INVALID_ALLOC;

	mediumHeapEntry_s	*e = (mediumHeapEntry_s *)((byte *)ptr - ALIGN_SIZE( MEDIUM_HEADER_SIZE ));
	idHeap::page_s		*p = e->page;
	bool				isInFreeList;

	isInFreeList = p->largestFree >= MEDIUM_SMALLEST_SIZE;

	assert( e->size );
	assert( e->freeBlock == 0 );

	mediumHeapEntry_s *prev = e->prev;

	// if the previous block is free we can merge
	if ( prev && prev->freeBlock ) {
		prev->size += e->size;
		prev->next = e->next;
		if ( e->next ) {
			e->next->prev = prev;
		}
		e = prev;
	}
	else {
		e->prevFree		= NULL;				// link to beginning of free list
		e->nextFree		= (mediumHeapEntry_s *)p->firstFree;
		if ( e->nextFree ) {
			assert( !(e->nextFree->prevFree) );
			e->nextFree->prevFree = e;
		}

		p->firstFree	= e;
		p->largestFree	= e->size;
		e->freeBlock	= 1;				// mark block as free
	}

	mediumHeapEntry_s *next = e->next;

	// if the next block is free we can merge
	if ( next && next->freeBlock ) {
		e->size += next->size;
		e->next = next->next;

		if ( next->next ) {
			next->next->prev = e;
		}

		if ( next->prevFree ) {
			next->prevFree->nextFree = next->nextFree;
		}
		else {
			assert( next == p->firstFree );
			p->firstFree = next->nextFree;
		}

		if ( next->nextFree ) {
			next->nextFree->prevFree = next->prevFree;
		}
	}

	if ( p->firstFree ) {
		p->largestFree = ((mediumHeapEntry_s *)(p->firstFree))->size;
	}
	else {
		p->largestFree = 0;
	}

	// did e become the largest block of the page ?

	if ( e->size > p->largestFree ) {
		assert( e != p->firstFree );
		p->largestFree = e->size;

		if ( e->prevFree ) {
			e->prevFree->nextFree = e->nextFree;
		}
		if ( e->nextFree ) {
			e->nextFree->prevFree = e->prevFree;
		}

		e->nextFree = (mediumHeapEntry_s *)p->firstFree;
		e->prevFree = NULL;
		if ( e->nextFree ) {
			e->nextFree->prevFree = e;
		}
		p->firstFree = e;
	}

	// if page wasn't in free list (because it was near-full), move it back there
	if ( !isInFreeList ) {

		// remove from "completely used" list
		if ( p->prev ) {
			p->prev->next = p->next;
		}
		if ( p->next ) {
			p->next->prev = p->prev;
		}
		if ( p == mediumFirstUsedPage ) {
			mediumFirstUsedPage = p->next;
		}

		p->next = NULL;
		p->prev = mediumLastFreePage;

		if ( mediumLastFreePage ) {
			mediumLastFreePage->next = p;
		}
		mediumLastFreePage = p;
		if ( !mediumFirstFreePage ) {
			mediumFirstFreePage = p;
		}
	}
}

//===============================================================
//
//	large heap code
//
//===============================================================

/*
================
idHeap::LargeAllocate

  allocates a block of memory from the operating system
  bytes	= number of bytes to allocate
  returns pointer to allocated memory
================
*/
void *idHeap::LargeAllocate( dword bytes ) {
	idHeap::page_s *p = AllocatePage( bytes + ALIGN_SIZE( LARGE_HEADER_SIZE ) );

	assert( p );

	if ( !p ) {
		return NULL;
	}

	byte *	d	= (byte*)(p->data) + ALIGN_SIZE( LARGE_HEADER_SIZE );
	intptr_t * dw	= (intptr_t*)(d - ALIGN_SIZE( LARGE_HEADER_SIZE ));
	dw[0]		= (intptr_t)p;			// write pointer back to page table
	d[-1]		= LARGE_ALLOC;			// allocation identifier

	// link to 'large used page list'
	p->prev = NULL;
	p->next = largeFirstUsedPage;
	if ( p->next ) {
		p->next->prev = p;
	}
	largeFirstUsedPage = p;

	return (void *)(d);
}

/*
================
idHeap::LargeFree

  frees a block of memory allocated by the 'large memory allocator'
  p	= pointer to allocated memory
================
*/
void idHeap::LargeFree( void *ptr) {
	idHeap::page_s*	pg;

	((byte *)(ptr))[-1] = INVALID_ALLOC;

	// get page pointer
	pg = (idHeap::page_s *)(*((intptr_t *)(((byte *)ptr) - ALIGN_SIZE( LARGE_HEADER_SIZE ))));

	// unlink from doubly linked list
	if ( pg->prev ) {
		pg->prev->next = pg->next;
	}
	if ( pg->next ) {
		pg->next->prev = pg->prev;
	}
	if ( pg == largeFirstUsedPage ) {
		largeFirstUsedPage = pg->next;
	}
	pg->next = pg->prev = NULL;

	FreePage(pg);
}

//===============================================================
//
//	memory allocation all in one place
//
//===============================================================

#undef new

static idHeap *			mem_heap = NULL;
static memoryStats_t	mem_total_allocs = { 0, 0x0fffffff, -1, 0 };
static memoryStats_t	mem_frame_allocs;
static memoryStats_t	mem_frame_frees;

/*
==================
Mem_ClearFrameStats
==================
*/
void Mem_ClearFrameStats( void ) {
	mem_frame_allocs.num = mem_frame_frees.num = 0;
	mem_frame_allocs.minSize = mem_frame_frees.minSize = 0x0fffffff;
	mem_frame_allocs.maxSize = mem_frame_frees.maxSize = -1;
	mem_frame_allocs.totalSize = mem_frame_frees.totalSize = 0;
}

/*
==================
Mem_GetFrameStats
==================
*/
void Mem_GetFrameStats( memoryStats_t &allocs, memoryStats_t &frees ) {
	allocs = mem_frame_allocs;
	frees = mem_frame_frees;
}

/*
==================
Mem_GetStats
==================
*/
void Mem_GetStats( memoryStats_t &stats ) {
	stats = mem_total_allocs;
}

/*
==================
Mem_UpdateStats
==================
*/
void Mem_UpdateStats( memoryStats_t &stats, int size ) {
	stats.num++;
	if ( size < stats.minSize ) {
		stats.minSize = size;
	}
	if ( size > stats.maxSize ) {
		stats.maxSize = size;
	}
	stats.totalSize += size;
}

/*
==================
Mem_UpdateAllocStats
==================
*/
void Mem_UpdateAllocStats( int size ) {
	Mem_UpdateStats( mem_frame_allocs, size );
	Mem_UpdateStats( mem_total_allocs, size );
}

/*
==================
Mem_UpdateFreeStats
==================
*/
void Mem_UpdateFreeStats( int size ) {
	Mem_UpdateStats( mem_frame_frees, size );
	mem_total_allocs.num--;
	mem_total_allocs.totalSize -= size;
}


/*
==================
Mem_Alloc
==================
*/
void *Mem_Alloc( const int size ) {
	if ( !size )
		return NULL;
	if ( !mem_heap )
		return malloc( size );
	Mem_Lock();
	void *mem = mem_heap->Allocate( size );
	Mem_UpdateAllocStats( mem_heap->Msize( mem ) );
	Mem_Unlock();
	return mem;
}

/*
==================
Mem_Free
==================
*/
void Mem_Free( void *ptr ) {
	if ( !ptr )
		return;
	if ( !mem_heap ) {
		free( ptr );
		return;
	}
	Mem_Lock();
	Mem_UpdateFreeStats( mem_heap->Msize( ptr ) );
	mem_heap->Free( ptr );
	Mem_Unlock();
}

/*
==================
Mem_Alloc16
==================
*/
void *Mem_Alloc16( const int size ) {
	if ( !size )
		return NULL;
	if ( !mem_heap )
		return malloc( size );
	Mem_Lock();
	void *mem = mem_heap->Allocate16( size );
	Mem_Unlock();
	// make sure the memory is 16 byte aligned
	assert( ( ((intptr_t)mem) & 15) == 0 );
	return mem;
}

/*
==================
Mem_Free16
==================
*/
void Mem_Free16( void *ptr ) {
	if ( !ptr )
		return;
	if ( !mem_heap ) {
		free( ptr );
		return;
	}
	// make sure the memory is 16 byte aligned
	assert( ( ((intptr_t)ptr) & 15) == 0 );
	Mem_Lock();
	mem_heap->Free16( ptr );
	Mem_Unlock();
}

/*
==================
Mem_ClearedAlloc
==================
*/
void *Mem_ClearedAlloc( const int size ) {
	void *mem = Mem_Alloc( size );
	SIMDProcessor->Memset( mem, 0, size );
	return mem;
}

/*
==================
Mem_ClearedAlloc
==================
*/
void Mem_AllocDefragBlock( void ) {
	mem_heap->AllocDefragBlock();
}

/*
==================
Mem_CopyString
==================
*/
char *Mem_CopyString( const char *in ) {
	char	*out;

	out = (char *)Mem_Alloc( strlen(in) + 1 );
	strcpy( out, in );
	return out;
}

/*
==================
Mem_Dump_f
==================
*/
void Mem_Dump_f( const idCmdArgs &args ) {
}

/*
==================
Mem_DumpCompressed_f
==================
*/
void Mem_DumpCompressed_f( const idCmdArgs &args ) {
}

/*
==================
Mem_Init
==================
*/
void Mem_Init( void ) {
	mem_heap = new idHeap;
	Mem_ClearFrameStats();
}

/*
==================
Mem_Shutdown
==================
*/
void Mem_Shutdown( void ) {
	idHeap *m = mem_heap;
	mem_heap = NULL;
	delete m;
}

/*
==================
Mem_EnableLeakTest
==================
*/
void Mem_EnableLeakTest( const char *name ) {
}
