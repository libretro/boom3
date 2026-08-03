#ifndef IOAPI_MAPPED_H
#define IOAPI_MAPPED_H

#include "ioapi.h"

/* minizip filefunc backend over libretro-common's VFS: mmap fast path
   where the platform supports it, buffered RFILE reads otherwise.
   See ioapi_mapped.cpp. */
void fill_mapped_filefunc64( zlib_filefunc64_def *pzlib_filefunc_def );

#endif
