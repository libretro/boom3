/*
efxlib.h - parser for the game's efxs/<map>.efx reverb definitions.
Same grammar as the OpenAL-era parser (EAX property blocks), but fills a
plain sndReverbParams_t consumed by idSoundReverb instead of AL effects.
*/

#ifndef __EFXLIBH
#define __EFXLIBH

#include "idlib/containers/List.h"
#include "idlib/Str.h"
#include "idlib/Lexer.h"
#include "sound/snd_reverb.h"

struct idSoundEffect {
	idStr				name;
	sndReverbParams_t	params;
};

class idEFXFile {
public:
	idEFXFile() {}
	~idEFXFile() { Clear(); }

	bool	FindEffect( const idStr &name, sndReverbParams_t *out ) const;
	bool	LoadFile( const char *filename, bool OSPath = false );
	void	Clear( void ) { effects.DeleteContents( true ); }
	bool	IsLoaded( void ) const { return effects.Num() > 0; }

private:
	bool	ReadEffect( idLexer &lexer, idSoundEffect *effect );

	idList<idSoundEffect *> effects;
};

#endif // __EFXLIBH
