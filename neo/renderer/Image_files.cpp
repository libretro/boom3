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

// DG replaced libjpeg with stb_image.h here; JPEG decoding now goes through
// libretro-common's rjpeg via image_transfer (see LoadJPG()), the same backend
// used for TGA, so stb_image.h is no longer needed for reading.
#include "sys/platform.h"

// libretro-common image_transfer: include right after platform.h and before
// the idlib/framework headers, so <formats/image.h>'s C declarations are seen
// before idlib installs its string-function macros (see File.cpp / the
// Image_async.cpp include-order fix).
#include <formats/image.h>

#include "renderer/tr_local.h"

#include "renderer/Image.h"

/*

This file only has a single entry point:

void R_LoadImage( const char *name, byte **pic, int *width, int *height, bool makePowerOf2 );

*/

/*
================
R_WriteTGA
================
*/
void R_WriteTGA( const char *filename, const byte *data, int width, int height, bool flipVertical ) {
	byte	*buffer;
	int		i;
	int		bufferSize = width*height*4 + 18;
	int     imgStart = 18;

	buffer = (byte *)Mem_Alloc( bufferSize );
	memset( buffer, 0, 18 );
	buffer[2] = 2;		// uncompressed type
	buffer[12] = width&255;
	buffer[13] = width>>8;
	buffer[14] = height&255;
	buffer[15] = height>>8;
	buffer[16] = 32;	// pixel size
	if ( !flipVertical ) {
		buffer[17] = (1<<5);	// flip bit, for normal top to bottom raster order
	}

	// swap rgb to bgr
	for ( i=imgStart ; i<bufferSize ; i+=4 ) {
		buffer[i] = data[i-imgStart+2];		// blue
		buffer[i+1] = data[i-imgStart+1];		// green
		buffer[i+2] = data[i-imgStart+0];		// red
		buffer[i+3] = data[i-imgStart+3];		// alpha
	}

	fileSystem->WriteFile( filename, buffer, bufferSize );

	Mem_Free (buffer);
}


/*
================
R_WritePalTGA
================
*/
void R_WritePalTGA( const char *filename, const byte *data, const byte *palette, int width, int height, bool flipVertical ) {
	byte	*buffer;
	int		i;
	int		bufferSize = (width * height) + (256 * 3) + 18;
	int     palStart = 18;
	int     imgStart = 18 + (256 * 3);

	buffer = (byte *)Mem_Alloc( bufferSize );
	memset( buffer, 0, 18 );
	buffer[1] = 1;		// color map type
	buffer[2] = 1;		// uncompressed color mapped image
	buffer[5] = 0;		// number of palette entries (lo)
	buffer[6] = 1;		// number of palette entries (hi)
	buffer[7] = 24;		// color map bpp
	buffer[12] = width&255;
	buffer[13] = width>>8;
	buffer[14] = height&255;
	buffer[15] = height>>8;
	buffer[16] = 8;	// pixel size
	if ( !flipVertical ) {
		buffer[17] = (1<<5);	// flip bit, for normal top to bottom raster order
	}

	// store palette, swapping rgb to bgr
	for ( i=palStart ; i<imgStart ; i+=3 ) {
		buffer[i] = palette[i-palStart+2];		// blue
		buffer[i+1] = palette[i-palStart+1];		// green
		buffer[i+2] = palette[i-palStart+0];		// red
	}

	// store the image data
	for ( i=imgStart ; i<bufferSize ; i++ ) {
		buffer[i] = data[i-imgStart];
	}

	fileSystem->WriteFile( filename, buffer, bufferSize );

	Mem_Free (buffer);
}


static void LoadBMP( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp );
static void LoadJPG( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp );


/*
========================================================================

PCX files are used for 8 bit images

========================================================================
*/

typedef struct {
	char	manufacturer;
	char	version;
	char	encoding;
	char	bits_per_pixel;
	unsigned short	xmin,ymin,xmax,ymax;
	unsigned short	hres,vres;
	unsigned char	palette[48];
	char	reserved;
	char	color_planes;
	unsigned short	bytes_per_line;
	unsigned short	palette_type;
	char	filler[58];
	unsigned char	data;			// unbounded
} pcx_t;


/*
========================================================================

TGA files are used for 24/32 bit images

========================================================================
*/

typedef struct _TargaHeader {
	unsigned char	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} TargaHeader;



/*
=========================================================

BMP LOADING

=========================================================
*/
typedef struct
{
	char id[2];
	unsigned int fileSize;
	unsigned int reserved0;
	unsigned int bitmapDataOffset;
	unsigned int bitmapHeaderSize;
	unsigned int width;
	unsigned int height;
	unsigned short planes;
	unsigned short bitsPerPixel;
	unsigned int compression;
	unsigned int bitmapDataSize;
	unsigned int hRes;
	unsigned int vRes;
	unsigned int colors;
	unsigned int importantColors;
	unsigned char palette[256][4];
} BMPHeader_t;

/*
==============
LoadBMP
==============
*/
static void LoadBMP( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp )
{
	int		columns, rows, numPixels;
	byte	*pixbuf;
	int		row, column;
	byte	*buf_p;
	byte	*buffer;
	int		length;
	BMPHeader_t bmpHeader;
	byte		*bmpRGBA;

	if ( !pic ) {
		fileSystem->ReadFile ( name, NULL, timestamp );
		return;	// just getting timestamp
	}

	*pic = NULL;

	//
	// load the file
	//
	length = fileSystem->ReadFile( name, (void **)&buffer, timestamp );
	if ( !buffer ) {
		return;
	}

	buf_p = buffer;

	bmpHeader.id[0] = *buf_p++;
	bmpHeader.id[1] = *buf_p++;
#ifdef MSB_FIRST
	bmpHeader.fileSize = D3_Swap32( * ( int * ) buf_p );
#else
	bmpHeader.fileSize = ( * ( int * ) buf_p );
#endif
	buf_p += 4;
#ifdef MSB_FIRST
	bmpHeader.reserved0 = D3_Swap32( * ( int * ) buf_p );
#else
	bmpHeader.reserved0 = ( * ( int * ) buf_p );
#endif
	buf_p += 4;
#ifdef MSB_FIRST
	bmpHeader.bitmapDataOffset = D3_Swap32( * ( int * ) buf_p );
#else
	bmpHeader.bitmapDataOffset = ( * ( int * ) buf_p );
#endif
	buf_p += 4;
#ifdef MSB_FIRST
	bmpHeader.bitmapHeaderSize = D3_Swap32( * ( int * ) buf_p );
#else
	bmpHeader.bitmapHeaderSize = ( * ( int * ) buf_p );
#endif
	buf_p += 4;
#ifdef MSB_FIRST
	bmpHeader.width = D3_Swap32( * ( int * ) buf_p );
#else
	bmpHeader.width = ( * ( int * ) buf_p );
#endif
	buf_p += 4;
#ifdef MSB_FIRST
	bmpHeader.height = D3_Swap32( * ( int * ) buf_p );
#else
	bmpHeader.height = ( * ( int * ) buf_p );
#endif
	buf_p += 4;
#ifdef MSB_FIRST
	bmpHeader.planes = D3_Swap16( * ( short * ) buf_p );
#else
	bmpHeader.planes = ( * ( short * ) buf_p );
#endif
	buf_p += 2;
#ifdef MSB_FIRST
	bmpHeader.bitsPerPixel = D3_Swap16( * ( short * ) buf_p );
#else
	bmpHeader.bitsPerPixel = ( * ( short * ) buf_p );
#endif
	buf_p += 2;
#ifdef MSB_FIRST
	bmpHeader.compression = D3_Swap32( * ( int * ) buf_p );
#else
	bmpHeader.compression = ( * ( int * ) buf_p );
#endif
	buf_p += 4;
#ifdef MSB_FIRST
	bmpHeader.bitmapDataSize = D3_Swap32( * ( int * ) buf_p );
#else
	bmpHeader.bitmapDataSize = ( * ( int * ) buf_p );
#endif
	buf_p += 4;
#ifdef MSB_FIRST
	bmpHeader.hRes = D3_Swap32( * ( int * ) buf_p );
#else
	bmpHeader.hRes = ( * ( int * ) buf_p );
#endif
	buf_p += 4;
#ifdef MSB_FIRST
	bmpHeader.vRes = D3_Swap32( * ( int * ) buf_p );
#else
	bmpHeader.vRes = ( * ( int * ) buf_p );
#endif
	buf_p += 4;
#ifdef MSB_FIRST
	bmpHeader.colors = D3_Swap32( * ( int * ) buf_p );
#else
	bmpHeader.colors = ( * ( int * ) buf_p );
#endif
	buf_p += 4;
#ifdef MSB_FIRST
	bmpHeader.importantColors = D3_Swap32( * ( int * ) buf_p );
#else
	bmpHeader.importantColors = ( * ( int * ) buf_p );
#endif
	buf_p += 4;

	memcpy( bmpHeader.palette, buf_p, sizeof( bmpHeader.palette ) );

	if ( bmpHeader.bitsPerPixel == 8 )
		buf_p += 1024;

	if ( bmpHeader.id[0] != 'B' && bmpHeader.id[1] != 'M' )
		common->Error( "LoadBMP: only Windows-style BMP files supported (%s)\n", name );
	if ( bmpHeader.fileSize != length )
		common->Error( "LoadBMP: header size does not match file size (%u vs. %d) (%s)\n", bmpHeader.fileSize, length, name );
	if ( bmpHeader.compression != 0 )
		common->Error( "LoadBMP: only uncompressed BMP files supported (%s)\n", name );
	if ( bmpHeader.bitsPerPixel < 8 )
		common->Error( "LoadBMP: monochrome and 4-bit BMP files not supported (%s)\n", name );

	columns = bmpHeader.width;
	rows = bmpHeader.height;
	if ( rows < 0 )
		rows = -rows;
	numPixels = columns * rows;

	if ( width )
		*width = columns;
	if ( height )
		*height = rows;

	bmpRGBA = (byte *)R_StaticAlloc( numPixels * 4 );
	*pic = bmpRGBA;


	for ( row = rows-1; row >= 0; row-- )
	{
		pixbuf = bmpRGBA + row*columns*4;

		for ( column = 0; column < columns; column++ )
		{
			unsigned char red, green, blue, alpha;
			int palIndex;
			unsigned short shortPixel;

			switch ( bmpHeader.bitsPerPixel )
			{
			case 8:
				palIndex = *buf_p++;
				*pixbuf++ = bmpHeader.palette[palIndex][2];
				*pixbuf++ = bmpHeader.palette[palIndex][1];
				*pixbuf++ = bmpHeader.palette[palIndex][0];
				*pixbuf++ = 0xff;
				break;
			case 16:
				shortPixel = * ( unsigned short * ) pixbuf;
				pixbuf += 2;
				*pixbuf++ = ( shortPixel & ( 31 << 10 ) ) >> 7;
				*pixbuf++ = ( shortPixel & ( 31 << 5 ) ) >> 2;
				*pixbuf++ = ( shortPixel & ( 31 ) ) << 3;
				*pixbuf++ = 0xff;
				break;

			case 24:
				blue = *buf_p++;
				green = *buf_p++;
				red = *buf_p++;
				*pixbuf++ = red;
				*pixbuf++ = green;
				*pixbuf++ = blue;
				*pixbuf++ = 255;
				break;
			case 32:
				blue = *buf_p++;
				green = *buf_p++;
				red = *buf_p++;
				alpha = *buf_p++;
				*pixbuf++ = red;
				*pixbuf++ = green;
				*pixbuf++ = blue;
				*pixbuf++ = alpha;
				break;
			default:
				common->Error( "LoadBMP: illegal pixel_size '%d' in file '%s'\n", bmpHeader.bitsPerPixel, name );
				break;
			}
		}
	}

	fileSystem->FreeFile( buffer );

}


/*
=================================================================

PCX LOADING

=================================================================
*/


/*
==============
LoadPCX
==============
*/
static void LoadPCX ( const char *filename, byte **pic, byte **palette, int *width, int *height, ID_TIME_T *timestamp ) {
	byte	*raw;
	pcx_t	*pcx;
	int		x, y;
	int		len;
	int		dataByte, runLength;
	byte	*out, *pix;
	int		xmax, ymax;

	if ( !pic ) {
		fileSystem->ReadFile( filename, NULL, timestamp );
		return;	// just getting timestamp
	}

	*pic = NULL;
	*palette = NULL;

	//
	// load the file
	//
	len = fileSystem->ReadFile( filename, (void **)&raw, timestamp );
	if (!raw) {
		return;
	}

	//
	// parse the PCX file
	//
	pcx = (pcx_t *)raw;
	raw = &pcx->data;

#ifdef MSB_FIRST
	xmax = D3_Swap16(pcx->xmax);
	ymax = D3_Swap16(pcx->ymax);
#else
	xmax = (pcx->xmax);
	ymax = (pcx->ymax);
#endif

	if (pcx->manufacturer != 0x0a
		|| pcx->version != 5
		|| pcx->encoding != 1
		|| pcx->bits_per_pixel != 8
		|| xmax >= 1024
		|| ymax >= 1024)
	{
		common->Printf( "Bad pcx file %s (%i x %i) (%i x %i)\n", filename, xmax+1, ymax+1, pcx->xmax, pcx->ymax);
		return;
	}

	out = (byte *)R_StaticAlloc( (ymax+1) * (xmax+1) );

	*pic = out;

	pix = out;

	if (palette)
	{
		*palette = (byte *)R_StaticAlloc(768);
		memcpy (*palette, (byte *)pcx + len - 768, 768);
	}

	if (width)
		*width = xmax+1;
	if (height)
		*height = ymax+1;
// FIXME: use bytes_per_line here?

	for (y=0 ; y<=ymax ; y++, pix += xmax+1)
	{
		for (x=0 ; x<=xmax ; )
		{
			dataByte = *raw++;

			if((dataByte & 0xC0) == 0xC0)
			{
				runLength = dataByte & 0x3F;
				dataByte = *raw++;
			}
			else
				runLength = 1;

			while(runLength-- > 0)
				pix[x++] = dataByte;
		}

	}

	if ( raw - (byte *)pcx > len)
	{
		common->Printf( "PCX file %s was malformed", filename );
		R_StaticFree (*pic);
		*pic = NULL;
	}

	fileSystem->FreeFile( pcx );
}


/*
==============
LoadPCX32
==============
*/
static void LoadPCX32 ( const char *filename, byte **pic, int *width, int *height, ID_TIME_T *timestamp) {
	byte	*palette;
	byte	*pic8;
	int		i, c, p;
	byte	*pic32;

	if ( !pic ) {
		fileSystem->ReadFile( filename, NULL, timestamp );
		return;	// just getting timestamp
	}
	LoadPCX (filename, &pic8, &palette, width, height, timestamp);
	if (!pic8) {
		*pic = NULL;
		return;
	}

	c = (*width) * (*height);
	pic32 = *pic = (byte *)R_StaticAlloc(4 * c );
	for (i = 0 ; i < c ; i++) {
		p = pic8[i];
		pic32[0] = palette[p*3];
		pic32[1] = palette[p*3 + 1];
		pic32[2] = palette[p*3 + 2];
		pic32[3] = 255;
		pic32 += 4;
	}

	R_StaticFree( pic8 );
	R_StaticFree( palette );
}

/*
=========================================================

TARGA LOADING

=========================================================
*/


/*
=============
LoadJPG
=============
*/
static void LoadJPG( const char *filename, unsigned char **pic, int *width, int *height, ID_TIME_T *timestamp ) {

	if ( pic ) {
		*pic = NULL;		// until proven otherwise
	}

	idFile *f = fileSystem->OpenFileRead( filename );
	if ( !f ) {
		return;
	}
	int len = f->Length();
	if ( timestamp ) {
		*timestamp = f->Timestamp();
	}
	if ( !pic ) {
		fileSystem->CloseFile( f );
		return;	// just getting timestamp
	}
	byte *fbuffer = (byte *)Mem_ClearedAlloc( len );
	f->Read( fbuffer, len );
	fileSystem->CloseFile( f );

	// decode through libretro-common's image_transfer (rjpeg), the same path
	// LoadTGA() uses for TGA, so the core has one image backend
	void *img = image_transfer_new( IMAGE_TYPE_JPEG );
	if ( img == NULL ) {
		Mem_Free( fbuffer );
		common->Warning( "Couldn't create JPEG decoder for %s\n", filename );
		return;
	}

	image_transfer_set_buffer_ptr( img, IMAGE_TYPE_JPEG, fbuffer, len );

	if ( !image_transfer_start( img, IMAGE_TYPE_JPEG ) ) {
		image_transfer_free( img, IMAGE_TYPE_JPEG );
		Mem_Free( fbuffer );
		common->Warning( "rjpeg was unable to start decoding JPG %s\n", filename );
		return;
	}

	while ( image_transfer_iterate( img, IMAGE_TYPE_JPEG ) ) {
	}

	if ( !image_transfer_is_valid( img, IMAGE_TYPE_JPEG ) ) {
		image_transfer_free( img, IMAGE_TYPE_JPEG );
		Mem_Free( fbuffer );
		common->Warning( "rjpeg was unable to load JPG %s\n", filename );
		return;
	}

	uint32_t *decoded = NULL;
	unsigned w = 0, h = 0;
	int process = IMAGE_PROCESS_NEXT;
	do {
		process = image_transfer_process( img, IMAGE_TYPE_JPEG,
				&decoded, len, &w, &h, 1 );
	} while ( process == IMAGE_PROCESS_NEXT );

	Mem_Free( fbuffer );

	if ( process == IMAGE_PROCESS_ERROR || process == IMAGE_PROCESS_ERROR_END
			|| decoded == NULL || w == 0 || h == 0 ) {
		image_transfer_free( img, IMAGE_TYPE_JPEG );
		common->Warning( "rjpeg was unable to load JPG %s\n", filename );
		return;
	}

	// image_transfer hands back 32bpp pixels as 0xAARRGGBB in native order;
	// the engine wants RGBA byte order, so swap R and B while copying into
	// the R_StaticAlloc() buffer it requires.
	int size = w * h * 4;
	*pic = (byte *)R_StaticAlloc( size );
	{
		const uint32_t *src = decoded;
		byte *dst = *pic;
		for ( unsigned i = 0; i < w * h; i++ ) {
			uint32_t px = src[i];
			dst[i*4 + 0] = (byte)( px         & 0xFF );	// R
			dst[i*4 + 1] = (byte)( ( px >>  8 ) & 0xFF );	// G
			dst[i*4 + 2] = (byte)( ( px >> 16 ) & 0xFF );	// B
			dst[i*4 + 3] = (byte)( ( px >> 24 ) & 0xFF );	// A
		}
	}
	*width  = w;
	*height = h;

	image_transfer_free( img, IMAGE_TYPE_JPEG );
}

//===================================================================

/*
=================
R_LoadImage

Loads any of the supported image types into a cannonical
32 bit format.

Automatically attempts to load .jpg files if .tga files fail to load.

*pic will be NULL if the load failed.

Anything that is going to make this into a texture would use
makePowerOf2 = true, but something loading an image as a lookup
table of some sort would leave it in identity form.

It is important to do this at image load time instead of texture load
time for bump maps.

Timestamp may be NULL if the value is going to be ignored

If pic is NULL, the image won't actually be loaded, it will just find the
timestamp.
=================
*/
/*
==================
LoadTGA

Decode a TGA using libretro-common's image_transfer: *pic is an R_StaticAlloc'd RGBA8 buffer in R,G,B,A byte order (little-endian), width/height set, timestamp filled. On any failure *pic is left NULL so the caller falls back / MakeDefaults.
==================
*/
static void LoadTGA( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp ) {
	byte	*fileBuf;
	int		fileLen;

	if ( pic ) {
		*pic = NULL;
	}

	// timestamp-only probe - early-out
	if ( !pic ) {
		fileSystem->ReadFile( name, NULL, timestamp );
		return;
	}

	fileLen = fileSystem->ReadFile( name, (void **)&fileBuf, timestamp );
	if ( !fileBuf ) {
		return;
	}

	void *img = image_transfer_new( IMAGE_TYPE_TGA );
	if ( !img ) {
		fileSystem->FreeFile( fileBuf );
		return;
	}

	image_transfer_set_buffer_ptr( img, IMAGE_TYPE_TGA, fileBuf, (size_t)fileLen );

	if ( !image_transfer_start( img, IMAGE_TYPE_TGA ) ) {
		image_transfer_free( img, IMAGE_TYPE_TGA );
		fileSystem->FreeFile( fileBuf );
		return;
	}

	// run the decoder to completion (this is the synchronous variant; the
	// per-frame pump will call image_transfer_iterate one step at a time)
	while ( image_transfer_iterate( img, IMAGE_TYPE_TGA ) ) {
	}

	if ( !image_transfer_is_valid( img, IMAGE_TYPE_TGA ) ) {
		image_transfer_free( img, IMAGE_TYPE_TGA );
		fileSystem->FreeFile( fileBuf );
		return;
	}

	// pull out the decoded RGBA. supports_rgba=true makes rtga pack each
	// pixel as (a<<24)|(b<<16)|(g<<8)|r, i.e. bytes [r,g,b,a] on little-endian
	uint32_t	*transferPixels = NULL;
	unsigned	w = 0, h = 0;
	int			ret;
	do {
		ret = image_transfer_process( img, IMAGE_TYPE_TGA,
				&transferPixels, (size_t)fileLen, &w, &h, true );
	} while ( ret == IMAGE_PROCESS_NEXT );

	image_transfer_free( img, IMAGE_TYPE_TGA );
	fileSystem->FreeFile( fileBuf );

	if ( ret == IMAGE_PROCESS_ERROR || ret == IMAGE_PROCESS_ERROR_END || !transferPixels ) {
		if ( transferPixels ) {
			free( transferPixels );
		}
		return;
	}

	// image_transfer allocates with malloc; copy into an R_StaticAlloc buffer
	// so the rest of the engine (which R_StaticFree's *pic) stays consistent.
	//
	// The engine consumes *pic as an RGBA8 BYTE stream (uploaded with
	// GL_RGBA, GL_UNSIGNED_BYTE), i.e. bytes [r,g,b,a] on every platform.
        // rtga packs each pixel NUMERICALLY as 
        // (a<<24)|(b<<16)|(g<<8)|r, so we must extract
	// the channels by shift rather than memcpy'ing the raw uint32 buffer,
	// otherwise the byte order would flip on big-endian. 
        // Extracting by shift is correct on both endians.
	int		numPixels = (int)( w * h );
	byte	*out = (byte *)R_StaticAlloc( numPixels * 4 );
	for ( int i = 0; i < numPixels; i++ ) {
		uint32_t p = transferPixels[i];
		out[i*4+0] = (byte)( p         & 0xff );	// r
		out[i*4+1] = (byte)( ( p >> 8 )  & 0xff );	// g
		out[i*4+2] = (byte)( ( p >> 16 ) & 0xff );	// b
		out[i*4+3] = (byte)( ( p >> 24 ) & 0xff );	// a
	}
	free( transferPixels );

	*pic = out;
	if ( width ) {
		*width = (int)w;
	}
	if ( height ) {
		*height = (int)h;
	}
}

void R_LoadImage( const char *cname, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool makePowerOf2 ) {
	idStr name = cname;

	if ( pic ) {
		*pic = NULL;
	}
	if ( timestamp ) {
		*timestamp = FILE_NOT_FOUND_TIMESTAMP;
	}
	if ( width ) {
		*width = 0;
	}
	if ( height ) {
		*height = 0;
	}

	name.DefaultFileExtension( ".tga" );

	if (name.Length()<5) {
		return;
	}

	name.ToLower();
	idStr ext;
	name.ExtractFileExtension( ext );

	if ( ext == "tga" ) {
		LoadTGA( name.c_str(), pic, width, height, timestamp );  // try tga first
		if ( ( pic && *pic == 0 ) || ( timestamp && *timestamp == FILE_NOT_FOUND_TIMESTAMP ) ) {
			name.StripFileExtension();
			name.DefaultFileExtension( ".jpg" );
			LoadJPG( name.c_str(), pic, width, height, timestamp );
		}
	} else if ( ext == "pcx" ) {
		LoadPCX32( name.c_str(), pic, width, height, timestamp );
	} else if ( ext == "bmp" ) {
		LoadBMP( name.c_str(), pic, width, height, timestamp );
	} else if ( ext == "jpg" ) {
		LoadJPG( name.c_str(), pic, width, height, timestamp );
	}

	if ( ( width && *width < 1 ) || ( height && *height < 1 ) ) {
		if ( pic && *pic ) {
			R_StaticFree( *pic );
			*pic = 0;
		}
	}

	//
	// convert to exact power of 2 sizes
	//
	if ( pic && *pic && makePowerOf2 ) {
		int		w, h;
		int		scaled_width, scaled_height;
		byte	*resampledBuffer;

		w = *width;
		h = *height;

		for (scaled_width = 1 ; scaled_width < w ; scaled_width<<=1)
			;
		for (scaled_height = 1 ; scaled_height < h ; scaled_height<<=1)
			;

		if ( scaled_width != w || scaled_height != h ) {
			if ( globalImages->image_roundDown.GetBool() && scaled_width > w ) {
				scaled_width >>= 1;
			}
			if ( globalImages->image_roundDown.GetBool() && scaled_height > h ) {
				scaled_height >>= 1;
			}
			int outWidth = scaled_width;
			int outHeight = scaled_height;
			resampledBuffer = R_ResampleTexture( *pic, w, h, outWidth, outHeight );
			if ( outWidth != scaled_width || outHeight != scaled_height ) {
				common->Warning( "Texture '%s' didn't have power-of-two size *and* was too big, scaled from %dx%d to %dx%d",
				                 name.c_str(), w, h, outWidth, outHeight );
			}

			R_StaticFree( *pic );
			*pic = resampledBuffer;
			*width = outWidth;
			*height = outHeight;
		}
	}
}


/*
=======================
R_LoadCubeImages

Loads six files with proper extensions
=======================
*/
bool R_LoadCubeImages( const char *imgName, cubeFiles_t extensions, byte *pics[6], int *outSize, ID_TIME_T *timestamp ) {
	int		i, j;
	const char	*cameraSides[6] =  { "_forward.tga", "_back.tga", "_left.tga", "_right.tga",
		"_up.tga", "_down.tga" };
	const char	*axisSides[6] =  { "_px.tga", "_nx.tga", "_py.tga", "_ny.tga",
		"_pz.tga", "_nz.tga" };
	const char	**sides;
	char	fullName[MAX_IMAGE_NAME];
	int		width, height, size = 0;

	if ( extensions == CF_CAMERA ) {
		sides = cameraSides;
	} else {
		sides = axisSides;
	}

	// FIXME: precompressed cube map files
	if ( pics ) {
		memset( pics, 0, 6*sizeof(pics[0]) );
	}
	if ( timestamp ) {
		*timestamp = 0;
	}

	for ( i = 0 ; i < 6 ; i++ ) {
		idStr::snPrintf( fullName, sizeof( fullName ), "%s%s", imgName, sides[i] );

		ID_TIME_T thisTime;
		if ( !pics ) {
			// just checking timestamps
			R_LoadImageProgram( fullName, NULL, &width, &height, &thisTime );
		} else {
			R_LoadImageProgram( fullName, &pics[i], &width, &height, &thisTime );
		}
		if ( thisTime == FILE_NOT_FOUND_TIMESTAMP ) {
			break;
		}
		if ( i == 0 ) {
			size = width;
		}
		if ( width != size || height != size ) {
			common->Warning( "Mismatched sizes on cube map '%s'", imgName );
			break;
		}
		if ( timestamp ) {
			if ( thisTime > *timestamp ) {
				*timestamp = thisTime;
			}
		}
		if ( pics && extensions == CF_CAMERA ) {
			// convert from "camera" images to native cube map images
			switch( i ) {
			case 0:	// forward
				R_RotatePic( pics[i], width);
				break;
			case 1:	// back
				R_RotatePic( pics[i], width);
				R_HorizontalFlip( pics[i], width, height );
				R_VerticalFlip( pics[i], width, height );
				break;
			case 2:	// left
				R_VerticalFlip( pics[i], width, height );
				break;
			case 3:	// right
				R_HorizontalFlip( pics[i], width, height );
				break;
			case 4:	// up
				R_RotatePic( pics[i], width);
				break;
			case 5: // down
				R_RotatePic( pics[i], width);
				break;
			}
		}
	}

	if ( i != 6 ) {
		// we had an error, so free everything
		if ( pics ) {
			for ( j = 0 ; j < i ; j++ ) {
				R_StaticFree( pics[j] );
				// DG: this lets ActuallyLoadImage() print a warning and load
				//     a default texture instead of crash later
				pics[j] = NULL;
			}
		}

		if ( timestamp ) {
			*timestamp = 0;
		}
		return false;
	}

	if ( outSize ) {
		*outSize = size;
	}
	return true;
}
