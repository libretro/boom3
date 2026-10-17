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

#ifndef __FRAME_BLOCK_H__
#define __FRAME_BLOCK_H__

/* A block of the per-frame memory R_FrameAlloc hands out. base is 16
 * byte aligned: the header is padded to 16 on every pointer width, and
 * the block comes from Mem_Alloc16. tools/alignment checks this. */
typedef struct frameMemoryBlock_s {
	struct frameMemoryBlock_s *next;
	int		size;
	int		used;
	/* pads the header to the next multiple of 16: 1 int on 32-bit,
	 * 4 on 64-bit (a header of exactly 16 would need none, but an
	 * empty array is not C89, so the 64-bit header is 32) */
	int		pad[ ( 16 - ( sizeof( void * ) + 2 * sizeof( int ) ) % 16 ) / sizeof( int ) ];
	unsigned char	base[16];	/* dynamically allocated as [size] */
} frameMemoryBlock_t;

#endif /* !__FRAME_BLOCK_H__ */
