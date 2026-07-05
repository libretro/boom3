#include "sys/platform.h"
#include "framework/File.h"

#include "idlib/Base64.h"

/*
Copyright (c) 1996 Lars Wirzenius.  All rights reserved.

June 14 2003: TTimo <ttimo@idsoftware.com>
	modified + endian bug fixes
	http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=197039

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

/*
============
idBase64::Encode
============
*/
static const char sixtet_to_base64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void idBase64::Encode( const byte *from, int size ) {
	int i, j;
	unsigned int w;
	byte *to;

	EnsureAlloced( 4*(size+3)/3 + 2 ); // ratio and padding + trailing \0
	to = data;

	w = 0;
	i = 0;
	while (size > 0) {
		w |= *from << i*8;
		++from;
		--size;
		++i;
		if (size == 0 || i == 3) {
			byte out[4];
			SixtetsForInt( out, w );
			for (j = 0; j*6 < i*8; ++j) {
				*to++ = sixtet_to_base64[ out[j] ];
			}
			if (size == 0) {
				for (j = i; j < 3; ++j) {
					*to++ = '=';
				}
			}
			w = 0;
			i = 0;
		}
	}

	*to++ = '\0';
	len = to - data;
}

/*
============
idBase64::DecodeLength
returns the minimum size in bytes of the target buffer for decoding
4 base64 digits <-> 3 bytes
============
*/
int idBase64::DecodeLength( void ) const {
	return 3*len/4;
}

/*
============
idBase64::Decode
============
*/
int idBase64::Decode( byte *to ) const {
	unsigned int w;
	int i, j;
	size_t n;
	static char base64_to_sixtet[256];
	static int tab_init = 0;
	byte *from = data;

	if (!tab_init) {
		memset( base64_to_sixtet, 0, 256 );
		for (i = 0; (j = sixtet_to_base64[i]) != '\0'; ++i) {
			base64_to_sixtet[j] = i;
		}
		tab_init = 1;
	}

	w = 0;
	i = 0;
	n = 0;
	byte in[4] = {0,0,0,0};
	while (*from != '\0' && *from != '=' ) {
		if (*from == ' ' || *from == '\n') {
			++from;
			continue;
		}
		in[i] = base64_to_sixtet[* (unsigned char *) from];
		++i;
		++from;
		if (*from == '\0' || *from == '=' || i == 4) {
			w = IntForSixtets( in );
			for (j = 0; j*8 < i*6; ++j) {
				*to++ = w & 0xff;
				++n;
				w >>= 8;
			}
			i = 0;
			w = 0;
		}
	}
	return n;
}

/*
============
idBase64::Encode
============
*/
void idBase64::Encode( const idStr &src ) {
	Encode( (const byte *)src.c_str(), src.Length() );
}

/*
============
idBase64::Decode
============
*/
void idBase64::Decode( idStr &dest ) const {
	byte *buf = new byte[ DecodeLength()+1 ]; // +1 for trailing \0
	int out = Decode( buf );
	buf[out] = '\0';
	dest = (const char *)buf;
	delete[] buf;
}

/*
============
idBase64::Decode
============
*/
void idBase64::Decode( idFile *dest ) const {
	byte *buf = new byte[ DecodeLength()+1 ]; // +1 for trailing \0
	int out = Decode( buf );
	dest->Write( buf, out );
	delete[] buf;
}
