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

#include "framework/bgread.h"

void bgread_read( RFILE *osFile, rzip_file_t *zipFile, void *buf, int len,
		int *bytes, retro_atomic_int_t *cancel, retro_atomic_int_t *stop )
{
	char *p = (char *)buf;

	while ( *bytes >= 0 && *bytes < len ) {
		int want = len - *bytes;
		int got;
		if ( cancel && retro_atomic_load_acquire_int( cancel ) ) {
			/* the main thread has read the file itself in the meantime */
			*bytes = -1;
			return;
		}
		if ( stop && retro_atomic_load_acquire_int( stop ) ) {
			return;
		}
		if ( want > BGREAD_CHUNK ) {
			want = BGREAD_CHUNK;
		}
		if ( zipFile ) {
			got = rzip_file_read( zipFile, p + *bytes, want );
		} else {
			got = (int)filestream_read( osFile, p + *bytes, want );
		}
		if ( got <= 0 ) {
			if ( *bytes == 0 ) {
				*bytes = got;
			}
			return;
		}
		*bytes += got;
		if ( got < want ) {
			return;	/* short file */
		}
	}
}
