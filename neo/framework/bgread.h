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

#ifndef __BGREAD_H__
#define __BGREAD_H__

#include <retro_atomic.h>
#include <streams/file_stream.h>

#include "framework/rzip.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The background reader's read, on whichever thread does it: one of
 * osFile and zipFile, into buf, len bytes from position bytes on. Reads
 * in BGREAD_CHUNK steps so a cancel takes effect within one chunk and
 * stop (the reader's exit, may be NULL) returns early with the read
 * resumable from bytes. bytes is the count read so far on entry and
 * exit, -1 once cancelled or failed. */
#define BGREAD_CHUNK	( 256 * 1024 )

void bgread_read( RFILE *osFile, rzip_file_t *zipFile, void *buf, int len,
		int *bytes, retro_atomic_int_t *cancel, retro_atomic_int_t *stop );

#ifdef __cplusplus
}
#endif

#endif /* !__BGREAD_H__ */
