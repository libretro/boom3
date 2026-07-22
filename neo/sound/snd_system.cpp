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

#include "sound/snd_local.h"
#include "sound/snd_mix_kernels.h"
#include "sound/snd_hrtf.h"
/* the channel history arrays in snd_local.h duplicate the tap constant */
typedef char hrtf_hist_size_matches[
	( sizeof( ((idSoundChannel *)0)->hrtfHistF ) / sizeof( float ) == SND_HRTF_HIST ) ? 1 : -1 ];
#include <limits.h>

idCVar idSoundSystemLocal::s_useReverb( "s_useReverb", "1", CVAR_SOUND | CVAR_BOOL | CVAR_ARCHIVE, "environmental reverb from efxs/*.efx" );
idCVar idSoundSystemLocal::s_reverbGain( "s_reverbGain", "0.5", CVAR_SOUND | CVAR_FLOAT | CVAR_ARCHIVE, "reverb wet gain", 0.0f, 1.0f );
idCVar idSoundSystemLocal::s_quadraticFalloff( "s_quadraticFalloff", "1", CVAR_SOUND | CVAR_BOOL, "" );
idCVar idSoundSystemLocal::s_drawSounds( "s_drawSounds", "0", CVAR_SOUND | CVAR_INTEGER, "", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );
idCVar idSoundSystemLocal::s_showStartSound( "s_showStartSound", "0", CVAR_SOUND | CVAR_BOOL, "" );
idCVar idSoundSystemLocal::s_useOcclusion( "s_useOcclusion", "1", CVAR_SOUND | CVAR_BOOL, "" );
idCVar idSoundSystemLocal::s_occlusionGainHF( "s_occlusionGainHF", "0.8", CVAR_SOUND | CVAR_FLOAT | CVAR_ARCHIVE, "HF gain per meter of extra portal path for occluded sounds; 1 = spectral occlusion off" );
idCVar idSoundSystemLocal::s_maxSoundsPerShader( "s_maxSoundsPerShader", "0", CVAR_SOUND | CVAR_ARCHIVE, "", 0, 10, idCmdSystem::ArgCompletion_Integer<0,10> );
idCVar idSoundSystemLocal::s_constantAmplitude( "s_constantAmplitude", "-1", CVAR_SOUND | CVAR_FLOAT, "" );
idCVar idSoundSystemLocal::s_minVolume6( "s_minVolume6", "0", CVAR_SOUND | CVAR_FLOAT, "" );
idCVar idSoundSystemLocal::s_dotbias6( "s_dotbias6", "0.8", CVAR_SOUND | CVAR_FLOAT, "" );
idCVar idSoundSystemLocal::s_minVolume2( "s_minVolume2", "0.25", CVAR_SOUND | CVAR_FLOAT, "" );
idCVar idSoundSystemLocal::s_dotbias2( "s_dotbias2", "1.1", CVAR_SOUND | CVAR_FLOAT, "" );
idCVar idSoundSystemLocal::s_spatializationDecay( "s_spatializationDecay", "2", CVAR_SOUND | CVAR_ARCHIVE | CVAR_FLOAT, "" );
idCVar idSoundSystemLocal::s_meterTopTime( "s_meterTopTime", "2000", CVAR_SOUND | CVAR_ARCHIVE | CVAR_INTEGER, "" );
idCVar idSoundSystemLocal::s_volume( "s_volume_dB", "0", CVAR_SOUND | CVAR_ARCHIVE | CVAR_FLOAT, "volume in dB" );
idCVar idSoundSystemLocal::s_playDefaultSound( "s_playDefaultSound", "1", CVAR_SOUND | CVAR_ARCHIVE | CVAR_BOOL, "play a beep for missing sounds" );
idCVar idSoundSystemLocal::s_subFraction( "s_subFraction", "0.75", CVAR_SOUND | CVAR_ARCHIVE | CVAR_FLOAT, "volume to subwoofer in 5.1" );
idCVar idSoundSystemLocal::s_globalFraction( "s_globalFraction", "0.8", CVAR_SOUND | CVAR_ARCHIVE | CVAR_FLOAT, "volume to all speakers when not spatialized" );
idCVar idSoundSystemLocal::s_doorDistanceAdd( "s_doorDistanceAdd", "150", CVAR_SOUND | CVAR_ARCHIVE | CVAR_FLOAT, "reduce sound volume with this distance when going through a door" );
idCVar idSoundSystemLocal::s_singleEmitter( "s_singleEmitter", "0", CVAR_SOUND | CVAR_INTEGER, "mute all sounds but this emitter" );
idCVar idSoundSystemLocal::s_numberOfSpeakers( "s_numberOfSpeakers", "2", CVAR_SOUND | CVAR_ARCHIVE, "number of speakers" );
idCVar idSoundSystemLocal::s_clipVolumes( "s_clipVolumes", "1", CVAR_SOUND | CVAR_BOOL, ""  );

idCVar idSoundSystemLocal::s_slowAttenuate( "s_slowAttenuate", "1", CVAR_SOUND | CVAR_BOOL, "slowmo sounds attenuate over shorted distance" );
idCVar idSoundSystemLocal::s_enviroSuitCutoffFreq( "s_enviroSuitCutoffFreq", "2000", CVAR_SOUND | CVAR_FLOAT, "" );
idCVar idSoundSystemLocal::s_enviroSuitCutoffQ( "s_enviroSuitCutoffQ", "2", CVAR_SOUND | CVAR_FLOAT, "" );
idCVar idSoundSystemLocal::s_reverbTime( "s_reverbTime", "1000", CVAR_SOUND | CVAR_FLOAT, "" );
idCVar idSoundSystemLocal::s_reverbFeedback( "s_reverbFeedback", "0.333", CVAR_SOUND | CVAR_FLOAT, "" );
idCVar idSoundSystemLocal::s_enviroSuitVolumeScale( "s_enviroSuitVolumeScale", "0.9", CVAR_SOUND | CVAR_FLOAT, "" );
idCVar idSoundSystemLocal::s_skipHelltimeFX( "s_skipHelltimeFX", "0", CVAR_SOUND | CVAR_BOOL, "" );

idCVar idSoundSystemLocal::s_outputLimiter( "s_outputLimiter", "1", CVAR_SOUND | CVAR_BOOL, "soft-knee saturator at the output stage; 0 = hard clip" );
idCVar idSoundSystemLocal::s_HRTF( "s_HRTF", "0", CVAR_SOUND | CVAR_BOOL | CVAR_ARCHIVE, "binaural rendering of spatialized sounds for headphones (KEMAR HRTF)" );
idCVar idSoundSystemLocal::s_reverse( "s_reverse", "0", CVAR_SOUND | CVAR_BOOL | CVAR_ARCHIVE, "swap left and right output channels" );



idCVar idSoundSystemLocal::s_decompressionLimit( "s_decompressionLimit", "6", CVAR_SOUND | CVAR_INTEGER | CVAR_ROM, "specifies maximum uncompressed sample length in seconds" );



idSoundSystemLocal	soundSystemLocal;

/*
   The active output rate. Set once from the core option before the sound
   system initialises; everything downstream reads it through snd_SampleRate().
   Defaults to the rate Doom 3's assets are authored at, so a frontend that
   never calls snd_SetSampleRate() behaves exactly as before.
*/
int snd_sampleRate = PRIMARYFREQ;

void snd_SetSampleRate( int hz ) {
	/*
	   Only rates that are integer multiples or simple ratios of the asset
	   rates are worth offering. 44100 is native; 48000 and 96000 are what
	   modern devices actually run at, so picking one of those lets the
	   frontend skip a resampling stage.

	   96000 is also the ceiling the libretro layer's audio budget is checked
	   against: it mixes sampleRate/framerate frames per retro_run into arrays
	   sized by MIXBUFFER_SAMPLES, and a static assertion there covers
	   96000 / 30fps = 3200 <= 4096. Adding a higher rate here means raising
	   MIXBUFFER_SAMPLES to match, and that assertion will say so.
	*/
	if ( hz != 32000 && hz != 44100 && hz != 48000 && hz != 96000 ) {
		hz = PRIMARYFREQ;
	}
	snd_sampleRate = hz;
}
idSoundSystem	*soundSystem  = &soundSystemLocal;


/*
===============
SoundReloadSounds_f

  this is called from the main thread
===============
*/
void SoundReloadSounds_f( const idCmdArgs &args ) {
	if ( !soundSystemLocal.soundCache ) {
		return;
	}
	bool force = false;
	if ( args.Argc() == 2 ) {
		force = true;
	}
	soundSystem->SetMute( true );
	soundSystemLocal.soundCache->ReloadSounds( force );
	soundSystem->SetMute( false );
	common->Printf( "sound: changed sounds reloaded\n" );
}

/*
===============
ListSounds_f

Optional parameter to only list sounds containing that string
===============
*/
void ListSounds_f( const idCmdArgs &args ) {
	int i;
	const char	*snd = args.Argv( 1 );

	if ( !soundSystemLocal.soundCache ) {
		common->Printf( "No sound.\n" );
		return;
	}

	int	totalSounds = 0;
	int totalSamples = 0;
	int totalMemory = 0;
	int totalPCMMemory = 0;
	for( i = 0; i < soundSystemLocal.soundCache->GetNumObjects(); i++ ) {
		const idSoundSample *sample = soundSystemLocal.soundCache->GetObject(i);
		if ( !sample ) {
			continue;
		}
		if ( snd && sample->name.Find( snd, false ) < 0 ) {
			continue;
		}

		const waveformatex_t &info = sample->objectInfo;

		const char *stereo = ( info.nChannels == 2 ? "ST" : "  " );
		const char *format = ( info.wFormatTag == WAVE_FORMAT_TAG_OGG ) ? "OGG" : "WAV";
		const char *defaulted = ( sample->defaultSound ? "(DEFAULTED)" : sample->purged ? "(PURGED)" : "" );

		common->Printf( "%s %dkHz %6dms %5dkB %4s %s%s\n", stereo, sample->objectInfo.nSamplesPerSec / 1000,
					soundSystemLocal.SamplesToMilliseconds( sample->LengthInOutputSamples() ),
					sample->objectMemSize >> 10, format, sample->name.c_str(), defaulted );

		if ( !sample->purged ) {
			totalSamples += sample->objectSize;
			if ( info.wFormatTag != WAVE_FORMAT_TAG_OGG )
				totalPCMMemory += sample->objectMemSize;
			totalMemory += sample->objectMemSize;
		}
		totalSounds++;
	}
	common->Printf( "%8d total sounds\n", totalSounds );
	common->Printf( "%8d total samples loaded\n", totalSamples );
	common->Printf( "%8d kB total system memory used\n", totalMemory >> 10 );
}

/*
===============
ListSoundDecoders_f
===============
*/
void ListSoundDecoders_f( const idCmdArgs &args ) {
	int i, j, numActiveDecoders, numWaitingDecoders;
	idSoundWorldLocal *sw = soundSystemLocal.currentSoundWorld;

	numActiveDecoders = numWaitingDecoders = 0;

	for ( i = 0; i < sw->emitters.Num(); i++ ) {
		idSoundEmitterLocal *sound = sw->emitters[i];

		if ( !sound )
			continue;

		// run through all the channels
		for ( j = 0; j < SOUND_MAX_CHANNELS; j++ ) {
			idSoundChannel	*chan = &sound->channels[j];

			if ( chan->decoder == NULL ) {
				continue;
			}

			idSoundSample *sample = chan->decoder->GetSample();

			if ( sample != NULL ) {
				continue;
			}

			const char *format = ( chan->leadinSample->objectInfo.wFormatTag == WAVE_FORMAT_TAG_OGG ) ? "OGG" : "WAV";
			common->Printf( "%3d waiting %s: %s\n", numWaitingDecoders, format, chan->leadinSample->name.c_str() );

			numWaitingDecoders++;
		}
	}

	for ( i = 0; i < sw->emitters.Num(); i++ ) {
		idSoundEmitterLocal *sound = sw->emitters[i];

		if ( !sound ) {
			continue;
		}

		// run through all the channels
		for ( j = 0; j < SOUND_MAX_CHANNELS; j++ ) {
			idSoundChannel	*chan = &sound->channels[j];

			if ( chan->decoder == NULL ) {
				continue;
			}

			idSoundSample *sample = chan->decoder->GetSample();

			if ( sample == NULL ) {
				continue;
			}

			const char *format = ( sample->objectInfo.wFormatTag == WAVE_FORMAT_TAG_OGG ) ? "OGG" : "WAV";

			int localTime = soundSystemLocal.GetCurrentSampleTime() - chan->triggerSampleTime;
			int sampleTime = sample->LengthInOutputSamples() * sample->objectInfo.nChannels;
			int percent;
			if ( localTime > sampleTime ) {
				if ( chan->parms.soundShaderFlags & SSF_LOOPING )
					percent = ( localTime % sampleTime ) * 100 / sampleTime;
				else
					percent = 100;
			} else {
				percent = localTime * 100 / sampleTime;
			}

			common->Printf( "%3d decoding %3d%% %s: %s\n", numActiveDecoders, percent, format, sample->name.c_str() );

			numActiveDecoders++;
		}
	}

	common->Printf( "%d decoders\n", numWaitingDecoders + numActiveDecoders );
	common->Printf( "%d waiting decoders\n", numWaitingDecoders );
	common->Printf( "%d active decoders\n", numActiveDecoders );
	common->Printf( "%d kB decoder memory in %d blocks\n", idSampleDecoder::GetUsedBlockMemory() >> 10, idSampleDecoder::GetNumUsedBlocks() );
}

/*
===============
TestSound_f

  this is called from the main thread
===============
*/
void TestSound_f( const idCmdArgs &args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "Usage: testSound <file>\n" );
		return;
	}
	if ( soundSystemLocal.currentSoundWorld ) {
		soundSystemLocal.currentSoundWorld->PlayShaderDirectly( args.Argv( 1 ) );
	}
}

/*
===============
SoundSystemRestart_f

restart the sound thread

  this is called from the main thread
===============
*/
void SoundSystemRestart_f( const idCmdArgs &args ) {
	soundSystem->SetMute( true );
	soundSystemLocal.ShutdownHW();
	soundSystemLocal.InitHW();
	soundSystem->SetMute( false );
}

/*
===============
idSoundSystemLocal::Init

initialize the sound system
===============
*/
void idSoundSystemLocal::Init() {
	common->Printf( "----- Initializing Sound System -----\n" );

	isInitialized = false;
	muted = false;
	shutdown = false;
	efxloaded = false;
	efxGeneration = 0;

	currentSoundWorld = NULL;
	soundCache = NULL;

	CurrentSoundTime = 0;
	// note: outputIsFloat is deliberately NOT reset here. Init() runs on the
	// first retro_run, i.e. AFTER retro_load_game negotiated the output
	// format and called SetOutputFloat(); resetting it here made the mixer
	// take the s16 branch (writing int32 accumulator words into the float
	// output buffer, reinterpreted as garbage floats) while the libretro
	// layer was in float mode. The MixFrame* entry points also assert the
	// format authoritatively on every call, so no reset ordering (s_restart,
	// re-Init, re-load) can ever desync the two again.

	memset( meterTops, 0, sizeof( meterTops ) );
	memset( meterTopsTime, 0, sizeof( meterTopsTime ) );

	for( int i = -600; i < 600; i++ ) {
		float pt = i * 0.1f;
		volumesDB[i+600] = pow( 2.0f,( pt * ( 1.0f / 6.0f ) ) );
	}

	// make a 16 byte aligned finalMixBuffer
	finalMixBuffer = (float *) ( ( ( (intptr_t)realAccum ) + 15 ) & ~15 );

	graph = NULL;


	idSampleDecoder::Init();
	soundCache = new idSoundCache();


	cmdSystem->AddCommand( "listSounds", ListSounds_f, CMD_FL_SOUND, "lists all sounds" );
	cmdSystem->AddCommand( "listSoundDecoders", ListSoundDecoders_f, CMD_FL_SOUND, "list active sound decoders" );
	cmdSystem->AddCommand( "reloadSounds", SoundReloadSounds_f, CMD_FL_SOUND|CMD_FL_CHEAT, "reloads all sounds" );
	cmdSystem->AddCommand( "testSound", TestSound_f, CMD_FL_SOUND | CMD_FL_CHEAT, "tests a sound", idCmdSystem::ArgCompletion_SoundName );
	cmdSystem->AddCommand( "s_restart", SoundSystemRestart_f, CMD_FL_SOUND, "restarts the sound system" );
}

/*
===============
idSoundSystemLocal::Shutdown
===============
*/
void idSoundSystemLocal::Shutdown() {
	ShutdownHW();

	// destroy all the cached sounds
	delete soundCache;
	soundCache = NULL;

	idSampleDecoder::Shutdown();
}

/*
===============
idSoundSystemLocal::InitHW
===============
*/
bool idSoundSystemLocal::InitHW() {
	common->Printf("Initializing sound system\n");

	/*
	   Output is stereo, always - the mixer never consults
	   s_numberOfSpeakers. The cvar survives purely as the display value
	   of the game menu's (repurposed) "Surround Speakers" row, which now
	   toggles s_HRTF; sync it here so the row shows the real state after
	   restarts and config loads. A console s_HRTF change mid-session
	   desyncs the row until the next s_restart - cosmetic only.
	*/
	s_numberOfSpeakers.SetInteger( s_HRTF.GetBool() ? 6 : 2 );

	/*
	   Binaural renderer: resolve the HRIR set at the final output rate.
	   Heap-allocated (~1.3 MB of per-rate coefficient masters) so the
	   tables live once; the one-shot resample costs a few tens of ms
	   only when the rate is not 44100.
	*/
	if ( hrtf == NULL ) {
		hrtf = (idSoundHRTF *)Mem_Alloc( sizeof( idSoundHRTF ) );
	}
	hrtf->Init( snd_SampleRate() );

	isInitialized = true;
	shutdown = false;

	return true;
}

/*
===============
idSoundSystemLocal::ShutdownHW
===============
*/
bool idSoundSystemLocal::ShutdownHW() {
	if ( !isInitialized ) {
		return false;
	}

	// There is no async sound thread in this core: MixFrameFloat/MixFrameS16
	// are called from retro_run on the main thread, which is the same thread
	// running this shutdown, and both already bail out on the shutdown flag.
	// The original 100ms sleep was waiting for an AsyncUpdate() that is never
	// implemented here, so it only delayed retro_unload_game.
	shutdown = true;

	common->Printf( "Shutting down sound hardware\n" );

	isInitialized = false;

	if ( graph ) {
		Mem_Free( graph );
		graph = NULL;
	}

	return true;
}


/*
===============
idSoundSystemLocal::GetCurrentSampleTime
===============
*/
int idSoundSystemLocal::GetCurrentSampleTime( void ) const {
	// CurrentSoundTime is the master output-rate sample clock, advanced only by
	// MixFrame(). Before the sound system is initialized nothing has been
	// mixed, so the clock is simply 0 - the old fallback derived a value
	// from Core_Milliseconds()*176.4 (wall clock, and 4x the real sample
	// rate at that), which was both nondeterministic and in wrong units.
	return CurrentSoundTime;
}

/*
===================
idSoundSystemLocal::MixFrameFloat / MixFrameS16

libretro: render exactly numFrames stereo frames and advance the output
sample clock. Called once per retro_run from the frontend's thread; there
is no other mixing path, no wall clock, no backend cursor, no thread.
numFrames is capped at MIXBUFFER_SAMPLES (per-channel gather buffers are
sized to it); the libretro layer never asks for more (44100/30fps = 1470).
===================
*/
void idSoundSystemLocal::MixFrameFloat( float *dest, int numFrames ) {
	outputIsFloat = true;	// authoritative: this entry point IS the float pipeline
	if ( numFrames > MIXBUFFER_SAMPLES )
		numFrames = MIXBUFFER_SAMPLES;
	memset( dest, 0, numFrames * 2 * sizeof( float ) );
	if ( !isInitialized || shutdown || numFrames <= 0 )
		return;
	if ( !muted && currentSoundWorld )
		currentSoundWorld->MixLoop( CurrentSoundTime, numFrames, dest );
	// saturate: the sum of channels is deliberately unclamped during mixing
	// (Doom 3's mix runs hot); every previous pipeline saturated at the final
	// int16 conversion, and the frontend's float chain expects [-1,1].
	// Default is the soft knee (identity below 3/4 FS, monotonic above -
	// see the saturator block in snd_mix_kernels.h); s_outputLimiter 0
	// restores the plain hard clip.
	if ( s_outputLimiter.GetBool() )
		Snd_SoftKneeFloatOutput( dest, numFrames * 2 );
	else
		Snd_ClampFloatOutput( dest, numFrames * 2 );

	/*
	   s_reverse: the "Reverse Channels" row in the game's audio menu.
	   The cvar vanished with the OpenAL backend, leaving that menu row
	   a dead toggle (retail mainmenu.gui is data this core cannot
	   ship modified, so the row cannot be removed - make it work
	   instead). A pure output permutation: stateless, bit-exact,
	   nothing downstream to care.
	*/
	if ( s_reverse.GetBool() ) {
		for ( int i = 0; i < numFrames; i++ ) {
			float t = dest[i*2]; dest[i*2] = dest[i*2+1]; dest[i*2+1] = t;
		}
	}
	CurrentSoundTime += numFrames;
}

void idSoundSystemLocal::MixFrameS16( short *dest, int numFrames ) {
	static int accum[MIXBUFFER_SAMPLES * 2];

	outputIsFloat = false;	// authoritative: this entry point IS the s16 pipeline

	if ( numFrames > MIXBUFFER_SAMPLES )
		numFrames = MIXBUFFER_SAMPLES;
	if ( numFrames <= 0 )
		return;
	memset( accum, 0, numFrames * 2 * sizeof( int ) );
	if ( isInitialized && !shutdown && !muted && currentSoundWorld )
		// s16 mode: MixLoop's destination is the int32 accumulator
		currentSoundWorld->MixLoop( CurrentSoundTime, numFrames, (float *)accum );
	// narrow the int32 accumulator: soft knee by default (identity below
	// 3/4 FS, so normal content stays byte-exact), hard saturation with
	// s_outputLimiter 0.
	if ( s_outputLimiter.GetBool() )
		Snd_SumToS16Soft( dest, accum, numFrames * 2 );
	else
		Snd_SumToS16( dest, accum, numFrames * 2 );

	// s_reverse: see MixFrameFloat
	if ( s_reverse.GetBool() ) {
		for ( int i = 0; i < numFrames; i++ ) {
			short t = dest[i*2]; dest[i*2] = dest[i*2+1]; dest[i*2+1] = t;
		}
	}
	if ( isInitialized && !shutdown )
		CurrentSoundTime += numFrames;
}


/*
===================
idSoundSystemLocal::dB2Scale
===================
*/
float idSoundSystemLocal::dB2Scale( const float val ) const {
	if ( val == 0.0f ) {
		return 1.0f;				// most common
	} else if ( val <= -60.0f ) {
		return 0.0f;
	} else if ( val >= 60.0f ) {
		return powf( 2.0f, val * ( 1.0f / 6.0f ) );
	}
	int ival = (int)( ( val + 60.0f ) * 10.0f );
	return volumesDB[ival];
}

/*
===================
idSoundSystemLocal::ImageForTime
===================
*/
cinData_t idSoundSystemLocal::ImageForTime( const int milliseconds, const bool waveform ) {
	cinData_t ret;
	int i, j;

	if ( !isInitialized ) {
		memset( &ret, 0, sizeof( ret ) );
		return ret;
	}

	Sys_EnterCriticalSection();

	if ( !graph ) {
		graph = (dword *)Mem_Alloc( 256*128 * 4);
	}
	memset( graph, 0, 256*128 * 4 );
	float *accum = finalMixBuffer;	// unfortunately, these are already clamped
	int time = Core_Milliseconds();

	int numSpeakers = 2;	// meter layout: output is stereo; the cvar is a menu display value

	if ( !waveform ) {
		for( j = 0; j < numSpeakers; j++ ) {
			int meter = 0;
			for( i = 0; i < MIXBUFFER_SAMPLES; i++ ) {
				float result = idMath::Fabs(accum[i*numSpeakers+j]);
				if ( result > meter ) {
					meter = result;
				}
			}

			meter /= 256;		// 32768 becomes 128
			if ( meter > 128 ) {
				meter = 128;
			}
			int offset;
			int xsize;
			if ( numSpeakers == 6 ) {
				offset = j * 40;
				xsize = 20;
			} else {
				offset = j * 128;
				xsize = 63;
			}
			int x,y;
			dword color = 0xff00ff00;
			for ( y = 0; y < 128; y++ ) {
				for ( x = 0; x < xsize; x++ ) {
					graph[(127-y)*256 + offset + x ] = color;
				}
				if ( y > meter )
					break;
			}

			if ( meter > meterTops[j] ) {
				meterTops[j] = meter;
				meterTopsTime[j] = time + s_meterTopTime.GetInteger();
			} else if ( time > meterTopsTime[j] && meterTops[j] > 0 ) {
				meterTops[j]--;
				if (meterTops[j]) {
					meterTops[j]--;
				}
			}
		}

		for( j = 0; j < numSpeakers; j++ ) {
			int meter = meterTops[j];

			int offset;
			int xsize;
			if ( numSpeakers == 6 ) {
				offset = j*40;
				xsize = 20;
			} else {
				offset = j*128;
				xsize = 63;
			}
			int x,y;
			dword color;
			if ( meter <= 80 ) {
				color = 0xff007f00;
			} else if ( meter <= 112 ) {
				color = 0xff007f7f;
			} else {
				color = 0xff00007f;
			}
			for ( y = meter; y < 128 && y < meter + 4; y++ ) {
				for ( x = 0; x < xsize; x++ ) {
					graph[(127-y)*256 + offset + x ] = color;
				}
			}
		}
	} else {
		dword colors[] = { 0xff007f00, 0xff007f7f, 0xff00007f, 0xff00ff00, 0xff00ffff, 0xff0000ff };

		for( j = 0; j < numSpeakers; j++ ) {
			int xx = 0;
			float fmeter;
			int step = MIXBUFFER_SAMPLES / 256;
			for( i = 0; i < MIXBUFFER_SAMPLES; i += step ) {
				fmeter = 0.0f;
				for( int x = 0; x < step; x++ ) {
					float result = accum[(i+x)*numSpeakers+j];
					result = result / 32768.0f;
					fmeter += result;
				}
				fmeter /= 4.0f;
				if ( fmeter < -1.0f ) {
					fmeter = -1.0f;
				} else if ( fmeter > 1.0f ) {
					fmeter = 1.0f;
				}
				int meter = (fmeter * 63.0f);
				graph[ (meter + 64) * 256 + xx ] = colors[j];

				if ( meter < 0 ) {
					meter = -meter;
				}
				if ( meter > meterTops[xx] ) {
					meterTops[xx] = meter;
					meterTopsTime[xx] = time + 100;
				} else if ( time>meterTopsTime[xx] && meterTops[xx] > 0 ) {
					meterTops[xx]--;
					if ( meterTops[xx] ) {
						meterTops[xx]--;
					}
				}
				xx++;
			}
		}
		for( i = 0; i < 256; i++ ) {
			int meter = meterTops[i];
			for ( int y = -meter; y < meter; y++ ) {
				graph[ (y+64)*256 + i ] = colors[j];
			}
		}
	}
	ret.imageHeight = 128;
	ret.imageWidth = 256;
	ret.image = (unsigned char *)graph;

	Sys_LeaveCriticalSection();

	return ret;
}

/*
===================
idSoundSystemLocal::GetSoundDecoderInfo
===================
*/
int idSoundSystemLocal::GetSoundDecoderInfo( int index, soundDecoderInfo_t &decoderInfo ) {
	int i, j, firstEmitter, firstChannel;
	idSoundWorldLocal *sw = soundSystemLocal.currentSoundWorld;

	if ( index < 0 ) {
		firstEmitter = 0;
		firstChannel = 0;
	} else {
		firstEmitter = index / SOUND_MAX_CHANNELS;
		firstChannel = index - firstEmitter * SOUND_MAX_CHANNELS + 1;
	}

	for ( i = firstEmitter; i < sw->emitters.Num(); i++ ) {
		idSoundEmitterLocal *sound = sw->emitters[i];

		if ( !sound ) {
			continue;
		}

		// run through all the channels
		for ( j = firstChannel; j < SOUND_MAX_CHANNELS; j++ ) {
			idSoundChannel	*chan = &sound->channels[j];

			if ( chan->decoder == NULL ) {
				continue;
			}

			idSoundSample *sample = chan->decoder->GetSample();

			if ( sample == NULL ) {
				continue;
			}

			decoderInfo.name = sample->name;
			decoderInfo.format = ( sample->objectInfo.wFormatTag == WAVE_FORMAT_TAG_OGG ) ? "OGG" : "WAV";
			decoderInfo.numChannels = sample->objectInfo.nChannels;
			decoderInfo.numSamplesPerSecond = sample->objectInfo.nSamplesPerSec;
			decoderInfo.numOutputSamples = sample->LengthInOutputSamples();
			decoderInfo.numBytes = sample->objectMemSize;
			decoderInfo.looping = ( chan->parms.soundShaderFlags & SSF_LOOPING ) != 0;
			decoderInfo.lastVolume = chan->lastVolume;
			decoderInfo.startSampleTime = chan->triggerSampleTime;
			decoderInfo.currentSampleTime = soundSystemLocal.GetCurrentSampleTime();

			return ( i * SOUND_MAX_CHANNELS + j );
		}

		firstChannel = 0;
	}
	return -1;
}

/*
===================
idSoundSystemLocal::AllocSoundWorld
===================
*/
idSoundWorld *idSoundSystemLocal::AllocSoundWorld( idRenderWorld *rw ) {
	idSoundWorldLocal	*local = new idSoundWorldLocal;

	local->Init( rw );

	return local;
}

/*
===================
idSoundSystemLocal::SetMute
===================
*/
void idSoundSystemLocal::SetMute( bool muteOn ) {
	muted = muteOn;
}

/*
===================
idSoundSystemLocal::SamplesToMilliseconds
===================
*/
int idSoundSystemLocal::SamplesToMilliseconds( int samples ) const {
	/*
	   Was samples / (PRIMARYFREQ/1000). That integer division truncates 44.1
	   to 44, a 0.23% error in every millisecond<->sample conversion: 2.3ms of
	   drift per second, 136ms over a one-minute fade. Do the division at full
	   precision instead. 48000 and 96000 divide exactly, so this only changes
	   the result at 44100 - where it was wrong.
	*/
	return (int)( ( (int64_t)samples * 1000 ) / snd_SampleRate() );
}

/*
===================
idSoundSystemLocal::SamplesToMilliseconds
===================
*/
int idSoundSystemLocal::MillisecondsToSamples( int ms ) const {
	/* see SamplesToMilliseconds above */
	return (int)( ( (int64_t)ms * snd_SampleRate() ) / 1000 );
}

/*
===================
idSoundSystemLocal::SetPlayingSoundWorld

specifying NULL will cause silence to be played
===================
*/
void idSoundSystemLocal::SetPlayingSoundWorld( idSoundWorld *soundWorld ) {
	currentSoundWorld = static_cast<idSoundWorldLocal *>(soundWorld);
}

/*
===================
idSoundSystemLocal::GetPlayingSoundWorld
===================
*/
idSoundWorld *idSoundSystemLocal::GetPlayingSoundWorld( void ) {
	return currentSoundWorld;
}

/*
===================
idSoundSystemLocal::BeginLevelLoad
===================
*/

void idSoundSystemLocal::BeginLevelLoad() {
	if ( !isInitialized ) {
		return;
	}
	soundCache->BeginLevelLoad();

	if ( efxloaded ) {
		EFXDatabase.Clear();
		efxloaded = false;
		efxGeneration++;
	}
}

/*
===================
idSoundSystemLocal::EndLevelLoad
===================
*/
void idSoundSystemLocal::EndLevelLoadStart( const char *mapstring ) {
	if ( isInitialized ) {
		idStr efxname( "efxs/" );
		idStr mapname( mapstring );

		mapname.SetFileExtension( ".efx" );
		mapname.StripPath();
		efxname += mapname;

		efxloaded = EFXDatabase.LoadFile( efxname );
		efxGeneration++;
		if ( efxloaded ) {
			common->Printf( "sound: found %s\n", efxname.c_str() );
		} else {
			common->Printf( "sound: missing %s\n", efxname.c_str() );
		}
	}

	if ( !isInitialized ) {
		return;
	}
	soundCache->EndLevelLoadStart();
}

bool idSoundSystemLocal::EndLevelLoadStep( int maxSamples ) {
	if ( !isInitialized ) {
		return false;
	}
	return soundCache->EndLevelLoadStep( maxSamples );
}

void idSoundSystemLocal::EndLevelLoadFinish( void ) {
	if ( !isInitialized ) {
		return;
	}
	soundCache->EndLevelLoadFinish();
}

void idSoundSystemLocal::SetDeferSampleLoads( bool defer ) {
	if ( !isInitialized ) {
		return;
	}
	soundCache->SetDeferLoads( defer );
}

void idSoundSystemLocal::DrainPendingSamples( void ) {
	if ( !isInitialized ) {
		return;
	}
	soundCache->DrainPending();
}

/*
===================
idSoundSystemLocal::EndLevelLoad

Blocking convenience (Start + drain + Finish) for callers not driven by the
per-frame load pump.
===================
*/
void idSoundSystemLocal::EndLevelLoad( const char *mapstring ) {
	EndLevelLoadStart( mapstring );
	while ( EndLevelLoadStep( 0x7fffffff ) ) {
	}
	EndLevelLoadFinish();
}

/*
============================================================
SoundFX and misc effects
============================================================
*/

/*
===================
idSoundSystemLocal::ProcessSample
===================
*/
void SoundFX_LowpassFast::ProcessSample( float* in, float* out ) {
	// compute output value
	out[0] = a1 * in[0] + a2 * in[-1] + a3 * in[-2] - b1 * out[-1] - b2 * out[-2];
}

void SoundFX_LowpassFast::SetParms( float p1, float p2, float p3 ) {
	float c;

	// set the vars
	freq = p1;
	res = p2;

	// precompute the coefs
	c = 1.0 / idMath::Tan( idMath::PI * freq / snd_SampleRate() );

	// compute coefs
	a1 = 1.0 / ( 1.0 + res * c + c * c );
	a2 = 2* a1;
	a3 = a1;

	b1 = 2.0 * ( 1.0 - c * c) * a1;
	b2 = ( 1.0 - res * c + c * c ) * a1;
}

/*
=================
idSoundSystemLocal::PrintMemInfo
=================
*/
void idSoundSystemLocal::PrintMemInfo( MemInfo_t *mi ) {
	soundCache->PrintMemInfo( mi );
}

