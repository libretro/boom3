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
#include <audio/audio_resampler.h>

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

		/*
		   The probe just read the whole file into oggFileData; the cache's
		   Read() wants exactly those bytes. The old path freed the buffer,
		   reopened the file and read it from the filesystem a second time,
		   which for pk4 content decompresses every OGG's zip entry twice
		   per level load - 1253 entries in the demo alone. Serve the read
		   from the copy already in memory instead; Close() owns and frees
		   it (see idWaveFile::Close).
		*/
		mpwfx.Format.wFormatTag = WAVE_FORMAT_TAG_OGG;
		mpbData = (short *)oggFileData;
		mpbDataCur = mpbData;
		mulDataSize = fileSize;
		mbIsReadingFromMemory = true;
		mMemSize = fileSize;
	}

	memcpy( pwfx, &mpwfx, sizeof( waveformatex_t ) );

	Sys_LeaveCriticalSection( CRITICAL_SECTION_ONE );

	isOgg = true;

	return 0;
}

/*
===================================================================================

  idSampleDecoderLocal

===================================================================================
*/

/* surplus output frames held between blocks; see rsCarry below */
#define RESAMPLE_CARRY_FRAMES	64

class idSampleDecoderLocal : public idSampleDecoder {
public:
	virtual void			Decode( idSoundSample *sample, int outputOffset, int outputCount, float *dest );
	virtual void			ClearDecoder( void );
	virtual idSoundSample *	GetSample( void ) const;
	virtual void			WriteStreamState( idFile *f );
	virtual void			ArmPendingStreamState( idFile *f );
	void					FreePendingStreamState( void );
	void					InitPendingStreamState( void ) { pendSinc = NULL; pendSincSize = 0; pendCarryFrames = 0; }
	virtual int				GetLastDecodeTime( void ) const;

	void					Clear( void );
	int						DecodePCM( idSoundSample *sample, int outputOffset, int outputCount, float *dest );
	int						DecodeOGG( idSoundSample *sample, int outputOffset, int outputCount, float *dest );

private:
	bool					failed;				// set if decoding failed
	int						lastFormat;			// last format being decoded
	idSoundSample *			lastSample;			// last sample being decoded
	int						lastSampleOffset;	// last offset into the decoded sample
	int						lastDecodeTime;		// last time decoding sound

	void *					atHandle;			// libretro-common audio_transfer OGG handle

	/*
	   Ogg rate-conversion state.

	   Ogg stays encoded at its authored rate - the sound cache resamples PCM
	   once at load but leaves Ogg alone, since resampling it there would force
	   a full decode into memory - so it has to be converted per block while
	   streaming. That means the resampler must itself be a stream: an earlier
	   per-block one that restarted its phase and discarded the source frames
	   it had not consumed put a discontinuity at every block boundary,
	   measured as a step of 8956 against a largest legitimate step of 864,
	   once per retro_run.

	   This is libretro-common's sinc resampler, which holds that state
	   internally. It was already in the tree and built, just unused.
	*/
	void *					rsHandle;			// sinc resampler, NULL until needed
	int						rsSrcRate;			// rate the handle was built for
	int						rsDstRate;
	/*
	   The resampler returns a few more frames than asked for - measured 2 to
	   5 per block at 44.1->96kHz, because the exact output count for a given
	   input depends on its internal phase. Discarding them would drop 2-5
	   samples of audio per block, which is a discontinuity every retro_run
	   and audible as scratching. They are held here and emitted first next
	   time instead.
	*/
	float					rsCarry[RESAMPLE_CARRY_FRAMES * 2];
	// pending savestate stream state, applied after the post-restore seek
	byte *					pendSinc;
	int						pendSincSize;
	int						pendSrcRate, pendDstRate;
	int						pendCarryFrames;
	float					pendCarry[RESAMPLE_CARRY_FRAMES * 2];
	int						rsCarryFrames;
};

idBlockAlloc<idSampleDecoderLocal, 64>		sampleDecoderAllocator;

/*
   Sinc resampler handle pool. Building the filter table costs ~3 ms at
   the quality in use (multiples of that on weak FPUs) and used to happen
   inside MixFrame at the first decode of every streaming sound whose
   rate differs from the output - with 22 kHz assets that is nearly every
   stream start, a recurring mid-play frame-time spike. Retired handles
   park here instead of being freed; adoption resets the stream state to
   the exact post-init state (see rarch_sinc_resampler_reset_state), so a
   pooled handle is bit-identical to a fresh one and the output is
   independent of pool history. All use is on the mixer thread.
*/
extern "C" void rarch_sinc_resampler_reset_state(void *re_);
#define RS_POOL_SIZE 8
static struct { void *handle; int srcRate, dstRate; } rsPool[RS_POOL_SIZE];

static void *RsPool_Acquire( int srcRate, int dstRate ) {
	for ( int i = 0; i < RS_POOL_SIZE; i++ ) {
		if ( rsPool[i].handle && rsPool[i].srcRate == srcRate && rsPool[i].dstRate == dstRate ) {
			void *h = rsPool[i].handle;
			rsPool[i].handle = NULL;
			rarch_sinc_resampler_reset_state( h );
			return h;
		}
	}
	return NULL;
}

static void RsPool_Retire( void *handle, int srcRate, int dstRate ) {
	for ( int i = 0; i < RS_POOL_SIZE; i++ ) {
		if ( rsPool[i].handle == NULL ) {
			rsPool[i].handle = handle;
			rsPool[i].srcRate = srcRate;
			rsPool[i].dstRate = dstRate;
			return;
		}
	}
	sinc_resampler.free( handle );	// pool full: oldest policy not needed at this size
}

static void RsPool_Shutdown( void ) {
	for ( int i = 0; i < RS_POOL_SIZE; i++ ) {
		if ( rsPool[i].handle ) {
			sinc_resampler.free( rsPool[i].handle );
			rsPool[i].handle = NULL;
		}
	}
}

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
	RsPool_Shutdown();
	sampleDecoderAllocator.Shutdown();
}

/*
====================
idSampleDecoder::Alloc
====================
*/
idSampleDecoder *idSampleDecoder::Alloc( void ) {
	idSampleDecoderLocal *decoder = sampleDecoderAllocator.Alloc();
	decoder->InitPendingStreamState();	// fresh slot: pointer is garbage
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
	localDecoder->FreePendingStreamState();
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
	/* NOTE: the pending savestate stream state is deliberately NOT
	   touched here: ClearDecoder ends in Clear(), and the first
	   post-restore decode clears to establish its format - wiping the
	   armed state there was exactly the bug. Fresh-slot initialization
	   happens once in idSampleDecoder::Alloc. */
	failed = false;
	lastFormat = WAVE_FORMAT_TAG_PCM;
	lastSample = NULL;
	lastSampleOffset = 0;
	lastDecodeTime = 0;
	atHandle = NULL;
	rsHandle = NULL;
	rsSrcRate = 0;
	rsDstRate = 0;
	rsCarryFrames = 0;
}

/*
====================
idSampleDecoderLocal::ClearDecoder
====================
*/
void idSampleDecoderLocal::ClearDecoder( void ) {
	/*
	   NOTE: the armed savestate stream state (pendSinc) deliberately
	   survives this: the first post-restore Decode clears to establish
	   the format before the pending state can apply. It is freed on
	   decoder retirement (idSampleDecoder::Free) or on re-arm.
	*/
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
			if ( rsHandle != NULL ) {
				RsPool_Retire( rsHandle, rsSrcRate, rsDstRate );
				rsHandle = NULL;
				rsSrcRate = 0;
				rsDstRate = 0;
			}
			rsCarryFrames = 0;
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
/*
====================
idSampleDecoderLocal::WriteStreamState / ArmPendingStreamState

Savestate stream continuity. The mutable stream state that the natural
post-restore reopen+seek cannot reproduce is the resampler's ring and
fractional-time phase plus the carried-over output frames: a fresh
resampler starts its output grid at phase zero for an arbitrary offset,
permanently shifting the resampled voice's subsample alignment. Writing
that state and re-applying it right after the seek's own reset makes the
restored stream continue bit-exactly. The vorbis side needs nothing: the
seek is sample-exact, so the PCM feed equals the continuous timeline's.
====================
*/
extern "C" size_t rarch_sinc_resampler_state_size( void *re_ );
extern "C" void rarch_sinc_resampler_get_state( void *re_, void *out );
extern "C" void rarch_sinc_resampler_set_state( void *re_, const void *in );

void idSampleDecoderLocal::WriteStreamState( idFile *f ) {
	int has = ( rsHandle != NULL ) ? 1 : 0;
	f->WriteInt( has );
	if ( !has ) {
		return;
	}
	f->WriteInt( rsSrcRate );
	f->WriteInt( rsDstRate );
	int sz = (int)rarch_sinc_resampler_state_size( rsHandle );
	f->WriteInt( sz );
	byte *tmp = (byte *)_alloca( sz );
	rarch_sinc_resampler_get_state( rsHandle, tmp );
	f->Write( tmp, sz );
	f->WriteInt( rsCarryFrames );
	if ( rsCarryFrames > 0 ) {
		f->Write( rsCarry, rsCarryFrames * 2 * sizeof( float ) );
	}
}

void idSampleDecoderLocal::FreePendingStreamState( void ) {
	if ( pendSinc != NULL ) {
		Mem_Free( pendSinc );
		pendSinc = NULL;
	}
	pendSincSize = 0;
	pendCarryFrames = 0;
}

void idSampleDecoderLocal::ArmPendingStreamState( idFile *f ) {
	FreePendingStreamState();
	int has = 0;
	f->ReadInt( has );
	if ( !has ) {
		return;
	}
	f->ReadInt( pendSrcRate );
	f->ReadInt( pendDstRate );
	int sz = 0;
	f->ReadInt( sz );
	if ( sz <= 0 || sz > 1 << 20 ) {
		return;
	}
	pendSinc = (byte *)Mem_Alloc( sz );
	pendSincSize = sz;
	f->Read( pendSinc, sz );
	int cf = 0;
	f->ReadInt( cf );
	if ( cf > 0 && cf <= RESAMPLE_CARRY_FRAMES ) {
		f->Read( pendCarry, cf * 2 * sizeof( float ) );
		pendCarryFrames = cf;
	}
}

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
void idSampleDecoderLocal::Decode( idSoundSample *sample, int outputOffset, int outputCount, float *dest ) {
	int readSamplesOut;

	if ( sample->objectInfo.wFormatTag != lastFormat || sample != lastSample ) {
		ClearDecoder();
	}

	lastDecodeTime = soundSystemLocal.CurrentSoundTime;

	if ( failed ) {
		memset( dest, 0, outputCount * sizeof( dest[0] ) );
		return;
	}

	// samples can be decoded both from the sound thread and the main thread for shakes
	Sys_EnterCriticalSection( CRITICAL_SECTION_ONE );

	switch( sample->objectInfo.wFormatTag ) {
		case WAVE_FORMAT_TAG_PCM: {
			readSamplesOut = DecodePCM( sample, outputOffset, outputCount, dest );
			break;
		}
		case WAVE_FORMAT_TAG_OGG: {
			readSamplesOut = DecodeOGG( sample, outputOffset, outputCount, dest );
			break;
		}
		default: {
			readSamplesOut = 0;
			break;
		}
	}

	Sys_LeaveCriticalSection( CRITICAL_SECTION_ONE );

	if ( readSamplesOut < outputCount )
		memset( dest + readSamplesOut, 0, ( outputCount - readSamplesOut ) * sizeof( dest[0] ) );
}

/*
====================
idSampleDecoderLocal::DecodePCM
====================
*/
int idSampleDecoderLocal::DecodePCM( idSoundSample *sample, int outputOffset, int outputCount, float *dest ) {
	const byte *first;
	int pos, size, readSamples;

	lastFormat = WAVE_FORMAT_TAG_PCM;
	lastSample = sample;

	/* source-domain view of the request. shift is 0 in the common case
	   (the sample was resampled to the output rate at load); the nonzero
	   values only serve the allocation-failure fallback that kept the
	   asset at its authored 11025/22050 rate. */
	int shift = 22050 / sample->objectInfo.nSamplesPerSec;
	int srcOffset = outputOffset >> shift;
	int srcCount = outputCount >> shift;

	if ( sample->nonCacheData == NULL ) {
		//assert( false );	// this should never happen ( note: I've seen that happen with the main thread down in idGameLocal::MapClear clearing entities - TTimo )
		// DG: see comment in DecodeOGG()
		common->Warning( "Called idSampleDecoderLocal::DecodePCM() on idSoundSample '%s' without nonCacheData\n", sample->name.c_str() );
		failed = true;
		return 0;
	}

	if ( !sample->FetchFromCache( srcOffset * sizeof( short ), &first, &pos, &size, false ) ) {
		failed = true;
		return 0;
	}

	if ( size - pos < srcCount * sizeof( short ) ) {
		readSamples = ( size - pos ) / sizeof( short );
	} else {
		readSamples = srcCount;
	}

	/*
	   PCM is resampled to the output rate when it is loaded, so the common
	   case here is nSamplesPerSec == snd_SampleRate() and the upsampler's
	   1:1 passthrough - which its implementations spell "44100" (the same
	   convention DecodeOGG uses; passing 48000/96000 lands in a dead
	   assert(0) branch that writes nothing, which played every .wav as
	   stale buffer garbage at those rates). The authored rate is only
	   passed through when the load-time resample could not run (allocation
	   failure keeps the native-rate data): at 44.1kHz output the historical
	   duplication is exact, at other rates it is a wrong-pitch fallback -
	   degraded, but it plays.
	*/
	SIMDProcessor->UpSamplePCMToOutput( dest, (const short *)(first+pos), readSamples,
			sample->objectInfo.nSamplesPerSec == snd_SampleRate() ? 44100 : sample->objectInfo.nSamplesPerSec,
			sample->objectInfo.nChannels );

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
UpSampleOGGToOutput to 44.1kHz.
====================
*/
int idSampleDecoderLocal::DecodeOGG( idSoundSample *sample, int outputOffset, int outputCount, float *dest ) {
	int readSamples, totalSamples;

	/*
	   Everything in this function is in OUTPUT samples (the mixer's clock
	   domain). The old pipeline counted in SOURCE samples and expanded by
	   << shift at the end because UpSampleOGGToOutput did the 11/22kHz ->
	   44.1kHz duplication. The streaming resampler below replaced that
	   duplication - it already produces output-rate frames - so the shift
	   accounting on top of it double-converted: a 22050 mono asset had its
	   request halved (>> shift), the resampler filled that half with
	   output-rate frames, and the << shift on the return claimed the full
	   block was written. Result: the second half of every mix block stayed
	   silent for every sub-44.1kHz Ogg (97% of the retail assets), which is
	   the block-cadence stutter this replaces. The only domain conversion
	   that survives is the seek below, where the file is addressed in
	   source frames.
	*/

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

	   outputOffset is in OUTPUT samples, but audio_transfer_seek() addresses
	   the file in SOURCE frames. Those are the same thing only when the asset
	   rate matches the output rate. When they differ - Ogg is left encoded at
	   its authored rate and resampled per block below - the offset has to be
	   scaled, or every seek lands the wrong distance into the stream: at
	   96kHz output with a 44.1kHz asset it overshoots by 2.18x, which is
	   audible as the stream constantly jumping forward.
	*/
	if ( outputOffset != lastSampleOffset ) {
		uint64_t srcFrame = (uint64_t)( outputOffset / sample->objectInfo.nChannels );
		if ( sample->objectInfo.nSamplesPerSec != snd_SampleRate() ) {
			srcFrame = ( srcFrame * (uint64_t)sample->objectInfo.nSamplesPerSec )
					/ (uint64_t)snd_SampleRate();
		}
		if ( !audio_transfer_seek( atHandle, AUDIO_TYPE_VORBIS, srcFrame ) ) {
			common->Warning( "idSampleDecoderLocal::DecodeOGG() seek(%d) for %s failed\n",
					outputOffset / sample->objectInfo.nChannels, sample->name.c_str() );
			failed = true;
			return 0;
		}
		/*
		   The resampler's filter history and any carried-over output frames
		   belong to the pre-seek position in the stream; blending them into
		   post-seek audio puts a burst of the old position at the new one
		   (audible when a looping sound wraps). Retiring the handle to the
		   pool and re-acquiring it below gets a clean-state handle for the
		   same rate pair at memset cost (the pool's adoption reset), or a
		   fresh init on a pool miss.
		*/
		rsCarryFrames = 0;
		if ( rsHandle != NULL ) {
			RsPool_Retire( rsHandle, rsSrcRate, rsDstRate );
			rsHandle = NULL;
			rsSrcRate = 0;
			rsDstRate = 0;
		}
	}

	lastSampleOffset = outputOffset;

	// decode
	totalSamples = outputCount;
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
			/*
			   Rebuild the resampler if the rate pair changed. Doing it lazily
			   keeps decoders that never touch a mismatched Ogg from paying for
			   the filter tables.
			*/
			if ( rsHandle == NULL || rsSrcRate != srcRate || rsDstRate != dstRate ) {
				if ( rsHandle != NULL ) {
					RsPool_Retire( rsHandle, rsSrcRate, rsDstRate );
					rsHandle = NULL;
				}
				rsHandle = RsPool_Acquire( srcRate, dstRate );
				if ( rsHandle == NULL )
				rsHandle = sinc_resampler.init( NULL, (double)dstRate / (double)srcRate,
						RESAMPLER_QUALITY_HIGHER, 0 );
				if ( rsHandle == NULL ) {
					common->Warning( "idSampleDecoderLocal: could not create resampler for %s\n",
							sample->name.c_str() );
					failed = true;
					break;
				}
				rsSrcRate = srcRate;
				rsDstRate = dstRate;
			}
			/*
			   Savestate stream continuity: a restore armed the saved
			   resampler state; apply it once the handle for the same
			   rate pair exists (right here, after the post-restore
			   seek's own reset), then the stream continues bit-exactly.
			*/
			if ( pendSinc != NULL && pendSrcRate == srcRate && pendDstRate == dstRate
					&& (int)rarch_sinc_resampler_state_size( rsHandle ) == pendSincSize ) {
				rarch_sinc_resampler_set_state( rsHandle, pendSinc );
				if ( pendCarryFrames > 0 ) {
					memcpy( rsCarry, pendCarry, pendCarryFrames * 2 * sizeof( float ) );
					rsCarryFrames = pendCarryFrames;
				}
				Mem_Free( pendSinc );
				pendSinc = NULL;
				pendSincSize = 0;
				pendCarryFrames = 0;
			}

			/*
			   Fill reqSamples output frames, never discarding any the resampler
			   produces. It returns a few more than the ratio implies - measured
			   2 to 5 per block - because the exact count depends on its internal
			   phase; those go into rsCarry and come out first next time. An
			   earlier version clamped and dropped them, which put a
			   discontinuity in the stream once per block.
			*/
			static float rsIn[MIXBUFFER_SAMPLES * 2];
			static float rsOut[( MIXBUFFER_SAMPLES + RESAMPLE_CARRY_FRAMES ) * 2];
			int filled = 0;

			if ( rsCarryFrames > 0 ) {
				int take = ( rsCarryFrames < reqSamples ) ? rsCarryFrames : reqSamples;
				for ( int i = 0; i < take; i++ ) {
					for ( int c = 0; c < ch; c++ ) {
						interleaved[i * ch + c] = rsCarry[i * 2 + c];
					}
				}
				filled = take;
				rsCarryFrames -= take;
				if ( rsCarryFrames > 0 ) {
					memmove( rsCarry, rsCarry + take * 2,
							(size_t)rsCarryFrames * 2 * sizeof( float ) );
				}
			}

			bool streamEnded = false;
			while ( filled < reqSamples ) {
				int still = reqSamples - filled;
				int srcWanted = (int)( ( (int64_t)still * srcRate ) / dstRate ) + 1;
				if ( srcWanted > MIXBUFFER_SAMPLES ) {
					srcWanted = MIXBUFFER_SAMPLES;
				}

				size_t framesGot = 0;
				int ret = audio_transfer_read_f32( atHandle, AUDIO_TYPE_VORBIS,
						rsIn, (size_t)srcWanted, &framesGot );
				if ( ret == AUDIO_PROCESS_ERROR || ret == AUDIO_PROCESS_ERROR_END ) {
					failed = true;
					streamEnded = true;
					break;
				}
				if ( framesGot == 0 ) {
					streamEnded = true;
					break;
				}

				/* the resampler works on interleaved stereo; widen mono */
				if ( ch == 1 ) {
					for ( int i = (int)framesGot - 1; i >= 0; i-- ) {
						float v = rsIn[i];
						rsIn[i*2+0] = v;
						rsIn[i*2+1] = v;
					}
				}

				struct resampler_data rd;
				memset( &rd, 0, sizeof( rd ) );
				rd.data_in      = rsIn;
				rd.data_out     = rsOut;
				rd.input_frames = framesGot;
				rd.ratio        = (double)dstRate / (double)srcRate;
				sinc_resampler.process( rsHandle, &rd );

				int got = (int)rd.output_frames;
				if ( got <= 0 ) {
					streamEnded = true;
					break;
				}

				int take = ( got < still ) ? got : still;
				for ( int i = 0; i < take; i++ ) {
					for ( int c = 0; c < ch; c++ ) {
						interleaved[( filled + i ) * ch + c] = rsOut[i * 2 + c];
					}
				}
				filled += take;

				int surplus = got - take;
				if ( surplus > 0 ) {
					if ( surplus > RESAMPLE_CARRY_FRAMES - rsCarryFrames ) {
						surplus = RESAMPLE_CARRY_FRAMES - rsCarryFrames;
					}
					if ( surplus > 0 ) {
						memcpy( rsCarry + rsCarryFrames * 2, rsOut + take * 2,
								(size_t)surplus * 2 * sizeof( float ) );
						rsCarryFrames += surplus;
					}
				}
			}

			if ( filled == 0 ) {
				break;		/* nothing left in the stream */
			}
			(void)streamEnded;
			gotFrames = filled;
		}

		// de-interleave into the planar layout UpSampleOGGToOutput expects
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
		SIMDProcessor->UpSampleOGGToOutput( dest + readSamples, planar,
				gotInterleaved, 44100, sample->objectInfo.nChannels );

		readSamples += gotInterleaved;
		totalSamples -= gotInterleaved;
	} while ( totalSamples > 0 );

	lastSampleOffset += readSamples;

	return readSamples;
}
