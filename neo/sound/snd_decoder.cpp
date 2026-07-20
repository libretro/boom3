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

#ifdef MSB_FIRST
  #define STB_VORBIS_BIG_ENDIAN
#endif

#include "sys/platform.h"
#include "framework/FileSystem.h"

// libretro-common audio_transfer: WAV/OGG decode (rwav/rvorbis).
#include <formats/audio.h>

#include "sound/snd_local.h"

/*
===================================================================================

  Thread safe decoder memory allocator.

  Each OggVorbis decoder consumes about 150kB of memory.

===================================================================================
*/

idDynamicBlockAlloc<byte, 1<<20, 128>		decoderMemoryAllocator;

const int MIN_OGGVORBIS_MEMORY				= 768 * 1024;

/*
===================================================================================

  OggVorbis file loading/decoding.

===================================================================================
*/

/*
====================
idWaveFile::OpenOGG
====================
*/
int idWaveFile::OpenOGG( const char* strFileName, waveformatex_t *pwfx ) {

	memset( pwfx, 0, sizeof( waveformatex_t ) );

	mhmmio = fileSystem->OpenFileRead( strFileName );
	if ( !mhmmio )
		return -1;

	Sys_EnterCriticalSection( CRITICAL_SECTION_ONE );

	int fileSize = mhmmio->Length();
	byte* oggFileData = (byte*)Mem_Alloc( fileSize );

	mhmmio->Read( oggFileData, fileSize );

	// Probe format and length via libretro-common audio_transfer (rvorbis).
	void *at = audio_transfer_new( AUDIO_TYPE_VORBIS );
	if ( at == NULL ) {
		Mem_Free( oggFileData );
		Sys_LeaveCriticalSection( CRITICAL_SECTION_ONE );
		common->Warning( "Opening OGG file '%s' with audio_transfer failed\n", strFileName );
		fileSystem->CloseFile( mhmmio );
		mhmmio = NULL;
		return -1;
	}
	audio_transfer_set_buffer_ptr( at, AUDIO_TYPE_VORBIS, oggFileData, fileSize );
	if ( !audio_transfer_start( at, AUDIO_TYPE_VORBIS )
			|| !audio_transfer_is_valid( at, AUDIO_TYPE_VORBIS ) ) {
		audio_transfer_free( at, AUDIO_TYPE_VORBIS );
		Mem_Free( oggFileData );
		Sys_LeaveCriticalSection( CRITICAL_SECTION_ONE );
		common->Warning( "Opening OGG file '%s' with audio_transfer failed\n", strFileName );
		fileSystem->CloseFile( mhmmio );
		mhmmio = NULL;
		return -1;
	}

	mfileTime = mhmmio->Timestamp();

	unsigned channels = 0, rate = 0;
	uint64_t totalFrames = 0;
	audio_transfer_info( at, AUDIO_TYPE_VORBIS, &channels, &rate, &totalFrames );
	int numSamples = (int)totalFrames;
	if ( numSamples == 0 ) {
		common->Warning( "Couldn't get sound length of '%s' with audio_transfer\n", strFileName );
		// TODO:  return -1 etc?
	}

	mpwfx.Format.nSamplesPerSec = rate;
	mpwfx.Format.nChannels = channels;
	mpwfx.Format.wBitsPerSample = sizeof(short) * 8;
	mdwSize = numSamples * channels;	// pcm samples * num channels
	mbIsReadingFromMemory = false;

	{
		audio_transfer_free( at, AUDIO_TYPE_VORBIS );
		fileSystem->CloseFile( mhmmio );
		mhmmio = NULL;
		Mem_Free( oggFileData );

		mpwfx.Format.wFormatTag = WAVE_FORMAT_TAG_OGG;
		mhmmio = fileSystem->OpenFileRead( strFileName );
		mMemSize = mhmmio->Length();

	}

	memcpy( pwfx, &mpwfx, sizeof( waveformatex_t ) );

	Sys_LeaveCriticalSection( CRITICAL_SECTION_ONE );

	isOgg = true;

	return 0;
}

/*
====================
idWaveFile::ReadOGG
====================
*/
int idWaveFile::ReadOGG( byte* pBuffer, int dwSizeToRead, int *pdwSizeRead ) {
	// Dead path: OpenOGG() stores the encoded OGG and decodes on demand via
	// the sample decoder (audio_transfer/rvorbis); it never leaves an open
	// streaming handle here, so 'ogg' is always NULL. Kept as a stub so the
	// idWaveFile::Read() dispatch stays well-formed.
	(void)pBuffer; (void)dwSizeToRead;
	if ( pdwSizeRead != NULL ) {
		*pdwSizeRead = 0;
	}
	return -1;
}

/*
====================
idWaveFile::CloseOGG
====================
*/
int idWaveFile::CloseOGG( void ) {
	// Dead path (see ReadOGG): 'ogg' is never set, so there's nothing to
	// close. Kept as a stub for the idWaveFile::Close() dispatch.
	return -1;
}


/*
===================================================================================

  idSampleDecoderLocal

===================================================================================
*/

class idSampleDecoderLocal : public idSampleDecoder {
public:
	virtual void			Decode( idSoundSample *sample, int sampleOffset44k, int sampleCount44k, float *dest );
	virtual void			ClearDecoder( void );
	virtual idSoundSample *	GetSample( void ) const;
	virtual int				GetLastDecodeTime( void ) const;

	void					Clear( void );
	int						DecodePCM( idSoundSample *sample, int sampleOffset44k, int sampleCount44k, float *dest );
	int						DecodeOGG( idSoundSample *sample, int sampleOffset44k, int sampleCount44k, float *dest );

private:
	bool					failed;				// set if decoding failed
	int						lastFormat;			// last format being decoded
	idSoundSample *			lastSample;			// last sample being decoded
	int						lastSampleOffset;	// last offset into the decoded sample
	int						lastDecodeTime;		// last time decoding sound

	void *					atHandle;			// libretro-common audio_transfer OGG handle
};

idBlockAlloc<idSampleDecoderLocal, 64>		sampleDecoderAllocator;

/*
====================
idSampleDecoder::Init
====================
*/
void idSampleDecoder::Init( void ) {
	decoderMemoryAllocator.Init();
	decoderMemoryAllocator.SetLockMemory( true );
	decoderMemoryAllocator.SetFixedBlocks( 10 );
}

/*
====================
idSampleDecoder::Shutdown
====================
*/
void idSampleDecoder::Shutdown( void ) {
	decoderMemoryAllocator.Shutdown();
	sampleDecoderAllocator.Shutdown();
}

/*
====================
idSampleDecoder::Alloc
====================
*/
idSampleDecoder *idSampleDecoder::Alloc( void ) {
	idSampleDecoderLocal *decoder = sampleDecoderAllocator.Alloc();
	decoder->Clear();
	return decoder;
}

/*
====================
idSampleDecoder::Free
====================
*/
void idSampleDecoder::Free( idSampleDecoder *decoder ) {
	idSampleDecoderLocal *localDecoder = static_cast<idSampleDecoderLocal *>( decoder );
	localDecoder->ClearDecoder();
	sampleDecoderAllocator.Free( localDecoder );
}

/*
====================
idSampleDecoder::GetNumUsedBlocks
====================
*/
int idSampleDecoder::GetNumUsedBlocks( void ) {
	return decoderMemoryAllocator.GetNumUsedBlocks();
}

/*
====================
idSampleDecoder::GetUsedBlockMemory
====================
*/
int idSampleDecoder::GetUsedBlockMemory( void ) {
	return decoderMemoryAllocator.GetUsedBlockMemory();
}

/*
====================
idSampleDecoderLocal::Clear
====================
*/
void idSampleDecoderLocal::Clear( void ) {
	failed = false;
	lastFormat = WAVE_FORMAT_TAG_PCM;
	lastSample = NULL;
	lastSampleOffset = 0;
	lastDecodeTime = 0;
	atHandle = NULL;
}

/*
====================
idSampleDecoderLocal::ClearDecoder
====================
*/
void idSampleDecoderLocal::ClearDecoder( void ) {
	Sys_EnterCriticalSection( CRITICAL_SECTION_ONE );

	switch( lastFormat ) {
		case WAVE_FORMAT_TAG_PCM: {
			break;
		}
		case WAVE_FORMAT_TAG_OGG: {
			if ( atHandle != NULL ) {
				audio_transfer_free( atHandle, AUDIO_TYPE_VORBIS );
				atHandle = NULL;
			}
			break;
		}
	}

	Clear();

	Sys_LeaveCriticalSection( CRITICAL_SECTION_ONE );
}

/*
====================
idSampleDecoderLocal::GetSample
====================
*/
idSoundSample *idSampleDecoderLocal::GetSample( void ) const {
	return lastSample;
}

/*
====================
idSampleDecoderLocal::GetLastDecodeTime
====================
*/
int idSampleDecoderLocal::GetLastDecodeTime( void ) const {
	return lastDecodeTime;
}

/*
====================
idSampleDecoderLocal::Decode
====================
*/
void idSampleDecoderLocal::Decode( idSoundSample *sample, int sampleOffset44k, int sampleCount44k, float *dest ) {
	int readSamples44k;

	if ( sample->objectInfo.wFormatTag != lastFormat || sample != lastSample ) {
		ClearDecoder();
	}

	lastDecodeTime = soundSystemLocal.CurrentSoundTime;

	if ( failed ) {
		memset( dest, 0, sampleCount44k * sizeof( dest[0] ) );
		return;
	}

	// samples can be decoded both from the sound thread and the main thread for shakes
	Sys_EnterCriticalSection( CRITICAL_SECTION_ONE );

	switch( sample->objectInfo.wFormatTag ) {
		case WAVE_FORMAT_TAG_PCM: {
			readSamples44k = DecodePCM( sample, sampleOffset44k, sampleCount44k, dest );
			break;
		}
		case WAVE_FORMAT_TAG_OGG: {
			readSamples44k = DecodeOGG( sample, sampleOffset44k, sampleCount44k, dest );
			break;
		}
		default: {
			readSamples44k = 0;
			break;
		}
	}

	Sys_LeaveCriticalSection( CRITICAL_SECTION_ONE );

	if ( readSamples44k < sampleCount44k )
		memset( dest + readSamples44k, 0, ( sampleCount44k - readSamples44k ) * sizeof( dest[0] ) );
}

/*
====================
idSampleDecoderLocal::DecodePCM
====================
*/
int idSampleDecoderLocal::DecodePCM( idSoundSample *sample, int sampleOffset44k, int sampleCount44k, float *dest ) {
	const byte *first;
	int pos, size, readSamples;

	lastFormat = WAVE_FORMAT_TAG_PCM;
	lastSample = sample;

	int shift = 22050 / sample->objectInfo.nSamplesPerSec;
	int sampleOffset = sampleOffset44k >> shift;
	int sampleCount = sampleCount44k >> shift;

	if ( sample->nonCacheData == NULL ) {
		//assert( false );	// this should never happen ( note: I've seen that happen with the main thread down in idGameLocal::MapClear clearing entities - TTimo )
		// DG: see comment in DecodeOGG()
		common->Warning( "Called idSampleDecoderLocal::DecodePCM() on idSoundSample '%s' without nonCacheData\n", sample->name.c_str() );
		failed = true;
		return 0;
	}

	if ( !sample->FetchFromCache( sampleOffset * sizeof( short ), &first, &pos, &size, false ) ) {
		failed = true;
		return 0;
	}

	if ( size - pos < sampleCount * sizeof( short ) ) {
		readSamples = ( size - pos ) / sizeof( short );
	} else {
		readSamples = sampleCount;
	}

	// duplicate samples for 44kHz output
	SIMDProcessor->UpSamplePCMTo44kHz( dest, (const short *)(first+pos), readSamples, sample->objectInfo.nSamplesPerSec, sample->objectInfo.nChannels );

	return ( readSamples << shift );
}

/*
====================
idSampleDecoderLocal::DecodeOGG
====================
*/

/*
====================
idSampleDecoderLocal::DecodeOGG

Ogg Vorbis decode via libretro-common audio_transfer (rvorbis): open-once,
seek on offset change, decode in <=MIXBUFFER_SAMPLES chunks with
audio_transfer_read_f32 (the float path - no int<->float round-trip), then
UpSampleOGGTo44kHz to 44.1kHz.
====================
*/
int idSampleDecoderLocal::DecodeOGG( idSoundSample *sample, int sampleOffset44k, int sampleCount44k, float *dest ) {
	int readSamples, totalSamples;

	int shift = 22050 / sample->objectInfo.nSamplesPerSec;
	int sampleOffset = sampleOffset44k >> shift;
	int sampleCount = sampleCount44k >> shift;

	// open OGG if not yet opened
	if ( lastSample == NULL ) {
		if ( sample->nonCacheData == NULL ) {
			common->Warning( "Called idSampleDecoderLocal::DecodeOGG() on idSoundSample '%s' without nonCacheData\n", sample->name.c_str() );
			failed = true;
			return 0;
		}
		assert( atHandle == NULL );
		atHandle = audio_transfer_new( AUDIO_TYPE_VORBIS );
		if ( atHandle == NULL ) {
			failed = true;
			return 0;
		}
		audio_transfer_set_buffer_ptr( atHandle, AUDIO_TYPE_VORBIS, sample->nonCacheData, sample->objectMemSize );
		if ( !audio_transfer_start( atHandle, AUDIO_TYPE_VORBIS )
				|| !audio_transfer_is_valid( atHandle, AUDIO_TYPE_VORBIS ) ) {
			common->Warning( "idSampleDecoderLocal::DecodeOGG() audio_transfer_start() for %s failed\n", sample->name.c_str() );
			audio_transfer_free( atHandle, AUDIO_TYPE_VORBIS );
			atHandle = NULL;
			failed = true;
			return 0;
		}
		lastFormat = WAVE_FORMAT_TAG_OGG;
		lastSample = sample;
	}

	if ( sample->objectInfo.nChannels > 2 ) {
		common->Warning( "Ogg Vorbis files with >2 channels are not supported!\n" );
		failed = true;
		return 0;
	}

	/*
	   Seek if the requested offset isn't where we are.

	   sampleOffset is in OUTPUT samples, but audio_transfer_seek() addresses
	   the file in SOURCE frames. Those are the same thing only when the asset
	   rate matches the output rate. When they differ - Ogg is left encoded at
	   its authored rate and resampled per block below - the offset has to be
	   scaled, or every seek lands the wrong distance into the stream: at
	   96kHz output with a 44.1kHz asset it overshoots by 2.18x, which is
	   audible as the stream constantly jumping forward.
	*/
	if ( sampleOffset != lastSampleOffset ) {
		uint64_t srcFrame = (uint64_t)( sampleOffset / sample->objectInfo.nChannels );
		if ( sample->objectInfo.nSamplesPerSec != snd_SampleRate() ) {
			srcFrame = ( srcFrame * (uint64_t)sample->objectInfo.nSamplesPerSec )
					/ (uint64_t)snd_SampleRate();
		}
		if ( !audio_transfer_seek( atHandle, AUDIO_TYPE_VORBIS, srcFrame ) ) {
			common->Warning( "idSampleDecoderLocal::DecodeOGG() seek(%d) for %s failed\n",
					sampleOffset / sample->objectInfo.nChannels, sample->name.c_str() );
			failed = true;
			return 0;
		}
	}

	lastSampleOffset = sampleOffset;

	// decode
	totalSamples = sampleCount;
	readSamples = 0;
	do {
		float interleaved[MIXBUFFER_SAMPLES * 2];
		float planarBuf[2][MIXBUFFER_SAMPLES];
		float *planar[2] = { planarBuf[0], planarBuf[1] };
		int ch = sample->objectInfo.nChannels;
		int reqSamples = Min( MIXBUFFER_SAMPLES, totalSamples / ch );
		if ( reqSamples == 0 ) {
			readSamples += totalSamples;
			totalSamples = 0;
			break;
		}

		/*
		   Ogg assets stay at their authored rate (the sound cache resamples
		   PCM at load time but deliberately leaves Ogg encoded, since
		   resampling it there would force a full decode into memory). When the
		   output rate differs, ask the decoder for proportionally fewer source
		   frames and interpolate up, or the stream plays at the wrong pitch -
		   8.8% fast at 48kHz, 118% fast at 96kHz.
		*/
		const int srcRate = sample->objectInfo.nSamplesPerSec;
		const int dstRate = snd_SampleRate();
		int srcWanted = reqSamples;
		if ( srcRate != dstRate ) {
			srcWanted = (int)( ( (int64_t)reqSamples * srcRate ) / dstRate );
			if ( srcWanted < 1 ) {
				srcWanted = 1;
			}
		}

		size_t framesGot = 0;
		int ret = audio_transfer_read_f32( atHandle, AUDIO_TYPE_VORBIS,
				interleaved, (size_t)srcWanted, &framesGot );
		if ( ret == AUDIO_PROCESS_ERROR || ret == AUDIO_PROCESS_ERROR_END ) {
			failed = true;
			break;
		}
		if ( framesGot == 0 ) {
			// end of stream: pad the rest with silence like the stb path's
			// trailing memset does via the caller.
			break;
		}

		if ( srcRate != dstRate ) {
			/*
			   Linear interpolation, matching what the rest of this path does
			   in spirit - the SIMD upsamplers are nearest-neighbour sample
			   duplication. Held in a fixed 16.16 accumulator so the result
			   depends only on the frame index, not on any running state.
			*/
			static float resampled[MIXBUFFER_SAMPLES * 2];
			int outFrames = (int)( ( (int64_t)framesGot * dstRate ) / srcRate );
			if ( outFrames > MIXBUFFER_SAMPLES ) {
				outFrames = MIXBUFFER_SAMPLES;
			}
			for ( int i = 0; i < outFrames; i++ ) {
				int64_t pos  = ( (int64_t)i * srcRate << 16 ) / dstRate;
				int     idx  = (int)( pos >> 16 );
				float   frac = (float)( pos & 0xFFFF ) * ( 1.0f / 65536.0f );
				int     idx1 = ( idx + 1 < (int)framesGot ) ? idx + 1 : (int)framesGot - 1;
				for ( int c = 0; c < ch; c++ ) {
					float a = interleaved[idx  * ch + c];
					float b = interleaved[idx1 * ch + c];
					resampled[i * ch + c] = a + ( b - a ) * frac;
				}
			}
			memcpy( interleaved, resampled, outFrames * ch * sizeof( float ) );
			framesGot = (size_t)outFrames;
		}

		// de-interleave into the planar layout UpSampleOGGTo44kHz expects
		for ( int i = 0; i < (int)framesGot; i++ ) {
			for ( int c = 0; c < ch; c++ ) {
				planarBuf[c][i] = interleaved[i * ch + c];
			}
		}

		int gotInterleaved = (int)framesGot * ch;
		/* already at the output rate after the conversion above, so tell the
		   upsampler 44100 (its 1:1 passthrough case) rather than the asset's
		   authored rate, which would make it duplicate samples again */
		SIMDProcessor->UpSampleOGGTo44kHz( dest + ( readSamples << shift ), planar,
				gotInterleaved, ( srcRate != dstRate ) ? 44100 : sample->objectInfo.nSamplesPerSec,
				sample->objectInfo.nChannels );

		readSamples += gotInterleaved;
		totalSamples -= gotInterleaved;
	} while ( totalSamples > 0 );

	lastSampleOffset += readSamples;

	return ( readSamples << shift );
}
