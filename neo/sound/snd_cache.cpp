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

// libretro-common integer sinc resampler: used to resample PCM samples to
// 44.1kHz once at load time (instead of the per-block duplicating upsampler
// in the mixer). Include before snd_local.h / idlib string macros.
#include <audio/sinc_resampler_int16.h>
#include <formats/audio.h>

#include "sound/snd_local.h"

#define USE_SOUND_CACHE_ALLOCATOR

#ifdef USE_SOUND_CACHE_ALLOCATOR
static idDynamicBlockAlloc<byte, 1<<20, 1<<10>	soundCacheAllocator;
#else
static idDynamicAlloc<byte, 1<<20, 1<<10>		soundCacheAllocator;
#endif

/*
===================
idSoundCache::idSoundCache()
===================
*/
idSoundCache::idSoundCache() {
	soundCacheAllocator.Init();
	soundCacheAllocator.SetLockMemory( true );
	listCache.AssureSize( 1024, NULL );
	listCache.SetGranularity( 256 );
	insideLevelLoad = false;
}

/*
===================
idSoundCache::~idSoundCache()
===================
*/
idSoundCache::~idSoundCache() {
	listCache.DeleteContents( true );
	soundCacheAllocator.Shutdown();
}

/*
===================
idSoundCache::::GetObject

returns a single cached object pointer
===================
*/
const idSoundSample* idSoundCache::GetObject( const int index ) const {
	if (index<0 || index>listCache.Num()) {
		return NULL;
	}
	return listCache[index];
}

/*
===================
idSoundCache::FindSound

Adds a sound object to the cache and returns a handle for it.
===================
*/
idSoundSample *idSoundCache::FindSound( const idStr& filename, bool loadOnDemandOnly ) {
	idStr fname;

	fname = filename;
	fname.BackSlashesToSlashes();
	fname.ToLower();

	declManager->MediaPrint( "%s\n", fname.c_str() );

	// check to see if object is already in cache
	for( int i = 0; i < listCache.Num(); i++ ) {
		idSoundSample *def = listCache[i];
		if ( def && def->name == fname ) {
			def->levelLoadReferenced = true;
			if ( def->purged && !loadOnDemandOnly ) {
				def->Load();
			}
			return def;
		}
	}

	// create a new entry
	idSoundSample *def = new idSoundSample;

	int shandle = listCache.FindNull();
	if ( shandle != -1 ) {
		listCache[shandle] = def;
	} else {
		shandle = listCache.Append( def );
	}

	def->name = fname;
	def->levelLoadReferenced = true;
	def->onDemand = loadOnDemandOnly;
	def->purged = true;

	if ( !loadOnDemandOnly ) {
		// this may make it a default sound if it can't be loaded
		def->Load();
	}

	return def;
}

/*
===================
idSoundCache::ReloadSounds

Completely nukes the current cache
===================
*/
void idSoundCache::ReloadSounds( bool force ) {
	int i;

	for( i = 0; i < listCache.Num(); i++ ) {
		idSoundSample *def = listCache[i];
		if ( def ) {
			def->Reload( force );
		}
	}
}

/*
====================
BeginLevelLoad

Mark all file based images as currently unused,
but don't free anything.  Calls to ImageFromFile() will
either mark the image as used, or create a new image without
loading the actual data.
====================
*/
void idSoundCache::BeginLevelLoad() {
	insideLevelLoad = true;

	for ( int i = 0 ; i < listCache.Num() ; i++ ) {
		idSoundSample *sample = listCache[ i ];
		if ( !sample ) {
			continue;
		}

		if ( com_purgeAll.GetBool() ) {
			sample->PurgeSoundSample();
		}

		sample->levelLoadReferenced = false;
	}

	soundCacheAllocator.FreeEmptyBaseBlocks();
}

/*
====================
EndLevelLoad

Free all samples marked as unused
====================
*/
void idSoundCache::EndLevelLoad() {
	int	useCount, purgeCount;
	common->Printf( "----- idSoundCache::EndLevelLoad -----\n" );

	insideLevelLoad = false;

	// purge the ones we don't need
	useCount = 0;
	purgeCount = 0;
	for ( int i = 0 ; i < listCache.Num() ; i++ ) {
		idSoundSample	*sample = listCache[ i ];
		if ( !sample ) {
			continue;
		}
		if ( sample->purged ) {
			continue;
		}
		if ( !sample->levelLoadReferenced ) {
//			common->Printf( "Purging %s\n", sample->name.c_str() );
			purgeCount += sample->objectMemSize;
			sample->PurgeSoundSample();
		} else {
			useCount += sample->objectMemSize;
		}
	}

	soundCacheAllocator.FreeEmptyBaseBlocks();

	common->Printf( "%5ik referenced\n", useCount / 1024 );
	common->Printf( "%5ik purged\n", purgeCount / 1024 );
}

/*
===================
idSoundCache::PrintMemInfo
===================
*/
void idSoundCache::PrintMemInfo( MemInfo_t *mi ) {
	int i, j, num = 0, total = 0;
	int *sortIndex;
	idFile *f;

	f = fileSystem->OpenFileWrite( mi->filebase + "_sounds.txt" );
	if ( !f ) {
		return;
	}

	// count
	for ( i = 0; i < listCache.Num(); i++, num++ ) {
		if ( !listCache[i] ) {
			break;
		}
	}

	// sort first
	sortIndex = new int[num];

	for ( i = 0; i < num; i++ ) {
		sortIndex[i] = i;
	}

	for ( i = 0; i < num - 1; i++ ) {
		for ( j = i + 1; j < num; j++ ) {
			if ( listCache[sortIndex[i]]->objectMemSize < listCache[sortIndex[j]]->objectMemSize ) {
				int temp = sortIndex[i];
				sortIndex[i] = sortIndex[j];
				sortIndex[j] = temp;
			}
		}
	}

	// print next
	for ( i = 0; i < num; i++ ) {
		idSoundSample *sample = listCache[sortIndex[i]];

		// this is strange
		if ( !sample ) {
			continue;
		}

		total += sample->objectMemSize;
		f->Printf( "%s %s\n", idStr::FormatNumber( sample->objectMemSize ).c_str(), sample->name.c_str() );
	}

	mi->soundAssetsTotal = total;

	f->Printf( "\nTotal sound bytes allocated: %s\n", idStr::FormatNumber( total ).c_str() );
	fileSystem->CloseFile( f );
	delete[] sortIndex;
}


/*
==========================================================================

idSoundSample

==========================================================================
*/

/*
===================
idSoundSample::idSoundSample
===================
*/
idSoundSample::idSoundSample() {
	memset( &objectInfo, 0, sizeof(waveformatex_t) );
	objectSize = 0;
	objectMemSize = 0;
	nonCacheData = NULL;
	amplitudeData = NULL;
	defaultSound = false;
	onDemand = false;
	purged = false;
	levelLoadReferenced = false;
}

/*
===================
idSoundSample::~idSoundSample
===================
*/
idSoundSample::~idSoundSample() {
	PurgeSoundSample();
}

/*
===================
idSoundSample::LengthIn44kHzSamples
===================
*/
int idSoundSample::LengthIn44kHzSamples( void ) const {
	// objectSize is samples
	if ( objectInfo.nSamplesPerSec == 11025 ) {
		return objectSize << 2;
	} else if ( objectInfo.nSamplesPerSec == 22050 ) {
		return objectSize << 1;
	} else {
		return objectSize << 0;
	}
}

/*
===================
idSoundSample::MakeDefault
===================
*/
void idSoundSample::MakeDefault( void ) {
	int		i;
	float	v;
	int		sample;

	memset( &objectInfo, 0, sizeof( objectInfo ) );

	objectInfo.nChannels = 1;
	objectInfo.wBitsPerSample = 16;
	objectInfo.nSamplesPerSec = 44100;

	objectSize = MIXBUFFER_SAMPLES * 2;
	objectMemSize = objectSize * sizeof( short );

	nonCacheData = (byte *)soundCacheAllocator.Alloc( objectMemSize );

	short *ncd = (short *)nonCacheData;

	for ( i = 0; i < MIXBUFFER_SAMPLES; i ++ ) {
		v = sin( idMath::PI * 2 * i / 64 );
		sample = v * 0x4000;
		ncd[i*2+0] = sample;
		ncd[i*2+1] = sample;
	}

	defaultSound = true;
}

/*
===================
idSoundSample::GetNewTimeStamp
===================
*/
ID_TIME_T idSoundSample::GetNewTimeStamp( void ) const {
	ID_TIME_T timestamp;

	fileSystem->ReadFile( name, NULL, &timestamp );
	if ( timestamp == FILE_NOT_FOUND_TIMESTAMP ) {
		idStr oggName = name;
		oggName.SetFileExtension( ".ogg" );
		fileSystem->ReadFile( oggName, NULL, &timestamp );
	}
	return timestamp;
}

// Resample PCM sample data to 44.1kHz once, at load time, using the
// libretro-common integer sinc resampler. Doing it here on the whole,
// contiguous sample (rather than per-block in the mixer) means the sinc
// filter has correct context across the entire sample and the mixer's
// per-block path becomes a trivial 1:1 copy. The int16 sinc is documented
// as deterministic and bit-identical across compilers/architectures, so
// this stays byte-reproducible. Default off (cvar) until proven out.
idCVar s_resampleOnLoad( "s_resampleOnLoad", "0", CVAR_SOUND | CVAR_BOOL | CVAR_ARCHIVE,
	"resample PCM sounds to 44kHz once at load with a sinc filter (vs per-block duplication)" );

/*
===================
ResamplePCMTo44k

Resamples interleaved 16-bit PCM from srcRate to 44100 Hz. Returns a newly
soundCacheAllocator'd buffer and writes the output sample-count (per channel)
to *outFrames. Returns NULL on failure (caller keeps the native-rate data).
===================
*/
static short *ResamplePCMTo44k( const short *src, int srcFrames, int channels, int srcRate, int *outFrames ) {
	if ( srcRate >= 44100 || srcFrames <= 0 ) {
		return NULL;	// nothing to do
	}

	double ratio = 44100.0 / (double)srcRate;

	// the int16 sinc resampler works on interleaved stereo; feed it 2ch.
	// For mono we duplicate to stereo, resample, then take one channel back.
	const short *in = src;
	short *tmpStereo = NULL;
	if ( channels == 1 ) {
		tmpStereo = (short *)Mem_Alloc( srcFrames * 2 * sizeof( short ) );
		for ( int i = 0; i < srcFrames; i++ ) {
			tmpStereo[i*2+0] = tmpStereo[i*2+1] = src[i];
		}
		in = tmpStereo;
	}

	int outCap = (int)( srcFrames * ratio ) + 64;
	short *outStereo = (short *)Mem_Alloc( outCap * 2 * sizeof( short ) );

	void *re = sinc_resampler_int16_init( 1.0, SINC_INT16_QUALITY_HIGHER );
	if ( !re ) {
		Mem_Free( outStereo );
		if ( tmpStereo ) {
			Mem_Free( tmpStereo );
		}
		return NULL;
	}

	struct resampler_data_int16 d;
	memset( &d, 0, sizeof( d ) );
	d.data_in = in;
	d.data_out = outStereo;
	d.input_frames = srcFrames;
	d.ratio = ratio;
	sinc_resampler_int16_process( re, &d );
	sinc_resampler_int16_free( re );

	int producedFrames = (int)d.output_frames;

	// pack into a cache buffer at the sample's channel count
	short *result = (short *)soundCacheAllocator.Alloc( producedFrames * channels * sizeof( short ) );
	if ( channels == 1 ) {
		for ( int i = 0; i < producedFrames; i++ ) {
			result[i] = outStereo[i*2+0];
		}
	} else {
		memcpy( result, outStereo, producedFrames * 2 * sizeof( short ) );
	}

	Mem_Free( outStereo );
	if ( tmpStereo ) {
		Mem_Free( tmpStereo );
	}

	*outFrames = producedFrames;
	return result;
}

// Load a WAV via libretro-common audio_transfer instead of idWaveFile. Reads
// the whole file, decodes straight to int16 with audio_transfer_read_s16
// (the cache is int16, so this is the no-round-trip target format), and
// fills *info + *outData (soundCacheAllocator'd) + *outBytes. Returns true on
// success. Gated by s_useAudioTransfer; falls back to idWaveFile otherwise.
idCVar s_useAudioTransfer( "s_useAudioTransfer", "0", CVAR_SOUND | CVAR_BOOL | CVAR_ARCHIVE,
	"decode WAV sounds via libretro-common audio_transfer instead of the built-in loader" );

static bool LoadWAV_transfer( const char *name, waveformatex_t *info, byte **outData, int *outBytes, int *outSamples ) {
	void *fileBuf = NULL;
	int fileLen = fileSystem->ReadFile( name, &fileBuf, NULL );
	if ( fileLen <= 0 || fileBuf == NULL ) {
		if ( fileBuf ) {
			fileSystem->FreeFile( fileBuf );
		}
		return false;
	}

	void *at = audio_transfer_new( AUDIO_TYPE_WAV );
	if ( !at ) {
		fileSystem->FreeFile( fileBuf );
		return false;
	}

	audio_transfer_set_buffer_ptr( at, AUDIO_TYPE_WAV, fileBuf, (size_t)fileLen );

	if ( !audio_transfer_start( at, AUDIO_TYPE_WAV ) || !audio_transfer_is_valid( at, AUDIO_TYPE_WAV ) ) {
		audio_transfer_free( at, AUDIO_TYPE_WAV );
		fileSystem->FreeFile( fileBuf );
		return false;
	}

	unsigned channels = 0, rate = 0;
	uint64_t totalFrames = 0;
	if ( !audio_transfer_info( at, AUDIO_TYPE_WAV, &channels, &rate, &totalFrames )
			|| channels < 1 || channels > 2 || totalFrames == 0 ) {
		audio_transfer_free( at, AUDIO_TYPE_WAV );
		fileSystem->FreeFile( fileBuf );
		return false;
	}

	int totalSamples = (int)( totalFrames * channels );
	short *pcm = (short *)soundCacheAllocator.Alloc( totalSamples * sizeof( short ) );

	// pull the whole stream out in bounded chunks (read_s16 is the caller-paced
	// analogue of image_transfer_process; here we drain it to completion at
	// load time). frames are per-channel.
	size_t framesLeft = (size_t)totalFrames;
	short *dst = pcm;
	while ( framesLeft > 0 ) {
		size_t got = 0;
		int ret = audio_transfer_read_s16( at, AUDIO_TYPE_WAV, dst, framesLeft, &got );
		if ( got > 0 ) {
			dst += got * channels;
			framesLeft -= ( got <= framesLeft ) ? got : framesLeft;
		}
		if ( ret == AUDIO_PROCESS_END || ret == AUDIO_PROCESS_ERROR || ret == AUDIO_PROCESS_ERROR_END || got == 0 ) {
			break;
		}
	}

	audio_transfer_free( at, AUDIO_TYPE_WAV );
	fileSystem->FreeFile( fileBuf );

	// fill the format info the way idWaveFile::Open would have
	memset( info, 0, sizeof( *info ) );
	info->wFormatTag = WAVE_FORMAT_TAG_PCM;
	info->nChannels = (word)channels;
	info->nSamplesPerSec = rate;
	info->wBitsPerSample = 16;
	info->nBlockAlign = (word)( channels * sizeof( short ) );
	info->nAvgBytesPerSec = rate * channels * sizeof( short );

	*outData = (byte *)pcm;
	*outBytes = totalSamples * sizeof( short );
	*outSamples = totalSamples;
	return true;
}

/*
===================
idSoundSample::Load

Loads based on name, possibly doing a MakeDefault if necessary
===================
*/
void idSoundSample::Load( void ) {
	defaultSound = false;
	purged = false;

	timestamp = GetNewTimeStamp();

	if ( timestamp == FILE_NOT_FOUND_TIMESTAMP ) {
		common->Warning( "Couldn't load sound '%s' using default", name.c_str() );
		MakeDefault();
		return;
	}

	// load it
	idWaveFile	fh;
	waveformatex_t info;

	// audio_transfer WAV path: decode straight to int16 into the cache. Only
	// for .wav (OGG stays on the streaming idWaveFile/decoder path). If it
	// fails for any reason we fall through to the built-in loader below.
	idStr ext;
	idStr( name ).ExtractFileExtension( ext );
	if ( s_useAudioTransfer.GetBool() && ext.Icmp( "wav" ) == 0 ) {
		byte *atData = NULL;
		int atBytes = 0, atSamples = 0;
		if ( LoadWAV_transfer( name, &info, &atData, &atBytes, &atSamples ) ) {
			if ( info.nChannels == 1 || info.nChannels == 2 ) {
				objectInfo = info;
				objectSize = atSamples;
				objectMemSize = atBytes;
				nonCacheData = atData;
				goto haveData;
			}
			// unusable channel count: drop it and fall back
			soundCacheAllocator.Free( atData );
		}
	}

	if ( fh.Open( name, &info ) == -1 ) {
		common->Warning( "Couldn't load sound '%s' using default", name.c_str() );
		MakeDefault();
		return;
	}

	if ( info.nChannels != 1 && info.nChannels != 2 ) {
		common->Warning( "idSoundSample: %s has %i channels, using default", name.c_str(), info.nChannels );
		fh.Close();
		MakeDefault();
		return;
	}

	if ( info.wBitsPerSample != 16 ) {
		common->Warning( "idSoundSample: %s is %dbits, expected 16bits using default", name.c_str(), info.wBitsPerSample );
		fh.Close();
		MakeDefault();
		return;
	}

	if ( info.nSamplesPerSec != 44100 && info.nSamplesPerSec != 22050 && info.nSamplesPerSec != 11025 ) {
		common->Warning( "idSoundCache: %s is %dHz, expected 11025, 22050 or 44100 Hz. Using default", name.c_str(), info.nSamplesPerSec );
		fh.Close();
		MakeDefault();
		return;
	}

	objectInfo = info;
	objectSize = fh.GetOutputSize();
	objectMemSize = fh.GetMemorySize();

	nonCacheData = (byte *)soundCacheAllocator.Alloc( objectMemSize );
	fh.Read( nonCacheData, objectMemSize, NULL );

	// note: the old OpenAL "hardware buffer" upload used to happen here; the
	// libretro core mixes everything in software from nonCacheData

haveData:
	// Optionally resample sub-44kHz PCM up to 44.1kHz once, here, with a sinc
	// filter - so the mixer never has to upsample per-block. OGG is left
	// alone (it stays encoded and streams; resampling it here would force a
	// full decode into memory). objectInfo.wFormatTag is PCM for .wav.
	if ( s_resampleOnLoad.GetBool()
			&& objectInfo.wFormatTag == WAVE_FORMAT_TAG_PCM
			&& objectInfo.nSamplesPerSec < 44100
			&& objectSize > 0 ) {
		int srcFrames = objectSize / objectInfo.nChannels;
		int outFrames = 0;
		short *resampled = ResamplePCMTo44k( (const short *)nonCacheData, srcFrames,
				objectInfo.nChannels, objectInfo.nSamplesPerSec, &outFrames );
		if ( resampled != NULL ) {
			soundCacheAllocator.Free( nonCacheData );
			nonCacheData = (byte *)resampled;
			objectSize = outFrames * objectInfo.nChannels;
			objectMemSize = objectSize * sizeof( short );
			objectInfo.nSamplesPerSec = 44100;
			objectInfo.nAvgBytesPerSec = 44100 * objectInfo.nChannels * sizeof( short );
		}
	}

	fh.Close();
}

/*
===================
idSoundSample::PurgeSoundSample
===================
*/
void idSoundSample::PurgeSoundSample() {
	purged = true;



	if ( amplitudeData ) {
		soundCacheAllocator.Free( amplitudeData );
		amplitudeData = NULL;
	}

	if ( nonCacheData ) {
		soundCacheAllocator.Free( nonCacheData );
		nonCacheData = NULL;
	}
}

/*
===================
idSoundSample::Reload
===================
*/
void idSoundSample::Reload( bool force ) {
	if ( !force ) {
		ID_TIME_T newTimestamp;

		// check the timestamp
		newTimestamp = GetNewTimeStamp();

		if ( newTimestamp == FILE_NOT_FOUND_TIMESTAMP ) {
			if ( !defaultSound ) {
				common->Warning( "Couldn't load sound '%s' using default", name.c_str() );
				MakeDefault();
			}
			return;
		}
		if ( newTimestamp == timestamp ) {
			return;	// don't need to reload it
		}
	}

	common->Printf( "reloading %s\n", name.c_str() );
	PurgeSoundSample();
	Load();
}

/*
===================
idSoundSample::FetchFromCache

Returns true on success.
===================
*/
bool idSoundSample::FetchFromCache( int offset, const byte **output, int *position, int *size, const bool allowIO ) {
	offset &= 0xfffffffe;

	if ( objectSize == 0 || offset < 0 || offset > objectSize * (int)sizeof( short ) || !nonCacheData ) {
		return false;
	}

	if ( output ) {
		*output = nonCacheData + offset;
	}
	if ( position ) {
		*position = 0;
	}
	if ( size ) {
		*size = objectSize * sizeof( short ) - offset;
		if ( *size > SCACHE_SIZE ) {
			*size = SCACHE_SIZE;
		}
	}
	return true;
}
