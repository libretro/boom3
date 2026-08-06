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

#if MACOS_X
#include <stdlib.h>
#include <unistd.h>			// this is for sleep()
#include <sys/time.h>
#include <sys/resource.h>
#include <mach/mach_time.h>
#endif

#include "sys/platform.h"
#include "idlib/geometry/DrawVert.h"
#include "idlib/geometry/JointTransform.h"
#include "idlib/math/Simd_Generic.h"
#include "idlib/math/Simd_SSE.h"
#include "idlib/math/Simd_SSE2.h"
#include "idlib/math/Simd_AltiVec.h"
#include "idlib/math/Plane.h"
#include "idlib/bv/Bounds.h"
#include "idlib/Lib.h"
#include "framework/Common.h"
#include "renderer/Model.h"

#include "idlib/math/Simd.h"

idSIMDProcessor	*	processor = NULL;			// pointer to SIMD processor
idSIMDProcessor *	generic = NULL;				// pointer to generic SIMD implementation
idSIMDProcessor *	SIMDProcessor = NULL;

/*
================
idSIMD::Init
================
*/
void idSIMD::Init( void ) {
	generic = new idSIMD_Generic;
	generic->cpuid = CPUID_GENERIC;
	processor = NULL;
	SIMDProcessor = generic;
}

/*
============
idSIMD::InitProcessor
============
*/
void idSIMD::InitProcessor( const char *module, bool forceGeneric ) {
	int cpuid;
	idSIMDProcessor *newProcessor;

	cpuid = idLib::sys->GetProcessorId();

	if ( forceGeneric ) {

		newProcessor = generic;

	} else {

		if ( !processor ) {
			if ( ( cpuid & CPUID_ALTIVEC ) ) {
				processor = new idSIMD_AltiVec;
			} else if ( ( cpuid & CPUID_SSE ) && ( cpuid & CPUID_SSE2 ) ) {
				processor = new idSIMD_SSE2;
			} else if ( ( cpuid & CPUID_SSE ) ) {
				processor = new idSIMD_SSE;
			} else {
				processor = generic;
			}
			processor->cpuid = cpuid;
		}

		newProcessor = processor;
	}

	if ( newProcessor != SIMDProcessor ) {
		SIMDProcessor = newProcessor;
		idLib::common->Printf( "%s using %s for SIMD processing\n", module, SIMDProcessor->GetName() );
	}

	if ( cpuid & CPUID_SSE ) {
		idLib::sys->FPU_SetFTZ( true );
		idLib::sys->FPU_SetDAZ( true );
	}
}

/*
================
idSIMD::Shutdown
================
*/
void idSIMD::Shutdown( void ) {
	if ( processor != generic ) {
		delete processor;
	}
	delete generic;
	generic = NULL;
	processor = NULL;
	SIMDProcessor = NULL;
}
