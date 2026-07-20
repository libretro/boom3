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

/* see rsTail in idSampleDecoderLocal */
#define RESAMPLE_TAIL_FRAMES	4

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

	/*
	   Ogg rate-conversion state. Ogg stays encoded at its authored rate and
	   is resampled per block while streaming, so the resampler has to be a
	   STREAM too: a block that restarts its interpolation phase at zero and
	   throws away the source frames it did not consume produces a
	   discontinuity at every block boundary. Measured before this state
	   existed: a step of 8956 where the signal's largest legitimate step was
	   864, once per block - about every 33ms - which is what it sounded like.

	   rsNum is the position numerator: rsNum/dstRate is the fractional source
	   frame the next output sample sits at. Keeping it exact rather than
	   pre-computing a fixed-point step means there is no per-sample rounding
	   and therefore no drift, however long the stream runs.

	   rsTail holds the source frames read but not yet consumed, which the
	   next block needs for its first interpolations.
	*/
	int64_t					rsNum;				// 0 <= rsNum < dstRate
	int						rsTailFrames;
	int						rsRate;				// rate the state belongs to
	/*
	   The tail is what a block read but did not consume, which is at most the
	   two frames the final output sample interpolates between - measured 1 to
	   2 across every rate and block size. Sized to 4 frames rather than a mix
	   buffer: 64 decoders are pooled, so MIXBUFFER_SAMPLES here would be 2MB
	   of state to hold two frames. The copy is bounded against this.
	*/
	float					rsTail[RESAMPLE_TAIL_FRAMES * 2];
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
	rsNum = 0;
	rsTailFrames = 0;
	rsRate = 0;
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
		/*
		   Static rather than stack: these are sized for the mix cap
		   (MIXBUFFER_SAMPLES, 4096 frames - what 96kHz at the 30fps floor
		   needs), so as locals they put 64KB on the stack and DecodeOGG is
		   reached from inside AddChannelContribution, which already has 49KB
		   of its own. 115KB of stack in the mix chain is a real problem on
		   targets with small thread stacks. Safe because the decoder runs only
		   on the main thread from a single call site and is not re-entrant -
		   the resampled buffer below was already static for the same reason.
		*/
		static float interleaved[MIXBUFFER_SAMPLES * 2];
		static float planarBuf[2][MIXBUFFER_SAMPLES];
		float *planar[2] = { planarBuf[0], planarBuf[1] };
		int ch = sample->objectInfo.nChannels;
		int reqSamples = Min( MIXBUFFER_SAMPLES, totalSamples / ch );
		int gotFrames = 0;		/* frames produced, always in the OUTPUT domain */
		if ( reqSamples == 0 ) {
			readSamples += totalSamples;
			totalSamples = 0;
			break;
		}

		/*
		   Ogg assets stay at their authored rate - the sound cache resamples
		   PCM once at load time but deliberately leaves Ogg encoded, since
		   resampling it there would force a full decode into memory. When the
		   output rate differs it has to be converted here, per block, or the
		   stream plays at the wrong pitch: 8.8% fast at 48kHz, 118% at 96kHz.

		   This is a streaming resampler, not a per-block one. Position is held
		   in an exact rational accumulator (rsNum/dstRate source frames) that
		   carries across calls, and the source frames read but not consumed are
		   kept in rsTail for the next block to interpolate from. Both matter:
		   restarting the phase each block put a discontinuity at every block
		   boundary, and rounding the phase increment to fixed point drifts.
		*/
		const int srcRate = sample->objectInfo.nSamplesPerSec;
		const int dstRate = snd_SampleRate();

		if ( srcRate == dstRate ) {
			/* no conversion: read straight through */
			size_t framesGot = 0;
			int ret = audio_transfer_read_f32( atHandle, AUDIO_TYPE_VORBIS,
					interleaved, (size_t)reqSamples, &framesGot );
			if ( ret == AUDIO_PROCESS_ERROR || ret == AUDIO_PROCESS_ERROR_END ) {
				failed = true;
				break;
			}
			if ( framesGot == 0 ) {
				break;
			}
			gotFrames = (int)framesGot;
		} else {
			/* reset the stream state if the rate changed under us */
			if ( rsRate != srcRate ) {
				rsRate = srcRate;
				rsNum = 0;
				rsTailFrames = 0;
			}

			/* carry the unconsumed tail to the front of the work buffer */
			if ( rsTailFrames > 0 ) {
				memcpy( interleaved, rsTail, (size_t)rsTailFrames * ch * sizeof( float ) );
			}

			/* the last output sample reads source frames idx and idx+1 */
			int64_t lastNum = rsNum + (int64_t)srcRate * ( reqSamples - 1 );
			int needTotal = (int)( lastNum / dstRate ) + 2;
			int needNew = needTotal - rsTailFrames;
			if ( needNew < 0 ) {
				needNew = 0;
			}
			if ( rsTailFrames + needNew > MIXBUFFER_SAMPLES ) {
				needNew = MIXBUFFER_SAMPLES - rsTailFrames;
			}

			size_t framesGot = 0;
			if ( needNew > 0 ) {
				int ret = audio_transfer_read_f32( atHandle, AUDIO_TYPE_VORBIS,
						interleaved + rsTailFrames * ch, (size_t)needNew, &framesGot );
				if ( ret == AUDIO_PROCESS_ERROR || ret == AUDIO_PROCESS_ERROR_END ) {
					failed = true;
					break;
				}
			}
			int avail = rsTailFrames + (int)framesGot;
			if ( avail <= 0 ) {
				break;		/* end of stream */
			}

			static float resampled[MIXBUFFER_SAMPLES * 2];
			for ( int i = 0; i < reqSamples; i++ ) {
				int64_t pos = rsNum + (int64_t)srcRate * i;
				int     idx = (int)( pos / dstRate );
				float   frac = (float)( pos % dstRate ) / (float)dstRate;
				int     i0 = ( idx     < avail ) ? idx     : avail - 1;
				int     i1 = ( idx + 1 < avail ) ? idx + 1 : avail - 1;
				for ( int c = 0; c < ch; c++ ) {
					float a = interleaved[i0 * ch + c];
					float b = interleaved[i1 * ch + c];
					resampled[i * ch + c] = a + ( b - a ) * frac;
				}
			}

			/* advance the exact position and keep whatever was not consumed */
			rsNum += (int64_t)srcRate * reqSamples;
			int consumed = (int)( rsNum / dstRate );
			rsNum -= (int64_t)consumed * dstRate;
			rsTailFrames = avail - consumed;
			if ( rsTailFrames < 0 ) {
				rsTailFrames = 0;
			} else if ( rsTailFrames > RESAMPLE_TAIL_FRAMES ) {
				/* cannot happen for the rate pairs offered - the tail is the 1-2
				   frames the last output sample straddles - but clamp rather than
				   overrun if a future rate makes it larger */
				rsTailFrames = RESAMPLE_TAIL_FRAMES;
			}
			if ( rsTailFrames > 0 ) {
				memcpy( rsTail, interleaved + consumed * ch,
						(size_t)rsTailFrames * ch * sizeof( float ) );
			}

			memcpy( interleaved, resampled, (size_t)reqSamples * ch * sizeof( float ) );
			gotFrames = reqSamples;
		}

		// de-interleave into the planar layout UpSampleOGGTo44kHz expects
		for ( int i = 0; i < gotFrames; i++ ) {
			for ( int c = 0; c < ch; c++ ) {
				planarBuf[c][i] = interleaved[i * ch + c];
			}
		}

		int gotInterleaved = gotFrames * ch;
		/*
		   The data is already at the output rate by this point, whether it came
		   through the straight-through path or the resampler, so pass 44100 -
		   the upsampler's 1:1 passthrough case. Passing the asset's authored
		   rate would make it duplicate samples on top of the conversion.
		*/
		SIMDProcessor->UpSampleOGGTo44kHz( dest + ( readSamples << shift ), planar,
				gotInterleaved, 44100, sample->objectInfo.nChannels );

		readSamples += gotInterleaved;
		totalSamples -= gotInterleaved;
	} while ( totalSamples > 0 );

	lastSampleOffset += readSamples;

	return ( readSamples << shift );
}
