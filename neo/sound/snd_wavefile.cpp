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
#include "framework/FileSystem.h"

#include "sound/snd_local.h"

//-----------------------------------------------------------------------------
// Name: idWaveFile::idWaveFile()
// Desc: Constructs the class.  Call Open() to open a wave file for reading.
//       Then call Read() as needed.  Calling the destructor or Close()
//       will close the file.
//-----------------------------------------------------------------------------
idWaveFile::idWaveFile( void ) {
	memset( &mpwfx, 0, sizeof( waveformatextensible_t ) );
	mhmmio		= NULL;
	mdwSize		= 0;
	mbIsReadingFromMemory = false;
	mpbData		= NULL;
	isOgg		= false;
}

//-----------------------------------------------------------------------------
// Name: idWaveFile::~idWaveFile()
// Desc: Destructs the class
//-----------------------------------------------------------------------------
idWaveFile::~idWaveFile( void ) {
	Close();

	memset( &mpwfx, 0, sizeof( waveformatextensible_t ) );
}

//-----------------------------------------------------------------------------
// Name: idWaveFile::Open()
// Desc: Opens a wave file for reading
//-----------------------------------------------------------------------------
int idWaveFile::Open( const char* strFileName, waveformatex_t* pwfx ) {

	mbIsReadingFromMemory = false;

	mpbData     = NULL;
	mpbDataCur  = mpbData;

	if( strFileName == NULL )
		return -1;

	idStr name = strFileName;

	// note: used to only check for .wav when making a build
	name.SetFileExtension( ".ogg" );
	if ( fileSystem->ReadFile( name, NULL, NULL ) != -1 ) {
		return OpenOGG( name, pwfx );
	}

	memset( &mpwfx, 0, sizeof( waveformatextensible_t ) );

	mhmmio = fileSystem->OpenFileRead( strFileName );
	if ( !mhmmio ) {
		mdwSize = 0;
		return -1;
	}
	if ( mhmmio->Length() <= 0 ) {
		mhmmio = NULL;
		return -1;
	}
	if ( ReadMMIO() != 0 ) {
		// ReadMMIO will fail if its an not a wave file
		Close();
		return -1;
	}

	mfileTime = mhmmio->Timestamp();

	if ( ResetFile() != 0 ) {
		Close();
		return -1;
	}

	// After the reset, the size of the wav file is mck.cksize so store it now
	mdwSize = mck.cksize / sizeof( short );
	mMemSize = mck.cksize;

	if ( mck.cksize != 0xffffffff )
	{
		if ( pwfx )
			memcpy( pwfx, (waveformatex_t *)&mpwfx, sizeof(waveformatex_t));
		return 0;
	}
	return -1;
}

//-----------------------------------------------------------------------------
// Name: idWaveFile::ReadMMIO()
// Desc: Support function for reading from a multimedia I/O stream.
//       mhmmio must be valid before calling.  This function uses it to
//       update mckRiff, and mpwfx.
//-----------------------------------------------------------------------------
int idWaveFile::ReadMMIO( void ) {
	mminfo_t		ckIn;           // chunk info. for general use.
	pcmwaveformat_t pcmWaveFormat;  // Temp PCM structure to load in.

	memset( &mpwfx, 0, sizeof( waveformatextensible_t ) );

	mhmmio->Read( &mckRiff, 12 );
	assert( !isOgg );
#ifdef MSB_FIRST
	mckRiff.ckid    = D3_Swap32( mckRiff.ckid );
	mckRiff.cksize  = D3_Swap32( mckRiff.cksize );
	mckRiff.fccType = D3_Swap32( mckRiff.fccType );
#endif
	mckRiff.dwDataOffset = 12;

	// Check to make sure this is a valid wave file
	if( (mckRiff.ckid != fourcc_riff) || (mckRiff.fccType != mmioFOURCC('W', 'A', 'V', 'E') ) )
		return -1;

	// Search the input file for for the 'fmt ' chunk.
	ckIn.dwDataOffset = 12;
	do {
		if (8 != mhmmio->Read( &ckIn, 8 ) )
			return -1;
		assert( !isOgg );
#ifdef MSB_FIRST
		ckIn.ckid   = D3_Swap32( ckIn.ckid );
		ckIn.cksize = D3_Swap32( ckIn.cksize );
#endif
		ckIn.dwDataOffset += ckIn.cksize-8;
	} while (ckIn.ckid != mmioFOURCC('f', 'm', 't', ' '));

	// Expect the 'fmt' chunk to be at least as large as <PCMWAVEFORMAT>;
	// if there are extra parameters at the end, we'll ignore them
	if( ckIn.cksize < sizeof(pcmwaveformat_t) )
		return -1;

	// Read the 'fmt ' chunk into <pcmWaveFormat>.
	if( mhmmio->Read( &pcmWaveFormat, sizeof(pcmWaveFormat) ) != sizeof(pcmWaveFormat) )
		return -1;
	assert( !isOgg );
#ifdef MSB_FIRST
	pcmWaveFormat.wf.wFormatTag = D3_Swap16( pcmWaveFormat.wf.wFormatTag );
	pcmWaveFormat.wf.nChannels = D3_Swap16( pcmWaveFormat.wf.nChannels );
	pcmWaveFormat.wf.nSamplesPerSec = D3_Swap32( pcmWaveFormat.wf.nSamplesPerSec );
	pcmWaveFormat.wf.nAvgBytesPerSec = D3_Swap32( pcmWaveFormat.wf.nAvgBytesPerSec );
	pcmWaveFormat.wf.nBlockAlign = D3_Swap16( pcmWaveFormat.wf.nBlockAlign );
	pcmWaveFormat.wBitsPerSample = D3_Swap16( pcmWaveFormat.wBitsPerSample );
#endif

	// Copy the bytes from the pcm structure to the waveformatex_t structure
	memcpy( &mpwfx, &pcmWaveFormat, sizeof(pcmWaveFormat) );

	// Allocate the waveformatex_t, but if its not pcm format, read the next
	// word, and thats how many extra bytes to allocate.
	if( pcmWaveFormat.wf.wFormatTag != WAVE_FORMAT_TAG_PCM )
		return -1; /* We don't handle these (32bit wavefiles, etc) */

	mpwfx.Format.cbSize = 0;

	return 0;
}

//-----------------------------------------------------------------------------
// Name: idWaveFile::ResetFile()
// Desc: Resets the internal mck pointer so reading starts from the
//       beginning of the file again
//-----------------------------------------------------------------------------
int idWaveFile::ResetFile( void )
{
	if( mbIsReadingFromMemory )
		mpbDataCur = mpbData;
	else 
	{
		if( mhmmio == NULL )
			return -1;

		// Seek to the data
		if( -1 == mhmmio->Seek( mckRiff.dwDataOffset + sizeof(fourcc), FS_SEEK_SET ) )
			return -1;

		// Search the input file for for the 'fmt ' chunk.
		mck.ckid = 0;
		do {
			byte ioin;
			if ( !mhmmio->Read( &ioin, 1 ) )
				return -1;
			mck.ckid = (mck.ckid>>8) | (ioin<<24);
		} while (mck.ckid != mmioFOURCC('d', 'a', 't', 'a'));

		mhmmio->Read( &mck.cksize, 4 );
		assert( !isOgg );
#ifdef MSB_FIRST
		mck.cksize = D3_Swap32( mck.cksize );
#endif
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Name: idWaveFile::Read()
// Desc: Reads section of data from a wave file into pBuffer and returns
//       how much read in pdwSizeRead, reading not more than dwSizeToRead.
//       This uses mck to determine where to start reading from.  So
//       subsequent calls will be continue where the last left off unless
//       Reset() is called.
//-----------------------------------------------------------------------------
int idWaveFile::Read( byte* pBuffer, int dwSizeToRead, int *pdwSizeRead ) {

	if ( mbIsReadingFromMemory )
	{
		/*
		   This path serves every OGG load (see OpenOGG). The old
		   arithmetic mixed units: mpbData is short* but dwSizeToRead and
		   mulDataSize are byte counts, so the bounds check compared
		   quantities scaled by two on both sides - correct only by
		   cancellation for the single full-size read the cache performs -
		   and the cursor advanced two bytes per byte read, which any
		   second partial read would have paid for. Everything below is in
		   bytes.
		*/
		if( mpbDataCur == NULL )
			return -1;
		{
			const byte *base = (const byte *)mpbData;
			const byte *cur  = (const byte *)mpbDataCur;
			int avail = (int)mulDataSize - (int)( cur - base );
			if ( dwSizeToRead > avail )
				dwSizeToRead = avail < 0 ? 0 : avail;
			SIMDProcessor->Memcpy( pBuffer, cur, dwSizeToRead );
			mpbDataCur = (short *)( cur + dwSizeToRead );
		}
	}
	else
	{
		if( mhmmio == NULL )
			return -1;
		if( pBuffer == NULL )
			return -1;

		dwSizeToRead = mhmmio->Read( pBuffer, dwSizeToRead );
#ifdef MSB_FIRST
		/* this is hit by ogg code, which does its 
		 * own byte swapping internally */
		if (!isOgg)
		{
			int elcount      = dwSizeToRead / 2;
			unsigned char *p = ( unsigned char * )pBuffer;
			unsigned char *q = p + 1;
			while ( elcount-- )
			{
				*p ^= *q;
				*q ^= *p;
				*p ^= *q;
				p += 2;
				q += 2;
			}
		}
#endif
	}
	if (pdwSizeRead)
		*pdwSizeRead = dwSizeToRead;
	return dwSizeToRead;
}

//-----------------------------------------------------------------------------
// Name: idWaveFile::Close()
// Desc: Closes the wave file
//-----------------------------------------------------------------------------
int idWaveFile::Close( void ) {
	/*
	   An OGG opened by OpenOGG serves the cache read from the probe's own
	   in-memory copy of the file; that buffer is owned and freed here.
	*/
	if ( isOgg && mbIsReadingFromMemory && mpbData != NULL ) {
		Mem_Free( (void *)mpbData );
		mpbData = NULL;
		mpbDataCur = NULL;
		mbIsReadingFromMemory = false;
	}
	if( mhmmio != NULL ) {
		fileSystem->CloseFile( mhmmio );
		mhmmio = NULL;
	}
	return 0;
}

