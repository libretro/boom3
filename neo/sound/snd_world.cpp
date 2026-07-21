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
#include "framework/Session.h"
#include "renderer/RenderWorld.h"

#include "sound/snd_local.h"
#include "sound/snd_mix_kernels.h"

/*
==================
idSoundWorldLocal::Init
==================
*/
void idSoundWorldLocal::Init( idRenderWorld *renderWorld ) {
	rw = renderWorld;
	writeDemo = NULL;

	listenerAxis.Identity();
	listenerPos.Zero();
	listenerPrivateId = 0;
	listenerQU.Zero();
	listenerArea = 0;
	reverbCachedArea = -2;		// no area resolved yet; -1 is the valid "outside world" value
	reverbCachedGen = -1;
	listenerAreaName = "Undefined";
	/*
	   The reverb had no Init call anywhere: its delay-line lengths, allpass
	   lengths and active flag were whatever the allocator handed over.
	   Latent since the FDN landed - it never fired because no shipped test
	   content carries an efxs/ file, so IsActive() read uninitialized-but-
	   usually-zero memory and the process loops (which divide by the line
	   lengths) never ran. The first real .efx load made it a division by
	   zero on the first wet mix block.
	*/
	reverb.Init();
	enviroFX.Init();


	gameMsec = 0;
	gameSampleTime = 0;
	pauseSampleTime = -1;

	for ( int i = 0 ; i < SOUND_MAX_CLASSES ; i++ ) {
		soundClassFade[i].Clear();
	}

	// fill in the 0 index spot
	idSoundEmitterLocal	*placeHolder = new idSoundEmitterLocal;
	emitters.Append( placeHolder );



	localSound = NULL;

	slowmoActive		= false;
	slowmoSpeed			= 0;
	enviroSuitActive	= false;
}

/*
===============
idSoundWorldLocal::idSoundWorldLocal
===============
*/
idSoundWorldLocal::idSoundWorldLocal() {
	listenerAreFiltersInitialized = false;
}

/*
===============
idSoundWorldLocal::~idSoundWorldLocal
===============
*/
idSoundWorldLocal::~idSoundWorldLocal() {
	Shutdown();
}

/*
===============
idSoundWorldLocal::Shutdown

  this is called from the main thread
===============
*/
void idSoundWorldLocal::Shutdown() {
	int i;

	if ( soundSystemLocal.currentSoundWorld == this ) {
		soundSystemLocal.currentSoundWorld = NULL;
	}


	for ( i = 0; i < emitters.Num(); i++ ) {
		if ( emitters[i] ) {
			delete emitters[i];
			emitters[i] = NULL;
		}
	}


	localSound = NULL;
}

/*
===================
idSoundWorldLocal::ClearAllSoundEmitters
===================
*/
void idSoundWorldLocal::ClearAllSoundEmitters() {
	int i;

	Sys_EnterCriticalSection();


	for ( i = 0; i < emitters.Num(); i++ ) {
		idSoundEmitterLocal *sound = emitters[i];
		sound->Clear();
	}
	localSound = NULL;

	Sys_LeaveCriticalSection();
}

/*
===================
idSoundWorldLocal::AllocLocalSoundEmitter
===================
*/
idSoundEmitterLocal *idSoundWorldLocal::AllocLocalSoundEmitter() {
	int i, index;
	idSoundEmitterLocal *def = NULL;

	index = -1;

	// never use the 0 index spot

	for ( i = 1 ; i < emitters.Num() ; i++ ) {
		def = emitters[i];

		// check for a completed and freed spot
		if ( def->removeStatus >= REMOVE_STATUS_SAMPLEFINISHED ) {
			index = i;
			if ( idSoundSystemLocal::s_showStartSound.GetInteger() ) {
				common->Printf( "sound: recycling sound def %d\n", i );
			}
			break;
		}
	}

	if ( index == -1 ) {
		// append a brand new one
		def = new idSoundEmitterLocal;

		// we need to protect this from the async thread
		Sys_EnterCriticalSection();
		index = emitters.Append( def );
		Sys_LeaveCriticalSection();

		if ( idSoundSystemLocal::s_showStartSound.GetInteger() ) {
			common->Printf( "sound: appended new sound def %d\n", index );
		}
	}

	def->Clear();
	def->index = index;
	def->removeStatus = REMOVE_STATUS_ALIVE;
	def->soundWorld = this;

	return def;
}

/*
===================
idSoundWorldLocal::AllocSoundEmitter

  this is called from the main thread
===================
*/
idSoundEmitter *idSoundWorldLocal::AllocSoundEmitter() {
	idSoundEmitterLocal *emitter = AllocLocalSoundEmitter();

	if ( idSoundSystemLocal::s_showStartSound.GetInteger() ) {
		common->Printf( "AllocSoundEmitter = %i\n",  emitter->index );
	}
	if ( writeDemo ) {
		writeDemo->WriteInt( DS_SOUND );
		writeDemo->WriteInt( SCMD_ALLOC_EMITTER );
		writeDemo->WriteInt( emitter->index );
	}

	return emitter;
}

/*
===================
idSoundWorldLocal::StartWritingDemo

  this is called from the main thread
===================
*/
void idSoundWorldLocal::StartWritingDemo( idDemoFile *demo ) {
	writeDemo = demo;

	writeDemo->WriteInt( DS_SOUND );
	writeDemo->WriteInt( SCMD_STATE );

	// use the normal save game code to archive all the emitters
	WriteToSaveGame( writeDemo );
}

/*
===================
idSoundWorldLocal::StopWritingDemo

  this is called from the main thread
===================
*/
void idSoundWorldLocal::StopWritingDemo() {
	writeDemo = NULL;
}

/*
===================
idSoundWorldLocal::ProcessDemoCommand

  this is called from the main thread
===================
*/
void idSoundWorldLocal::ProcessDemoCommand( idDemoFile *readDemo ) {
	int	index;
	idSoundEmitterLocal	*def;

	if ( !readDemo ) {
		return;
	}

	int dc;

	if ( !readDemo->ReadInt( dc ) ) {
		return;
	}

	switch( (soundDemoCommand_t)dc ) {
	case SCMD_STATE:
		// we need to protect this from the async thread
		// other instances of calling idSoundWorldLocal::ReadFromSaveGame do this while the sound code is muted
		// setting muted and going right in may not be good enough here, as we async thread may already be in an async tick (in which case we could still race to it)
		Sys_EnterCriticalSection();
		ReadFromSaveGame( readDemo );
		Sys_LeaveCriticalSection();
		UnPause();
		break;
	case SCMD_PLACE_LISTENER:
		{
			idVec3	origin;
			idMat3	axis;
			int		listenerId;
			int		gameTime;

			readDemo->ReadVec3( origin );
			readDemo->ReadMat3( axis );
			readDemo->ReadInt( listenerId );
			readDemo->ReadInt( gameTime );

			PlaceListener( origin, axis, listenerId, gameTime, "" );
		};
		break;
	case SCMD_ALLOC_EMITTER:
		readDemo->ReadInt( index );
		if ( index < 1 || index > emitters.Num() ) {
			common->Error( "idSoundWorldLocal::ProcessDemoCommand: bad emitter number" );
		}
		if ( index == emitters.Num() ) {
			// append a brand new one
			def = new idSoundEmitterLocal;
			emitters.Append( def );
		}
		def = emitters[ index ];
		def->Clear();
		def->index = index;
		def->removeStatus = REMOVE_STATUS_ALIVE;
		def->soundWorld = this;
		break;
	case SCMD_FREE:
		{
			int	immediate;

			readDemo->ReadInt( index );
			readDemo->ReadInt( immediate );
			EmitterForIndex( index )->Free( immediate != 0 );
		}
		break;
	case SCMD_UPDATE:
		{
			idVec3 origin;
			int listenerId;
			soundShaderParms_t parms;

			readDemo->ReadInt( index );
			readDemo->ReadVec3( origin );
			readDemo->ReadInt( listenerId );
			readDemo->ReadFloat( parms.minDistance );
			readDemo->ReadFloat( parms.maxDistance );
			readDemo->ReadFloat( parms.volume );
			readDemo->ReadFloat( parms.shakes );
			readDemo->ReadInt( parms.soundShaderFlags );
			readDemo->ReadInt( parms.soundClass );
			EmitterForIndex( index )->UpdateEmitter( origin, listenerId, &parms );
		}
		break;
	case SCMD_START:
		{
			const idSoundShader *shader;
			int			channel;
			float		diversity;
			int			shaderFlags;

			readDemo->ReadInt( index );
			shader = declManager->FindSound( readDemo->ReadHashString() );
			readDemo->ReadInt( channel );
			readDemo->ReadFloat( diversity );
			readDemo->ReadInt( shaderFlags );
			EmitterForIndex( index )->StartSound( shader, (s_channelType)channel, diversity, shaderFlags );
		}
		break;
	case SCMD_MODIFY:
		{
			int		channel;
			soundShaderParms_t parms;

			readDemo->ReadInt( index );
			readDemo->ReadInt( channel );
			readDemo->ReadFloat( parms.minDistance );
			readDemo->ReadFloat( parms.maxDistance );
			readDemo->ReadFloat( parms.volume );
			readDemo->ReadFloat( parms.shakes );
			readDemo->ReadInt( parms.soundShaderFlags );
			readDemo->ReadInt( parms.soundClass );
			EmitterForIndex( index )->ModifySound( (s_channelType)channel, &parms );
		}
		break;
	case SCMD_STOP:
		{
			int		channel;

			readDemo->ReadInt( index );
			readDemo->ReadInt( channel );
			EmitterForIndex( index )->StopSound( (s_channelType)channel );
		}
		break;
	case SCMD_FADE:
		{
			int		channel;
			float	to, over;

			readDemo->ReadInt( index );
			readDemo->ReadInt( channel );
			readDemo->ReadFloat( to );
			readDemo->ReadFloat( over );
			EmitterForIndex( index )->FadeSound((s_channelType)channel, to, over );
		}
		break;
	}
}

/*
===================
idSoundWorldLocal::CurrentShakeAmplitudeForPosition

  this is called from the main thread
===================
*/
float idSoundWorldLocal::CurrentShakeAmplitudeForPosition( const int time, const idVec3 &listererPosition ) {
	float amp = 0.0f;
	int localTime;

	if ( idSoundSystemLocal::s_constantAmplitude.GetFloat() >= 0.0f ) {
		return 0.0f;
	}

	localTime = soundSystemLocal.GetCurrentSampleTime();

	for ( int i = 1; i < emitters.Num(); i++ ) {
		idSoundEmitterLocal *sound = emitters[i];
		if ( !sound->hasShakes ) {
			continue;
		}
		amp += FindAmplitude( sound, localTime, &listererPosition, SCHANNEL_ANY, true );
	}
	return amp;
}

/*
===================
idSoundWorldLocal::MixLoop

Sum all sound contributions into finalMixBuffer, an unclamped float buffer holding
all output channels.  MIXBUFFER_SAMPLES samples will be created, with each sample consisting
of 2 floats (the libretro core is stereo-only). numFrames is the variable
per-retro_run block size; the mix destination is either the float output
buffer (float mode, gains pre-normalized to [-1,1]) or the int32
accumulator (s16 mode) - AddChannelContribution dispatches on the sound
system's negotiated output format.

called once per retro_run from MixFrameFloat/MixFrameS16
===================
*/
void idSoundWorldLocal::MixLoop( int currentSampleTime, int numFrames, float *finalMixBuffer ) {
	int i, j;
	idSoundEmitterLocal *sound;

	// if noclip flying outside the world, leave silence
	if ( listenerArea == -1 ) {
			return;
	}


	// debugging option to mute all but a single soundEmitter
	// environmental reverb: pick the preset for the listener's portal area
	// (area number -> area name -> "default", same chain as the OpenAL era)
	const bool reverbOn = idSoundSystemLocal::s_useReverb.GetBool()
	                       && soundSystemLocal.efxloaded && listenerArea >= 0;
	if ( reverbOn && ( listenerArea != reverbCachedArea
	                   || reverbCachedGen != soundSystemLocal.efxGeneration ) ) {
		/*
		   Resolve the preset only when the listener's area changes. This
		   ran unconditionally per mix block - an idStr construction plus
		   up to three linear name searches through the EFX database, 60
		   times a second, for a result that can only change when
		   listenerArea does (PlaceListener refreshes listenerArea and
		   listenerAreaName together, and the database is immutable after
		   level load). reverbCachedArea is reset alongside efxloaded so a
		   reloaded database re-resolves.
		*/
		idStr s( listenerArea );
		sndReverbParams_t rp;
		bool found = soundSystemLocal.EFXDatabase.FindEffect( s, &rp );
		if ( !found ) {
			s = listenerAreaName;
			found = soundSystemLocal.EFXDatabase.FindEffect( s, &rp );
		}
		if ( !found ) {
			s = "default";
			found = soundSystemLocal.EFXDatabase.FindEffect( s, &rp );
		}
		if ( found && reverbEffectName != s ) {
			reverbEffectName = s;
			reverb.SetParams( rp );
		}
		reverbCachedArea = listenerArea;
		reverbCachedGen = soundSystemLocal.efxGeneration;
	}
	// clear the mono send accumulator for this block (format-appropriate)
	if ( soundSystemLocal.outputIsFloat ) {
		memset( reverbSendF, 0, numFrames * sizeof( float ) );
	} else {
		memset( reverbSendI, 0, numFrames * sizeof( int ) );
	}

	if ( idSoundSystemLocal::s_singleEmitter.GetInteger() > 0 && idSoundSystemLocal::s_singleEmitter.GetInteger() < emitters.Num() ) {
		sound = emitters[idSoundSystemLocal::s_singleEmitter.GetInteger()];

		if ( sound && sound->playing ) {
			// run through all the channels
			for ( j = 0; j < SOUND_MAX_CHANNELS ; j++ ) {
				idSoundChannel	*chan = &sound->channels[j];

				// see if we have a sound triggered on this channel
				if ( !chan->triggerState ) {
					continue;
				}

				AddChannelContribution( sound, chan, currentSampleTime, numFrames, finalMixBuffer );
			}
		}
		return;
	}

	for ( i = 1; i < emitters.Num(); i++ ) {
		sound = emitters[i];

		if ( !sound ) {
			continue;
		}
		// if no channels are active, do nothing
		if ( !sound->playing ) {
			continue;
		}
		// run through all the channels
		for ( j = 0; j < SOUND_MAX_CHANNELS ; j++ ) {
			idSoundChannel	*chan = &sound->channels[j];

			// see if we have a sound triggered on this channel
			if ( !chan->triggerState ) {
				continue;
			}

			AddChannelContribution( sound, chan, currentSampleTime, numFrames, finalMixBuffer );
		}
	}



	// environmental reverb: process the accumulated mono send and add the
	// stereo wet into this block, in whichever format the mixer is running
	if ( reverbOn && reverb.IsActive() ) {
		const float wet = idSoundSystemLocal::s_reverbGain.GetFloat();
		if ( soundSystemLocal.outputIsFloat ) {
			reverb.ProcessFloat( reverbSendF, finalMixBuffer, numFrames, wet );
		} else {
			reverb.ProcessS16( reverbSendI, (int *)finalMixBuffer, numFrames, wet );
		}
	}

	/*
	   Enviro-suit muffling (ROE): the suit sits over the listener's ears,
	   so it processes the final mix after the reverb - the room's wet
	   energy muffles with everything else. The old call sat before the
	   reverb existed and was hard-disabled behind 'if ( false && ... )'
	   with a port-me note; this is that port.
	*/
	if ( enviroSuitActive ) {
		enviroFX.SetParms( idSoundSystemLocal::s_enviroSuitCutoffFreq.GetFloat(),
		                   idSoundSystemLocal::s_enviroSuitCutoffQ.GetFloat(),
		                   idSoundSystemLocal::s_enviroSuitVolumeScale.GetFloat(),
		                   idSoundSystemLocal::s_reverbTime.GetFloat(),
		                   idSoundSystemLocal::s_reverbFeedback.GetFloat() );
		if ( soundSystemLocal.outputIsFloat ) {
			enviroFX.ProcessFloat( finalMixBuffer, numFrames );
		} else {
			enviroFX.ProcessS16( (int *)finalMixBuffer, numFrames );
		}
	}
}

//==============================================================================

//==============================================================================


/*
===================
idSoundWorldLocal::ResolveOrigin

Find out of the sound is completely occluded by a closed door portal, or
the virtual sound origin position at the portal closest to the listener.
  this is called by the main thread

dist is the distance from the orignial sound origin to the current portal that enters soundArea
def->distance is the distance we are trying to reduce.

If there is no path through open portals from the sound to the listener, def->distance will remain
set at maxDistance
===================
*/
static const int MAX_PORTAL_TRACE_DEPTH = 10;

void idSoundWorldLocal::ResolveOrigin( const int stackDepth, const soundPortalTrace_t *prevStack, const int soundArea, const float dist, const idVec3& soundOrigin, idSoundEmitterLocal *def ) {

	if ( dist >= def->distance ) {
		// we can't possibly hear the sound through this chain of portals
		return;
	}

	if ( soundArea == listenerArea ) {
		float	fullDist = dist + (soundOrigin - listenerQU).LengthFast();
		if ( fullDist < def->distance ) {
			def->distance = fullDist;
			def->spatializedOrigin = soundOrigin;
		}
		return;
	}

	if ( stackDepth == MAX_PORTAL_TRACE_DEPTH ) {
		// don't spend too much time doing these calculations in big maps
		return;
	}

	soundPortalTrace_t newStack;
	newStack.portalArea = soundArea;
	newStack.prevStack = prevStack;

	int numPortals = rw->NumPortalsInArea( soundArea );
	for( int p = 0; p < numPortals; p++ ) {
		exitPortal_t re = rw->GetPortal( soundArea, p );

		float	occlusionDistance = 0;

		// air blocking windows will block sound like closed doors
		if ( (re.blockingBits & ( PS_BLOCK_VIEW | PS_BLOCK_AIR ) ) ) {
			// we could just completely cut sound off, but reducing the volume works better
			// continue;
			occlusionDistance = idSoundSystemLocal::s_doorDistanceAdd.GetFloat();
		}

		// what area are we about to go look at
		int otherArea = re.areas[0];
		if ( re.areas[0] == soundArea ) {
			otherArea = re.areas[1];
		}

		// if this area is already in our portal chain, don't bother looking into it
		const soundPortalTrace_t *prev;
		for ( prev = prevStack ; prev ; prev = prev->prevStack ) {
			if ( prev->portalArea == otherArea )
				break;
		}
		if ( prev )
			continue;

		// pick a point on the portal to serve as our virtual sound origin
		idVec3	source;

		idPlane	pl;
		re.w->GetPlane( pl );

		float	scale;
		idVec3	dir = listenerQU - soundOrigin;
		if ( !pl.RayIntersection( soundOrigin, dir, scale ) ) {
			source = re.w->GetCenter();
		} else {
			source = soundOrigin + scale * dir;

			// if this point isn't inside the portal edges, slide it in
			for ( int i = 0 ; i < re.w->GetNumPoints() ; i++ ) {
				int j = ( i + 1 ) % re.w->GetNumPoints();
				idVec3	edgeDir = (*(re.w))[j].ToVec3() - (*(re.w))[i].ToVec3();
				idVec3	edgeNormal;

				edgeNormal.Cross( pl.Normal(), edgeDir );

				idVec3	fromVert = source - (*(re.w))[j].ToVec3();

				float	d = edgeNormal * fromVert;
				if ( d > 0 ) {
					// move it in
					float div = edgeNormal.Normalize();
					d /= div;

					source -= d * edgeNormal;
				}
			}
		}

		idVec3 tlen = source - soundOrigin;
		float tlenLength = tlen.LengthFast();

		ResolveOrigin( stackDepth+1, &newStack, otherArea, dist+tlenLength+occlusionDistance, source, def );
	}
}


/*
===================
idSoundWorldLocal::PlaceListener

  this is called by the main thread
===================
*/
void idSoundWorldLocal::PlaceListener( const idVec3& origin, const idMat3& axis,
									const int listenerId, const int gameTime, const idStr& areaName  ) {

	int currentSampleTime;

	if ( !soundSystemLocal.isInitialized ) {
		return;
	}

	if ( pauseSampleTime >= 0 ){
		return;
	}

	if ( writeDemo ) {
		writeDemo->WriteInt( DS_SOUND );
		writeDemo->WriteInt( SCMD_PLACE_LISTENER );
		writeDemo->WriteVec3( origin );
		writeDemo->WriteMat3( axis );
		writeDemo->WriteInt( listenerId );
		writeDemo->WriteInt( gameTime );
	}

	currentSampleTime = soundSystemLocal.GetCurrentSampleTime();

	// we usually expect gameTime to be increasing by 16 or 32 msec, but when
	// a cinematic is fast-forward skipped through, it can jump by a significant
	// amount, while the output sample position will not have changed accordingly,
	// which would make sounds (like long character speaches) continue from the
	// old time.  Fix this by killing all non-looping sounds
	if ( gameTime > gameMsec + 500 ) {
		OffsetSoundTime( - ( gameTime - gameMsec ) * 0.001f * (float)snd_SampleRate() );
	}

	gameMsec = gameTime;
	// the normal 16 msec / frame
	gameSampleTime = idMath::FtoiFast( gameMsec * 0.001f * (float)snd_SampleRate() );

	listenerPrivateId = listenerId;

	listenerQU = origin;							// Doom units
	listenerPos = origin * DOOM_TO_METERS;			// meters
	listenerAxis = axis;
	listenerAreaName = areaName;
	listenerAreaName.ToLower();

	if ( rw ) {
		listenerArea = rw->PointInArea( listenerQU );	// where are we?
	} else {
		listenerArea = 0;
	reverbCachedArea = -2;		// no area resolved yet; -1 is the valid "outside world" value
	reverbCachedGen = -1;
	}

	if ( listenerArea < 0 ) {
		return;
	}

	ForegroundUpdate( currentSampleTime );
}

/*
==================
idSoundWorldLocal::ForegroundUpdate
==================
*/
void idSoundWorldLocal::ForegroundUpdate( int currentSampleTime ) {
	int j, k;
	idSoundEmitterLocal	*def;

	if ( !soundSystemLocal.isInitialized ) {
		return;
	}

	Sys_EnterCriticalSection();

	//
	// check to see if each sound is visible or not
	// speed up by checking maxdistance to origin
	// although the sound may still need to play if it has
	// just become occluded so it can ramp down to 0
	//
	for ( j = 1; j < emitters.Num(); j++ ) {
		def = emitters[j];

		if ( def->removeStatus >= REMOVE_STATUS_SAMPLEFINISHED ) {
			continue;
		}

		// see if our last channel just finished
		def->CheckForCompletion( currentSampleTime );

		if ( !def->playing ) {
			continue;
		}

		// update virtual origin / distance, etc
		def->Spatialize( listenerPos, listenerArea, rw );

		// per-sound debug options
		if ( idSoundSystemLocal::s_drawSounds.GetInteger() && rw ) {
			if ( def->distance < def->maxDistance || idSoundSystemLocal::s_drawSounds.GetInteger() > 1 ) {
				idBounds ref;
				ref.Clear();
				ref.AddPoint( idVec3( -10, -10, -10 ) );
				ref.AddPoint( idVec3(  10,  10,  10 ) );
				float vis = (1.0f - (def->distance / def->maxDistance));

				// draw a box
				rw->DebugBounds( idVec4( vis, 0.25f, vis, vis ), ref, def->origin );

				// draw an arrow to the audible position, possible a portal center
				if ( def->origin != def->spatializedOrigin ) {
					rw->DebugArrow( colorRed, def->origin, def->spatializedOrigin, 4 );
				}

				// draw the index
				idVec3	textPos = def->origin;
				textPos[2] -= 8;
				rw->DrawText( va("%i", def->index), textPos, 0.1f, idVec4(1,0,0,1), listenerAxis );
				textPos[2] += 8;

				// run through all the channels
				for ( k = 0; k < SOUND_MAX_CHANNELS ; k++ ) {
					idSoundChannel	*chan = &def->channels[k];

					// see if we have a sound triggered on this channel
					if ( !chan->triggerState ) {
						continue;
					}

					char	text[1024];
					float	min = chan->parms.minDistance;
					float	max = chan->parms.maxDistance;
					const char	*defaulted = chan->leadinSample->defaultSound ? "(DEFAULTED)" : "";
					sprintf( text, "%s (%i/%i %i/%i)%s", chan->soundShader->GetName(), (int)def->distance,
						(int)def->realDistance, (int)min, (int)max, defaulted );
					rw->DrawText( text, textPos, 0.1f, idVec4(1,0,0,1), listenerAxis );
					textPos[2] += 8;
				}
			}
		}
	}

	Sys_LeaveCriticalSection();
}

/*
===================
idSoundWorldLocal::OffsetSoundTime
===================
*/
void idSoundWorldLocal::OffsetSoundTime( int offsetSamples ) {
	int i, j;

	for ( i = 0; i < emitters.Num(); i++ ) {
		if ( emitters[i] == NULL ) {
			continue;
		}
		for ( j = 0; j < SOUND_MAX_CHANNELS; j++ ) {
			idSoundChannel *chan = &emitters[i]->channels[ j ];

			if ( !chan->triggerState ) {
				continue;
			}

			chan->triggerSampleTime += offsetSamples;
		}
	}
}

/*
===================
idSoundWorldLocal::WriteToSaveGame
===================
*/
void idSoundWorldLocal::WriteToSaveGame( idFile *savefile ) {
	int i, j, num, currentSoundTime;
	const char *name;

	// the game soundworld is always paused at this point, save that time down
	if ( pauseSampleTime > 0 ) {
		currentSoundTime = pauseSampleTime;
	} else {
		currentSoundTime = soundSystemLocal.GetCurrentSampleTime();
	}

	// write listener data
	savefile->WriteVec3(listenerQU);
	savefile->WriteMat3(listenerAxis);
	savefile->WriteInt(listenerPrivateId);
	savefile->WriteInt(gameMsec);
	savefile->WriteInt(gameSampleTime);
	savefile->WriteInt(currentSoundTime);

	num = emitters.Num();
	savefile->WriteInt(num);

	for ( i = 1; i < emitters.Num(); i++ ) {
		idSoundEmitterLocal *def = emitters[i];

		if ( def->removeStatus != REMOVE_STATUS_ALIVE ) {
			int skip = -1;
			savefile->Write( &skip, sizeof( skip ) );
			continue;
		}

		savefile->WriteInt(i);

		// Write the emitter data
		savefile->WriteVec3( def->origin );
		savefile->WriteInt( def->listenerId );
		WriteToSaveGameSoundShaderParams( savefile, &def->parms );
		savefile->WriteFloat( def->amplitude );
		savefile->WriteInt( def->ampTime );
		for (int k = 0; k < SOUND_MAX_CHANNELS; k++)
			WriteToSaveGameSoundChannel( savefile, &def->channels[k] );
		savefile->WriteFloat( def->distance );
		savefile->WriteBool( def->hasShakes );
		savefile->WriteInt( def->lastValidPortalArea );
		savefile->WriteFloat( def->maxDistance );
		savefile->WriteBool( def->playing );
		savefile->WriteFloat( def->realDistance );
		savefile->WriteInt( def->removeStatus );
		savefile->WriteVec3( def->spatializedOrigin );

		// write the channel data
		for( j = 0; j < SOUND_MAX_CHANNELS; j++ ) {
			idSoundChannel *chan = &def->channels[ j ];

			// Write out any sound commands for this def
			if ( chan->triggerState && chan->soundShader && chan->leadinSample ) {

				savefile->WriteInt( j );

				// write the pointers out separately
				name = chan->soundShader->GetName();
				savefile->WriteString( name );

				name = chan->leadinSample->name;
				savefile->WriteString( name );
			}
		}

		// End active channels with -1
		int end = -1;
		savefile->WriteInt( end );
	}

	// new in Doom3 v1.2
	savefile->Write( &slowmoActive, sizeof( slowmoActive ) );
	savefile->Write( &slowmoSpeed, sizeof( slowmoSpeed ) );
	savefile->Write( &enviroSuitActive, sizeof( enviroSuitActive ) );
}

/*
 ===================
 idSoundWorldLocal::WriteToSaveGameSoundShaderParams
 ===================
 */
void idSoundWorldLocal::WriteToSaveGameSoundShaderParams( idFile *saveGame, soundShaderParms_t *params ) {
	saveGame->WriteFloat(params->minDistance);
	saveGame->WriteFloat(params->maxDistance);
	saveGame->WriteFloat(params->volume);
	saveGame->WriteFloat(params->shakes);
	saveGame->WriteInt(params->soundShaderFlags);
	saveGame->WriteInt(params->soundClass);
}

/*
 ===================
 idSoundWorldLocal::WriteToSaveGameSoundChannel
 ===================
 */
void idSoundWorldLocal::WriteToSaveGameSoundChannel( idFile *saveGame, idSoundChannel *ch ) {
	saveGame->WriteBool( ch->triggerState );
	saveGame->WriteUnsignedChar( 0 );
	saveGame->WriteUnsignedChar( 0 );
	saveGame->WriteUnsignedChar( 0 );
	saveGame->WriteInt( ch->triggerSampleTime );
	saveGame->WriteInt( ch->triggerGameSampleTime );
	WriteToSaveGameSoundShaderParams( saveGame, &ch->parms );
	saveGame->WriteInt( 0 /* ch->leadinSample */ );
	saveGame->WriteInt( ch->triggerChannel );
	saveGame->WriteInt( 0 /* ch->soundShader */ );
	saveGame->WriteInt( 0 /* ch->decoder */ );
	saveGame->WriteFloat(ch->diversity );
	saveGame->WriteFloat(ch->lastVolume );
	for (int m = 0; m < 6; m++)
		saveGame->WriteFloat( ch->lastV[m] );
	saveGame->WriteInt( ch->channelFade.fadeStartSample );
	saveGame->WriteInt( ch->channelFade.fadeEndSample );
	saveGame->WriteFloat( ch->channelFade.fadeStartVolume );
	saveGame->WriteFloat( ch->channelFade.fadeEndVolume );
}

/*
===================
idSoundWorldLocal::ReadFromSaveGame
===================
*/
void idSoundWorldLocal::ReadFromSaveGame( idFile *savefile ) {
	int i, num, handle, listenerId, gameTime, channel;
	int savedSoundTime, currentSoundTime, soundTimeOffset;
	idSoundEmitterLocal *def;
	idVec3 origin;
	idMat3 axis;
	idStr soundShader;

	ClearAllSoundEmitters();

	savefile->ReadVec3( origin );
	savefile->ReadMat3( axis );
	savefile->ReadInt( listenerId );
	savefile->ReadInt( gameTime );
	savefile->ReadInt( gameSampleTime );
	savefile->ReadInt( savedSoundTime );

	// we will adjust the sound starting times from those saved with the demo
	currentSoundTime = soundSystemLocal.GetCurrentSampleTime();
	soundTimeOffset = currentSoundTime - savedSoundTime;

	// at the end of the level load we unpause the sound world and adjust the sound starting times once more
	pauseSampleTime = currentSoundTime;

	// place listener
	PlaceListener( origin, axis, listenerId, gameTime, "Undefined" );

	// make sure there are enough
	// slots to read the saveGame in.  We don't shrink the list
	// if there are extras.
	savefile->ReadInt( num );

	while( emitters.Num() < num ) {
		def = new idSoundEmitterLocal;
		def->index = emitters.Append( def );
		def->soundWorld = this;
	}

	// read in the state
	for ( i = 1; i < num; i++ ) {

		savefile->ReadInt( handle );
		if ( handle < 0 ) {
			continue;
		}
		if ( handle != i ) {
			common->Error( "idSoundWorldLocal::ReadFromSaveGame: index mismatch" );
		}
		def = emitters[i];

		def->removeStatus = REMOVE_STATUS_ALIVE;
		def->playing = true;		// may be reset by the first UpdateListener

		savefile->ReadVec3( def->origin );
		savefile->ReadInt( def->listenerId );
		ReadFromSaveGameSoundShaderParams( savefile, &def->parms );
		savefile->ReadFloat( def->amplitude );
		savefile->ReadInt( def->ampTime );
		for (int k = 0; k < SOUND_MAX_CHANNELS; k++)
			ReadFromSaveGameSoundChannel( savefile, &def->channels[k] );
		savefile->ReadFloat( def->distance );
		savefile->ReadBool( def->hasShakes );
		savefile->ReadInt( def->lastValidPortalArea );
		savefile->ReadFloat( def->maxDistance );
		savefile->ReadBool( def->playing );
		savefile->ReadFloat( def->realDistance );
		savefile->ReadInt( (int&)def->removeStatus );
		savefile->ReadVec3( def->spatializedOrigin );

		// read the individual channels
		savefile->ReadInt( channel );

		while ( channel >= 0 ) {
			if ( channel > SOUND_MAX_CHANNELS ) {
				common->Error( "idSoundWorldLocal::ReadFromSaveGame: channel > SOUND_MAX_CHANNELS" );
			}

			idSoundChannel *chan = &def->channels[channel];

			if ( !chan->decoder ) {
				// The pointer in the save file is not valid, so we grab a new one
				chan->decoder = idSampleDecoder::Alloc();
			}

			savefile->ReadString( soundShader );
			chan->soundShader = declManager->FindSound( soundShader );

			savefile->ReadString( soundShader );
			// load savegames with s_noSound 1
			if ( soundSystemLocal.soundCache ) {
				chan->leadinSample = soundSystemLocal.soundCache->FindSound( soundShader, false );
			} else {
				chan->leadinSample = NULL;
			}

			// adjust the hardware start time
			chan->triggerSampleTime += soundTimeOffset;

			// make sure the channel restarts mixing
			chan->triggered = chan->triggerState;

			// adjust the hardware fade time
			if ( chan->channelFade.fadeStartSample != 0 ) {
				chan->channelFade.fadeStartSample += soundTimeOffset;
				chan->channelFade.fadeEndSample += soundTimeOffset;
			}

			// next command
			savefile->ReadInt( channel );
		}
	}

	if ( session->GetSaveGameVersion() >= 17 ) {
		savefile->Read( &slowmoActive, sizeof( slowmoActive ) );
		savefile->Read( &slowmoSpeed, sizeof( slowmoSpeed ) );
		savefile->Read( &enviroSuitActive, sizeof( enviroSuitActive ) );
	} else {
		slowmoActive		= false;
		slowmoSpeed			= 0;
		enviroSuitActive	= false;
	}
}

/*
 ===================
 idSoundWorldLocal::ReadFromSaveGameSoundShaderParams
 ===================
 */
void idSoundWorldLocal::ReadFromSaveGameSoundShaderParams( idFile *saveGame, soundShaderParms_t *params ) {
	saveGame->ReadFloat(params->minDistance);
	saveGame->ReadFloat(params->maxDistance);
	saveGame->ReadFloat(params->volume);
	saveGame->ReadFloat(params->shakes);
	saveGame->ReadInt(params->soundShaderFlags);
	saveGame->ReadInt(params->soundClass);
}

/*
 ===================
 idSoundWorldLocal::ReadFromSaveGameSoundChannel
 ===================
 */
void idSoundWorldLocal::ReadFromSaveGameSoundChannel( idFile *saveGame, idSoundChannel *ch ) {
	saveGame->ReadBool( ch->triggerState );
	char tmp;
	int i;
	saveGame->ReadChar( tmp );
	saveGame->ReadChar( tmp );
	saveGame->ReadChar( tmp );
	saveGame->ReadInt( ch->triggerSampleTime );
	saveGame->ReadInt( ch->triggerGameSampleTime );
	ReadFromSaveGameSoundShaderParams( saveGame, &ch->parms );
	saveGame->ReadInt( i );
	ch->leadinSample = NULL;
	saveGame->ReadInt( ch->triggerChannel );
	saveGame->ReadInt( i );
	ch->soundShader = NULL;
	saveGame->ReadInt( i );
	ch->decoder = NULL;
	saveGame->ReadFloat(ch->diversity );
	saveGame->ReadFloat(ch->lastVolume );
	for (int m = 0; m < 6; m++)
		saveGame->ReadFloat( ch->lastV[m] );
	saveGame->ReadInt( ch->channelFade.fadeStartSample );
	saveGame->ReadInt( ch->channelFade.fadeEndSample );
	saveGame->ReadFloat( ch->channelFade.fadeStartVolume );
	saveGame->ReadFloat( ch->channelFade.fadeEndVolume );
}

/*
===================
idSoundWorldLocal::EmitterForIndex
===================
*/
idSoundEmitter	*idSoundWorldLocal::EmitterForIndex( int index ) {
	if ( index == 0 ) {
		return NULL;
	}
	if ( index >= emitters.Num() ) {
		common->Error( "idSoundWorldLocal::EmitterForIndex: %i > %i", index, emitters.Num() );
	}
	return emitters[index];
}

/*
===============
idSoundWorldLocal::StopAllSounds

  this is called from the main thread
===============
*/
void idSoundWorldLocal::StopAllSounds() {

	for ( int i = 0; i < emitters.Num(); i++ ) {
		idSoundEmitterLocal * def = emitters[i];
		def->StopSound( SCHANNEL_ANY );
	}
}

/*
===============
idSoundWorldLocal::Pause
===============
*/
void idSoundWorldLocal::Pause( void ) {
	if ( pauseSampleTime >= 0 ) {
		common->Warning( "idSoundWorldLocal::Pause: already paused" );
		return;
	}

	pauseSampleTime = soundSystemLocal.GetCurrentSampleTime();

	for ( int i = 0; i < emitters.Num(); i++ ) {
		idSoundEmitterLocal * emitter = emitters[i];

		// if no channels are active, do nothing
		if ( emitter == NULL || !emitter->playing ) {
			continue;
		}
		emitter->PauseAll();
	}
}

/*
===============
idSoundWorldLocal::UnPause
===============
*/
void idSoundWorldLocal::UnPause( void ) {
	int offsetSamples;

	if ( pauseSampleTime < 0 ) {
		common->Warning( "idSoundWorldLocal::UnPause: not paused" );
		return;
	}

	offsetSamples = soundSystemLocal.GetCurrentSampleTime() - pauseSampleTime;
	OffsetSoundTime( offsetSamples );

	pauseSampleTime = -1;

	for ( int i = 0; i < emitters.Num(); i++ ) {
		idSoundEmitterLocal * emitter = emitters[i];

		// if no channels are active, do nothing
		if ( emitter == NULL || !emitter->playing ) {
			continue;
		}
		emitter->UnPauseAll();
	}
}

/*
===============
idSoundWorldLocal::IsPaused
===============
*/
bool idSoundWorldLocal::IsPaused( void ) {
	return ( pauseSampleTime >= 0 );
}

/*
===============
idSoundWorldLocal::PlayShaderDirectly

start a music track

  this is called from the main thread
===============
*/
void idSoundWorldLocal::PlayShaderDirectly( const char *shaderName, int channel ) {

	if ( localSound && channel == -1 ) {
		localSound->StopSound( SCHANNEL_ANY );
	} else if ( localSound ) {
		localSound->StopSound( channel );
	}

	if ( !shaderName || !shaderName[0] ) {
		return;
	}

	const idSoundShader *shader = declManager->FindSound( shaderName );
	if ( !shader ) {
		return;
	}

	if ( !localSound ) {
		localSound = AllocLocalSoundEmitter();
	}

	/*
	   Was a function-local static idRandom, which survived nothing: a
	   savestate restore resumed the boot-fresh sequence, so the next
	   music/GUI trigger's diversity - which selects the shader entry
	   and sample offset - diverged from an uninterrupted run. As a
	   world member its seed rides in the DSP savestate section (v4).
	*/
	float	diversity = playShaderRnd.RandomFloat();

	localSound->StartSound( shader, ( channel == -1 ) ? SCHANNEL_ONE : channel , diversity, SSF_GLOBAL );

	// in case we are at the console without a game doing updates, force an update
	ForegroundUpdate( soundSystemLocal.GetCurrentSampleTime() );
}

/*
===============
idSoundWorldLocal::CalcEars

Determine the volumes from each speaker for a given sound emitter
===============
*/
void idSoundWorldLocal::CalcEars( int numSpeakers, idVec3 spatializedOrigin, idVec3 listenerPos,
								 idMat3 listenerAxis, float ears[6], float spatialize ) {
	idVec3 svec = spatializedOrigin - listenerPos;
	idVec3 ovec;

	ovec[0] = svec * listenerAxis[0];
	ovec[1] = svec * listenerAxis[1];
	ovec[2] = svec * listenerAxis[2];

	ovec.Normalize();

	if ( numSpeakers == 6 ) {
		static idVec3	speakerVector[6] = {
			idVec3(  0.707f,  0.707f, 0.0f ),	// front left
			idVec3(  0.707f, -0.707f, 0.0f ),	// front right
			idVec3(  0.707f,  0.0f,   0.0f ),	// front center
			idVec3(  0.0f,    0.0f,   0.0f ),	// sub
			idVec3( -0.707f,  0.707f, 0.0f ),	// rear left
			idVec3( -0.707f, -0.707f, 0.0f )	// rear right
		};
		for ( int i = 0 ; i < 6 ; i++ ) {
			if ( i == 3 ) {
				ears[i] = idSoundSystemLocal::s_subFraction.GetFloat();		// subwoofer
				continue;
			}
			float dot = ovec * speakerVector[i];
			ears[i] = (idSoundSystemLocal::s_dotbias6.GetFloat() + dot) / ( 1.0f + idSoundSystemLocal::s_dotbias6.GetFloat() );
			if ( ears[i] < idSoundSystemLocal::s_minVolume6.GetFloat() ) {
				ears[i] = idSoundSystemLocal::s_minVolume6.GetFloat();
			}
		}
	} else {
		float dot = ovec.y;
		float dotBias = idSoundSystemLocal::s_dotbias2.GetFloat();

		// when we are inside the minDistance, start reducing the amount of spatialization
		// so NPC voices right in front of us aren't quieter that off to the side
		dotBias += ( idSoundSystemLocal::s_spatializationDecay.GetFloat() - dotBias ) * ( 1.0f - spatialize );

		ears[0] = (idSoundSystemLocal::s_dotbias2.GetFloat() + dot) / ( 1.0f + dotBias );
		ears[1] = (idSoundSystemLocal::s_dotbias2.GetFloat() - dot) / ( 1.0f + dotBias );

		if ( ears[0] < idSoundSystemLocal::s_minVolume2.GetFloat() ) {
			ears[0] = idSoundSystemLocal::s_minVolume2.GetFloat();
		}
		if ( ears[1] < idSoundSystemLocal::s_minVolume2.GetFloat() ) {
			ears[1] = idSoundSystemLocal::s_minVolume2.GetFloat();
		}

		ears[2] =
		ears[3] =
		ears[4] =
		ears[5] = 0.0f;
	}
}

/*
   One-pole alpha for the 5 kHz statistical-HF shelves (occlusion, air
   absorption). The rate is fixed for the session; recompute only if it
   ever changes so the expf runs once, not per channel per block.
*/
static float Snd_Shelf5kAlpha( void ) {
	static float alpha; static int lastRate;
	int rate = snd_SampleRate();
	if ( rate != lastRate ) {
		lastRate = rate;
		alpha = 1.0f - expf( -6.2831853f * 5000.0f / rate );
	}
	return alpha;
}

/*
===============
idSoundWorldLocal::AddChannelContribution

Adds the contribution of a single sound channel to finalMixBuffer
this is called from the async thread

Mixes MIXBUFFER_SAMPLES samples starting at currentSampleTime sample time into
finalMixBuffer
===============
*/
void idSoundWorldLocal::AddChannelContribution( idSoundEmitterLocal *sound, idSoundChannel *chan,
				   int currentSampleTime, int numFrames, float *finalMixBuffer ) {
	int j;
	float volume;

	//
	// get the sound definition and parameters from the entity
	//
	soundShaderParms_t *parms = &chan->parms;

	// assume we have a sound triggered on this channel
	assert( chan->triggerState );

	// fetch the actual wave file and see if it's valid
	idSoundSample *sample = chan->leadinSample;
	if ( sample == NULL ) {
		return;
	}

	// if you don't want to hear all the beeps from missing sounds
	if ( sample->defaultSound && !idSoundSystemLocal::s_playDefaultSound.GetBool() ) {
		return;
	}

	// get the actual shader
	const idSoundShader *shader = chan->soundShader;

	// this might happen if the foreground thread just deleted the sound emitter
	if ( !shader ) {
		return;
	}

	float maxd = parms->maxDistance;
	float mind = parms->minDistance;

	int  mask = shader->speakerMask;
	bool omni = ( parms->soundShaderFlags & SSF_OMNIDIRECTIONAL) != 0;
	bool global = ( parms->soundShaderFlags & SSF_GLOBAL ) != 0;
	bool noOcclusion = ( parms->soundShaderFlags & SSF_NO_OCCLUSION ) || !idSoundSystemLocal::s_useOcclusion.GetBool();

	// speed goes from 1 to 0.2
	if ( idSoundSystemLocal::s_slowAttenuate.GetBool() && slowmoActive && !chan->disallowSlow ) {
		maxd *= slowmoSpeed;
	}

	// stereo samples are always omni
	if ( sample->objectInfo.nChannels == 2 ) {
		omni = true;
	}

	// if the sound is playing from the current listener, it will not be spatialized at all
	if ( sound->listenerId == listenerPrivateId ) {
		global = true;
	}

	//
	// see if it's in range
	//

	// convert volumes from decibels to float scale

	// leadin volume scale for shattering lights
	// this isn't exactly correct, because the modified volume will get applied to
	// some initial chunk of the loop as well, because the volume is scaled for the
	// entire mix buffer
	if ( shader->leadinVolume && currentSampleTime - chan->triggerSampleTime < sample->LengthInOutputSamples() ) {
		volume = soundSystemLocal.dB2Scale( shader->leadinVolume );
	} else {
		volume = soundSystemLocal.dB2Scale( parms->volume );
	}

	// DG: moved global volume scale down to after clamping to 1.0

	// volume fading
	float	fadeDb = chan->channelFade.FadeDbAtSampleTime( currentSampleTime );
	volume *= soundSystemLocal.dB2Scale( fadeDb );

	fadeDb = soundClassFade[parms->soundClass].FadeDbAtSampleTime( currentSampleTime );
	volume *= soundSystemLocal.dB2Scale( fadeDb );


	//
	// if it's a global sound then
	// it's not affected by distance or occlusion
	//
	float	spatialize = 1;
	float	sendDist = 0.0f;	// meters; stays 0 for global sounds
	idVec3 spatializedOriginInMeters;
	if ( !global ) {
		float	dlen;

		if ( noOcclusion ) {
			// use the real origin and distance
			spatializedOriginInMeters = sound->origin * DOOM_TO_METERS;
			dlen = sound->realDistance;
		} else {
			// use the possibly portal-occluded origin and distance
			spatializedOriginInMeters = sound->spatializedOrigin * DOOM_TO_METERS;
			dlen = sound->distance;
		}

		sendDist = dlen;

		// reduce volume based on distance
		if ( dlen >= maxd ) {
			volume = 0.0f;
		} else if ( dlen > mind ) {
			float frac = idMath::ClampFloat( 0.0f, 1.0f, 1.0f - ((dlen - mind) / (maxd - mind)));
			if ( idSoundSystemLocal::s_quadraticFalloff.GetBool() ) {
				frac *= frac;
			}
			volume *= frac;
		} else if ( mind > 0.0f ) {
			// we tweak the spatialization bias when you are inside the minDistance
			spatialize = dlen / mind;
		}
	}

	//
	// if it is a private sound, set the volume to zero
	// unless we match the listenerId
	//
	if ( parms->soundShaderFlags & SSF_PRIVATE_SOUND ) {
		if ( sound->listenerId != listenerPrivateId ) {
			volume = 0;
		}
	}
	if ( parms->soundShaderFlags & SSF_ANTI_PRIVATE_SOUND ) {
		if ( sound->listenerId == listenerPrivateId ) {
			volume = 0;
		}
	}

	/*
	   Unity clamp BEFORE the master volume scale. This is the balance half
	   of the s_scaleDownAndClamp block removed in c58acc51: many stock
	   shaders author volumes above 0 dB and only balance correctly against
	   each other when they saturate at a common ceiling (dhewm3 #326,
	   dhewm3/dhewm3#326 comment on weapon volumes). The ordering is the
	   point - clamping before s_volume keeps that balance invariant under
	   the master volume, while the post-removal code (s_clipVolumes on the
	   ears, downstream of the s_volume multiply) re-separates overloud
	   shaders as the master comes down: at s_volume_dB -20, a +6 dB and a
	   +12 dB shader differ by 2x again instead of staying pinned equal.

	   SSF_UNCLAMPED shaders opt out, the same convention s_clipVolumes
	   already uses: the flag's authored meaning is over-unity gain, and
	   the output saturator bounds whatever they sum to.

	   The removed block's x0.333 headroom is deliberately NOT resurrected.
	   Summation overload was never an OpenAL artifact - both current
	   pipelines saturate at the final narrow - but a static -9.5 dB pad
	   is the wrong tool: it discards ~1.6 bits of s16 output resolution
	   on every sample and makes nothing bit-transparent. Overload is now
	   handled where the information still exists, at the int32
	   accumulator's narrow to s16 (see Snd_SumToS16Soft).
	*/
	if ( !( parms->soundShaderFlags & SSF_UNCLAMPED ) && volume > 1.0f ) {
		volume = 1.0f;
	}

	// global volume scale - DG: done after clamping to 1.0, so reducing the
	// global volume doesn't cause the different weapon volume issues described above
	volume *= soundSystemLocal.dB2Scale( idSoundSystemLocal::s_volume.GetFloat() );

	//
	// do we have anything to add?
	//
	if ( volume < SND_EPSILON && chan->lastVolume < SND_EPSILON ) {
		return;
	}
	chan->lastVolume = volume;

	//
	// fetch the sound from the cache as output-rate samples
	//
	int offset = currentSampleTime - chan->triggerSampleTime;
	/* static, not stack: 32KB here plus 16KB for srcS16 below, and this is
	   the frame that DecodeOGG is reached from. Single call path on the main
	   thread, not recursive. */
	static float inputSamples[MIXBUFFER_SAMPLES*2+16];
	float *alignedInputSamples = (float *) ( ( ( (intptr_t)inputSamples ) + 15 ) & ~15 );

	if ( true ) {
		const bool stereoSample = ( sample->objectInfo.nChannels == 2 );

		if ( slowmoActive && !chan->disallowSlow ) {
			idSlowChannel slow = sound->GetSlowChannel( chan );

			slow.AttachSoundChannel( chan );

				if ( stereoSample ) {
					// need to add a stereo path, but very few samples go through this
					memset( alignedInputSamples, 0, sizeof( alignedInputSamples[0] ) * numFrames * 2 );
				} else {
					slow.GatherChannelSamples( offset, numFrames, alignedInputSamples );
				}

			sound->SetSlowChannel( chan, slow );
		} else {
			sound->ResetSlowChannel( chan );

			// if we are getting a stereo sample adjust accordingly
			if ( stereoSample ) {
				// we should probably check to make sure any looping is also to a stereo sample...
				chan->GatherChannelSamples( offset*2, numFrames*2, alignedInputSamples );
			} else {
				chan->GatherChannelSamples( offset, numFrames, alignedInputSamples );
			}
		}

		/*
		   Occlusion spectral filtering: when the audible path runs through
		   portals rather than the straight line, the sound arrives muffled
		   as well as attenuated. The volume model already carries the
		   longer distance and s_doorDistanceAdd; this adds the spectral
		   half: a per-channel one-pole shelf at the 5 kHz statistical HF
		   reference over the gathered source, hfGain =
		   s_occlusionGainHF ^ (portal path - straight line, meters),
		   clamped at 0.1. Filtering the source here, before the mix and
		   the reverb send both consume it, muffles the dry voice and the
		   energy it drives into the room alike - the wall stands between
		   the source and everything. At the default 0.8/m a doorway's
		   ~2 m detour passes 64% of the HF; s_occlusionGainHF 1 (or an
		   unoccluded path) takes the exact legacy path, bit for bit.
		   Enhancement beyond the original engine, which occluded volume
		   only.
		*/
		/*
		   Pipeline split, same as the air shelf's airLpF/airLpI: the
		   float pipeline filters the gathered floats here; the s16
		   pipeline defers to an integer shelf over the converted block
		   below (occFilterS16/occHfG), because a float IIR with
		   persistent state would break the s16 pipeline's
		   cross-compiler bit-exactness for occluded channels. The
		   filter runs before both consumers either way - the mix and
		   the reverb send see the same muffled source.
		*/
		bool  occFilterS16 = false;
		float occHfG = 1.0f;
		if ( !global && !stereoSample && !noOcclusion ) {
			float occG = idSoundSystemLocal::s_occlusionGainHF.GetFloat();
			float detour = sound->distance - sound->realDistance;
			if ( occG < 0.9999f && detour > 0.01f ) {
				float hfG;
				if ( occG == chan->occCacheG && detour == chan->occCacheDetour ) {
					hfG = chan->occCacheHfG;	// bit-identical reuse
				} else {
					hfG = powf( occG < 0.1f ? 0.1f : occG, detour );
					if ( hfG < 0.1f ) hfG = 0.1f;
					chan->occCacheG = occG;
					chan->occCacheDetour = detour;
					chan->occCacheHfG = hfG;
				}
				if ( soundSystemLocal.outputIsFloat ) {
					const float alpha = Snd_Shelf5kAlpha();
					float lp = chan->occLpF;
					for ( int k = 0; k < numFrames; k++ ) {
						float x = alignedInputSamples[k];
						lp += alpha * ( x - lp );
						alignedInputSamples[k] = lp + hfG * ( x - lp );
					}
					chan->occLpF = lp;
				} else {
					occFilterS16 = true;
					occHfG = hfG;
				}
			}
		}

		//
		// work out the left / right ear values
		//
		float	ears[6];
		if ( global || omni ) {
			// same for all speakers
			for ( int i = 0 ; i < 6 ; i++ ) {
				ears[i] = idSoundSystemLocal::s_globalFraction.GetFloat() * volume;
			}
			ears[3] = idSoundSystemLocal::s_subFraction.GetFloat() * volume;		// subwoofer

		} else {
			CalcEars( 2, spatializedOriginInMeters, listenerPos, listenerAxis, ears, spatialize );

			for ( int i = 0 ; i < 6 ; i++ ) {
				ears[i] *= volume;
			}
		}

		// if the mask is 0, it really means do every channel
		if ( !mask ) {
			mask = 255;
		}
		// cleared mask bits set the mix volume to zero
		for ( int i = 0 ; i < 6 ; i++ ) {
			if ( !(mask & ( 1 << i ) ) ) {
				ears[i] = 0;
			}
		}

		// if sounds are generally normalized, using a mixing volume over 1.0 will
		// almost always cause clipping noise.  If samples aren't normalized, there
		// is a good call to allow overvolumes
		if ( idSoundSystemLocal::s_clipVolumes.GetBool() && !( parms->soundShaderFlags & SSF_UNCLAMPED )  ) {
			for ( int i = 0 ; i < 6 ; i++ ) {
				if ( ears[i] > 1.0f ) {
					ears[i] = 1.0f;
				}
			}
		}

		// if this is the very first mixing block, set the lastV
		// to the current volume
		if ( currentSampleTime == chan->triggerSampleTime ) {
			for ( j = 0 ; j < 6 ; j++ ) {
				chan->lastV[j] = ears[j];
			}
		}

		//
		// mix into the frame block - stereo only, variable block size,
		// format selected once at load by libretro float-audio negotiation
		//
		/*
		   Declared out here so the reverb send below can reuse the s16
		   conversion the mix already did, instead of running Snd_FloatToS16
		   over the same block a second time. Measured 1.64x on the s16 reverb
		   path (177us -> 108us per frame at 32 active voices).
		*/
		static short srcS16[MIXBUFFER_SAMPLES*2];
		bool haveS16 = false;

		if ( soundSystemLocal.outputIsFloat ) {
			// float pipeline: fold the [-1,1] output normalization into the
			// per-block gains, so the accumulation buffer IS the final
			// float output - no conversion pass at the libretro edge
			const float norm = 1.0f / 32768.0f;
			float lastN[2]    = { chan->lastV[0] * norm, chan->lastV[1] * norm };
			float currentN[2] = { ears[0] * norm,        ears[1] * norm };
			if ( stereoSample ) {
				Snd_MixTwoSpeakerStereo( finalMixBuffer, alignedInputSamples, numFrames, lastN, currentN );
			} else {
				Snd_MixTwoSpeakerMono( finalMixBuffer, alignedInputSamples, numFrames, lastN, currentN );
			}
		} else {
			// all-s16 pipeline: quantize gains to Q15 at a single defined
			// choke point, convert the gathered block to s16 (lossless for
			// PCM sources - decode floats are exact integers), and mix in
			// integer math into the int32 accumulator. Bit-deterministic
			// across compilers and architectures.
			const int n = stereoSample ? numFrames*2 : numFrames;
			Snd_FloatToS16( srcS16, alignedInputSamples, n );
			haveS16 = true;
			if ( occFilterS16 ) {
				/*
				   Integer occlusion shelf, the twin of the float one
				   above: state recursion in the reverb's toward-zero
				   QMUL idiom (provable decay to exact zero on silence,
				   matching AIRQ), applied in place so the mix and the
				   reverb-send reuse of srcS16 both see the muffled
				   source. Output is a convex combination of x and lp
				   for gains in [0,1], so it stays in s16 range with no
				   clamp. Occlusion never applies to stereo samples, so
				   srcS16 is mono here.
				*/
				#define OCCQ(x,g) ( (x) >= 0 ? (int)( ( (long long)(x) * (g) ) >> 15 ) \
				                             : -(int)( ( -(long long)(x) * (g) ) >> 15 ) )
				const int occAQ = Snd_ClampGainQ15( Snd_Shelf5kAlpha() );
				const int occGQ = Snd_ClampGainQ15( occHfG );
				int lp = chan->occLpI;
				for ( int k = 0; k < numFrames; k++ ) {
					int x = srcS16[k];
					lp += OCCQ( x - lp, occAQ );
					srcS16[k] = (short)( lp + OCCQ( x - lp, occGQ ) );
				}
				chan->occLpI = lp;
				#undef OCCQ
			}
			int lastQ[2]    = { Snd_ClampGainQ15( chan->lastV[0] ), Snd_ClampGainQ15( chan->lastV[1] ) };
			int currentQ[2] = { Snd_ClampGainQ15( ears[0] ),        Snd_ClampGainQ15( ears[1] ) };
			int *accum = (int *)finalMixBuffer;   // s16 mode: the buffer is the int32 accumulator
			if ( stereoSample ) {
				Snd_MixTwoSpeakerStereoS16( accum, srcS16, numFrames, lastQ, currentQ );
			} else {
				Snd_MixTwoSpeakerMonoS16( accum, srcS16, numFrames, lastQ, currentQ );
			}
		}

		// environmental reverb send: accumulate this channel into the world's
		// mono send at its (mono-summed) volume. Private/no-occlusion global
		// channels (menu, voiceovers) skip the room: send only spatialized
		// sounds, matching the character of the old per-source EFX send.
		if ( idSoundSystemLocal::s_useReverb.GetBool() && soundSystemLocal.efxloaded
		     && !( chan->parms.soundShaderFlags & ( SSF_GLOBAL | SSF_PRIVATE_SOUND ) ) ) {
			float sendVol = ( ears[0] + ears[1] ) * 0.5f;

			/*
			   Per-source distance terms on the wet send, from the resolved
			   preset:

			   - roomRolloffFactor: an additional inverse-distance law on
			     the room path only, gain = mind / (mind + rrf*(d - mind))
			     - the standard EAX/OpenAL room rolloff model. The dry path
			     keeps the game's own attenuation, which sendVol already
			     carries.
			   - airAbsorptionGainHF: per EAX an HF-only loss of
			     airAbs^meters on the way to the room, applied as a
			     per-channel one-pole shelf at the 5 kHz statistical HF
			     reference: y = lp + hfGain*(x - lp). This replaces the
			     earlier frequency-independent attenuation, which carried
			     the level effect but not the spectral tilt; distant
			     sources now drive the room with genuinely darker energy.
			     The shelf engages only when the preset absorbs (hfGain
			     below unity), so a non-absorbing preset takes the exact
			     legacy path.
			*/
			const sndReverbParams_t &rp = reverb.Params();
			float airHfGain = 1.0f;
			if ( sendDist > 0.0f ) {
				float mind = parms->minDistance > 0.01f ? parms->minDistance : 0.01f;
				if ( rp.roomRolloffFactor > 0.0f && sendDist > mind ) {
					sendVol *= mind / ( mind + rp.roomRolloffFactor * ( sendDist - mind ) );
				}
				if ( rp.airAbsorptionGainHF < 0.9999f ) {
					if ( rp.airAbsorptionGainHF == chan->airCacheAbs && sendDist == chan->airCacheDist ) {
						airHfGain = chan->airCacheHfG;	// bit-identical reuse
					} else {
						airHfGain = powf( rp.airAbsorptionGainHF, sendDist );
						if ( airHfGain < 0.05f ) airHfGain = 0.05f;
						chan->airCacheAbs = rp.airAbsorptionGainHF;
						chan->airCacheDist = sendDist;
						chan->airCacheHfG = airHfGain;
					}
				}
			}
			const bool airOn = airHfGain < 0.9999f;
			// one-pole at 5 kHz in the current output rate
			const float airAlpha = Snd_Shelf5kAlpha();
			if ( soundSystemLocal.outputIsFloat ) {
				if ( stereoSample ) {
					for ( int k = 0; k < numFrames; k++ ) {
						float x = ( alignedInputSamples[(size_t)k*2] + alignedInputSamples[(size_t)k*2+1] ) * 0.5f;
						if ( airOn ) {
							chan->airLpF += airAlpha * ( x - chan->airLpF );
							x = chan->airLpF + airHfGain * ( x - chan->airLpF );
						}
						reverbSendF[k] += x * sendVol;
					}
				} else {
					for ( int k = 0; k < numFrames; k++ ) {
						float x = alignedInputSamples[k];
						if ( airOn ) {
							chan->airLpF += airAlpha * ( x - chan->airLpF );
							x = chan->airLpF + airHfGain * ( x - chan->airLpF );
						}
						reverbSendF[k] += x * sendVol;
					}
				}
			} else {
				const int sq = Snd_ClampGainQ15( sendVol );
				/*
				   srcS16 already holds this block converted, done by the mix
				   above. The previous code declared a second buffer and ran
				   Snd_FloatToS16 over the identical input again - its comment
				   claimed to reuse the conversion but it did not.
				*/
				if ( !haveS16 ) {
					Snd_FloatToS16( srcS16, alignedInputSamples, stereoSample ? numFrames*2 : numFrames );
					haveS16 = true;
				}
				/* rounded Q15 requantizer, matching the mix kernels: the
				   send feeds the reverb input, it is not inside a feedback
				   loop, so rounding is safe (the reverb's internal QMUL
				   keeps its deliberate toward-zero truncation - that one
				   guarantees tail decay). */
				/*
				   The shelf's state recursion uses the reverb's toward-zero
				   QMUL idiom so the state provably decays to exact zero
				   with silent input; the final volume multiply keeps the
				   rounded feed-forward quantizer above.
				*/
				#define AIRQ(x,g) ( (x) >= 0 ? (int)( ( (long long)(x) * (g) ) >> 15 ) \
				                             : -(int)( ( -(long long)(x) * (g) ) >> 15 ) )
				const int airAlphaQ = Snd_ClampGainQ15( airAlpha );
				const int airHfGQ   = Snd_ClampGainQ15( airHfGain );
				if ( stereoSample ) {
					for ( int k = 0; k < numFrames; k++ ) {
						int x = ( srcS16[(size_t)k*2] + srcS16[(size_t)k*2+1] ) / 2;
						if ( airOn ) {
							chan->airLpI += AIRQ( x - chan->airLpI, airAlphaQ );
							x = chan->airLpI + AIRQ( x - chan->airLpI, airHfGQ );
						}
						reverbSendI[k] += ( x * sq + 0x4000 ) >> 15;
					}
				} else {
					for ( int k = 0; k < numFrames; k++ ) {
						int x = srcS16[k];
						if ( airOn ) {
							chan->airLpI += AIRQ( x - chan->airLpI, airAlphaQ );
							x = chan->airLpI + AIRQ( x - chan->airLpI, airHfGQ );
						}
						reverbSendI[k] += ( x * sq + 0x4000 ) >> 15;
					}
				}
				#undef AIRQ
			}
		}

		for ( j = 0 ; j < 6 ; j++ ) {
			chan->lastV[j] = ears[j];
		}

	}

	soundSystemLocal.soundStats.activeSounds++;

}

/*
===============
idSoundWorldLocal::FindAmplitude

  this is called from the main thread

  if listenerPosition is NULL, this is being used for shader parameters,
  like flashing lights and glows based on sound level.  Otherwise, it is being used for
  the screen-shake on a player.

  This doesn't do the portal-occlusion currently, because it would have to reset all the defs
  which would be problematic in multiplayer
===============
*/
float idSoundWorldLocal::FindAmplitude( idSoundEmitterLocal *sound, const int localTime, const idVec3 *listenerPosition,
									   const s_channelType channel, bool shakesOnly ) {
	int		i, j;
	soundShaderParms_t *parms;
	float	volume;
	int		activeChannelCount;
	static const int AMPLITUDE_SAMPLES = MIXBUFFER_SAMPLES/8;
	float	sourceBuffer[AMPLITUDE_SAMPLES];
	float	sumBuffer[AMPLITUDE_SAMPLES];
	// work out the distance from the listener to the emitter
	float	dlen;

	if ( !sound->playing ) {
		return 0;
	}

	if ( listenerPosition ) {
		// this doesn't do the portal spatialization
		idVec3 dist = sound->origin - *listenerPosition;
		dlen = dist.Length();
		dlen *= DOOM_TO_METERS;
	} else {
		dlen = 1;
	}

	activeChannelCount = 0;

	for ( i = 0; i < SOUND_MAX_CHANNELS ; i++ ) {
		idSoundChannel	*chan = &sound->channels[ i ];

		if ( !chan->triggerState ) {
			continue;
		}

		if ( channel != SCHANNEL_ANY && chan->triggerChannel != channel) {
			continue;
		}

		parms = &chan->parms;

		int	localTriggerTimes = chan->triggerSampleTime;

		bool looping = ( parms->soundShaderFlags & SSF_LOOPING ) != 0;
		// check for screen shakes
		float shakes = parms->shakes;
		if ( shakesOnly && shakes <= 0.0f ) {
			continue;
		}

		//
		// calculate volume
		//
		if ( !listenerPosition ) {
			// just look at the raw wav data for light shader evaluation
			volume = 1.0;
		} else {
			volume = parms->volume;
			volume = soundSystemLocal.dB2Scale( volume );
			if ( shakesOnly ) {
				volume *= shakes;
			}

			if ( listenerPosition && !( parms->soundShaderFlags & SSF_GLOBAL )  ) {
				// check for overrides
				float maxd = parms->maxDistance;
				float mind = parms->minDistance;

				if ( dlen >= maxd ) {
					volume = 0.0f;
				} else if ( dlen > mind ) {
					float frac = idMath::ClampFloat( 0, 1, 1.0f - ((dlen - mind) / (maxd - mind)));
					if ( idSoundSystemLocal::s_quadraticFalloff.GetBool() ) {
						frac *= frac;
					}
					volume *= frac;
				}
			}
		}

		if ( volume <= 0 ) {
			continue;
		}

		//
		// fetch the sound from the cache
		// this doesn't handle stereo samples correctly...
		//
		if ( !listenerPosition && chan->parms.soundShaderFlags & SSF_NO_FLICKER ) {
			// the NO_FLICKER option is to allow a light to still play a sound, but
			// not have it effect the intensity
			for ( j = 0 ; j < (AMPLITUDE_SAMPLES); j++ ) {
				sourceBuffer[j] = j & 1 ? 32767.0f : -32767.0f;
			}
		} else {
			idSoundSample* sample = looping ? chan->soundShader->entries[0] : chan->leadinSample;
			if ( sample == NULL ) // DG: this happens if sound is disabled (s_noSound 1)
				continue;

			// build the min/max table on first query so this path never
			// touches the channel's decoder (see EnsureAmplitudeData)
			sample->EnsureAmplitudeData();

			int offset = (localTime - localTriggerTimes);	// offset in samples
			int size = sample->LengthInOutputSamples();
			short *amplitudeData = (short *)( sample->amplitudeData );
			if ( amplitudeData ) {
				// when the amplitudeData is present use that fill a dummy sourceBuffer
				// this is to allow for amplitude based effect on hardware audio solutions
				if ( looping ) offset %= size;
				if ( offset < size ) {
					for ( j = 0 ; j < (AMPLITUDE_SAMPLES); j++ ) {
						sourceBuffer[j] = j & 1 ? amplitudeData[ ( offset / 512 ) * 2 ] : amplitudeData[ ( offset / 512 ) * 2 + 1 ];
					}
				}
			} else {
				// get actual sample data
				chan->GatherChannelSamples( offset, AMPLITUDE_SAMPLES, sourceBuffer );
			}
		}
		activeChannelCount++;
		if ( activeChannelCount == 1 ) {
			// store to the buffer
			for( j = 0; j < AMPLITUDE_SAMPLES; j++ ) {
				sumBuffer[ j ] = volume * sourceBuffer[ j ];
			}
		} else {
			// add to the buffer
			for( j = 0; j < AMPLITUDE_SAMPLES; j++ ) {
				sumBuffer[ j ] += volume * sourceBuffer[ j ];
			}
		}
	}

	if ( activeChannelCount == 0 ) {
		return 0.0;
	}

	float high = -32767.0f;
	float low = 32767.0f;

	// use a 20th of a second
	for( i = 0; i < (AMPLITUDE_SAMPLES); i++ ) {
		float fabval = sumBuffer[i];
		if ( high < fabval ) {
			high = fabval;
		}
		if ( low > fabval ) {
			low = fabval;
		}
	}

	float sout;
	sout = atan( (high - low) / 32767.0f) / DEG2RAD(45);

	return sout;
}

/*
=================
idSoundWorldLocal::FadeSoundClasses

fade all sounds in the world with a given shader soundClass
to is in Db (sigh), over is in seconds
=================
*/
void	idSoundWorldLocal::FadeSoundClasses( const int soundClass, const float to, const float over ) {
	if ( soundClass < 0 || soundClass >= SOUND_MAX_CLASSES ) {
		common->Error( "idSoundWorldLocal::FadeSoundClasses: bad soundClass %i", soundClass );
	}

	idSoundFade	*fade = &soundClassFade[ soundClass ];

	int	lengthSamples = soundSystemLocal.MillisecondsToSamples( over * 1000 );

	// if it is already fading to this volume at this rate, don't change it
	if ( fade->fadeEndVolume == to &&
		fade->fadeEndSample - fade->fadeStartSample == lengthSamples ) {
		return;
	}

	int	startSampleTime;

	// per-frame mixing: the next mix pass starts exactly at the current
	// sample clock, so fades take effect on the very next frame (the old
	// +MIXBUFFER_SAMPLES lookahead matched the old block-ahead scheduler
	// and added ~93ms of latency)
	startSampleTime = soundSystemLocal.GetCurrentSampleTime();

	// fade it
	fade->fadeStartVolume = fade->FadeDbAtSampleTime( startSampleTime );
	fade->fadeStartSample = startSampleTime;
	fade->fadeEndSample = startSampleTime + lengthSamples;
	fade->fadeEndVolume = to;
}

/*
=================
idSoundWorldLocal::SetSlowmo
=================
*/
void idSoundWorldLocal::SetSlowmo( bool active ) {
	slowmoActive = active;
}

/*
=================
idSoundWorldLocal::SetSlowmoSpeed
=================
*/
void idSoundWorldLocal::SetSlowmoSpeed( float speed ) {
	slowmoSpeed = speed;
}

/*
=================
idSoundWorldLocal::SetEnviroSuit
=================
*/
void idSoundWorldLocal::SetEnviroSuit( bool active ) {
	enviroSuitActive = active;
}

/*
===================
idSoundWorldLocal::WriteDSPState / ReadDSPState

Savestate-only snapshot of the mixer's filter memories - everything that
carries audio history across blocks and is deliberately NOT in the
on-disk savegame: the reverb (delay lines, allpass/predelay/echo
buffers, modulation phase, crossfade position - the whole object), the
enviro-suit chain (biquad and comb state), and the per-channel occlusion
and air shelves. Without this a restore keeps the lines' future-time
tails (rewind) or zeroes them (cross-session), so post-restore audio
diverges from an uninterrupted run at full audibility; with it the
round trip is byte-reproducible.

Raw object images guarded by size fields: savestates already carry a
same-binary contract, and a size mismatch skips the section cleanly.
The per-channel powf caches and the reverb's derivedClean flag are NOT
saved: they are pure functions of saved inputs and re-derive to
bit-identical values.
===================
*/
static const int SND_DSP_VERSION = 4;	// v2: + airLpI; v3: + occLpI; v4: + PlayShaderDirectly's diversity RNG seed

void idSoundWorldLocal::WriteDSPState( idFile *f ) {
	f->WriteInt( SND_DSP_VERSION );
	f->WriteInt( playShaderRnd.GetSeed() );
	f->WriteInt( (int)sizeof( reverb ) );
	f->Write( &reverb, sizeof( reverb ) );
	f->WriteInt( (int)sizeof( enviroFX ) );
	f->Write( &enviroFX, sizeof( enviroFX ) );

	int count = 0;
	for ( int e = 0; e < emitters.Num(); e++ ) {
		if ( emitters[e] ) count++;
	}
	f->WriteInt( count );
	for ( int e = 0; e < emitters.Num(); e++ ) {
		idSoundEmitterLocal *emitter = emitters[e];
		if ( !emitter ) continue;
		f->WriteInt( emitter->index );
		for ( int c = 0; c < SOUND_MAX_CHANNELS; c++ ) {
			f->WriteFloat( emitter->channels[c].occLpF );
			f->WriteFloat( emitter->channels[c].airLpF );
			f->WriteInt( emitter->channels[c].airLpI );
			f->WriteInt( emitter->channels[c].occLpI );
			idSoundChannel *ch = &emitter->channels[c];
			int hasStream = ( ch->triggerState && ch->decoder != NULL ) ? 1 : 0;
			if ( hasStream ) {
				idFile_Memory tmp( "strm" );
				ch->decoder->WriteStreamState( &tmp );
				f->WriteInt( tmp.Length() );
				f->Write( tmp.GetDataPtr(), tmp.Length() );
			} else {
				f->WriteInt( 0 );
			}
		}
	}
}

void idSoundWorldLocal::ReadDSPState( idFile *f ) {
	int ver = 0, sz = 0;
	f->ReadInt( ver );
	if ( ver != SND_DSP_VERSION ) return;
	{
		int seed = 0;
		f->ReadInt( seed );
		playShaderRnd.SetSeed( seed );
	}
	f->ReadInt( sz );
	if ( sz != (int)sizeof( reverb ) ) return;
	f->Read( &reverb, sizeof( reverb ) );
	reverb.InvalidateDerived();	// re-derive from restored cur: bit-identical
	f->ReadInt( sz );
	if ( sz != (int)sizeof( enviroFX ) ) return;
	f->Read( &enviroFX, sizeof( enviroFX ) );

	int count = 0;
	f->ReadInt( count );
	for ( int i = 0; i < count; i++ ) {
		int idx = -1;
		f->ReadInt( idx );
		idSoundEmitterLocal *emitter = ( idx >= 0 && idx < emitters.Num() ) ? emitters[idx] : NULL;
		for ( int c = 0; c < SOUND_MAX_CHANNELS; c++ ) {
			float occ = 0.0f, air = 0.0f;
			int airI = 0, occI = 0;
			f->ReadFloat( occ );
			f->ReadFloat( air );
			f->ReadInt( airI );
			f->ReadInt( occI );
			if ( emitter ) {
				emitter->channels[c].occLpF = occ;
				emitter->channels[c].airLpF = air;
				emitter->channels[c].airLpI = airI;
				emitter->channels[c].occLpI = occI;
			}
			int blobLen = 0;
			f->ReadInt( blobLen );
			if ( blobLen > 0 && blobLen < ( 1 << 20 ) ) {
				byte *blob = (byte *)Mem_Alloc( blobLen );
				f->Read( blob, blobLen );
				if ( emitter ) {
					idSoundChannel *ch = &emitter->channels[c];
					if ( ch->pendStream ) Mem_Free( ch->pendStream );
					ch->pendStream = blob;
					ch->pendStreamSize = blobLen;
				} else {
					Mem_Free( blob );
				}
			}
		}
	}
}
