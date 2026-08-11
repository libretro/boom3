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
	/* The built-in fallback for when the map has no efxs/<map>.efx.
	 * Retail Doom 3 shipped no efx files at all - they came with the
	 * expansion - so without this the reverb engine never engages on
	 * stock content.  The fallback is one effect named "default",
	 * which is the last rung of the world's lookup ladder, holding
	 * the canonical EAX GENERIC preset.  It is parsed from embedded
	 * text through the exact same grammar as a file, so the resulting
	 * parameters are bit-identical to what an on-disk file with the
	 * same content would produce - by construction, and verified by
	 * test. */
	bool	LoadDefaults( void );
	bool	LoadEmbedded( const char *mapname );
	/* test access: enumerate the parsed effects */
	int		NumEffects( void ) const { return effects.Num(); }
	const idSoundEffect *GetEffect( int i ) const { return effects[i]; }
	static const char *DefaultSource( int *length );
	static const char *EmbeddedSource( const char *mapname, int *length );
	/* test entry: the shared parser on a caller-supplied lexer */
	bool	TestParse( idLexer &src ) { return ParseSource( src ); }
	void	Clear( void ) { effects.DeleteContents( true ); }
	bool	IsLoaded( void ) const { return effects.Num() > 0; }

private:
	bool	ReadEffect( idLexer &lexer, idSoundEffect *effect );
	bool	ParseSource( idLexer &src );

	idList<idSoundEffect *> effects;
};

#endif // __EFXLIBH
