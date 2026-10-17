/* Regression test for the collision model polygon stride: a polygon
 * carries a pointer and bounds, its edges[] is 4 bytes an entry, and the
 * polygons sit end to end in one block, so the size of one must keep
 * the next 8 byte aligned. The same measure sizes the block the .cm
 * file records. */
#include "sys/platform.h"
#include "cm/CollisionModel_local.h"
#include <stdio.h>

// the header's inline destructors reference these; nothing here runs them
bool idWinding::ReAllocate( int, bool ) { return false; }
bool idFixedWinding::ReAllocate( int, bool ) { return false; }

int main( void ) {
	int fail = 0;
	for ( int n = 1; n < 256; n++ ) {
		int s = CM_PolygonSize( n );
		int need = (int)( sizeof( cm_polygon_t ) + ( n - 1 ) * sizeof( int ) );
		if ( s % 8 || s < need || s - need >= 8 ) {
			printf( "FAIL %d edges -> %d bytes (need %d)\n", n, s, need );
			fail = 1;
		}
	}
	printf( fail ? "RESULT: FAIL\n" : "RESULT: ALL OK\n" );
	return fail;
}
