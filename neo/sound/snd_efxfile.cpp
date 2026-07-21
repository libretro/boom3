/*
snd_efxfile.cpp - reverb definition parser for .efx files under efxs/.

Grammar-compatible with the OpenAL-era dhewm3 parser (which followed
Creative's EAX property conventions): "reverb <name> { ... }" blocks with
millibel gains converted to linear, "environment size" mapped to density
via Creative's size^3/16 formula, and the 0x20 flag bit as decay hf limit.
Output is a plain sndReverbParams_t per effect for idSoundReverb.
*/

#include "sys/platform.h"
#include "idlib/math/Math.h"
#include "framework/Common.h"
#include "framework/FileSystem.h"

#include "sound/efxlib.h"

// millibels -> linear gain, clamped to the EAXREVERB property range
static inline float mB_to_gain_clamped( float millibels, float minG, float maxG ) {
	float g = powf( 10.0f, millibels / 2000.0f );
	return g < minG ? minG : ( g > maxG ? maxG : g );
}

bool idEFXFile::FindEffect( const idStr &name, sndReverbParams_t *out ) const {
	for ( int i = 0; i < effects.Num(); i++ ) {
		if ( effects[i]->name == name ) {
			*out = effects[i]->params;
			return true;
		}
	}
	return false;
}

bool idEFXFile::ReadEffect( idLexer &src, idSoundEffect *effect ) {
	idToken name, token;

	if ( !src.ReadToken( &token ) )
		return false;

	if ( token != "reverb" ) {
		src.Error( "idEFXFile::ReadEffect: Unknown effect definition" );
		return false;
	}

	src.ReadTokenOnLine( &token );
	name = token;

	if ( !src.ReadToken( &token ) )
		return false;
	if ( token != "{" ) {
		src.Error( "idEFXFile::ReadEffect: { not found, found %s", token.c_str() );
		return false;
	}

	sndReverbParams_t &p = effect->params;
	p.SetDefaults();

	do {
		if ( !src.ReadToken( &token ) ) {
			src.Error( "idEFXFile::ReadEffect: EOF without closing brace" );
			return false;
		}

		if ( token == "}" ) {
			effect->name = name;
			break;
		}

		if ( token == "environment" ) {
			src.ParseInt();		// no EFX equivalent, ignored (as before)
		} else if ( token == "environment size" ) {
			// Creative's EFX-Util.lib: density = clamp(size^3 / 16, 0, 1)
			float size = src.ParseFloat();
			p.density = idMath::ClampFloat( 0.0f, 1.0f, ( size * size * size ) / 16.0f );
		} else if ( token == "environment diffusion" ) {
			p.diffusion = src.ParseFloat();
		} else if ( token == "room" ) {
			p.gain = mB_to_gain_clamped( src.ParseInt(), 0.0f, 1.0f );
		} else if ( token == "room hf" ) {
			p.gainHF = mB_to_gain_clamped( src.ParseInt(), 0.0f, 1.0f );
		} else if ( token == "room lf" ) {
			p.gainLF = mB_to_gain_clamped( src.ParseInt(), 0.0f, 1.0f );
		} else if ( token == "decay time" ) {
			p.decayTime = src.ParseFloat();
		} else if ( token == "decay hf ratio" ) {
			p.decayHFRatio = src.ParseFloat();
		} else if ( token == "decay lf ratio" ) {
			p.decayLFRatio = src.ParseFloat();
		} else if ( token == "reflections" ) {
			p.reflectionsGain = mB_to_gain_clamped( src.ParseInt(), 0.0f, 3.16f );
		} else if ( token == "reflections delay" ) {
			p.reflectionsDelay = src.ParseFloat();
		} else if ( token == "reflections pan" ) {
			p.reflectionsPan[0] = src.ParseFloat();
			p.reflectionsPan[1] = src.ParseFloat();
			p.reflectionsPan[2] = src.ParseFloat();
		} else if ( token == "reverb" ) {
			p.lateReverbGain = mB_to_gain_clamped( src.ParseInt(), 0.0f, 10.0f );
		} else if ( token == "reverb delay" ) {
			p.lateReverbDelay = src.ParseFloat();
		} else if ( token == "reverb pan" ) {
			p.lateReverbPan[0] = src.ParseFloat();
			p.lateReverbPan[1] = src.ParseFloat();
			p.lateReverbPan[2] = src.ParseFloat();
		} else if ( token == "echo time" ) {
			p.echoTime = src.ParseFloat();
		} else if ( token == "echo depth" ) {
			p.echoDepth = src.ParseFloat();
		} else if ( token == "modulation time" ) {
			p.modulationTime = src.ParseFloat();
		} else if ( token == "modulation depth" ) {
			p.modulationDepth = src.ParseFloat();
		} else if ( token == "air absorption hf" ) {
			p.airAbsorptionGainHF = mB_to_gain_clamped( src.ParseFloat(), 0.892f, 1.0f );
		} else if ( token == "hf reference" ) {
			p.hfReference = src.ParseFloat();
		} else if ( token == "lf reference" ) {
			p.lfReference = src.ParseFloat();
		} else if ( token == "room rolloff factor" ) {
			p.roomRolloffFactor = src.ParseFloat();
		} else if ( token == "flags" ) {
			src.ReadTokenOnLine( &token );
			unsigned int flags = token.GetUnsignedIntValue();
			p.decayHFLimit = ( flags & 0x20 ) ? 1 : 0;
		} else {
			src.ReadTokenOnLine( &token );
			src.Error( "idEFXFile::ReadEffect: Invalid parameter in reverb definition" );
		}
	} while ( 1 );

	return true;
}

bool idEFXFile::LoadFile( const char *filename, bool OSPath ) {
	idLexer src( LEXFL_NOSTRINGCONCAT );

	src.LoadFile( filename, OSPath );
	if ( !src.IsLoaded() ) {
		return false;
	}

	if ( !src.ExpectTokenString( "Version" ) ) {
		return false;
	}
	if ( src.ParseInt() != 1 ) {
		src.Error( "idEFXFile::LoadFile: Unknown file version" );
		return false;
	}

	while ( !src.EndOfFile() ) {
		idSoundEffect *effect = new idSoundEffect;
		if ( ReadEffect( src, effect ) )
			effects.Append( effect );
		else
			delete effect;
	}

	return true;
}
