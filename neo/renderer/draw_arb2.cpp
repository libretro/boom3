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
#include "renderer/VertexCache.h"

#include "renderer/tr_local.h"
#include "renderer/spectral_mix.h"
#include "renderer/arb2_nvrec.h"

#include <encodings/crc32.h>

#ifndef GL_VERTEX_PROGRAM_PARAMETER_BUFFER_NV
#define GL_VERTEX_PROGRAM_PARAMETER_BUFFER_NV   0x8DA2
#endif
#ifndef GL_FRAGMENT_PROGRAM_PARAMETER_BUFFER_NV
#define GL_FRAGMENT_PROGRAM_PARAMETER_BUFFER_NV 0x8DA4
#endif
#ifndef GL_DRAW_INDIRECT_BUFFER
#define GL_DRAW_INDIRECT_BUFFER                 0x8F3F
#endif
#ifndef GL_COPY_WRITE_BUFFER
#define GL_COPY_WRITE_BUFFER                    0x8F37
#endif
#ifndef GL_BUFFER_GPU_ADDRESS_NV
#define GL_BUFFER_GPU_ADDRESS_NV                0x8F1D
#endif
#ifndef GL_READ_ONLY
#define GL_READ_ONLY                            0x88B8
#endif

int arb2Tier = ARB2_TIER_ARB;
int arb2TierRequest = -1;

// DG: if this is defined, the soft particle shaders will be compiled into the executable
//  otherwise soft_particle.vfp will be opened as a file just like the other shaders
//  (useful when tweaking that shader - when loaded from disk, you can use `reloadARBprograms`
//   instead of recompiling the executable)
#ifndef D3_INTEGRATE_SOFTPART_SHADERS
  #define D3_INTEGRATE_SOFTPART_SHADERS 1
#endif

/*
=========================================================================================

GENERAL INTERACTION RENDERING

=========================================================================================
*/

/*
====================
GL_SelectTextureNoClient
====================
*/
static void GL_SelectTextureNoClient( int unit ) {
	backEnd.glState.currenttmu = unit;
	qglActiveTextureARB( GL_TEXTURE0_ARB + unit );
}

/*
==================
RB_ARB2_DrawInteraction
==================
*/
/*
Redundant-upload filter for the interaction programs' env parameters.

RB_CreateSingleDrawInteractions calls RB_ARB2_DrawInteraction once per
(light stage x surface stage) pair, and re-sends all twelve vertex
parameters every time.  Six of them - the light and view origin and the
four light projection planes - are set once per surface before the stage
loops and never change inside them, and the two colour-modulate
parameters follow din->vertexColor, which is a property of the surface
stage's vertex colour mode rather than of the light.  On a surface with
several stages, or a light with several stages, those uploads repeat
with identical contents.

The cache stores what was last handed to the driver and skips a call
whose four floats are unchanged.  It is reset at the top and bottom of
RB_ARB2_CreateDrawInteractions, so its lifetime never extends past the
loop where this file is the only writer of these parameters - anything
outside (RB_SetProgramEnvironment, the fog and blend light passes, the
post-process chain) starts from a cleared cache.

Values that do reach the driver are exactly the values the old code
sent, so rendering is unchanged.

With EXT_gpu_program_parameters the changed parameters are not sent one
by one: each setter widens a pending span instead, and RB_EnvFlush hands
the driver the span from the cache in one ProgramEnvParameters4fvEXT call
per target just before the draw.  A row inside the span that did not
change is sent again with the value the driver already holds - every
draw sets all of the rows, so none of them is unset.
*/
#define RB_ENV_CACHE_SIZE ( PP_COLOR_ADD + 1 )
#define RB_FRAG_ENV_CACHE_SIZE 2

static float	rb_envCache[RB_ENV_CACHE_SIZE][4];
static bool	rb_envCacheValid[RB_ENV_CACHE_SIZE];

static float	rb_fragEnvCache[RB_FRAG_ENV_CACHE_SIZE][4];
static bool	rb_fragEnvCacheValid[RB_FRAG_ENV_CACHE_SIZE];

// pending spans [lo, hi], empty when lo > hi; only used while rb_envSpans
static int		rb_envLo = RB_ENV_CACHE_SIZE, rb_envHi = -1;
static int		rb_fragEnvLo = RB_FRAG_ENV_CACHE_SIZE, rb_fragEnvHi = -1;
static bool	rb_envSpans;

static ID_INLINE void RB_EnvCacheReset( void ) {
	memset( rb_envCacheValid, 0, sizeof( rb_envCacheValid ) );
	memset( rb_fragEnvCacheValid, 0, sizeof( rb_fragEnvCacheValid ) );
	rb_envLo = RB_ENV_CACHE_SIZE;
	rb_envHi = -1;
	rb_fragEnvLo = RB_FRAG_ENV_CACHE_SIZE;
	rb_fragEnvHi = -1;
}

static ID_INLINE void RB_EnvFlush( void ) {
	if ( rb_envLo <= rb_envHi ) {
		glCaps.ProgramEnvParameters4fvEXT( GL_VERTEX_PROGRAM_ARB, rb_envLo, rb_envHi - rb_envLo + 1, rb_envCache[rb_envLo] );
		rb_envLo = RB_ENV_CACHE_SIZE;
		rb_envHi = -1;
	}
	if ( rb_fragEnvLo <= rb_fragEnvHi ) {
		glCaps.ProgramEnvParameters4fvEXT( GL_FRAGMENT_PROGRAM_ARB, rb_fragEnvLo, rb_fragEnvHi - rb_fragEnvLo + 1,
			rb_fragEnvCache[rb_fragEnvLo] );
		rb_fragEnvLo = RB_FRAG_ENV_CACHE_SIZE;
		rb_fragEnvHi = -1;
	}
}

static ID_INLINE void RB_VertexEnvParm( int index, const float *v ) {
	if ( index < RB_ENV_CACHE_SIZE ) {
		/* Bitwise comparison, not float equality: -0.0f == 0.0f is true
		 * but the two are different values to hand a shader, and a NaN
		 * compares unequal to itself and would defeat the skip in the
		 * other direction.  memcmp gives "same bits, same upload". */
		float * const c = rb_envCache[index];
		if ( rb_envCacheValid[index] && !memcmp( c, v, 4 * sizeof( float ) ) ) {
			return;
		}
		memcpy( c, v, 4 * sizeof( float ) );
		rb_envCacheValid[index] = true;
		if ( rb_envSpans ) {
			if ( index < rb_envLo ) {
				rb_envLo = index;
			}
			if ( index > rb_envHi ) {
				rb_envHi = index;
			}
			return;
		}
	}
	qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, index, v );
}

/* Same filter for the fragment target's two interaction colours.  These
 * are lightColor * the surface stage's colour registers, so they repeat
 * across surfaces that share a material under one light - how often is
 * content-dependent, and the filter simply costs a comparison when they
 * do not. */
static ID_INLINE void RB_FragmentEnvParm( int index, const float *v ) {
	if ( index < RB_FRAG_ENV_CACHE_SIZE ) {
		float * const c = rb_fragEnvCache[index];
		if ( rb_fragEnvCacheValid[index] && !memcmp( c, v, 4 * sizeof( float ) ) ) {
			return;
		}
		memcpy( c, v, 4 * sizeof( float ) );
		rb_fragEnvCacheValid[index] = true;
		if ( rb_envSpans ) {
			if ( index < rb_fragEnvLo ) {
				rb_fragEnvLo = index;
			}
			if ( index > rb_fragEnvHi ) {
				rb_fragEnvHi = index;
			}
			return;
		}
	}
	qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, index, v );
}

/*
   HDR specular boost: dielectric specular is physically a few percent
   of incident light, and under an SDR ceiling those few percent read as
   mud.  The specular energy enters the interaction program as a per-draw
   constant, so scaling it lifts reflections on lit surfaces only - GUIs,
   2D and fullbright content never pass through this path.  Boosted
   highlights that reach the bloom threshold then bleed into the
   paper-white..peak headroom, which is where reflective punch actually
   lives on an HDR display.  Identity (1.0) when HDR is off or the
   option is disabled: bit-identical SDR.

   Whether the boost produces highlights or just clipping depends on
   there being somewhere for the boosted energy to go, and that is
   decided by two other options, not one:

     - the encoding fold (doom_hdr_headroom) buys one gamma-domain stop
       by scaling the scene down before the epilogue;
     - a float scene target only carries values past 1.0 if the epilogue
       is the unclamped variant, which is what doom_hdr_true_blend
       selects.  With true_blend disabled the epilogue is MUL_SAT and
       clamps every pass at 1.0 exactly as an integer target does, so an
       FP16 target on its own is not headroom.

   The gate below therefore mirrors the epilogue's own condition.  It
   previously tested the float target alone, which made the boost act as
   a highlight clipper in the one configuration where the user had
   turned off both true_blend and the headroom fold - the boost pushed
   specular past a ceiling that was still there, converting highlights
   to flat white.  That is the opposite of the intent, so refuse the
   boost instead.
*/
static float	rb_specularBoost = 1.0f;

static void RB_UpdateSpecularBoost( void ) {
	rb_specularBoost = RB_HDRSpecularBoost();
}

void	RB_ARB2_DrawInteraction( const drawInteraction_t *din ) {
	// load all the vertex program parameters
	RB_VertexEnvParm( PP_LIGHT_ORIGIN, din->localLightOrigin.ToFloatPtr() );
	RB_VertexEnvParm( PP_VIEW_ORIGIN, din->localViewOrigin.ToFloatPtr() );
	RB_VertexEnvParm( PP_LIGHT_PROJECT_S, din->lightProjection[0].ToFloatPtr() );
	RB_VertexEnvParm( PP_LIGHT_PROJECT_T, din->lightProjection[1].ToFloatPtr() );
	RB_VertexEnvParm( PP_LIGHT_PROJECT_Q, din->lightProjection[2].ToFloatPtr() );
	RB_VertexEnvParm( PP_LIGHT_FALLOFF_S, din->lightProjection[3].ToFloatPtr() );
	RB_VertexEnvParm( PP_BUMP_MATRIX_S, din->bumpMatrix[0].ToFloatPtr() );
	RB_VertexEnvParm( PP_BUMP_MATRIX_T, din->bumpMatrix[1].ToFloatPtr() );
	RB_VertexEnvParm( PP_DIFFUSE_MATRIX_S, din->diffuseMatrix[0].ToFloatPtr() );
	RB_VertexEnvParm( PP_DIFFUSE_MATRIX_T, din->diffuseMatrix[1].ToFloatPtr() );
	RB_VertexEnvParm( PP_SPECULAR_MATRIX_S, din->specularMatrix[0].ToFloatPtr() );
	RB_VertexEnvParm( PP_SPECULAR_MATRIX_T, din->specularMatrix[1].ToFloatPtr() );

	// testing fragment based normal mapping
	if ( r_testARBProgram.GetBool() ) {
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 2, din->localLightOrigin.ToFloatPtr() );
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 3, din->localViewOrigin.ToFloatPtr() );
	}

	static const float zero[4] = { 0, 0, 0, 0 };
	static const float one[4] = { 1, 1, 1, 1 };
	static const float negOne[4] = { -1, -1, -1, -1 };

	switch ( din->vertexColor ) {
	case SVC_IGNORE:
		RB_VertexEnvParm( PP_COLOR_MODULATE, zero );
		RB_VertexEnvParm( PP_COLOR_ADD, one );
		break;
	case SVC_MODULATE:
		RB_VertexEnvParm( PP_COLOR_MODULATE, one );
		RB_VertexEnvParm( PP_COLOR_ADD, zero );
		break;
	case SVC_INVERSE_MODULATE:
		RB_VertexEnvParm( PP_COLOR_MODULATE, negOne );
		RB_VertexEnvParm( PP_COLOR_ADD, one );
		break;
	}

	// set the constant colors
	RB_FragmentEnvParm( 0, din->diffuseColor.ToFloatPtr() );
	if ( rb_specularBoost != 1.0f ) {
		float sc[4];
		sc[0] = din->specularColor[0] * rb_specularBoost;
		sc[1] = din->specularColor[1] * rb_specularBoost;
		sc[2] = din->specularColor[2] * rb_specularBoost;
		sc[3] = din->specularColor[3];
		RB_FragmentEnvParm( 1, sc );
	} else {
		RB_FragmentEnvParm( 1, din->specularColor.ToFloatPtr() );
	}
	RB_EnvFlush();

	// set the textures

	// texture 1 will be the per-surface bump map
	GL_SelectTextureNoClient( 1 );
	din->bumpImage->Bind();

	// texture 2 will be the light falloff texture
	GL_SelectTextureNoClient( 2 );
	din->lightFalloffImage->Bind();

	// texture 3 will be the light projection texture
	GL_SelectTextureNoClient( 3 );
	din->lightImage->Bind();

	// texture 4 is the per-surface diffuse map
	GL_SelectTextureNoClient( 4 );
	din->diffuseImage->Bind();

	// texture 5 is the per-surface specular map
	GL_SelectTextureNoClient( 5 );
	din->specularImage->Bind();

	// draw it
	RB_DrawElementsWithCounters( din->surf->geo );
}

/*
   The NV tiers' interaction parameters.

   NV_parameter_buffer_object gives the vertex and fragment programs one
   buffer to read every per-draw parameter from, and rb_nvRec mirrors
   what that buffer holds.  Each draw stores its sixteen vec4s into the
   record, which keeps a dirty span of the ones that actually changed,
   and one ProgramBufferParametersfvNV call uploads the span - against a
   ProgramEnvParameter4fvARB per changed parameter on the ARB tier.

   The span is relative to the previous draw, not to the start of a
   light's list: the buffer is written by nothing else, so its contents
   carry over between lists, views and frames.  R_ARB2_Init resets the
   record whenever it makes a new buffer.

   The vertex colour and diffuse/specular colour parameters are the same
   values the ARB path sends as env[16..17] and fragment env[0..1].
*/
static arb2_nvrec_t	rb_nvRec;
static GLuint		rb_nvParmBuffer;
static bool		rb_nvInteraction;	// NV programs compiled and the buffer made

static void RB_ARB2NV_DrawInteraction( const drawInteraction_t *din ) {
	static const float zero[4] = { 0, 0, 0, 0 };
	static const float one[4] = { 1, 1, 1, 1 };
	static const float negOne[4] = { -1, -1, -1, -1 };
	arb2_nvrec_t * const r = &rb_nvRec;
	const float *colorModulate = zero;
	const float *colorAdd = one;
	float sc[4];

	ARB2_NVREC_SET( r, ARB2_NVREC_LIGHT_ORIGIN, din->localLightOrigin.ToFloatPtr() );
	ARB2_NVREC_SET( r, ARB2_NVREC_VIEW_ORIGIN, din->localViewOrigin.ToFloatPtr() );
	ARB2_NVREC_SET( r, ARB2_NVREC_PROJECT_S, din->lightProjection[0].ToFloatPtr() );
	ARB2_NVREC_SET( r, ARB2_NVREC_PROJECT_T, din->lightProjection[1].ToFloatPtr() );
	ARB2_NVREC_SET( r, ARB2_NVREC_PROJECT_Q, din->lightProjection[2].ToFloatPtr() );
	ARB2_NVREC_SET( r, ARB2_NVREC_FALLOFF_S, din->lightProjection[3].ToFloatPtr() );
	ARB2_NVREC_SET( r, ARB2_NVREC_BUMP_S, din->bumpMatrix[0].ToFloatPtr() );
	ARB2_NVREC_SET( r, ARB2_NVREC_BUMP_T, din->bumpMatrix[1].ToFloatPtr() );
	ARB2_NVREC_SET( r, ARB2_NVREC_DIFFUSE_S, din->diffuseMatrix[0].ToFloatPtr() );
	ARB2_NVREC_SET( r, ARB2_NVREC_DIFFUSE_T, din->diffuseMatrix[1].ToFloatPtr() );
	ARB2_NVREC_SET( r, ARB2_NVREC_SPECULAR_S, din->specularMatrix[0].ToFloatPtr() );
	ARB2_NVREC_SET( r, ARB2_NVREC_SPECULAR_T, din->specularMatrix[1].ToFloatPtr() );

	switch ( din->vertexColor ) {
	case SVC_IGNORE:
		break;
	case SVC_MODULATE:
		colorModulate = one;
		colorAdd = zero;
		break;
	case SVC_INVERSE_MODULATE:
		colorModulate = negOne;
		break;
	}
	ARB2_NVREC_SET( r, ARB2_NVREC_COLOR_MODULATE, colorModulate );
	ARB2_NVREC_SET( r, ARB2_NVREC_COLOR_ADD, colorAdd );

	ARB2_NVREC_SET( r, ARB2_NVREC_DIFFUSE_COLOR, din->diffuseColor.ToFloatPtr() );
	if ( rb_specularBoost != 1.0f ) {
		sc[0] = din->specularColor[0] * rb_specularBoost;
		sc[1] = din->specularColor[1] * rb_specularBoost;
		sc[2] = din->specularColor[2] * rb_specularBoost;
		sc[3] = din->specularColor[3];
		ARB2_NVREC_SET( r, ARB2_NVREC_SPECULAR_COLOR, sc );
	} else {
		ARB2_NVREC_SET( r, ARB2_NVREC_SPECULAR_COLOR, din->specularColor.ToFloatPtr() );
	}

	// the fragment target's binding 0 is the same buffer, so one upload
	// reaches both programs
	if ( ARB2_NVREC_DIRTY( r ) ) {
		glCaps.ProgramBufferParametersfvNV( GL_VERTEX_PROGRAM_PARAMETER_BUFFER_NV, 0,
			ARB2_NVREC_FIRST_WORD( r ), ARB2_NVREC_WORDS( r ), r->v[r->lo] );
		ARB2_NVREC_CLEAN( r );
	}

	// texture 0 is not read for a non-ambient light: the light vector is
	// normalised by NRM.  1..5 as on the ARB path.
	GL_SelectTextureNoClient( 1 );
	din->bumpImage->Bind();
	GL_SelectTextureNoClient( 2 );
	din->lightFalloffImage->Bind();
	GL_SelectTextureNoClient( 3 );
	din->lightImage->Bind();
	GL_SelectTextureNoClient( 4 );
	din->diffuseImage->Bind();
	GL_SelectTextureNoClient( 5 );
	din->specularImage->Bind();

	RB_DrawElementsWithCounters( din->surf->geo );
}

/*
   The NV5 tier: a light's interactions as multi-draw-indirect batches.

   The vertex cache runs in its streaming mode on this tier, so static
   geometry sits in a few shared buffers and each frame's in one ring,
   and surfaces that share the vertex buffer, the index buffer and the
   modelview matrix are drawn by one MultiDrawElementsIndirect with base
   vertices and first indices in place of per-surface attribute pointers.

   Each draw's parameters are a record in the vertex cache's persistent
   ring, written where the GPU reads them.  The vertex program finds its
   record by global load (NV_shader_buffer_load) at the batch's base
   address, in program.env[ARB2_NV5_ENV_BASE], plus an offset fetched from
   an instanced attribute that baseInstance indexes - assembly programs
   have no draw index of their own.  It hands the fragment program what
   it needs as flat attributes.

   Textures go to units ARB2_NV5_SLOT_UNIT and up, which the engine's
   own unit tracking never touches, and the record names the units; the
   fragment program samples them through a texture array, which
   NV_gpu_program5 lets any index address.  A unit keeps its texture
   across batches until a batch needs it for another one, so a light's
   falloff and projection and the common materials stay bound.

   The scissor stays at the union of the batch's surface rectangles and
   the fragment program kills outside each draw's own, on the pixel
   edges glScissor would clip at.

   What a batch cannot take - a depth range hack, geometry outside the
   shared buffers, a surface whose interactions could outgrow the batch
   or its texture units, a full ring - draws through the NV4 path.
*/
#define ARB2_NV5_REC_UNITS	ARB2_NVREC_VECS		// bump, falloff, projection, diffuse units
#define ARB2_NV5_REC_UNITS2	( ARB2_NVREC_VECS + 1 )	// specular unit
#define ARB2_NV5_REC_CLIP	( ARB2_NVREC_VECS + 2 )	// the draw's scissor in window pixels, x1 y1 x2 y2 exclusive
#define ARB2_NV5_REC_VECS	( ARB2_NVREC_VECS + 3 )
#define ARB2_NV5_REC_BYTES	( ARB2_NV5_REC_VECS * 16 )
#define ARB2_NV5_BATCH		64		// draws a batch holds
#define ARB2_NV5_CHUNK		512		// draws a ring reservation holds, batches carved from it in turn
#define ARB2_NV5_SLOT_UNIT	8		// first texture unit of the batch textures
#define ARB2_NV5_MAX_SLOTS	24
#define ARB2_NV5_ENV_BASE	90		// vertex program.env: the batch's record base address, low and high words

typedef struct {
	GLuint	count;
	GLuint	instanceCount;
	GLuint	firstIndex;
	GLint	baseVertex;
	GLuint	baseInstance;
} arb2Nv5Command_t;

typedef struct {
	unsigned long long	ringAddress;	// GPU address of rb_nv5.ring
	float				*rec;			// the batch's records in the ring, NULL when none reserved
	arb2Nv5Command_t	*cmd;			// its indirect commands
	float				*chunkRec;		// this frame's ring reservation, NULL when none
	arb2Nv5Command_t	*chunkCmd;
	const viewEntity_t	*space;
	vertCache_t			*vertexBlock;	// a block of the batch's vertex buffer
	vertCache_t			*indexBlock;	// and of its index buffer
	GLuint				ring;			// the vertex cache ring buffer the address belongs to
	GLuint				chunkRing;		// the ring buffer the chunk is in
	GLuint				idBuffer;		// record offsets, the instanced attribute
	GLuint				pointerBuffer;	// the vertex buffer the attribute pointers start at, 0 when none
	GLuint				slotTex[ARB2_NV5_MAX_SLOTS];	// texture each slot unit holds, 0 when unknown
	int					slotBatch[ARB2_NV5_MAX_SLOTS];	// the batch that last named the slot
	int					recOffset;		// ring offsets of rec and cmd
	int					cmdOffset;
	int					chunkRecOffset;
	int					chunkCmdOffset;
	int					chunkUsed;		// draws of the chunk earlier batches took
	int					chunkFrame;		// backEnd.frameCount it was reserved in
	int					count;			// draws in the batch
	int					serial;			// batch number, for slotBatch
	int					pinned;			// slots the batch names
	int					slots;			// slot units this context has
	int					surfBaseVertex;
	int					surfFirstIndex;
	int					clip[4];		// union of the batch's draw rectangles
	GLenum				indexType;
	bool				programs;		// the NV5 programs compiled
	bool				ringFull;		// no more batches this list
	bool				batchedMode;	// the NV5 programs are bound, not the NV4 ones
} arb2Nv5_t;

static arb2Nv5_t	rb_nv5;

// the GPU address of the vertex cache's ring, made resident the first time
static unsigned long long RB_ARB2NV5_RingAddress( GLuint ring ) {
	if ( ring != rb_nv5.ring ) {
		qglBindBufferARB( GL_COPY_WRITE_BUFFER, ring );
		glCaps.MakeBufferResidentNV( GL_COPY_WRITE_BUFFER, GL_READ_ONLY );
		glCaps.GetBufferParameterui64vNV( GL_COPY_WRITE_BUFFER, GL_BUFFER_GPU_ADDRESS_NV, &rb_nv5.ringAddress );
		qglBindBufferARB( GL_COPY_WRITE_BUFFER, 0 );
		rb_nv5.ring = ring;
	}
	return rb_nv5.ringAddress;
}

/* Forget what the slot units hold: the textures may have been deleted
 * and their names reused since. */
static void RB_ARB2NV5_ResetSlots( void ) {
	int i;
	for ( i = 0; i < ARB2_NV5_MAX_SLOTS; i++ ) {
		rb_nv5.slotTex[i] = 0;
		rb_nv5.slotBatch[i] = -1;
	}
	rb_nv5.pinned = 0;
}

/* The slot unit holding img for this batch, binding it there if no slot
 * does yet.  The caller has made sure a free slot exists. */
static int RB_ARB2NV5_Slot( idImage *img ) {
	GLuint	tex;
	int		i;
	int		victim;

	// what Bind would bind: the partial image while the full one loads
	img->Touch();
	if ( img->texnum == idImage::TEXTURE_NOT_LOADED && img->partialImage ) {
		img = img->partialImage;
		img->Touch();
	}
	// the programs sample 2D; a stage of another type reads black, as an
	// unbound 2D unit would have
	if ( img->type != TT_2D ) {
		img = globalImages->blackImage;
		img->Touch();
	}
	img->frameUsed = backEnd.frameCount;
	img->bindCount++;
	tex = img->texnum;

	victim = -1;
	for ( i = 0; i < rb_nv5.slots; i++ ) {
		if ( rb_nv5.slotTex[i] == tex ) {
			break;
		}
		if ( rb_nv5.slotBatch[i] != rb_nv5.serial
				&& ( victim < 0 || rb_nv5.slotBatch[i] < rb_nv5.slotBatch[victim] ) ) {
			victim = i;
		}
	}
	if ( i == rb_nv5.slots ) {
		if ( victim < 0 ) {
			// RB_ARB2NV5_AddSurface's bound makes this unreachable
			common->Warning( "RB_ARB2NV5_Slot: no free texture unit" );
			victim = 0;
		}
		i = victim;
		qglActiveTextureARB( GL_TEXTURE0_ARB + ARB2_NV5_SLOT_UNIT + i );
		qglBindTexture( GL_TEXTURE_2D, tex );
		rb_nv5.slotTex[i] = tex;
		// the active unit is no longer the one the engine has recorded
		backEnd.glState.currenttmu = -1;
	}
	if ( rb_nv5.slotBatch[i] != rb_nv5.serial ) {
		rb_nv5.slotBatch[i] = rb_nv5.serial;
		rb_nv5.pinned++;
	}
	return i;
}

static void RB_ARB2NV5_BindPrograms( bool batched ) {
	const bool ambient = backEnd.vLight->lightShader->IsAmbientLight();
	if ( batched ) {
		qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_NV5_INTERACTION );
		qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, ambient ? FPROG_NV5_INTERACTION_AMBIENT : FPROG_NV5_INTERACTION );
	} else {
		qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_NV_INTERACTION );
		qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, ambient ? FPROG_NV_INTERACTION_AMBIENT : FPROG_NV_INTERACTION );
	}
	rb_nv5.batchedMode = batched;
}

/* Draw the batch with its state set: the modelview matrix of its space,
 * the scissor around its draws, the attribute pointers at the start of
 * its vertex buffer, its index buffer, and its records' address. */
static void RB_ARB2NV5_Flush( void ) {
	unsigned long long	base;
	idScreenRect		rect;

	if ( rb_nv5.count == 0 ) {
		return;
	}
	if ( !rb_nv5.batchedMode ) {
		RB_ARB2NV5_BindPrograms( true );
	}

	if ( rb_nv5.space != backEnd.currentSpace ) {
		backEnd.currentSpace = rb_nv5.space;
		qglLoadMatrixf( rb_nv5.space->modelViewMatrix );
	}
	if ( r_useScissor.GetBool() ) {
		rect.x1 = rb_nv5.clip[0] - backEnd.viewDef->viewport.x1;
		rect.y1 = rb_nv5.clip[1] - backEnd.viewDef->viewport.y1;
		rect.x2 = rb_nv5.clip[2] - 1 - backEnd.viewDef->viewport.x1;
		rect.y2 = rb_nv5.clip[3] - 1 - backEnd.viewDef->viewport.y1;
		if ( !backEnd.currentScissor.Equals( rect ) ) {
			backEnd.currentScissor = rect;
			qglScissor( rb_nv5.clip[0], rb_nv5.clip[1],
				rb_nv5.clip[2] - rb_nv5.clip[0], rb_nv5.clip[3] - rb_nv5.clip[1] );
		}
	}

	// BaseVertex binds the vertex buffer; the pointers start at it, and
	// name the buffer that was bound when they were set
	vertexCache.BaseVertex( rb_nv5.vertexBlock, sizeof( idDrawVert ) );
	if ( rb_nv5.pointerBuffer != rb_nv5.vertexBlock->vbo ) {
		const idDrawVert *ac = (const idDrawVert *)NULL;
		qglColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DRAWVERT( ac, color ) );
		qglVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, normal ) );
		qglVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, tangents[1] ) );
		qglVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, tangents[0] ) );
		qglVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, st ) );
		qglVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, xyz ) );
		rb_nv5.pointerBuffer = rb_nv5.vertexBlock->vbo;
	}
	vertexCache.Position( rb_nv5.indexBlock );	// binds the index buffer

	base = rb_nv5.ringAddress + (unsigned long long)rb_nv5.recOffset;
	glCaps.ProgramEnvParameterI4uiNV( GL_VERTEX_PROGRAM_ARB, ARB2_NV5_ENV_BASE,
		(GLuint)( base & 0xffffffffu ), (GLuint)( base >> 32 ), 0, 0 );
	glCaps.MultiDrawElementsIndirect( GL_TRIANGLES, rb_nv5.indexType,
		(const void *)(intptr_t)rb_nv5.cmdOffset, rb_nv5.count, 0 );

	rb_nv5.chunkUsed += rb_nv5.count;
	rb_nv5.count = 0;
	rb_nv5.rec = NULL;
	rb_nv5.serial++;
	rb_nv5.pinned = 0;
}

/* RB_CollectSingleDrawInteractions' callback: one record and one
 * indirect command. */
static void RB_ARB2NV5_Collect( const drawInteraction_t *din ) {
	static const float zero[4] = { 0, 0, 0, 0 };
	static const float one[4] = { 1, 1, 1, 1 };
	static const float negOne[4] = { -1, -1, -1, -1 };
	const srfTriangles_t	*geo = din->surf->geo;
	const float	*colorModulate = zero;
	const float	*colorAdd = one;
	float		*r;
	arb2Nv5Command_t *c;
	int			k;

	k = rb_nv5.count++;
	r = rb_nv5.rec + k * ARB2_NV5_REC_VECS * 4;

	memcpy( r + ARB2_NVREC_LIGHT_ORIGIN * 4, din->localLightOrigin.ToFloatPtr(), 16 );
	memcpy( r + ARB2_NVREC_VIEW_ORIGIN * 4, din->localViewOrigin.ToFloatPtr(), 16 );
	memcpy( r + ARB2_NVREC_PROJECT_S * 4, din->lightProjection[0].ToFloatPtr(), 16 );
	memcpy( r + ARB2_NVREC_PROJECT_T * 4, din->lightProjection[1].ToFloatPtr(), 16 );
	memcpy( r + ARB2_NVREC_PROJECT_Q * 4, din->lightProjection[2].ToFloatPtr(), 16 );
	memcpy( r + ARB2_NVREC_FALLOFF_S * 4, din->lightProjection[3].ToFloatPtr(), 16 );
	memcpy( r + ARB2_NVREC_BUMP_S * 4, din->bumpMatrix[0].ToFloatPtr(), 16 );
	memcpy( r + ARB2_NVREC_BUMP_T * 4, din->bumpMatrix[1].ToFloatPtr(), 16 );
	memcpy( r + ARB2_NVREC_DIFFUSE_S * 4, din->diffuseMatrix[0].ToFloatPtr(), 16 );
	memcpy( r + ARB2_NVREC_DIFFUSE_T * 4, din->diffuseMatrix[1].ToFloatPtr(), 16 );
	memcpy( r + ARB2_NVREC_SPECULAR_S * 4, din->specularMatrix[0].ToFloatPtr(), 16 );
	memcpy( r + ARB2_NVREC_SPECULAR_T * 4, din->specularMatrix[1].ToFloatPtr(), 16 );

	switch ( din->vertexColor ) {
	case SVC_IGNORE:
		break;
	case SVC_MODULATE:
		colorModulate = one;
		colorAdd = zero;
		break;
	case SVC_INVERSE_MODULATE:
		colorModulate = negOne;
		break;
	}
	memcpy( r + ARB2_NVREC_COLOR_MODULATE * 4, colorModulate, 16 );
	memcpy( r + ARB2_NVREC_COLOR_ADD * 4, colorAdd, 16 );
	memcpy( r + ARB2_NVREC_DIFFUSE_COLOR * 4, din->diffuseColor.ToFloatPtr(), 16 );
	r[ARB2_NVREC_SPECULAR_COLOR * 4 + 0] = din->specularColor[0] * rb_specularBoost;
	r[ARB2_NVREC_SPECULAR_COLOR * 4 + 1] = din->specularColor[1] * rb_specularBoost;
	r[ARB2_NVREC_SPECULAR_COLOR * 4 + 2] = din->specularColor[2] * rb_specularBoost;
	r[ARB2_NVREC_SPECULAR_COLOR * 4 + 3] = din->specularColor[3];

	r[ARB2_NV5_REC_UNITS * 4 + 0] = (float)RB_ARB2NV5_Slot( din->bumpImage );
	r[ARB2_NV5_REC_UNITS * 4 + 1] = (float)RB_ARB2NV5_Slot( din->lightFalloffImage );
	r[ARB2_NV5_REC_UNITS * 4 + 2] = (float)RB_ARB2NV5_Slot( din->lightImage );
	r[ARB2_NV5_REC_UNITS * 4 + 3] = (float)RB_ARB2NV5_Slot( din->diffuseImage );
	r[ARB2_NV5_REC_UNITS2 * 4 + 0] = (float)RB_ARB2NV5_Slot( din->specularImage );
	r[ARB2_NV5_REC_UNITS2 * 4 + 1] = 0.0f;
	r[ARB2_NV5_REC_UNITS2 * 4 + 2] = 0.0f;
	r[ARB2_NV5_REC_UNITS2 * 4 + 3] = 0.0f;

	if ( r_useScissor.GetBool() ) {
		const idScreenRect *s = &din->surf->scissorRect;
		const int x1 = backEnd.viewDef->viewport.x1 + s->x1;
		const int y1 = backEnd.viewDef->viewport.y1 + s->y1;
		const int x2 = backEnd.viewDef->viewport.x1 + s->x2 + 1;
		const int y2 = backEnd.viewDef->viewport.y1 + s->y2 + 1;
		r[ARB2_NV5_REC_CLIP * 4 + 0] = (float)x1;
		r[ARB2_NV5_REC_CLIP * 4 + 1] = (float)y1;
		r[ARB2_NV5_REC_CLIP * 4 + 2] = (float)x2;
		r[ARB2_NV5_REC_CLIP * 4 + 3] = (float)y2;
		if ( k == 0 ) {
			rb_nv5.clip[0] = x1;
			rb_nv5.clip[1] = y1;
			rb_nv5.clip[2] = x2;
			rb_nv5.clip[3] = y2;
		} else {
			if ( x1 < rb_nv5.clip[0] ) rb_nv5.clip[0] = x1;
			if ( y1 < rb_nv5.clip[1] ) rb_nv5.clip[1] = y1;
			if ( x2 > rb_nv5.clip[2] ) rb_nv5.clip[2] = x2;
			if ( y2 > rb_nv5.clip[3] ) rb_nv5.clip[3] = y2;
		}
	} else {
		// no scissor: a rectangle no pixel is outside
		r[ARB2_NV5_REC_CLIP * 4 + 0] = -1.0e9f;
		r[ARB2_NV5_REC_CLIP * 4 + 1] = -1.0e9f;
		r[ARB2_NV5_REC_CLIP * 4 + 2] = 1.0e9f;
		r[ARB2_NV5_REC_CLIP * 4 + 3] = 1.0e9f;
	}

	c = rb_nv5.cmd + k;
	c->count = (GLuint)geo->numIndexes;
	c->instanceCount = 1;
	c->firstIndex = (GLuint)rb_nv5.surfFirstIndex;
	c->baseVertex = rb_nv5.surfBaseVertex;
	c->baseInstance = (GLuint)k;

	backEnd.pc.c_drawElements++;
	backEnd.pc.c_drawIndexes += geo->numIndexes;
	backEnd.pc.c_drawVertexes += geo->numVerts;
	backEnd.pc.c_vboIndexes += geo->numIndexes;
}

/* 0 when the batch already names img's texture, else 1: a slot img may
 * need.  An image still loading counts as needing one. */
static int RB_ARB2NV5_NewUnit( const idImage *img ) {
	int i;
	if ( !img ) {
		return 0;
	}
	for ( i = 0; i < rb_nv5.slots; i++ ) {
		if ( rb_nv5.slotBatch[i] == rb_nv5.serial && rb_nv5.slotTex[i] == img->texnum ) {
			return 0;
		}
	}
	return 1;
}

/* Collect surf into the batch, flushing the batch first when surf cannot
 * join it.  False when surf has to be drawn through the NV4 path. */
static bool RB_ARB2NV5_AddSurface( const drawSurf_t *surf ) {
	const srfTriangles_t	*geo = surf->geo;
	vertCache_t	*vb;
	vertCache_t	*ib;
	GLuint		ring;
	int			isize;
	int			baseVertex;
	int			lightStages;
	int			stages;
	int			worstDraws;
	int			worstUnits;
	int			i;

	if ( r_skipInteractions.GetBool() || !geo || !geo->ambientCache || !geo->indexCache
			|| !r_useIndexBuffers.GetBool() || rb_nv5.ringFull
			|| surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
		return false;
	}
	vb = geo->ambientCache;
	ib = geo->indexCache;
	isize = ( vertexCache.IndexType( ib ) == GL_UNSIGNED_SHORT ) ? 2 : 4;
	if ( !ib->vbo || ( ib->offset % isize ) != 0 ) {
		return false;
	}
	baseVertex = vertexCache.BaseVertex( vb, sizeof( idDrawVert ) );
	if ( baseVertex < 0 ) {
		return false;
	}

	/* The most interactions surf can make - one per bump, diffuse or
	   specular stage for each light stage - and the most textures they
	   can name that the batch does not already: the falloff, the light
	   stages' projections, those stages' textures, and the black and
	   flat normal images RB_SubmittInteraction substitutes for missing
	   ones.  The batch is flushed first unless both still fit; after a
	   flush nothing is named, so the units are counted again in full. */
	lightStages = backEnd.vLight->lightShader->GetNumStages();
	stages = 0;
	worstUnits = RB_ARB2NV5_NewUnit( backEnd.vLight->falloffImage )
		+ RB_ARB2NV5_NewUnit( globalImages->blackImage )
		+ RB_ARB2NV5_NewUnit( globalImages->flatNormalMap );
	for ( i = 0; i < lightStages; i++ ) {
		worstUnits += RB_ARB2NV5_NewUnit( backEnd.vLight->lightShader->GetStage( i )->texture.image );
	}
	for ( i = 0; i < surf->material->GetNumStages(); i++ ) {
		const shaderStage_t *stage = surf->material->GetStage( i );
		if ( stage->lighting != SL_AMBIENT ) {
			stages++;
			worstUnits += RB_ARB2NV5_NewUnit( stage->texture.image );
		}
	}
	worstDraws = lightStages * stages;
	if ( worstDraws <= 0 ) {
		return true;	// nothing to draw
	}
	if ( worstDraws > ARB2_NV5_BATCH ) {
		return false;
	}
	if ( rb_nv5.count > 0
			&& ( surf->space != rb_nv5.space || vb->vbo != rb_nv5.vertexBlock->vbo
				|| ib->vbo != rb_nv5.indexBlock->vbo || vertexCache.IndexType( ib ) != rb_nv5.indexType
				|| rb_nv5.count + worstDraws > ARB2_NV5_BATCH || rb_nv5.pinned + worstUnits > rb_nv5.slots ) ) {
		RB_ARB2NV5_Flush();
		worstUnits = 3 + lightStages + stages;
	}
	if ( rb_nv5.pinned + worstUnits > rb_nv5.slots ) {
		return false;
	}

	if ( !rb_nv5.rec ) {
		/* Batches take consecutive draws of a chunk reserved once per
		   frame - the ring's segments are fenced per frame, so a chunk is
		   never written past the frame it was reserved in - and a new
		   chunk when a whole batch might not fit the rest. */
		if ( !rb_nv5.chunkRec || rb_nv5.chunkFrame != backEnd.frameCount
				|| rb_nv5.chunkUsed + ARB2_NV5_BATCH > ARB2_NV5_CHUNK ) {
			rb_nv5.chunkRec = (float *)vertexCache.RingReserve( ARB2_NV5_CHUNK * ARB2_NV5_REC_BYTES, 16,
				&ring, &rb_nv5.chunkRecOffset );
			rb_nv5.chunkCmd = rb_nv5.chunkRec ? (arb2Nv5Command_t *)vertexCache.RingReserve(
				ARB2_NV5_CHUNK * (int)sizeof( arb2Nv5Command_t ), 16, &ring, &rb_nv5.chunkCmdOffset ) : NULL;
			if ( !rb_nv5.chunkRec || !rb_nv5.chunkCmd ) {
				rb_nv5.chunkRec = NULL;
				rb_nv5.ringFull = true;
				return false;
			}
			rb_nv5.chunkUsed = 0;
			rb_nv5.chunkFrame = backEnd.frameCount;
			rb_nv5.chunkRing = ring;
		}
		rb_nv5.rec = rb_nv5.chunkRec + rb_nv5.chunkUsed * ARB2_NV5_REC_VECS * 4;
		rb_nv5.cmd = rb_nv5.chunkCmd + rb_nv5.chunkUsed;
		rb_nv5.recOffset = rb_nv5.chunkRecOffset + rb_nv5.chunkUsed * ARB2_NV5_REC_BYTES;
		rb_nv5.cmdOffset = rb_nv5.chunkCmdOffset + rb_nv5.chunkUsed * (int)sizeof( arb2Nv5Command_t );
		ring = rb_nv5.chunkRing;
		RB_ARB2NV5_RingAddress( ring );
		qglBindBufferARB( GL_DRAW_INDIRECT_BUFFER, ring );
	}

	rb_nv5.space = surf->space;
	rb_nv5.vertexBlock = vb;
	rb_nv5.indexBlock = ib;
	rb_nv5.indexType = vertexCache.IndexType( ib );
	rb_nv5.surfBaseVertex = baseVertex;
	rb_nv5.surfFirstIndex = (int)( ib->offset / isize );
	RB_CollectSingleDrawInteractions( surf, RB_ARB2NV5_Collect );
	return true;
}

/* A light's list on the NV5 tier: batches where they can be made, the
 * NV4 path for the surfaces they cannot take. */
static void RB_ARB2NV5_DrawList( const drawSurf_t *surf ) {
	idDrawVert	*ac;

	rb_nv5.count = 0;
	rb_nv5.rec = NULL;
	rb_nv5.pinned = 0;
	rb_nv5.pointerBuffer = 0;
	RB_ARB2NV5_BindPrograms( true );

	// the record offsets, fetched per draw through baseInstance
	qglBindBufferARB( GL_ARRAY_BUFFER_ARB, rb_nv5.idBuffer );
	glCaps.VertexAttribIPointer( 1, 1, GL_UNSIGNED_INT, 0, NULL );
	glCaps.VertexAttribDivisor( 1, 1 );
	qglEnableVertexAttribArrayARB( 1 );
	vertexCache.InvalidateBindings();

	for ( ; surf ; surf = surf->nextOnLight ) {
		if ( RB_ARB2NV5_AddSurface( surf ) ) {
			continue;
		}
		RB_ARB2NV5_Flush();
		if ( rb_nv5.batchedMode ) {
			RB_ARB2NV5_BindPrograms( false );
		}
		ac = (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );
		qglColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DRAWVERT( ac, color ) );
		qglVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, normal ) );
		qglVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, tangents[1] ) );
		qglVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, tangents[0] ) );
		qglVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, st ) );
		qglVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, xyz ) );
		rb_nv5.pointerBuffer = 0;
		RB_CreateSingleDrawInteractions( surf, RB_ARB2NV_DrawInteraction );
	}
	RB_ARB2NV5_Flush();

	qglDisableVertexAttribArrayARB( 1 );
	glCaps.VertexAttribDivisor( 1, 0 );
	qglBindBufferARB( GL_DRAW_INDIRECT_BUFFER, 0 );
}


/*
=============
RB_ARB2_CreateDrawInteractions

=============
*/
void RB_ARB2_CreateDrawInteractions( const drawSurf_t *surf ) {
	bool	ambient;
	bool	nv;
	bool	nv5;

	if ( !surf ) {
		return;
	}

	ambient = backEnd.vLight->lightShader->IsAmbientLight();
	nv = rb_nvInteraction && !r_testARBProgram.GetBool();
	nv5 = nv && rb_nv5.programs && vertexCache.Streaming();

	// perform setup here that will be constant for all interactions
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	// bind the vertex program
	if ( r_testARBProgram.GetBool() ) {
		qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_TEST );
		qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_TEST );
	} else if ( nv5 ) {
		// RB_ARB2NV5_DrawList binds its programs
	} else if ( nv ) {
		qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_NV_INTERACTION );
		qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, ambient ? FPROG_NV_INTERACTION_AMBIENT : FPROG_NV_INTERACTION );
	} else {
		qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_INTERACTION );
		qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_INTERACTION );
	}

	qglEnable(GL_VERTEX_PROGRAM_ARB);
	qglEnable(GL_FRAGMENT_PROGRAM_ARB);

	// enable the vertex arrays
	qglEnableVertexAttribArrayARB( 8 );
	qglEnableVertexAttribArrayARB( 9 );
	qglEnableVertexAttribArrayARB( 10 );
	qglEnableVertexAttribArrayARB( 11 );
	qglEnableClientState( GL_COLOR_ARRAY );

	// texture 0 is the normalization cube map for the vector towards the
	// light; the NV programs normalise it with NRM and only an ambient
	// light reads the unit
	if ( ambient ) {
		GL_SelectTextureNoClient( 0 );
		globalImages->ambientNormalMap->Bind();
	} else if ( !nv ) {
		GL_SelectTextureNoClient( 0 );
		globalImages->normalCubeMapImage->Bind();
	}

	// texture 6 is the specular lookup table
	GL_SelectTextureNoClient( 6 );
	if ( r_testARBProgram.GetBool() ) {
		globalImages->specular2DTableImage->Bind();	// variable specularity in alpha channel
	} else {
		globalImages->specularTableImage->Bind();
	}


	/* DG: brightness and gamma in shader as program.env[21].
	 *
	 * This was uploaded from RB_ARB2_DrawInteraction, so every light
	 * stage x surface stage pair paid for it - including the pow() that
	 * RB_HDRGammaBrightness does when the scene encode scale is not 1,
	 * and a float division.  Its inputs (r_brightness, r_gamma, the
	 * encode scale) cannot change while this loop runs, and nothing
	 * inside the loop writes env[21], so it is computed and sent once
	 * for the whole set of interactions. */
	if ( r_gammaInShader.GetBool() ) {
		float parm[4];
		parm[0] = parm[1] = parm[2] = RB_HDRGammaBrightness( false );
		parm[3] = 1.0/r_gamma.GetFloat(); // 1.0/gamma so the shader doesn't have to do this calculation
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, PP_GAMMA_BRIGHTNESS, parm );
	}

	RB_EnvCacheReset();
	rb_envSpans = glCaps.programEnvParameters;
	RB_UpdateSpecularBoost();

	if ( nv5 ) {
		RB_ARB2NV5_DrawList( surf );
		surf = NULL;
	}
	for ( ; surf ; surf=surf->nextOnLight ) {
		// perform setup here that will not change over multiple interaction passes

		// set the vertex pointers
		idDrawVert	*ac = (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );
		qglColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DRAWVERT( ac, color ) );
		qglVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, normal ) );
		qglVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, tangents[1] ) );
		qglVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, tangents[0] ) );
		qglVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, st ) );
		qglVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, xyz ) );

		// this may cause RB_ARB2_DrawInteraction to be exacuted multiple
		// times with different colors and images if the surface or light have multiple layers
		RB_CreateSingleDrawInteractions( surf, nv ? RB_ARB2NV_DrawInteraction : RB_ARB2_DrawInteraction );
	}

	RB_EnvCacheReset();
	rb_envSpans = false;

	qglDisableVertexAttribArrayARB( 8 );
	qglDisableVertexAttribArrayARB( 9 );
	qglDisableVertexAttribArrayARB( 10 );
	qglDisableVertexAttribArrayARB( 11 );
	qglDisableClientState( GL_COLOR_ARRAY );

	// disable features
	GL_SelectTextureNoClient( 6 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 5 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 4 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 3 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 2 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 1 );
	globalImages->BindNull();

	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );

	qglDisable(GL_VERTEX_PROGRAM_ARB);
	qglDisable(GL_FRAGMENT_PROGRAM_ARB);
}


/*
   Shadow volumes built on the GPU, on the NV5 tier where the context has
   geometry programs, global stores and transform feedback.

   R_CreateVertexProgramTurboShadowVolume finds which triangles face the
   light, counts the ones outside the light frustum as facing, and writes
   a quad for each sil edge between a facing and a back-facing triangle,
   then the two caps of each back-facing triangle, as indexes into the
   shadow cache.  Here two geometry programs do the same from the source
   surface's shadow cache when the volume is drawn:

   - the cap program, over the surface's triangles, finds each one's
     facing with R_DeriveFacePlanes' plane and the dot with the light
     origin - the CPU's operations in the CPU's order, each .PREC so none
     is fused or reordered, and idMath::RSqrt's integer step on the
     float's bits - then the frustum cull the same way; it stores the
     facing for the sil pass and emits the back-facing triangle's caps;

   - the sil program, over the sil edges drawn as lines (v1, v2), loads
     the facing of the edge's two triangles from a table of them and
     emits the quad when they differ.

   Each captures positions with transform feedback, rasterizer off, and
   the stencil passes draw the captures through shadow.vp: the vertexes
   the CPU's indexes name, in the same order, so the same pixels.  The
   facing array is a frame's linear allocation, a barrier ahead of the
   capture that reuses its memory.
*/
#ifndef GL_GEOMETRY_PROGRAM_NV
#define GL_GEOMETRY_PROGRAM_NV                  0x8C26
#endif
#ifndef GL_TRANSFORM_FEEDBACK
#define GL_TRANSFORM_FEEDBACK                   0x8E22
#endif
#ifndef GL_TRANSFORM_FEEDBACK_BUFFER
#define GL_TRANSFORM_FEEDBACK_BUFFER            0x8C8E
#endif
#ifndef GL_RASTERIZER_DISCARD
#define GL_RASTERIZER_DISCARD                   0x8C89
#endif
#ifndef GL_INTERLEAVED_ATTRIBS
#define GL_INTERLEAVED_ATTRIBS                  0x8C8C
#endif
#ifndef GL_COPY_READ_BUFFER
#define GL_COPY_READ_BUFFER                     0x8F36
#endif
#ifndef GL_READ_WRITE
#define GL_READ_WRITE                           0x88BA
#endif
#ifndef GL_ALL_BARRIER_BITS
#define GL_ALL_BARRIER_BITS                     0xFFFFFFFF
#endif
#ifndef GL_SHADER_GLOBAL_ACCESS_BARRIER_BIT_NV
#define GL_SHADER_GLOBAL_ACCESS_BARRIER_BIT_NV  0x00000010
#endif

#define ARB2_GS_ENV_FLOATS		11			// geometry program.env[0..10]: light origin, planes, cull masks
#define ARB2_GS_ENV_FACING		11			// uint: the capture's facing array address, low and high words
#define ARB2_GS_ENV_TABLE		12			// uint: the sil edges' triangle table address
#define ARB2_GS_ENV_PLANES		13			// uint: the source's face planes' address
#define ARB2_GS_STORE_BYTES		0x100000	// a sil edge store buffer, or the one edge set that outgrows it
#define ARB2_GS_MAX_STORES		32
#define ARB2_GS_NO_FACE			0xffffffffu	// a dangling edge's missing triangle, which counts as facing
#define ARB2_GS_MAX_VOLUME_STORES	16		// stores of volumes kept between frames

/* What a store holds for a surface, by the pointer it was made from: its
 * sil edges (per edge v1 v2 as line indexes, then per edge p1 p2), or its
 * face planes. */
typedef struct {
	const void		*key;		// NULL in an empty slot
	int				numEdges;
	int				store;
	unsigned int	offset;
	unsigned int	bytes;
	unsigned int	serial;		// which upload of the key this is
} arb2SilEntry_t;

// a volume kept between frames: its owner's gpuShadow_t names it by index
typedef struct {
	const gpuShadow_t	*owner;			// NULL when free
	GLuint				query[2];		// primitives the sil and cap passes wrote
	unsigned int		offset;			// in volumeStore[store]: the sil planes, then the caps
	unsigned int		bytes;
	int					store;
	int					silVerts;
	int					capVerts;
	int					prev, next;		// stored volumes, most recently drawn first; free ones through next
	int					lastFrame;
	int					retryFrame;
	int					state;
	unsigned int		planeSerial;	// the face planes its capture was made from
} arb2GsVolume_t;

typedef struct {
	arb2SilEntry_t		*entries;		// open addressing on the key, linear probes
	unsigned long long	storeAddress[ARB2_GS_MAX_STORES];
	range_alloc_t		storeRanges[ARB2_GS_MAX_STORES];
	GLuint				store[ARB2_GS_MAX_STORES];
	unsigned long long	facingAddress;
	GLuint				tfo[2];			// the sil planes, the caps
	GLuint				tfBuffer[2];
	GLuint				facing;			// a float per triangle, written by the cap program
	int					tfBytes[2];
	int					numStores;
	unsigned int		serial;
	int					numEntries;
	int					maxEntries;		// a power of two, 0 when none
	int					facingBytes;
	int					facingUsed;		// what this frame's captures took
	int					facingFrame;
	arb2GsVolume_t		*volumes;
	const arb2GsVolume_t *drawn;		// what RB_ARB2_DrawGpuShadow draws; NULL: the transform feedback buffers
	range_alloc_t		volumeRanges[ARB2_GS_MAX_VOLUME_STORES];
	GLuint				volumeStore[ARB2_GS_MAX_VOLUME_STORES];
	int					numVolumeStores;
	int					numVolumes;
	int					maxVolumes;
	int					freeVolume;
	int					lruHead, lruTail;
	int					epoch;			// a new context's volumes are new ones
	bool				buffers;		// the transform feedback objects and buffers exist
	bool				queries;		// volumes are kept
} arb2GpuShadow_t;

static arb2GpuShadow_t	rb_gs;

static ID_INLINE int RB_GS_Home( const void *key ) {
	return (int)( ( (unsigned int)( (uintptr_t)key >> 4 ) * 2654435761u ) & (unsigned int)( rb_gs.maxEntries - 1 ) );
}

// the slot holding key, or the empty slot it would go in
static int RB_GS_Slot( const void *key ) {
	int i = RB_GS_Home( key );
	while ( rb_gs.entries[i].key != NULL && rb_gs.entries[i].key != key ) {
		i = ( i + 1 ) & ( rb_gs.maxEntries - 1 );
	}
	return i;
}

static bool RB_GS_Grow( void ) {
	arb2SilEntry_t	*old;
	int				oldMax;
	int				i;

	old = rb_gs.entries;
	oldMax = rb_gs.maxEntries;
	rb_gs.maxEntries = oldMax ? oldMax * 2 : 256;
	rb_gs.entries = (arb2SilEntry_t *)Mem_ClearedAlloc( rb_gs.maxEntries * sizeof( arb2SilEntry_t ) );
	if ( !rb_gs.entries ) {
		rb_gs.entries = old;
		rb_gs.maxEntries = oldMax;
		return false;
	}
	for ( i = 0; i < oldMax; i++ ) {
		if ( old[i].key ) {
			rb_gs.entries[RB_GS_Slot( old[i].key )] = old[i];
		}
	}
	Mem_Free( old );
	return true;
}

/*
==================
RB_ARB2_ForgetShadowData
==================
*/
void RB_ARB2_ForgetShadowData( const void *key ) {
	arb2SilEntry_t	*e;
	int				i, j, k;

	if ( !rb_gs.numEntries || !key ) {
		return;
	}
	i = RB_GS_Slot( key );
	e = rb_gs.entries;
	if ( e[i].key == NULL ) {
		return;
	}
	range_alloc_free( &rb_gs.storeRanges[e[i].store], e[i].offset, e[i].bytes );
	rb_gs.numEntries--;

	// close the gap: pull back each later entry of the run that may live here
	j = i;
	for ( ;; ) {
		j = ( j + 1 ) & ( rb_gs.maxEntries - 1 );
		if ( e[j].key == NULL ) {
			break;
		}
		k = RB_GS_Home( e[j].key );
		if ( i <= j ? ( i < k && k <= j ) : ( i < k || k <= j ) ) {
			continue;
		}
		e[i] = e[j];
		i = j;
	}
	e[i].key = NULL;
}

// a buffer for the sil edge stores, resident for the sil program's loads
static bool RB_GS_NewStore( unsigned int bytes ) {
	const int	n = rb_gs.numStores;
	GLuint		buffer;

	if ( n == ARB2_GS_MAX_STORES || !range_alloc_init( &rb_gs.storeRanges[n], bytes ) ) {
		return false;
	}
	buffer = 0;
	qglGenBuffersARB( 1, &buffer );
	qglBindBufferARB( GL_COPY_WRITE_BUFFER, buffer );
	qglBufferDataARB( GL_COPY_WRITE_BUFFER, bytes, NULL, GL_STATIC_DRAW_ARB );
	glCaps.MakeBufferResidentNV( GL_COPY_WRITE_BUFFER, GL_READ_ONLY );
	glCaps.GetBufferParameterui64vNV( GL_COPY_WRITE_BUFFER, GL_BUFFER_GPU_ADDRESS_NV, &rb_gs.storeAddress[n] );
	qglBindBufferARB( GL_COPY_WRITE_BUFFER, 0 );
	rb_gs.store[n] = buffer;
	rb_gs.numStores++;
	return true;
}

typedef void ( *arb2GsFill_t )( const srfTriangles_t *src, void *data );

static void RB_GS_FillSilEdges( const srfTriangles_t *src, void *out ) {
	GLuint		*data = (GLuint *)out;
	const int	numFaces = src->numIndexes / 3;
	int			i;

	for ( i = 0; i < src->numSilEdges; i++ ) {
		const silEdge_t *sil = &src->silEdges[i];
		data[i * 2 + 0] = (GLuint)sil->v1;
		data[i * 2 + 1] = (GLuint)sil->v2;
		data[src->numSilEdges * 2 + i * 2 + 0] = (GLuint)sil->p1;
		data[src->numSilEdges * 2 + i * 2 + 1] = sil->p2 < numFaces ? (GLuint)sil->p2 : ARB2_GS_NO_FACE;
	}
}

static void RB_GS_FillPlanes( const srfTriangles_t *src, void *out ) {
	memcpy( out, src->facePlanes, ( src->numIndexes / 3 ) * sizeof( src->facePlanes[0] ) );
}

// bytes into buffer at offset, made by fill: in the ring and copied on the GPU, or staged where it is full
static void RB_GS_Write( GLuint buffer, unsigned int offset, unsigned int bytes, arb2GsFill_t fill,
		const srfTriangles_t *src ) {
	void	*dst;
	void	*data;
	GLuint	ring;
	int		ringOffset;

	dst = glCaps.copyBuffer ? vertexCache.RingReserve( (int)bytes, 16, &ring, &ringOffset ) : NULL;
	data = dst ? dst : Mem_Alloc( bytes );
	fill( src, data );
	if ( dst ) {
		qglBindBufferARB( GL_COPY_READ_BUFFER, ring );
		qglBindBufferARB( GL_COPY_WRITE_BUFFER, buffer );
		glCaps.CopyBufferSubData( GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, ringOffset, offset, bytes );
		qglBindBufferARB( GL_COPY_READ_BUFFER, 0 );
	} else {
		qglBindBufferARB( GL_COPY_WRITE_BUFFER, buffer );
		qglBufferSubDataARB( GL_COPY_WRITE_BUFFER, offset, bytes, data );
		Mem_Free( data );
	}
	qglBindBufferARB( GL_COPY_WRITE_BUFFER, 0 );
}

// the store entry made from key, uploaded by fill the first time it is drawn
static const arb2SilEntry_t *RB_GS_Entry( const void *key, unsigned int bytes, arb2GsFill_t fill,
		const srfTriangles_t *src ) {
	arb2SilEntry_t	*e;
	unsigned int	offset;
	int				store;

	if ( !rb_gs.maxEntries || ( rb_gs.numEntries + 1 ) * 2 > rb_gs.maxEntries ) {
		if ( !RB_GS_Grow() ) {
			return NULL;
		}
	}
	e = &rb_gs.entries[RB_GS_Slot( key )];
	if ( e->key ) {
		return e;
	}

	for ( store = 0; store < rb_gs.numStores; store++ ) {
		if ( range_alloc_alloc( &rb_gs.storeRanges[store], bytes, 16, &offset ) ) {
			break;
		}
	}
	if ( store == rb_gs.numStores ) {
		if ( !RB_GS_NewStore( bytes > ARB2_GS_STORE_BYTES ? bytes : ARB2_GS_STORE_BYTES )
				|| !range_alloc_alloc( &rb_gs.storeRanges[store], bytes, 16, &offset ) ) {
			return NULL;
		}
	}

	RB_GS_Write( rb_gs.store[store], offset, bytes, fill, src );

	e->key = key;
	e->numEdges = src->numSilEdges;
	e->store = store;
	e->offset = offset;
	e->bytes = bytes;
	e->serial = ++rb_gs.serial;
	rb_gs.numEntries++;
	return e;
}

// a transform feedback buffer of at least bytes, bound to its object
static void RB_GS_Feedback( int i, int bytes ) {
	if ( rb_gs.tfBytes[i] < bytes ) {
		int size = rb_gs.tfBytes[i] ? rb_gs.tfBytes[i] : 0x40000;
		while ( size < bytes ) {
			size *= 2;
		}
		qglBindBufferARB( GL_TRANSFORM_FEEDBACK_BUFFER, rb_gs.tfBuffer[i] );
		qglBufferDataARB( GL_TRANSFORM_FEEDBACK_BUFFER, size, NULL, GL_STREAM_COPY_ARB );
		rb_gs.tfBytes[i] = size;
	}
	glCaps.BindTransformFeedback( GL_TRANSFORM_FEEDBACK, rb_gs.tfo[i] );
	glCaps.BindBufferRange( GL_TRANSFORM_FEEDBACK_BUFFER, 0, rb_gs.tfBuffer[i], 0, rb_gs.tfBytes[i] );
}

/*
   A volume whose source surface is not rewritten every frame - a static
   entity's, a fixed light's - captures the same triangles each time it
   is drawn, so it is captured once and drawn from its own range of a
   volume store afterwards.  Its first capture counts what it writes with
   queries, read when the GPU has answered, never waited on; once known,
   a capture into a range of exactly that size keeps it.  Ranges come
   from a few stores, least recently drawn evicted when they are full.
   A deformed source, and a volume rebuilt every frame, never gets that
   far and is captured each time it is drawn.
*/
#define ARB2_GS_VOLUME_BYTES	0x400000	// a volume store, or the one volume that outgrows it
#define ARB2_GS_RETRY			64			// frames before a volume that found no room tries again

#ifndef GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN
#define GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN	0x8C88
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT							0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE				0x8867
#endif

enum {
	ARB2_GS_PENDING = 1,	// counting queries in flight
	ARB2_GS_COUNTED,		// sizes known, nothing stored
	ARB2_GS_STORED			// in volumeStore[store] at offset, or empty
};

static ID_INLINE arb2GsVolume_t *RB_GS_Volume( const gpuShadow_t *gs ) {
	if ( gs->volumeEpoch != rb_gs.epoch || gs->volume < 0 || gs->volume >= rb_gs.numVolumes
			|| rb_gs.volumes[gs->volume].owner != gs ) {
		return NULL;
	}
	return &rb_gs.volumes[gs->volume];
}

static void RB_GS_Unlink( int i ) {
	arb2GsVolume_t *v = &rb_gs.volumes[i];

	if ( v->prev >= 0 ) {
		rb_gs.volumes[v->prev].next = v->next;
	} else {
		rb_gs.lruHead = v->next;
	}
	if ( v->next >= 0 ) {
		rb_gs.volumes[v->next].prev = v->prev;
	} else {
		rb_gs.lruTail = v->prev;
	}
	v->prev = v->next = -1;
}

// its range given back; it is captured again when next drawn
static void RB_GS_Unstore( int i ) {
	arb2GsVolume_t *v = &rb_gs.volumes[i];

	if ( v->state == ARB2_GS_STORED && v->bytes ) {
		range_alloc_free( &rb_gs.volumeRanges[v->store], v->offset, v->bytes );
		RB_GS_Unlink( i );
	}
	if ( v->state == ARB2_GS_STORED ) {
		v->state = ARB2_GS_COUNTED;
	}
}

/*
==================
RB_ARB2_ForgetGpuShadow
==================
*/
void RB_ARB2_ForgetGpuShadow( const gpuShadow_t *gs ) {
	arb2GsVolume_t *v = RB_GS_Volume( gs );

	if ( v ) {
		RB_GS_Unstore( gs->volume );
		v->owner = NULL;
		v->next = rb_gs.freeVolume;
		rb_gs.freeVolume = gs->volume;
	}
}

// a volume entry for gs, counting queries and all
static arb2GsVolume_t *RB_GS_NewVolume( gpuShadow_t *gs ) {
	arb2GsVolume_t	*v;
	int				i;

	if ( rb_gs.freeVolume >= 0 ) {
		i = rb_gs.freeVolume;
		rb_gs.freeVolume = rb_gs.volumes[i].next;
	} else {
		if ( rb_gs.numVolumes == rb_gs.maxVolumes ) {
			const int		max = rb_gs.maxVolumes ? rb_gs.maxVolumes * 2 : 256;
			arb2GsVolume_t	*grown = (arb2GsVolume_t *)Mem_ClearedAlloc( max * sizeof( arb2GsVolume_t ) );
			if ( !grown ) {
				return NULL;
			}
			if ( rb_gs.volumes ) {
				memcpy( grown, rb_gs.volumes, rb_gs.numVolumes * sizeof( arb2GsVolume_t ) );
				Mem_Free( rb_gs.volumes );
			}
			rb_gs.volumes = grown;
			rb_gs.maxVolumes = max;
		}
		i = rb_gs.numVolumes;
		glCaps.GenQueries( 2, rb_gs.volumes[i].query );
		if ( !rb_gs.volumes[i].query[0] || !rb_gs.volumes[i].query[1] ) {
			return NULL;
		}
		rb_gs.numVolumes++;
	}
	v = &rb_gs.volumes[i];
	v->owner = gs;
	v->state = ARB2_GS_PENDING;
	v->store = -1;
	v->bytes = 0;
	v->prev = v->next = -1;
	v->retryFrame = 0;
	gs->volume = i;
	gs->volumeEpoch = rb_gs.epoch;
	return v;
}

// a range of a volume store, evicting what was not drawn this frame
static bool RB_GS_StoreRange( arb2GsVolume_t *v ) {
	int s;

	for ( ;; ) {
		for ( s = 0; s < rb_gs.numVolumeStores; s++ ) {
			if ( range_alloc_alloc( &rb_gs.volumeRanges[s], v->bytes, 16, &v->offset ) ) {
				v->store = s;
				return true;
			}
		}
		if ( rb_gs.numVolumeStores < ARB2_GS_MAX_VOLUME_STORES ) {
			const unsigned int size = v->bytes > ARB2_GS_VOLUME_BYTES ? v->bytes : ARB2_GS_VOLUME_BYTES;
			GLuint buffer = 0;
			if ( !range_alloc_init( &rb_gs.volumeRanges[s], size ) ) {
				return false;
			}
			qglGenBuffersARB( 1, &buffer );
			qglBindBufferARB( GL_COPY_WRITE_BUFFER, buffer );
			qglBufferDataARB( GL_COPY_WRITE_BUFFER, size, NULL, GL_STATIC_COPY_ARB );
			qglBindBufferARB( GL_COPY_WRITE_BUFFER, 0 );
			rb_gs.volumeStore[s] = buffer;
			rb_gs.numVolumeStores++;
			continue;
		}
		if ( rb_gs.lruTail < 0 || rb_gs.volumes[rb_gs.lruTail].lastFrame == backEnd.frameCount ) {
			return false;
		}
		RB_GS_Unstore( rb_gs.lruTail );
	}
}

// what the queries counted, when the GPU has answered
static bool RB_GS_Counted( arb2GsVolume_t *v ) {
	GLuint	available[2];
	GLuint	written[2];
	int		i;

	for ( i = 0; i < 2; i++ ) {
		available[i] = 0;
		glCaps.GetQueryObjectuiv( v->query[i], GL_QUERY_RESULT_AVAILABLE, &available[i] );
	}
	if ( !available[0] || !available[1] ) {
		return false;
	}
	for ( i = 0; i < 2; i++ ) {
		written[i] = 0;
		glCaps.GetQueryObjectuiv( v->query[i], GL_QUERY_RESULT, &written[i] );
	}
	v->silVerts = (int)written[0] * 3;
	v->capVerts = (int)written[1] * 3;
	v->bytes = (unsigned int)( v->silVerts + v->capVerts ) * 16;
	return true;
}

/*
   The two capture passes: into the transform feedback buffers when v is
   NULL or not stored, counted when it is pending, else into its range.
   The face planes are planes' when the source keeps them, else this
   frame's, read where they were written in the ring; behind the facing
   array, in the rare frame the ring is full.
*/
static void RB_GS_Run( const srfTriangles_t *tri, const arb2SilEntry_t *sil, const arb2SilEntry_t *planes,
		const arb2GsVolume_t *v, bool count ) {
	static const GLint	attribs[3] = { GL_POSITION, 4, 0 };
	const gpuShadow_t	*gs = tri->gpuShadow;
	const srfTriangles_t *src = gs->source;
	const bool			stored = v && v->state == ARB2_GS_STORED;
	unsigned long long	address;
	float				env[ARB2_GS_ENV_FLOATS][4];
	unsigned long long	planeAddress = 0;
	int					numFaces;
	int					facingBytes;
	int					need;
	int					i;
	bool				reuse;

	// this capture's facing array, and the face planes of a source rewritten every frame
	numFaces = src->numIndexes / 3;
	facingBytes = ( numFaces * 4 + 15 ) & ~15;
	need = facingBytes;
	if ( !planes ) {
		GLuint	ring;
		int		ringOffset;
		void	*p = vertexCache.RingReserve( numFaces * 16, 16, &ring, &ringOffset );
		if ( p ) {
			RB_GS_FillPlanes( src, p );
			planeAddress = RB_ARB2NV5_RingAddress( ring ) + (unsigned long long)ringOffset;
		} else {
			need += numFaces * 16;
		}
	}
	reuse = false;
	if ( rb_gs.facingFrame != backEnd.frameCount ) {
		rb_gs.facingFrame = backEnd.frameCount;
		rb_gs.facingUsed = 0;
		reuse = true;
	}
	if ( rb_gs.facingUsed + need > rb_gs.facingBytes ) {
		if ( need > rb_gs.facingBytes ) {
			int size = rb_gs.facingBytes ? rb_gs.facingBytes : 0x40000;
			while ( size < need ) {
				size *= 2;
			}
			// respecifying the store makes the buffer non-resident
			qglBindBufferARB( GL_COPY_WRITE_BUFFER, rb_gs.facing );
			qglBufferDataARB( GL_COPY_WRITE_BUFFER, size, NULL, GL_STREAM_COPY_ARB );
			glCaps.MakeBufferResidentNV( GL_COPY_WRITE_BUFFER, GL_READ_WRITE );
			glCaps.GetBufferParameterui64vNV( GL_COPY_WRITE_BUFFER, GL_BUFFER_GPU_ADDRESS_NV, &rb_gs.facingAddress );
			qglBindBufferARB( GL_COPY_WRITE_BUFFER, 0 );
			rb_gs.facingBytes = size;
		}
		rb_gs.facingUsed = 0;
		reuse = true;
	}

	// the programs' parameters: planes four to a vector, one component each
	memset( env, 0, sizeof( env ) );
	env[0][0] = gs->lightOrigin[0];
	env[0][1] = gs->lightOrigin[1];
	env[0][2] = gs->lightOrigin[2];
	env[0][3] = (float)R_ShadowDotOrder();
	for ( i = 0; i < 6; i++ ) {
		const int v4 = i < 4 ? 1 : 5;
		const int c = i < 4 ? i : i - 4;
		env[v4 + 0][c] = gs->clipPlanes[i][0];
		env[v4 + 1][c] = gs->clipPlanes[i][1];
		env[v4 + 2][c] = gs->clipPlanes[i][2];
		env[v4 + 3][c] = gs->clipPlanes[i][3];
		env[i < 4 ? 9 : 10][c] = ( gs->cullPlanes & ( 1 << i ) ) ? 1.0f : 0.0f;
	}
	if ( glCaps.programEnvParameters ) {
		glCaps.ProgramEnvParameters4fvEXT( GL_GEOMETRY_PROGRAM_NV, 0, ARB2_GS_ENV_FLOATS, env[0] );
	} else {
		for ( i = 0; i < ARB2_GS_ENV_FLOATS; i++ ) {
			qglProgramEnvParameter4fvARB( GL_GEOMETRY_PROGRAM_NV, i, env[i] );
		}
	}
	address = rb_gs.facingAddress + (unsigned long long)rb_gs.facingUsed;
	glCaps.ProgramEnvParameterI4uiNV( GL_GEOMETRY_PROGRAM_NV, ARB2_GS_ENV_FACING,
		(GLuint)( address & 0xffffffffu ), (GLuint)( address >> 32 ), 0, 0 );
	address = rb_gs.storeAddress[sil->store] + (unsigned long long)( sil->offset + (unsigned int)sil->numEdges * 8 );
	glCaps.ProgramEnvParameterI4uiNV( GL_GEOMETRY_PROGRAM_NV, ARB2_GS_ENV_TABLE,
		(GLuint)( address & 0xffffffffu ), (GLuint)( address >> 32 ), 0, 0 );
	if ( planes ) {
		address = rb_gs.storeAddress[planes->store] + (unsigned long long)planes->offset;
	} else if ( planeAddress ) {
		address = planeAddress;
	} else {
		// the copy must not overtake reads of the bytes it replaces
		glCaps.MemoryBarrier( GL_ALL_BARRIER_BITS );
		RB_GS_Write( rb_gs.facing, (unsigned int)( rb_gs.facingUsed + facingBytes ),
			(unsigned int)numFaces * 16, RB_GS_FillPlanes, src );
		address = rb_gs.facingAddress + (unsigned long long)( rb_gs.facingUsed + facingBytes );
	}
	glCaps.ProgramEnvParameterI4uiNV( GL_GEOMETRY_PROGRAM_NV, ARB2_GS_ENV_PLANES,
		(GLuint)( address & 0xffffffffu ), (GLuint)( address >> 32 ), 0, 0 );
	rb_gs.facingUsed += need;

	qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_NV5_SHADOW );
	qglEnable( GL_GEOMETRY_PROGRAM_NV );
	qglEnable( GL_RASTERIZER_DISCARD );
	glCaps.TransformFeedbackAttribsNV( 1, attribs, GL_INTERLEAVED_ATTRIBS );

	// the shadow cache's w = 1 vertexes
	qglVertexPointer( 4, GL_FLOAT, 2 * sizeof( shadowCache_t ), vertexCache.Position( tri->shadowCache ) );

	// facing, and the caps; a stored volume without caps still needs the facing
	if ( reuse ) {
		glCaps.MemoryBarrier( GL_SHADER_GLOBAL_ACCESS_BARRIER_BIT_NV );
	}
	qglBindProgramARB( GL_GEOMETRY_PROGRAM_NV, GPROG_NV5_SHADOW_CAPS );
	if ( stored && v->capVerts ) {
		glCaps.BindTransformFeedback( GL_TRANSFORM_FEEDBACK, rb_gs.tfo[1] );
		glCaps.BindBufferRange( GL_TRANSFORM_FEEDBACK_BUFFER, 0, rb_gs.volumeStore[v->store],
			v->offset + v->silVerts * 16, v->capVerts * 16 );
	} else {
		RB_GS_Feedback( 1, stored ? 16 : numFaces * 6 * 16 );
	}
	if ( count ) {
		glCaps.BeginQuery( GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, v->query[1] );
	}
	glCaps.BeginTransformFeedback( GL_TRIANGLES );
	if ( src->indexCache && r_useIndexBuffers.GetBool() ) {
		qglDrawElements( GL_TRIANGLES, src->numIndexes, vertexCache.IndexType( src->indexCache ),
			vertexCache.Position( src->indexCache ) );
	} else {
		vertexCache.UnbindIndex();
		qglDrawElements( GL_TRIANGLES, src->numIndexes, GL_INDEX_TYPE, src->indexes );
	}
	glCaps.EndTransformFeedback();
	if ( count ) {
		glCaps.EndQuery( GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN );
	}
	glCaps.MemoryBarrier( GL_SHADER_GLOBAL_ACCESS_BARRIER_BIT_NV );

	// the sil planes
	if ( !stored || v->silVerts ) {
		qglBindProgramARB( GL_GEOMETRY_PROGRAM_NV, GPROG_NV5_SHADOW_SIL );
		if ( stored ) {
			glCaps.BindTransformFeedback( GL_TRANSFORM_FEEDBACK, rb_gs.tfo[0] );
			glCaps.BindBufferRange( GL_TRANSFORM_FEEDBACK_BUFFER, 0, rb_gs.volumeStore[v->store],
				v->offset, v->silVerts * 16 );
		} else {
			RB_GS_Feedback( 0, sil->numEdges * 6 * 16 );
		}
		qglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, rb_gs.store[sil->store] );
		if ( count ) {
			glCaps.BeginQuery( GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, v->query[0] );
		}
		glCaps.BeginTransformFeedback( GL_TRIANGLES );
		qglDrawElements( GL_LINES, sil->numEdges * 2, GL_UNSIGNED_INT, (const void *)(intptr_t)sil->offset );
		glCaps.EndTransformFeedback();
		if ( count ) {
			glCaps.EndQuery( GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN );
		}
	}

	glCaps.BindTransformFeedback( GL_TRANSFORM_FEEDBACK, 0 );
	qglDisable( GL_RASTERIZER_DISCARD );
	qglDisable( GL_GEOMETRY_PROGRAM_NV );
	qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
	vertexCache.InvalidateBindings();
}

/*
==================
RB_ARB2_CaptureGpuShadow

Readies a gpuShadow_t volume for RB_ARB2_DrawGpuShadow: from its range
of a volume store, or captured into the transform feedback buffers.
Called with shadow.vp bound, which it leaves bound.
==================
*/
bool RB_ARB2_CaptureGpuShadow( const srfTriangles_t *tri ) {
	gpuShadow_t			*gs = tri->gpuShadow;
	const srfTriangles_t *src = gs->source;
	const arb2SilEntry_t *sil;
	const arb2SilEntry_t *planes;
	arb2GsVolume_t		*v;
	bool				count;

	if ( !r_gpuShadowVolumes || !tri->shadowCache || src->numSilEdges <= 0
			|| !src->facePlanes || !src->facePlanesCalculated ) {
		return false;
	}
	sil = RB_GS_Entry( src->silEdges, (unsigned int)src->numSilEdges * 16, RB_GS_FillSilEdges, src );
	planes = NULL;
	if ( !src->deformedSurface ) {
		planes = RB_GS_Entry( src->facePlanes, (unsigned int)( src->numIndexes / 3 ) * 16, RB_GS_FillPlanes, src );
		if ( !planes ) {
			return false;
		}
	}
	if ( !sil ) {
		return false;
	}

	/* A volume is kept from its second frame on: one built for a single
	   frame never pays for its queries.  One made from face planes since
	   rewritten is made again. */
	v = RB_GS_Volume( gs );
	if ( v && planes && v->planeSerial != planes->serial ) {
		RB_ARB2_ForgetGpuShadow( gs );
		gs->firstFrame = 0;
		v = NULL;
	}
	count = false;
	if ( !v && rb_gs.queries && !src->deformedSurface ) {
		if ( !gs->firstFrame ) {
			gs->firstFrame = backEnd.frameCount + 1;
		} else if ( gs->firstFrame != backEnd.frameCount + 1 ) {
			v = RB_GS_NewVolume( gs );
			count = v != NULL;
			if ( v ) {
				v->planeSerial = planes ? planes->serial : 0;
			}
		}
	}

	if ( v && !count ) {
		if ( v->state == ARB2_GS_PENDING && RB_GS_Counted( v ) ) {
			v->state = ARB2_GS_COUNTED;
		}
		if ( v->state == ARB2_GS_COUNTED && backEnd.frameCount >= v->retryFrame ) {
			if ( !v->bytes ) {
				v->state = ARB2_GS_STORED;
			} else if ( RB_GS_StoreRange( v ) ) {
				v->state = ARB2_GS_STORED;
				v->lastFrame = backEnd.frameCount;
				RB_GS_Run( tri, sil, planes, v, false );
			} else {
				v->retryFrame = backEnd.frameCount + ARB2_GS_RETRY;
			}
		}
		if ( v->state == ARB2_GS_STORED ) {
			if ( v->bytes ) {
				const int i = (int)( v - rb_gs.volumes );
				RB_GS_Unlink( i );
				v->next = rb_gs.lruHead;
				if ( rb_gs.lruHead >= 0 ) {
					rb_gs.volumes[rb_gs.lruHead].prev = i;
				} else {
					rb_gs.lruTail = i;
				}
				rb_gs.lruHead = i;
			}
			v->lastFrame = backEnd.frameCount;
			rb_gs.drawn = v;
			return true;
		}
	}

	RB_GS_Run( tri, sil, planes, v, count );
	rb_gs.drawn = NULL;
	return true;
}

/*
==================
RB_ARB2_DrawGpuShadow

The sil planes, and the caps when numIndexes asks for them.
==================
*/
void RB_ARB2_DrawGpuShadow( const srfTriangles_t *tri, int numIndexes ) {
	const arb2GsVolume_t	*v = rb_gs.drawn;
	const bool				caps = numIndexes == tri->numIndexes;
	int						i;

	if ( v ) {
		if ( v->bytes ) {
			qglBindBufferARB( GL_ARRAY_BUFFER_ARB, rb_gs.volumeStore[v->store] );
			qglVertexPointer( 4, GL_FLOAT, sizeof( shadowCache_t ), (const void *)(intptr_t)v->offset );
			if ( caps ? v->silVerts + v->capVerts : v->silVerts ) {
				qglDrawArrays( GL_TRIANGLES, 0, caps ? v->silVerts + v->capVerts : v->silVerts );
			}
		}
	} else {
		for ( i = 0; i < ( caps ? 2 : 1 ); i++ ) {
			qglBindBufferARB( GL_ARRAY_BUFFER_ARB, rb_gs.tfBuffer[i] );
			qglVertexPointer( 4, GL_FLOAT, sizeof( shadowCache_t ), (const void *)NULL );
			glCaps.DrawTransformFeedback( GL_TRIANGLES, rb_gs.tfo[i] );
		}
	}
	vertexCache.InvalidateBindings();
}

// the transform feedback objects and buffers, made with the tier
static void RB_GS_Init( void ) {
	int i;

	while ( qglGetError() != GL_NO_ERROR ) {
	}
	glCaps.GenTransformFeedbacks( 2, rb_gs.tfo );
	qglGenBuffersARB( 2, rb_gs.tfBuffer );
	qglGenBuffersARB( 1, &rb_gs.facing );
	rb_gs.buffers = qglGetError() == GL_NO_ERROR && rb_gs.tfo[0] && rb_gs.tfo[1]
		&& rb_gs.tfBuffer[0] && rb_gs.tfBuffer[1] && rb_gs.facing;
	for ( i = 0; i < 2; i++ ) {
		rb_gs.tfBytes[i] = 0;
	}
	rb_gs.facingFrame = -1;
	rb_gs.freeVolume = -1;
	rb_gs.lruHead = rb_gs.lruTail = -1;
	rb_gs.queries = glCaps.queries != 0;
}

// a new context: the old one's buffers went with it, and its stores' contents
static void RB_GS_Reset( void ) {
	int i;
	int epoch;

	for ( i = 0; i < rb_gs.numStores; i++ ) {
		range_alloc_deinit( &rb_gs.storeRanges[i] );
	}
	for ( i = 0; i < rb_gs.numVolumeStores; i++ ) {
		range_alloc_deinit( &rb_gs.volumeRanges[i] );
	}
	if ( rb_gs.entries ) {
		Mem_Free( rb_gs.entries );
	}
	if ( rb_gs.volumes ) {
		Mem_Free( rb_gs.volumes );
	}
	epoch = rb_gs.epoch;
	memset( &rb_gs, 0, sizeof( rb_gs ) );
	rb_gs.epoch = epoch + 1;
	rb_gs.freeVolume = -1;
	rb_gs.lruHead = rb_gs.lruTail = -1;
	r_gpuShadowVolumes = false;
}

/*
==================
RB_ARB2_DrawInteractions
==================
*/
void RB_ARB2_DrawInteractions( void ) {
	viewLight_t		*vLight;

	GL_SelectTexture( 0 );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );

	// the NV interaction programs read program.buffer[0]; bound per view
	// rather than once per context, since the frontend's state handling
	// may move bindings between frames
	if ( rb_nvInteraction ) {
		glCaps.BindBufferBaseNV( GL_VERTEX_PROGRAM_PARAMETER_BUFFER_NV, 0, rb_nvParmBuffer );
		glCaps.BindBufferBaseNV( GL_FRAGMENT_PROGRAM_PARAMETER_BUFFER_NV, 0, rb_nvParmBuffer );
	}
	// the NV5 slot units: textures may have been deleted and their
	// names reused since the last view; and a ring full in the last view
	// may have room in this one's frame
	if ( rb_nv5.programs ) {
		RB_ARB2NV5_ResetSlots();
		rb_nv5.ringFull = false;
	}

	//
	// for each light, perform adding and shadowing
	//
	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		backEnd.vLight = vLight;

		// do fogging later
		if ( vLight->lightShader->IsFogLight() ) {
			continue;
		}
		if ( vLight->lightShader->IsBlendLight() ) {
			continue;
		}

		if ( !vLight->localInteractions && !vLight->globalInteractions
			&& !vLight->translucentInteractions ) {
			continue;
		}

		// clear the stencil buffer if needed
		if ( vLight->globalShadows || vLight->localShadows ) {
			backEnd.currentScissor = vLight->scissorRect;
			if ( r_useScissor.GetBool() ) {
				qglScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
					backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
					backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
					backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
			}
			qglClear( GL_STENCIL_BUFFER_BIT );
		} else {
			// no shadows, so no need to read or write the stencil buffer
			// we might in theory want to use GL_ALWAYS instead of disabling
			// completely, to satisfy the invarience rules
			qglStencilFunc( GL_ALWAYS, 128, 255 );
		}

		if ( r_useShadowVertexProgram.GetBool() ) {
			// the shadow program only for a list there is to draw: an empty
			// pass draws nothing, and each interaction pass binds its own
			if ( vLight->globalShadows ) {
				qglEnable( GL_VERTEX_PROGRAM_ARB );
				qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
				RB_StencilShadowPass( vLight->globalShadows );
			}
			RB_ARB2_CreateDrawInteractions( vLight->localInteractions );
			if ( vLight->localShadows ) {
				qglEnable( GL_VERTEX_PROGRAM_ARB );
				qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
				RB_StencilShadowPass( vLight->localShadows );
			}
			RB_ARB2_CreateDrawInteractions( vLight->globalInteractions );
			qglDisable( GL_VERTEX_PROGRAM_ARB );	// if there weren't any globalInteractions, it would have stayed on
		} else {
			RB_StencilShadowPass( vLight->globalShadows );
			RB_ARB2_CreateDrawInteractions( vLight->localInteractions );
			RB_StencilShadowPass( vLight->localShadows );
			RB_ARB2_CreateDrawInteractions( vLight->globalInteractions );
		}

		// translucent surfaces never get stencil shadowed
		if ( r_skipTranslucent.GetBool() ) {
			continue;
		}

		qglStencilFunc( GL_ALWAYS, 128, 255 );

		backEnd.depthFunc = GLS_DEPTHFUNC_LESS;
		RB_ARB2_CreateDrawInteractions( vLight->translucentInteractions );

		backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
	}

	// disable stencil shadow test
	qglStencilFunc( GL_ALWAYS, 128, 255 );

	GL_SelectTexture( 0 );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
}

//===================================================================================


typedef struct {
	GLenum			target;
	GLuint			ident;
	char			name[64];
	bool			loaded;		// the driver accepted the program at its last load
	bool			stock;		// the game's own text of a program whose reach is known
} progDef_t;

static	const int	MAX_GLPROGS = 200;

// a single file can have both a vertex program and a fragment program
static progDef_t	progs[MAX_GLPROGS] = {
	{ GL_VERTEX_PROGRAM_ARB, VPROG_TEST, "test.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_TEST, "test.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_INTERACTION, "interaction.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_INTERACTION, "interaction.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_BUMPY_ENVIRONMENT, "bumpyEnvironment.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_BUMPY_ENVIRONMENT, "bumpyEnvironment.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_AMBIENT, "ambientLight.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_AMBIENT, "ambientLight.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW, "shadow.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_ENVIRONMENT, "environment.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_ENVIRONMENT, "environment.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_GLASSWARP, "arbVP_glasswarp.txt" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_GLASSWARP, "arbFP_glasswarp.txt" },

	// SteveL #3878: Particle softening applied by the engine
	{ GL_VERTEX_PROGRAM_ARB, VPROG_SOFT_PARTICLE, "soft_particle.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_SOFT_PARTICLE, "soft_particle.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_FF_GAMMA, "ff_gamma.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_FF_GAMMA_CUBE, "ff_gamma_cube.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_FF_GAMMA_GUI, "ff_gamma_gui.vfp" },

	// the NV tiers' interaction programs, built in; loaded only on an NV tier
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV_INTERACTION, "nv_interaction.vp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_NV_INTERACTION, "nv_interaction.fp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_NV_INTERACTION_AMBIENT, "nv_interactionAmbient.fp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV5_INTERACTION, "nv5_interaction.vp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_NV5_INTERACTION, "nv5_interaction.fp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_NV5_INTERACTION_AMBIENT, "nv5_interactionAmbient.fp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV5_SHADOW, "nv5_shadow.vp" },
	{ GL_GEOMETRY_PROGRAM_NV, GPROG_NV5_SHADOW_CAPS, "nv5_shadowCaps.gp" },
	{ GL_GEOMETRY_PROGRAM_NV, GPROG_NV5_SHADOW_SIL, "nv5_shadowSil.gp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_SOFT_PARTICLE_KEEP, "soft_particle.vfp" },

	// additional programs can be dynamically specified in materials
};

#if D3_INTEGRATE_SOFTPART_SHADERS
// DG: the following two shaders are taken from TheDarkMod 2.04 (glprogs/soft_particle.vfp)
// (C) 2005-2016 Broken Glass Studios (The Dark Mod Team) and the individual authors
//     released under a revised BSD license and GPLv3
const char* softpartVShader = "!!ARBvp1.0  \n"
	"OPTION ARB_position_invariant;  \n"
	"# NOTE: unlike the TDM shader, the following lines use .texcoord and .color  \n"
	"#   instead of .attrib[8] and .attrib[3], to make it work with non-nvidia drivers \n"
	"#   Furthermore, I added support for a texture matrix \n"
	"PARAM defaultTexCoord = { 0, 0.5, 0, 1 }; \n"
	"MOV    result.texcoord.zw, defaultTexCoord; \n"
	"# program.env[12] is PP_DIFFUSE_MATRIX_S, 13 is PP_DIFFUSE_MATRIX_T \n"
	"DP4    result.texcoord.x, vertex.texcoord, program.env[12]; \n"
	"DP4    result.texcoord.y, vertex.texcoord, program.env[13]; \n"
	"MOV    result.color, vertex.color; \n"
	"END \n";

const char* softpartFShader = "!!ARBfp1.0  \n"
	"# == Fragment Program == \n"
	"# taken from The Dark Mod 2.04, adjusted for dhewm3 \n"
	"# (C) 2005-2016 Broken Glass Studios (The Dark Mod Team) \n"
	"# \n"
	"# Input textures \n"
	"#   texture[0]   particle diffusemap \n"
	"#   texture[1]   _currentDepth \n"
	"# \n"
	"# Constants set by the engine: \n"
	"#   program.env[22] is reciprocal of _currentDepth size. Lets us convert a screen position to a texcoord in _currentDepth \n"
	"#      { 1.0f / depthtex.width, 1.0f / depthtex.height, float(depthtex.width)/int(depthtex.width), \n"
	"#          float(depthtex.height)/int(depthtex.height) } \n"
	"#   program.env[23] is the particle radius, given as { radius, 1/(fadeRange), 1/radius } \n"
	"#      fadeRange is the particle diameter for alpha blends (like smoke), but the particle radius for additive \n"
	"#      blends (light glares), because additive effects work differently. Fog is half as apparent when a wall   \n"
	"#      is in the middle of it. Light glares lose no visibility when they have something to reflect off.  \n"
	"#   program.env[24] is the color channel mask. Particles with additive blend need their RGB channels modified to blend them out. \n"
	"#                                              Particles with an alpha blend need their alpha channel modified. \n"
	"# \n"
	"# Hard-coded constants \n"
	"#    depth_consts allows us to recover the original depth in Doom units of anything in the depth \n"
	"#    buffer. Doom3's and thus TDM's projection matrix differs slightly from the classic projection matrix as \n"
	"#    it implements a \"nearly-infinite\" zFar. The matrix is hard-coded in the engine, so we use hard-coded \n"
	"#    constants here for efficiency. depth_consts is derived from the numbers in that matrix. \n"
	"# \n"
	"# next line: prevent dhewm3 from injecting gamma in shader code into this shader,  \n"
	"#            because that looks bad when rendered with additive blending (gets too bright) \n"
	"# nodhewm3gammahack \n"
	"\n"
	"PARAM   depth_consts = { 0.33333333, -0.33316667, 0.0, 0.0 }; \n"
	"PARAM   particle_radius  = program.env[23]; \n"
	"TEMP    tmp, scene_depth, particle_depth, near_fade, fade; \n"
	"\n"
	"# Map the fragment to a texcoord on our depth image, and sample to find scene_depth \n"
	"MUL   tmp.xy, fragment.position, program.env[22]; \n"
	"TEX   scene_depth, tmp, texture[1], 2D; \n"
	"MIN   scene_depth.x, scene_depth, 0.9994; # Required by TDM projection matrix. Equates to max recoverable  \n"
	"                                        # depth of 30k units, which is enough. 0.9995 is infinite depth. \n"
	"                                        # This is needed only if there is caulk sky on show (which writes \n"
	"                                        # no depth, so leaves 1 in the depth texture).  \n"
	"\n"
	"# Recover original depth in doom units  \n"
	"MAD   tmp.x, scene_depth, depth_consts.x, depth_consts.y; \n"
	"RCP   scene_depth, tmp.x; \n"
	"\n"
	"# Convert particle depth to doom units too \n"
	"MAD   tmp.x, fragment.position.z, depth_consts.x, depth_consts.y; \n"
	"RCP   particle_depth, tmp.x; \n"
	"\n"
	"# Scale the depth difference by the particle diameter to calc an alpha  \n"
	"# value based on how much of the 3d volume represented by the particle  \n"
	"# is in front of the solid scene  \n"
	"ADD      tmp, -scene_depth, particle_depth;     # NB depth is negative. 0 at the eye, -100 at 100 units into the screen. \n"
	"ADD      tmp, tmp, particle_radius.x;           # Add the radius so a depth difference of particle radius now equals 0 \n"
	"MUL_SAT  fade, tmp, particle_radius.y;          # divide by the particle radius or diameter and clamp \n"
	"\n"
	"# Also fade if the particle is too close to our eye position, so it doesn't 'pop' in and out of view \n"
	"# Start a linear fade at particle_radius distance from the particle. \n"
	"MUL_SAT  near_fade, particle_depth, -particle_radius.z;  \n"
	"\n"
	"# Calculate final fade and apply the channel mask \n"
	"MUL      fade, near_fade, fade; \n"
	"\n"
	"# A fragment faded to nothing is dropped before its texture fetch and its \n"
	"# blend when program.env[23].w is 1: the engine sets that only for blends \n"
	"# whose colour a zero fade leaves exactly as it was. \n"
	"SGE      tmp.x, depth_consts.z, fade.x; \n"
	"MUL      tmp.x, tmp.x, particle_radius.w; \n"
	"KIL      -tmp.x; \n"
	"\n"
	"ADD_SAT  fade, fade, program.env[24];  # saturate the channels that don't want modifying \n"
	"\n"
	"# Set the color. Multiply by vertex/fragment color as that's how the particle system fades particles in and out \n"
	"TEMP  oColor; \n"
	"TEX   oColor, fragment.texcoord, texture[0], 2D; \n"
	"MUL   oColor, oColor, fade; \n"
	"MUL   oColor, oColor, fragment.color; \n"
	"\n"
	"# Apply the HDR scene encoding fold, program.env[25].xyz (1.0 outside 30-bit HDR). \n"
	"# This program opts out of the injected gamma epilogue above, and the epilogue is \n"
	"# where every other stage picks the fold up - so without this the particle writes \n"
	"# at full scale into a scene stored at s, and the present pass's 1/s expand hands \n"
	"# it back at exactly 2x. The opt-out is about gamma on additive blends, not about \n"
	"# the fold, which is a plain linear scale on the stored value. Alpha is left alone \n"
	"# so blend weights are untouched: env[25].w is 1.0 and IEEE x*1.0 == x exactly, \n"
	"# so the alpha rides through this one MUL instead of needing its own MOV. \n"
	"MUL   result.color, oColor, program.env[25]; \n"
	"\n"
	"END \n";

#endif // D3_INTEGRATE_SOFTPART_SHADERS

/*
   The NV tiers' interaction programs.

   The vertex program is interaction.vfp's, reading its parameters from
   program.buffer[0] in the arb2_nvrec.h layout instead of program.env,
   and reusing the light vector it already has rather than subtracting
   it a second time.

   The fragment programs keep interaction.vfp's arithmetic and its
   register names, and carry the two lines the load-time splices anchor
   on - the specular table read (Toksvig) and the final colour product
   (spectral mix) - so every splice and the injected gamma epilogue
   apply to them unchanged.  Against interaction.vfp they differ in:

   - the light vector of a non-ambient light is normalised by NRM, not
     looked up in the 8-bit normalization cube map: one fetch fewer and
     no quantization of the vector.  An ambient light keeps the
     ambientNormalMap lookup, which is data rather than a normaliser;

   - a fragment where the light projection times the falloff is zero in
     every channel is killed.  Its contribution is exactly zero - every
     term is multiplied by them, and the epilogue maps zero to zero -
     so on an additive pass killing it writes the same framebuffer
     while sparing the blend.  The kill comes after the last fetch: KIL
     ends the fragment, and the quad's implicit derivatives must still
     see it for the fetches before.

   No OPTION line, so the epilogue's TEMP goes after the header, and no
   word in them may contain the end token in capitals.
*/
static const char nvInteractionVP[] =
	"!!NVvp4.0\n"
	"OPTION ARB_position_invariant;\n"
	"BUFFER4 parm[] = { program.buffer[0] };\n"
	"TEMP R0, R1, R2;\n"
	"PARAM defaultTexCoord = { 0, 0.5, 0, 1 };\n"
	"SUB R0, parm[0], vertex.position;\n"
	"DP3 result.texcoord[0].x, vertex.attrib[9], R0;\n"
	"DP3 result.texcoord[0].y, vertex.attrib[10], R0;\n"
	"DP3 result.texcoord[0].z, vertex.attrib[11], R0;\n"
	"MOV result.texcoord[1], defaultTexCoord;\n"
	"DP4 result.texcoord[1].x, vertex.attrib[8], parm[6];\n"
	"DP4 result.texcoord[1].y, vertex.attrib[8], parm[7];\n"
	"MOV result.texcoord[2], defaultTexCoord;\n"
	"DP4 result.texcoord[2].x, vertex.position, parm[5];\n"
	"DP4 result.texcoord[3].x, vertex.position, parm[2];\n"
	"DP4 result.texcoord[3].y, vertex.position, parm[3];\n"
	"DP4 result.texcoord[3].w, vertex.position, parm[4];\n"
	"MOV result.texcoord[4], defaultTexCoord;\n"
	"DP4 result.texcoord[4].x, vertex.attrib[8], parm[8];\n"
	"DP4 result.texcoord[4].y, vertex.attrib[8], parm[9];\n"
	"MOV result.texcoord[5], defaultTexCoord;\n"
	"DP4 result.texcoord[5].x, vertex.attrib[8], parm[10];\n"
	"DP4 result.texcoord[5].y, vertex.attrib[8], parm[11];\n"
	"DP3 R1, R0, R0;\n"
	"RSQ R1, R1.x;\n"
	"MUL R0, R0, R1.x;\n"
	"SUB R1, parm[1], vertex.position;\n"
	"DP3 R2, R1, R1;\n"
	"RSQ R2, R2.x;\n"
	"MUL R1, R1, R2.x;\n"
	"ADD R0, R0, R1;\n"
	"DP3 result.texcoord[6].x, vertex.attrib[9], R0;\n"
	"DP3 result.texcoord[6].y, vertex.attrib[10], R0;\n"
	"DP3 result.texcoord[6].z, vertex.attrib[11], R0;\n"
	"MAD result.color, vertex.color, parm[12], parm[13];\n"
	"END\n";

#define NV_INTERACTION_FP_HEAD \
	"!!NVfp4.0\n" \
	"BUFFER4 parm[] = { program.buffer[0] };\n" \
	"TEMP light, color, R1, R2, localNormal, specular, proj, fall;\n" \
	"PARAM subOne = { -1, -1, -1, -1 };\n" \
	"PARAM scaleTwo = { 2, 2, 2, 2 };\n" \
	"PARAM one = { 1, 1, 1, 1 };\n" \
	"DP3 specular, fragment.texcoord[6], fragment.texcoord[6];\n" \
	"RSQ specular, specular.x;\n" \
	"MUL specular, specular.x, fragment.texcoord[6];\n"

#define NV_INTERACTION_FP_TAIL \
	"TEX localNormal, fragment.texcoord[1], texture[1], 2D;\n" \
	"MOV localNormal.x, localNormal.a;\n" \
	"MAD localNormal, localNormal, scaleTwo, subOne;\n" \
	"DP3 light, light, localNormal;\n" \
	"TXP proj, fragment.texcoord[3], texture[3], 2D;\n" \
	"MUL light, light, proj;\n" \
	"TXP fall, fragment.texcoord[2], texture[2], 2D;\n" \
	"MUL light, light, fall;\n" \
	"TEX R1, fragment.texcoord[4], texture[4], 2D;\n" \
	"MUL color, R1, parm[14];\n" \
	"DP3 specular, specular, localNormal;\n" \
	"TEX R1, specular, texture[6], 2D;\n" \
	"MUL R1, R1, parm[15];\n" \
	"TEX R2, fragment.texcoord[5], texture[5], 2D;\n" \
	"ADD R2, R2, R2;\n" \
	"MAD color, R1, R2, color;\n" \
	"MUL color, light, color;\n" \
	"MUL result.color, color, fragment.color;\n" \
	"MUL R1, proj, fall;\n" \
	"DP4 R1.x, |R1|, one;\n" \
	"SEQ R1.x, R1.x, 0.0;\n" \
	"KIL -R1.x;\n" \
	"END\n"

static const char nvInteractionFP[] =
	NV_INTERACTION_FP_HEAD
	"NRM light.xyz, fragment.texcoord[0];\n"
	NV_INTERACTION_FP_TAIL;

static const char nvInteractionAmbientFP[] =
	NV_INTERACTION_FP_HEAD
	"TEX light, fragment.texcoord[0], texture[0], CUBE;\n"
	"MAD light, light, scaleTwo, subOne;\n"
	NV_INTERACTION_FP_TAIL;

/*
   The NV5 tier's batched interaction programs.

   The vertex program is the NV4 one with its parameters loaded from the
   draw's record: program.env[90].xy holds the batch's record base address,
   vertex.attrib[1] - instanced, so baseInstance picks it - the record's
   offset from it, and the record is ARB2_NV5_REC_VECS vec4s in the
   arb2_nvrec.h order with three more behind: the texture units, the
   specular unit, and the draw's scissor rectangle.  The colours, units
   and rectangle reach the fragment program as flat attributes.

   The fragment programs are the NV4 ones sampling the per-draw textures
   through a unit array, killing outside the draw's scissor rectangle the
   way glScissor clips - a pixel's centre is half a pixel inside the edges
   it belongs between - and keeping the two splice anchors.  The array's
   extent depends on the context's units, so they are built at tier
   selection.
*/
static const char nv5InteractionVP[] =
	"!!NVvp5.0\n"
	"OPTION ARB_position_invariant;\n"
	"UINT ATTRIB recOff = vertex.attrib[1];\n"
	"LONG TEMP addr, step;\n"
	"UINT TEMP t;\n"
	"TEMP R0, R1, R2, lo, vo, ps, pt, pq, pf, bs, bt, ds, dt, ss, st, cm, ca;\n"
	"PARAM defaultTexCoord = { 0, 0.5, 0, 1 };\n"
	"PK64.U addr.x, program.env[90];\n"
	"MOV.U t, { 0, 0, 0, 0 };\n"
	"MOV.U t.x, recOff.x;\n"
	"PK64.U step.x, t;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"MOV.U t.x, 16;\n"
	"PK64.U step.x, t;\n"
	"LOAD.F32X4 lo, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 vo, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 ps, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 pt, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 pq, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 pf, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 bs, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 bt, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 ds, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 dt, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 ss, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 st, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 cm, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 ca, addr.x;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 R0, addr.x;\n"
	"MOV result.attrib[0], R0;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 R0, addr.x;\n"
	"MOV result.attrib[1], R0;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 R0, addr.x;\n"
	"MOV result.attrib[2], R0;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 R0, addr.x;\n"
	"MOV result.attrib[3], R0;\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 R0, addr.x;\n"
	"MOV result.attrib[4], R0;\n"
	"SUB R0, lo, vertex.position;\n"
	"DP3 result.texcoord[0].x, vertex.attrib[9], R0;\n"
	"DP3 result.texcoord[0].y, vertex.attrib[10], R0;\n"
	"DP3 result.texcoord[0].z, vertex.attrib[11], R0;\n"
	"MOV result.texcoord[1], defaultTexCoord;\n"
	"DP4 result.texcoord[1].x, vertex.attrib[8], bs;\n"
	"DP4 result.texcoord[1].y, vertex.attrib[8], bt;\n"
	"MOV result.texcoord[2], defaultTexCoord;\n"
	"DP4 result.texcoord[2].x, vertex.position, pf;\n"
	"DP4 result.texcoord[3].x, vertex.position, ps;\n"
	"DP4 result.texcoord[3].y, vertex.position, pt;\n"
	"DP4 result.texcoord[3].w, vertex.position, pq;\n"
	"MOV result.texcoord[4], defaultTexCoord;\n"
	"DP4 result.texcoord[4].x, vertex.attrib[8], ds;\n"
	"DP4 result.texcoord[4].y, vertex.attrib[8], dt;\n"
	"MOV result.texcoord[5], defaultTexCoord;\n"
	"DP4 result.texcoord[5].x, vertex.attrib[8], ss;\n"
	"DP4 result.texcoord[5].y, vertex.attrib[8], st;\n"
	"DP3 R1, R0, R0;\n"
	"RSQ R1, R1.x;\n"
	"MUL R0, R0, R1.x;\n"
	"SUB R1, vo, vertex.position;\n"
	"DP3 R2, R1, R1;\n"
	"RSQ R2, R2.x;\n"
	"MUL R1, R1, R2.x;\n"
	"ADD R0, R0, R1;\n"
	"DP3 result.texcoord[6].x, vertex.attrib[9], R0;\n"
	"DP3 result.texcoord[6].y, vertex.attrib[10], R0;\n"
	"DP3 result.texcoord[6].z, vertex.attrib[11], R0;\n"
	"MAD result.color, vertex.color, cm, ca;\n"
	"END\n";

/* %d: the last unit of the texture array */
#define NV5_INTERACTION_FP_HEAD \
	"!!NVfp5.0\n" \
	"FLAT ATTRIB dcol = fragment.attrib[0];\n" \
	"FLAT ATTRIB scol = fragment.attrib[1];\n" \
	"FLAT ATTRIB units = fragment.attrib[2];\n" \
	"FLAT ATTRIB units2 = fragment.attrib[3];\n" \
	"FLAT ATTRIB clip = fragment.attrib[4];\n" \
	"TEXTURE tex[] = { texture[8..%d] };\n" \
	"INT TEMP A0, A1;\n" \
	"TEMP light, color, R1, R2, localNormal, specular, proj, fall;\n" \
	"PARAM subOne = { -1, -1, -1, -1 };\n" \
	"PARAM scaleTwo = { 2, 2, 2, 2 };\n" \
	"PARAM one = { 1, 1, 1, 1 };\n" \
	"ROUND.S A0, units;\n" \
	"ROUND.S A1.x, units2.x;\n" \
	"DP3 specular, fragment.texcoord[6], fragment.texcoord[6];\n" \
	"RSQ specular, specular.x;\n" \
	"MUL specular, specular.x, fragment.texcoord[6];\n"

#define NV5_INTERACTION_FP_TAIL \
	"TEX localNormal, fragment.texcoord[1], tex[A0.x], 2D;\n" \
	"MOV localNormal.x, localNormal.a;\n" \
	"MAD localNormal, localNormal, scaleTwo, subOne;\n" \
	"DP3 light, light, localNormal;\n" \
	"TXP proj, fragment.texcoord[3], tex[A0.z], 2D;\n" \
	"MUL light, light, proj;\n" \
	"TXP fall, fragment.texcoord[2], tex[A0.y], 2D;\n" \
	"MUL light, light, fall;\n" \
	"TEX R1, fragment.texcoord[4], tex[A0.w], 2D;\n" \
	"MUL color, R1, dcol;\n" \
	"DP3 specular, specular, localNormal;\n" \
	"TEX R1, specular, texture[6], 2D;\n" \
	"MUL R1, R1, scol;\n" \
	"TEX R2, fragment.texcoord[5], tex[A1.x], 2D;\n" \
	"ADD R2, R2, R2;\n" \
	"MAD color, R1, R2, color;\n" \
	"MUL color, light, color;\n" \
	"MUL result.color, color, fragment.color;\n" \
	"SUB R1.xy, fragment.position, clip;\n" \
	"SUB R1.zw, clip, fragment.position.xyxy;\n" \
	"KIL R1;\n" \
	"MUL R1, proj, fall;\n" \
	"DP4 R1.x, |R1|, one;\n" \
	"SEQ R1.x, R1.x, 0.0;\n" \
	"KIL -R1.x;\n" \
	"END\n"

/*
   The NV5 tier's shadow volume programs, RB_ARB2_CaptureGpuShadow's.

   Geometry program.env[0] is the light origin, its w the order the CPU's
   dot products add in (R_ShadowDotOrder); [1..4] the x, y, z and d of
   frustum planes 0-3 and [5..8] of planes 4-5 in .xy, [9] and [10] 1.0
   for each of them the cull tests; [11].xy the address of the capture's
   facing array, [12].xy that of the sil edges' triangles, [13].xy that
   of the source's face planes - the CPU's own, so facing is the CPU's
   last step alone: the plane's dot with the light origin, each operation
   .PREC and in the CPU's order.
*/
static const char nv5ShadowVP[] =
	"!!NVvp5.0\n"
	"MOV result.position, vertex.position;\n"
	"END\n";

/* the distance of vertex v from four planes, as Dot( plane, drawVert )
 * adds it: d last (u), or right after x (t), picked by the order */
#define NV5_SHADOW_DIST( v, x, y, z, d ) \
	"MUL.PREC t, " v ".x, " x ";\n" \
	"MUL.PREC w, " v ".y, " y ";\n" \
	"ADD.PREC u, t, w;\n" \
	"ADD.PREC t, t, " d ";\n" \
	"ADD.PREC t, t, w;\n" \
	"MUL.PREC w, " v ".z, " z ";\n" \
	"ADD.PREC u, u, w;\n" \
	"ADD.PREC t, t, w;\n" \
	"ADD.PREC u, u, " d ";\n" \
	"CMP t, -light.w, t, u;\n"

// the planes of four, masked, that all three vertexes are behind
#define NV5_SHADOW_CULL( c, x, y, z, d, mask ) \
	NV5_SHADOW_DIST( "a", x, y, z, d ) \
	"SLT " c ", t, 0.1;\n" \
	NV5_SHADOW_DIST( "b", x, y, z, d ) \
	"SLT u, t, 0.1;\n" \
	"MUL " c ", " c ", u;\n" \
	NV5_SHADOW_DIST( "c", x, y, z, d ) \
	"SLT u, t, 0.1;\n" \
	"MUL " c ", " c ", u;\n" \
	"MUL " c ", " c ", " mask ";\n"

static const char nv5ShadowCapsGP[] =
	"!!NVgp5.0\n"
	"PRIMITIVE_IN TRIANGLES;\n"
	"PRIMITIVE_OUT TRIANGLE_STRIP;\n"
	"VERTICES_OUT 6;\n"
	"PARAM light = program.env[0];\n"
	"PARAM px = program.env[1];\n"
	"PARAM py = program.env[2];\n"
	"PARAM pz = program.env[3];\n"
	"PARAM pd = program.env[4];\n"
	"PARAM qx = program.env[5];\n"
	"PARAM qy = program.env[6];\n"
	"PARAM qz = program.env[7];\n"
	"PARAM qd = program.env[8];\n"
	"PARAM pm = program.env[9];\n"
	"PARAM qm = program.env[10];\n"
	"TEMP a, b, c, pl, t, u, w, cp, cq, face;\n"
	"UINT TEMP k;\n"
	"LONG TEMP addr, step;\n"
	"MOV a, vertex[0].position;\n"
	"MOV b, vertex[1].position;\n"
	"MOV c, vertex[2].position;\n"
	"MOV.U k, { 0, 0, 0, 0 };\n"
	"MUL.U k.x, primitive.id, 16;\n"
	"PK64.U step.x, k;\n"
	"PK64.U addr.x, program.env[13];\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.F32X4 pl, addr.x;\n"
	"MUL.PREC t.xyz, pl, light;\n"
	"ADD.PREC u.x, t.x, t.y;\n"
	"ADD.PREC u.x, u.x, t.z;\n"
	"ADD.PREC u.x, u.x, pl.w;\n"
	"ADD.PREC w.x, t.x, pl.w;\n"
	"ADD.PREC w.x, w.x, t.y;\n"
	"ADD.PREC w.x, w.x, t.z;\n"
	"CMP face.x, -light.w, w.x, u.x;\n"
	"SGE face.x, face.x, 0.0;\n"
	NV5_SHADOW_CULL( "cp", "px", "py", "pz", "pd", "pm" )
	NV5_SHADOW_CULL( "cq", "qx", "qy", "qz", "qd", "qm" )
	"MAX cp, cp, cq;\n"
	"MAX cp.xy, cp, cp.zwzw;\n"
	"MAX face.x, face.x, cp.x;\n"
	"MAX face.x, face.x, cp.y;\n"
	"MUL.U k.x, primitive.id, 4;\n"
	"PK64.U step.x, k;\n"
	"PK64.U addr.x, program.env[11];\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"STORE.F32 face, addr.x;\n"
	"SEQ.CC face.y, face.x, 0.0;\n"
	"IF NE.y;\n"
	"MOV result.position, c;\n"
	"EMIT;\n"
	"MOV result.position, b;\n"
	"EMIT;\n"
	"MOV result.position, a;\n"
	"EMIT;\n"
	"ENDPRIM;\n"
	"MOV a.w, 0.0;\n"
	"MOV b.w, 0.0;\n"
	"MOV c.w, 0.0;\n"
	"MOV result.position, a;\n"
	"EMIT;\n"
	"MOV result.position, b;\n"
	"EMIT;\n"
	"MOV result.position, c;\n"
	"EMIT;\n"
	"ENDPRIM;\n"
	"ENDIF;\n"
	"END\n";

static const char nv5ShadowSilGP[] =
	"!!NVgp5.0\n"
	"PRIMITIVE_IN LINES;\n"
	"PRIMITIVE_OUT TRIANGLE_STRIP;\n"
	"VERTICES_OUT 6;\n"
	"TEMP a, b, f, w;\n"
	"UINT TEMP k, e;\n"
	"LONG TEMP addr, step, base;\n"
	"MOV a, vertex[0].position;\n"
	"MOV b, vertex[1].position;\n"
	"MOV.U k, { 0, 0, 0, 0 };\n"
	"MUL.U k.x, primitive.id, 8;\n"
	"PK64.U step.x, k;\n"
	"PK64.U addr.x, program.env[12];\n"
	"ADD.U64 addr.x, addr.x, step.x;\n"
	"LOAD.U32X2 e.xy, addr.x;\n"
	"PK64.U base.x, program.env[11];\n"
	"MUL.U k.x, e.x, 4;\n"
	"PK64.U step.x, k;\n"
	"ADD.U64 addr.x, base.x, step.x;\n"
	"MOV f, { 1, 1, 0, 0 };\n"
	"LOAD.F32 f.x, addr.x;\n"
	"SNE.U.CC k.z, e.y, 4294967295;\n"
	"IF NE.z;\n"
	"MUL.U k.x, e.y, 4;\n"
	"PK64.U step.x, k;\n"
	"ADD.U64 addr.x, base.x, step.x;\n"
	"LOAD.F32 f.y, addr.x;\n"
	"ENDIF;\n"
	"SNE.CC w.z, f.x, f.y;\n"
	"IF NE.z;\n"
	"ADD w.xy, 1.0, -f;\n"
	"MOV result.position, a;\n"
	"EMIT;\n"
	"MOV b.w, w.x;\n"
	"MOV result.position, b;\n"
	"EMIT;\n"
	"MOV b.w, w.y;\n"
	"MOV result.position, b;\n"
	"EMIT;\n"
	"ENDPRIM;\n"
	"MOV a.w, w.y;\n"
	"MOV result.position, a;\n"
	"EMIT;\n"
	"MOV a.w, w.x;\n"
	"MOV result.position, a;\n"
	"EMIT;\n"
	"MOV b.w, 0.0;\n"
	"MOV result.position, b;\n"
	"EMIT;\n"
	"ENDPRIM;\n"
	"ENDIF;\n"
	"END\n";

static char nv5InteractionFP[2048];
static char nv5InteractionAmbientFP[2048];

static void R_ARB2NV5_BuildPrograms( int lastUnit ) {
	idStr::snPrintf( nv5InteractionFP, sizeof( nv5InteractionFP ),
		NV5_INTERACTION_FP_HEAD
		"NRM light.xyz, fragment.texcoord[0];\n"
		NV5_INTERACTION_FP_TAIL, lastUnit );
	idStr::snPrintf( nv5InteractionAmbientFP, sizeof( nv5InteractionAmbientFP ),
		NV5_INTERACTION_FP_HEAD
		"TEX light, fragment.texcoord[0], texture[0], CUBE;\n"
		"MAD light, light, scaleTwo, subOne;\n"
		NV5_INTERACTION_FP_TAIL, lastUnit );
}

static ID_INLINE bool R_ARB2NV5Program( int ident ) {
	return ident == VPROG_NV5_INTERACTION || ident == FPROG_NV5_INTERACTION
		|| ident == FPROG_NV5_INTERACTION_AMBIENT;
}

static ID_INLINE bool R_ARB2ShadowProgram( int ident ) {
	return ident == VPROG_NV5_SHADOW || ident == GPROG_NV5_SHADOW_CAPS || ident == GPROG_NV5_SHADOW_SIL;
}

static ID_INLINE bool R_ARB2NVProgram( int ident ) {
	return ident == VPROG_NV_INTERACTION || ident == FPROG_NV_INTERACTION
		|| ident == FPROG_NV_INTERACTION_AMBIENT;
}

/* The first "END" that is a token of its own - not the head of ENDIF,
 * ENDREP or ENDLOOP, which the NV grammars have. */
static char *R_FindEndToken( char *text ) {
	char *e = strstr( text, "END" );
	while ( e != NULL ) {
		const char c = e[3];
		if ( !( c == '_' || ( c >= '0' && c <= '9' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) ) ) {
			return e;
		}
		e = strstr( e + 3, "END" );
	}
	return NULL;
}

/*
=================
R_LoadARBProgram
=================
*/

static char* findLineThatStartsWith( char* text, const char* findMe ) {
	char* res = strstr( text, findMe );
	while ( res != NULL ) {
		// skip whitespace before match, if any
		char* cur = res;
		if ( cur > text ) {
			--cur;
		}
		while ( cur > text && ( *cur == ' ' || *cur == '\t' ) ) {
			--cur;
		}
		// now we should be at a newline (or at the beginning)
		if ( cur == text ) {
			return cur;
		}
		if ( *cur == '\n' || *cur == '\r' ) {
			return cur+1;
		}
		// otherwise maybe we're in commented out text or whatever, search on
		res = strstr( res+1, findMe );
	}
	return NULL;
}

static ID_INLINE bool isARBidentifierChar( int c ) {
	// according to chapter 3.11.2 in ARB_fragment_program.txt identifiers can only
	// contain these chars (first char mustn't be a number, but that doesn't matter here)
	// NOTE: isalnum() or isalpha() apparently doesn't work, as it also matches spaces (?!)
	return  c == '$' || c == '_'
	      || (c >= '0' && c <= '9')
	      || (c >= 'A' && c <= 'Z')
	      || (c >= 'a' && c <= 'z');
}

/* r_gamma unity-ness the currently loaded ARB programs were built for.
   R_CheckCvars reloads them when the live cvar crosses that boundary. */
extern bool hdr_luma_clamp;

bool arbProgramsUnityGamma = true;
bool arbProgramsLumaClamp = false;

/*
=================
R_ARBProgramTextIsStock / R_ARBProgramIsStock

Whether a program file is the game's own text of one of the post-process
programs whose screen reach is known (the heat haze family and the colour
process): CRC-32 of the file with its carriage returns dropped.  Anything
else - a mod's edit included - reads _currentRender wherever it likes.
=================
*/
static bool R_ARBProgramTextIsStock( const char *text ) {
	static const uint32_t stock[4] = {
		0x640e9fedu,	// heatHaze.vfp
		0x40440f51u,	// heatHazeWithMask.vfp
		0x3ddd175eu,	// heatHazeWithMaskAndVertex.vfp
		0x2026ebd3u		// colorProcess.vfp
	};
	const char *run = text;
	const char *p;
	uint32_t crc = 0;
	int i;
	for ( p = text; ; p++ ) {
		if ( *p == '\r' || *p == '\0' ) {
			crc = encoding_crc32( crc, (const uint8_t *)run, (size_t)( p - run ) );
			if ( *p == '\0' ) {
				break;
			}
			run = p + 1;
		}
	}
	for ( i = 0; i < 4; i++ ) {
		if ( crc == stock[i] ) {
			return true;
		}
	}
	return false;
}

bool R_ARBProgramIsStock( int ident ) {
	int i;
	for ( i = 0; i < MAX_GLPROGS && progs[i].name[0]; i++ ) {
		if ( (int)progs[i].ident == ident ) {
			return progs[i].stock;
		}
	}
	return false;
}

void R_LoadARBProgram( int progIndex ) {
	int		ofs;
	int		err;
	char	*buffer;
	char	*start = NULL, *end;
	const char *nvSrc;
	bool	nvProg;
	bool	interactionFP;

	progs[progIndex].loaded = false;
	progs[progIndex].stock = false;
	nvProg = R_ARB2NVProgram( progs[progIndex].ident ) || R_ARB2NV5Program( progs[progIndex].ident )
		|| R_ARB2ShadowProgram( progs[progIndex].ident );
	interactionFP = progs[progIndex].ident == FPROG_INTERACTION
		|| progs[progIndex].ident == FPROG_NV_INTERACTION
		|| progs[progIndex].ident == FPROG_NV_INTERACTION_AMBIENT
		|| progs[progIndex].ident == FPROG_NV5_INTERACTION
		|| progs[progIndex].ident == FPROG_NV5_INTERACTION_AMBIENT;

	if ( nvProg ) {
		// a lower tier never binds these, and its driver may not parse them
		if ( arb2Tier < ( R_ARB2NV5Program( progs[progIndex].ident ) ? ARB2_TIER_NV5 : ARB2_TIER_NV4 ) ) {
			return;
		}
		if ( R_ARB2ShadowProgram( progs[progIndex].ident ) && ( arb2Tier < ARB2_TIER_NV5 || !rb_gs.buffers ) ) {
			return;
		}
		switch ( progs[progIndex].ident ) {
		case VPROG_NV_INTERACTION:			nvSrc = nvInteractionVP; break;
		case FPROG_NV_INTERACTION:			nvSrc = nvInteractionFP; break;
		case FPROG_NV_INTERACTION_AMBIENT:	nvSrc = nvInteractionAmbientFP; break;
		case VPROG_NV5_INTERACTION:			nvSrc = nv5InteractionVP; break;
		case FPROG_NV5_INTERACTION:			nvSrc = nv5InteractionFP; break;
		case VPROG_NV5_SHADOW:				nvSrc = nv5ShadowVP; break;
		case GPROG_NV5_SHADOW_CAPS:			nvSrc = nv5ShadowCapsGP; break;
		case GPROG_NV5_SHADOW_SIL:			nvSrc = nv5ShadowSilGP; break;
		default:							nvSrc = nv5InteractionAmbientFP; break;
		}
		common->Printf( "<internal> %s", progs[progIndex].name );
		buffer = (char *)_alloca( strlen( nvSrc ) + 1 );
		strcpy( buffer, nvSrc );
	}
	else
#if D3_INTEGRATE_SOFTPART_SHADERS
	if ( progs[progIndex].ident == VPROG_SOFT_PARTICLE || progs[progIndex].ident == FPROG_SOFT_PARTICLE
	     || progs[progIndex].ident == FPROG_SOFT_PARTICLE_KEEP
	     || progs[progIndex].ident == FPROG_FF_GAMMA || progs[progIndex].ident == FPROG_FF_GAMMA_CUBE
	     || progs[progIndex].ident == FPROG_FF_GAMMA_GUI ) {
		/*
		   The fixed-function replication programs: old-style material
		   stages historically rendered without fragment programs, which
		   meant the gamma-in-shader epilogue never touched them -
		   r_gamma/r_brightness silently skipped GUIs, 2D, and every
		   plain textured stage, and (the reason these exist now) no
		   uniform output-encoding scale could cover the whole frame.
		   Each replicates the fixed-function math exactly:
		   tex * lerp(vertexColor, 1-vertexColor, invFlag) * constColor
		   with local[0] = constant color, local[1].x = inverse-modulate
		   flag. The loader injects the standard gamma epilogue below,
		   same as every other program.
		*/
		static const char ffGammaFShader[] =
			"!!ARBfp1.0\n"
			"PARAM cc = program.local[0];\n"
			"PARAM inv = program.local[1];\n"
			"TEMP t, v, w;\n"
			"TEX t, fragment.texcoord[0], texture[0], 2D;\n"
			"SUB v.rgb, 1.0, fragment.color;\n"
			"LRP w.rgb, inv.x, v, fragment.color;\n"
			"MOV w.a, fragment.color.a;\n"
			"MUL t, t, w;\n"
			"MUL result.color, t, cc;\n"
			"END\n";
		/* The GUI variant.  Identical to ffGammaFShader except that the
		   sampled texel is pushed through the ACES 2.0 inverse first, as
		   a lookup on texture unit 1.

		   An in-world GUI is authored in display space - its 0.5 grey is
		   meant to read as 0.5 - but the engine hands that texture to the
		   pipeline as albedo, so the output transform moves it: an
		   authored 0.10 arrives at 0.064.  Pre-correcting the texel means
		   the forward transform puts it back exactly.

		   This is a separate program rather than a branch in the shared
		   one because it is bound only for SS_GUI surfaces, so no other
		   stage pays for the three extra fetches. */
		static const char ffGammaGuiFShader[] =
			"!!ARBfp1.0\n"
			"PARAM cc = program.local[0];\n"
			"PARAM inv = program.local[1];\n"
			"TEMP t, v, w, g;\n"
			"TEX t, fragment.texcoord[0], texture[0], 2D;\n"
			"MOV g, 0.5;\n"
			"MOV g.x, t.r;\n"
			"TEX t.r, g, texture[1], 2D;\n"
			"MOV g.x, t.g;\n"
			"TEX t.g, g, texture[1], 2D;\n"
			"MOV g.x, t.b;\n"
			"TEX t.b, g, texture[1], 2D;\n"
			"SUB v.rgb, 1.0, fragment.color;\n"
			"LRP w.rgb, inv.x, v, fragment.color;\n"
			"MOV w.a, fragment.color.a;\n"
			"MUL t, t, w;\n"
			"MUL result.color, t, cc;\n"
			"END\n";
		static const char ffGammaCubeFShader[] =
			"!!ARBfp1.0\n"
			"PARAM cc = program.local[0];\n"
			"PARAM inv = program.local[1];\n"
			"TEMP t, v, w;\n"
			"TEX t, fragment.texcoord[0], texture[0], CUBE;\n"
			"SUB v.rgb, 1.0, fragment.color;\n"
			"LRP w.rgb, inv.x, v, fragment.color;\n"
			"MOV w.a, fragment.color.a;\n"
			"MUL t, t, w;\n"
			"MUL result.color, t, cc;\n"
			"END\n";
		// these shaders are loaded directly from a string
		common->Printf( "<internal> %s", progs[progIndex].name );
		const char* srcstr = (progs[progIndex].ident == VPROG_SOFT_PARTICLE) ? softpartVShader
			: (progs[progIndex].ident == FPROG_SOFT_PARTICLE || progs[progIndex].ident == FPROG_SOFT_PARTICLE_KEEP)
			? softpartFShader
			: (progs[progIndex].ident == FPROG_FF_GAMMA) ? ffGammaFShader
			: (progs[progIndex].ident == FPROG_FF_GAMMA_GUI) ? ffGammaGuiFShader
			: ffGammaCubeFShader;

		// copy to stack memory
		buffer = (char *)_alloca( strlen( srcstr ) + 1 );
		strcpy( buffer, srcstr );
	}
	else
#endif // D3_INTEGRATE_SOFTPART_SHADERS
	{
		idStr	fullPath = "glprogs/";
		fullPath += progs[progIndex].name;
		char	*fileBuffer;
		common->Printf( "%s", fullPath.c_str() );

		// load the program even if we don't support it, so
		// fs_copyfiles can generate cross-platform data dumps
		fileSystem->ReadFile( fullPath.c_str(), (void **)&fileBuffer, NULL );
		if ( !fileBuffer ) {
			common->Printf( ": File not found\n" );
			return;
		}

		progs[progIndex].stock = R_ARBProgramTextIsStock( fileBuffer );

		// copy to stack memory and free
		buffer = (char *)_alloca( strlen( fileBuffer ) + 1 );
		strcpy( buffer, fileBuffer );
		fileSystem->FreeFile( fileBuffer );
	}

	if ( progs[progIndex].ident == FPROG_SOFT_PARTICLE_KEEP ) {
		// the soft particle program without its drop: the KIL becomes a comment
		char *kil = strstr( buffer, "KIL      -tmp.x;" );
		if ( kil ) {
			kil[0] = '#';
		}
	}

	if ( !glConfig.isInitialized ) {
		return;
	}

	//
	// submit the program string at start to GL
	//
	if ( progs[progIndex].ident == 0 ) {
		// allocate a new identifier for this program
		progs[progIndex].ident = PROG_USER + progIndex;
	}

	// vertex and fragment programs can both be present in a single file, so
	// scan for the proper header to be the start point, and stamp a 0 in after the end

	if ( progs[progIndex].target == GL_VERTEX_PROGRAM_ARB ) {
		if ( !glConfig.ARBVertexProgramAvailable ) {
			common->Printf( ": GL_VERTEX_PROGRAM_ARB not available\n" );
			return;
		}
		start = strstr( buffer, nvProg ? "!!NVvp" : "!!ARBvp" );
	}
	if ( progs[progIndex].target == GL_FRAGMENT_PROGRAM_ARB ) {
		if ( !glConfig.ARBFragmentProgramAvailable ) {
			common->Printf( ": GL_FRAGMENT_PROGRAM_ARB not available\n" );
			return;
		}
		start = strstr( buffer, nvProg ? "!!NVfp" : "!!ARBfp" );
	}
	if ( !start ) {
		common->Printf( ": !!ARB not found\n" );
		return;
	}
	/*
	   Pseudo-spectral mixing, for the interaction program only.

	   id Tech 4 lights a surface with a per-channel product, and the
	   last instruction of interaction.vfp is exactly that product:

	     MUL result.color, color, fragment.color;

	   where color carries the albedo terms and fragment.color is the
	   light's colour.  Swapping that one instruction for a basis change
	   either side of the multiply is the whole change - the interior of
	   the program is untouched, so nothing about how the light is shaped
	   or attenuated moves.

	   The matrix is in renderer/spectral_mix.h, fitted against spectral
	   ground truth.  Its rows sum to one, so white light on a white
	   surface returns exactly white and neutrals cannot shift.  With the
	   matrix set to identity this tail reproduces the stock product bit
	   for bit, which is how the option being off is guaranteed to be a
	   no-op rather than merely intended to be.

	   26 ALU against the stock 16, and 9 temporaries against 6, both
	   inside native limits on the drivers this was checked on.
	*/
	if ( interactionFP && r_spectralMix.GetBool() ) {
		const char *mulLine = strstr( start, "MUL result.color, color, fragment.color;" );
		if ( mulLine ) {
			idStr head( start );
			head.CapLength( (int)( mulLine - start ) );
			idStr tail( mulLine + strlen( "MUL result.color, color, fragment.color;" ) );
			idStr mix;
			mix.Append( "TEMP smA, smL, smP;\n" );
			for ( int i = 0; i < 3; i++ ) {
				mix += va( "PARAM smM%d = { %.7f, %.7f, %.7f, 0 };\n", i,
						spectral_mix_forward[i][0], spectral_mix_forward[i][1],
						spectral_mix_forward[i][2] );
			}
			for ( int i = 0; i < 3; i++ ) {
				mix += va( "PARAM smN%d = { %.7f, %.7f, %.7f, 0 };\n", i,
						spectral_mix_inverse[i][0], spectral_mix_inverse[i][1],
						spectral_mix_inverse[i][2] );
			}
			mix.Append( "DP3 smA.x, color, smM0;\nDP3 smA.y, color, smM1;\nDP3 smA.z, color, smM2;\n" );
			mix.Append( "DP3 smL.x, fragment.color, smM0;\nDP3 smL.y, fragment.color, smM1;\n"
						"DP3 smL.z, fragment.color, smM2;\n" );
			mix.Append( "MUL smP, smA, smL;\n" );
			mix.Append( "DP3 result.color.x, smP, smN0;\nDP3 result.color.y, smP, smN1;\n"
						"DP3 result.color.z, smP, smN2;\n" );
			mix.Append( "MOV result.color.w, fragment.color.w;\n" );

			idStr rebuilt = head + mix + tail;
			buffer = (char *)_alloca( rebuilt.Length() + 1 );
			strcpy( buffer, rebuilt.c_str() );
			start = buffer;
		}
	}
	/* Toksvig specular antialiasing, spliced the same way.  The stock
	   program never normalises localNormal, and R_MipMap averages
	   without renormalising, so at the specular table read the
	   normal length still carries the variance the mip folded in.
	   The dot is raised to ft = len / (len + s(1-len)) before the
	   table, which is exactly pow(dot, s*ft) against the table's
	   own exponent - wider lobe where the surface is rough at this
	   footprint, the stock lobe where it is flat.  s matches the
	   falloff shape: 9.1 is the tailed ramp's true exponent; the
	   original ramp is a shifted quadratic, not a power, and 12 is
	   the same effective exponent the GLES backend hardcodes to
	   match the D3 look - an approximation, and said so.  Only R2 is
	   used, dead at this point; no temporaries are added. */
	if ( interactionFP && r_specularAA.GetBool() ) {
		extern int r_specularFalloffShape;
		const char *tabLine = strstr( start, "TEX\tR1, specular, texture[6], 2D;" );
		if ( !tabLine )
			tabLine = strstr( start, "TEX R1, specular, texture[6], 2D;" );
		if ( !tabLine ) {
			common->Warning( "r_specularAA: specular table read not found; program left stock" );
		} else {
			idStr head( start );
			head.CapLength( (int)( tabLine - start ) );
			idStr tail( tabLine );
			idStr tok;
			const float s = ( r_specularFalloffShape != 0 ) ? 9.1f : 12.0f;
			tok.Append( "DP3 R2.x, localNormal, localNormal;\n" );
			tok.Append( "RSQ R2.y, R2.x;\n" );
			tok.Append( "MUL R2.x, R2.x, R2.y;\n" );
			tok.Append( "MAD R2.y, R2.x, -1.0, 1.0;\n" );
			tok += va( "MAD R2.y, R2.y, %.4f, R2.x;\n", s );
			tok.Append( "RCP R2.y, R2.y;\n" );
			tok.Append( "MUL R2.x, R2.x, R2.y;\n" );
			tok.Append( "MAX R2.z, specular.x, 0.0;\n" );
			tok.Append( "POW R2.z, R2.z, R2.x;\n" );
			tok.Append( "MOV specular, R2.z;\n" );
			idStr rebuilt = head + tok + tail;
			buffer = (char *)_alloca( rebuilt.Length() + 1 );
			strcpy( buffer, rebuilt.c_str() );
			start = buffer;
		}
	}


	end = nvProg ? R_FindEndToken( start ) : strstr( start, "END" );

	if ( !end ) {
		common->Printf( ": END not found\n" );
		return;
	}
	end[3] = 0;

	// DG: hack gamma correction into shader
	if ( r_gammaInShader.GetBool() && progs[progIndex].target == GL_FRAGMENT_PROGRAM_ARB
	     && strstr( start, "nodhewm3gammahack" ) == NULL )
	{

		// note that strlen("dhewm3tmpres") == strlen("result.color")
		const char* tmpres = "TEMP dhewm3tmpres; # injected by dhewm3 for gamma correction\n";
		const char* lumaTmp = "TEMP dhewm3luma; # injected by dhewm3 for luminance-aware clamping\n";

		// Note: program.env[21].xyz = r_brightness; program.env[21].w = 1.0/r_gamma
		// outColor.rgb = pow(dhewm3tmpres.rgb*r_brightness, vec3(1.0/r_gamma))
		// outColor.a = dhewm3tmpres.a;
		/*
		   With the FP16 scene target the framebuffer no longer clamps
		   at 1.0, so the epilogue must not either - MUL_SAT here would
		   put the ceiling right back and defeat unbounded additive
		   accumulation. The negative-base POW hazard is the only
		   reason for a clamp at all, so the FP16 variant clamps the
		   LOW side only. Chosen at program load; the precision option
		   is restart-required for exactly this reason.
		*/
		extern bool hdr_fp16_scene;
		extern bool hdr_fp32_scene;
		extern bool hdr_unbounded_blend;
		const bool unbounded = ( hdr_fp16_scene || hdr_fp32_scene ) && hdr_unbounded_blend;

		/*
		   env[21].w is 1.0/r_gamma, and r_gamma defaults to 1 - so on a
		   default install every one of these programs was issuing three
		   POW instructions per fragment to compute the identity.

		   That is not a rounding-error cost. The epilogue goes into
		   every ARB fragment program the renderer has, and Doom 3
		   re-shades every lit surface once per light, so the POWs scale
		   with overdraw rather than with screen area. At 4K with five
		   lights averaged over the frame it works out around 124M POW,
		   i.e. ~250M transcendental ops per frame - roughly twice what
		   the entire HDR composite pass costs, to do nothing.

		   POW is the only transcendental in the epilogue, so dropping it
		   at unity gamma removes the whole cost. MUL_SAT can write
		   straight to result.color with a write mask, so the unity
		   variants are two instructions rather than five.

		   Not byte-identical, and worth being straight about why: the
		   hardware evaluates POW as exp2(y*log2(x)) to roughly 22 bits,
		   so pow(x, 1.0) returns x to within ~2.4e-7 relative rather
		   than exactly x. Removing it makes the result MORE accurate,
		   by an amount ~4000x smaller than one 10-bit output code.

		   R_CheckCvars reloads the programs when r_gamma crosses the
		   unity boundary, so the live cvar keeps working.
		*/
		const bool unityGamma = ( r_gamma.GetFloat() == 1.0f );
		arbProgramsUnityGamma = unityGamma;

		/*
		   Luminance-aware highlight blending.

		   The bounded epilogue ends in MUL_SAT, which clamps each
		   channel on its own.  That is what puts the ceiling in the
		   right place, but it also rewrites the colour: (1.8, 0.9, 0.4)
		   becomes (1.0, 0.9, 0.4), which is a different hue and a
		   different saturation, not a dimmer version of the same light.
		   Stack a few translucent passes on a clamped target and every
		   overbright pixel drifts toward whichever channel saturated
		   first - the classic magenta-ish fringe on additive fire and
		   the flat cyan on overbright coolant glows.

		   Instead, when a pixel cannot fit, desaturate it toward its own
		   luminance by exactly the amount needed to bring the peak
		   channel to 1:

		     L    = dot(rgb, Rec.709 luma)
		     peak = max(r, g, b)
		     t    = saturate( (peak - 1) / (peak - L) )
		     rgb' = lerp(rgb, L, t)

		   At t = 0 nothing changes, so anything that already fits is
		   bit-identical to the old path.  As a pixel gets brighter it
		   walks toward its own luminance instead of toward a channel
		   corner, which is what a highlight rolling off looks like -
		   hue holds, saturation gives way, and the pixel ends at
		   neutral white rather than at a primary.  Luminance is
		   preserved until L itself reaches the ceiling, which is the
		   part per-channel clamping cannot do at all: clipping the
		   peak channel destroys luminance the moment any channel
		   exceeds 1.

		   Only the bounded variants get this.  The unbounded epilogue
		   deliberately has no ceiling, so there is nothing to roll off
		   and the extra instructions would be pure cost.

		   Off by default, and off means the exact previous text.
		*/
		const bool lumaClamp = hdr_luma_clamp && !unbounded;

		/* Record the option as asked for, not as applied.
		 *
		 * R_CheckCvars reloads the programs when this disagrees with
		 * the option, so it has to be the same quantity the option is.
		 * Storing the applied value made them disagree permanently
		 * whenever the option was on and the unbounded epilogue was in
		 * use - the request is true, the application is false - and the
		 * check then reloaded every ARB program in the renderer on
		 * every single frame, forever. */
		arbProgramsLumaClamp = hdr_luma_clamp;

		/* The desaturation block, shared by both bounded variants.  It
		 * leaves the result in dhewm3tmpres.xyz, low-clamped and with
		 * the peak channel at or below 1. */
#define DHEWM3_LUMA_CLAMP_BLOCK \
			"MAX dhewm3tmpres.xyz, dhewm3tmpres, 0.0;\n" \
			"DP3 dhewm3luma.x, dhewm3tmpres, {0.2126, 0.7152, 0.0722, 0.0};\n" \
			"MAX dhewm3luma.y, dhewm3tmpres.x, dhewm3tmpres.y;\n" \
			"MAX dhewm3luma.y, dhewm3luma.y, dhewm3tmpres.z;\n" \
			"SUB dhewm3luma.z, dhewm3luma.y, 1.0;\n" \
			"SUB dhewm3luma.w, dhewm3luma.y, dhewm3luma.x;\n" \
			"MAX dhewm3luma.w, dhewm3luma.w, 0.0001;\n" \
			"RCP dhewm3luma.w, dhewm3luma.w;\n" \
			"MUL_SAT dhewm3luma.z, dhewm3luma.z, dhewm3luma.w;\n" \
			"LRP dhewm3tmpres.xyz, dhewm3luma.z, dhewm3luma.x, dhewm3tmpres;\n"

		const char* extraLines = lumaClamp ? ( unityGamma ?
			"# gamma correction in shader, injected by dhewm3 (unity gamma, luminance-aware clamp) \n"
			"MUL dhewm3tmpres.xyz, program.env[21], dhewm3tmpres;\n"
			DHEWM3_LUMA_CLAMP_BLOCK
			"MOV_SAT result.color.xyz, dhewm3tmpres;\n"
			"MOV result.color.w, dhewm3tmpres.w;\n"
			"\nEND\n\n"
		:
			"# gamma correction in shader, injected by dhewm3 (luminance-aware clamp) \n"
			"MUL dhewm3tmpres.xyz, program.env[21], dhewm3tmpres;\n"
			DHEWM3_LUMA_CLAMP_BLOCK
			"MOV_SAT dhewm3tmpres.xyz, dhewm3tmpres;\n"
			"POW result.color.x, dhewm3tmpres.x, program.env[21].w;\n"
			"POW result.color.y, dhewm3tmpres.y, program.env[21].w;\n"
			"POW result.color.z, dhewm3tmpres.z, program.env[21].w;\n"
			"MOV result.color.w, dhewm3tmpres.w;\n"
			"\nEND\n\n"
		) : unityGamma ? ( unbounded ?
			"# gamma correction in shader, injected by dhewm3 (unity gamma, unclamped high side) \n"
			"MAX dhewm3tmpres.xyz, dhewm3tmpres, 0.0;\n"
			"MUL result.color.xyz, program.env[21], dhewm3tmpres;\n"
			"MOV result.color.w, dhewm3tmpres.w;\n"
			"\nEND\n\n"
		:
			"# gamma correction in shader, injected by dhewm3 (unity gamma) \n"
			"MUL_SAT result.color.xyz, program.env[21], dhewm3tmpres;\n"
			"MOV result.color.w, dhewm3tmpres.w;\n"
			"\nEND\n\n"
		) : unbounded ?
			"# gamma correction in shader, injected by dhewm3 (unclamped high side for FP16 scene) \n"
			"MAX dhewm3tmpres.xyz, dhewm3tmpres, 0.0;\n"
			"MUL dhewm3tmpres.xyz, program.env[21], dhewm3tmpres;\n"
			"POW result.color.x, dhewm3tmpres.x, program.env[21].w;\n"
			"POW result.color.y, dhewm3tmpres.y, program.env[21].w;\n"
			"POW result.color.z, dhewm3tmpres.z, program.env[21].w;\n"
			"MOV result.color.w, dhewm3tmpres.w;\n"
			"\nEND\n\n"
		:
			"# gamma correction in shader, injected by dhewm3 \n"
			// MUL_SAT clamps the result to [0, 1] - it must not be negative because
			// POW might not work with a negative base (it looks wrong with intel's Linux driver)
			// and clamping values >1 to 1 is ok because when writing to result.color
			// it's clamped anyway and pow(base, exp) is always >= 1 for base >= 1
			"MUL_SAT dhewm3tmpres.xyz, program.env[21], dhewm3tmpres;\n" // first multiply with brightness
			"POW result.color.x, dhewm3tmpres.x, program.env[21].w;\n" // then do pow(dhewm3tmpres.xyz, vec3(1/gamma))
			"POW result.color.y, dhewm3tmpres.y, program.env[21].w;\n" // (apparently POW only supports scalars, not whole vectors)
			"POW result.color.z, dhewm3tmpres.z, program.env[21].w;\n"
			"MOV result.color.w, dhewm3tmpres.w;\n" // alpha remains unmodified
			"\nEND\n\n"; // we add this block right at the end, replacing the original "END" string

		int fullLen = strlen( start ) + strlen( tmpres ) + strlen( extraLines )
				+ ( lumaClamp ? strlen( lumaTmp ) : 0 );
		char* outStr = (char*)_alloca( fullLen + 1 );

		// add tmpres right after OPTION line (if any)
		char* insertPos = findLineThatStartsWith( start, "OPTION" );
		if ( insertPos == NULL ) {
			// no OPTION? then just put it after the first line (usually sth like "!!ARBfp1.0\n")
			insertPos = start;
		}
		// but we want the position *after* that line
		while( *insertPos != '\0' && *insertPos != '\n' && *insertPos != '\r' ) {
			++insertPos;
		}
		// skip  the newline character(s) as well
		while( *insertPos == '\n' || *insertPos == '\r' ) {
			++insertPos;
		}

		// copy text up to insertPos
		int curLen = insertPos-start;
		memcpy( outStr, start, curLen );
		// copy tmpres ("TEMP dhewm3tmpres; # ..")
		memcpy( outStr+curLen, tmpres, strlen( tmpres ) );
		curLen += strlen( tmpres );
		// and the scratch register the luminance-aware clamp needs
		if ( lumaClamp ) {
			memcpy( outStr+curLen, lumaTmp, strlen( lumaTmp ) );
			curLen += strlen( lumaTmp );
		}
		// copy remaining original shader up to (excluding) "END"
		int remLen = end - insertPos;
		memcpy( outStr+curLen, insertPos, remLen );
		curLen += remLen;

		outStr[curLen] = '\0'; // make sure it's NULL-terminated so normal string functions work

		// replace all existing occurrences of "result.color" with "dhewm3tmpres"
		for( char* resCol = strstr( outStr, "result.color" );
		     resCol != NULL; resCol = strstr( resCol+13, "result.color" ) ) {
			memcpy( resCol, "dhewm3tmpres", 12 ); // both strings have the same length.

			// if this was part of "OUTPUT bla = result.color;", replace
			// "OUTPUT bla" with "ALIAS  bla" (so it becomes "ALIAS  bla = dhewm3tmpres;")
			{
				char* s = resCol - 1;
				// first skip whitespace before "result.color"
				while( s > outStr && (*s == ' ' || *s == '\t') ) {
					--s;
				}
				// if there's no '=' before result.color, this line can't be affected
				if ( *s != '=' || s <= outStr + 8 ) {
					continue; // go on with next "result.color" in the for-loop
				}
				--s; // we were on '=', so go to the char before and it's time to skip whitespace again
				while( s > outStr && ( *s == ' ' || *s == '\t' ) ) {
					--s;
				}
				// now we should be at the end of "bla" (or however the variable/alias is called)
				if ( s <= outStr+7 || !isARBidentifierChar( *s ) ) {
					continue;
				}
				--s;
				// skip all the remaining chars that are legal in identifiers
				while( s > outStr && isARBidentifierChar( *s ) ) {
					--s;
				}
				// there should be at least one space/tab between "OUTPUT" and "bla"
				if ( s <= outStr + 6 || ( *s != ' ' && *s != '\t' ) ) {
					continue;
				}
				--s;
				// skip remaining whitespace (if any)
				while( s > outStr && ( *s == ' ' || *s == '\t' ) ) {
					--s;
				}
				// now we should be at "OUTPUT" (specifically at its last 'T'),
				// if this is indeed such a case
				if ( s <= outStr + 5 || *s != 'T' ) {
					continue;
				}
				s -= 5; // skip to start of "OUTPUT", if this is indeed "OUTPUT"
				if ( idStr::Cmpn( s, "OUTPUT", 6 ) == 0 ) {
					// it really is "OUTPUT" => replace "OUTPUT" with "ALIAS "
					memcpy(s, "ALIAS ", 6);
				}
			}
		}

		assert( curLen + strlen( extraLines ) <= fullLen );

		// now add extraLines that calculate and set a gamma-corrected result.color
		// strcat() should be safe because fullLen was calculated taking all parts into account
		strcat( outStr, extraLines );
		start = outStr;
	}

	qglBindProgramARB( progs[progIndex].target, progs[progIndex].ident );
	qglGetError();

	qglProgramStringARB( progs[progIndex].target, GL_PROGRAM_FORMAT_ASCII_ARB,
		strlen( start ), start );

	err = qglGetError();
	qglGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, (GLint *)&ofs );
	if ( err == GL_INVALID_OPERATION ) {
		const GLubyte *str = qglGetString( GL_PROGRAM_ERROR_STRING_ARB );
		common->Printf( "\nGL_PROGRAM_ERROR_STRING_ARB: %s\n", str );
		if ( ofs < 0 ) {
			common->Printf( "GL_PROGRAM_ERROR_POSITION_ARB < 0 with error\n" );
		} else if ( ofs >= (int)strlen( start ) ) {
			common->Printf( "error at end of program\n" );
		} else {
			int printOfs = Max( ofs - 20, 0 ); // DG: print some more context
			common->Printf( "error at %i:\n%s", ofs, start + printOfs );
		}
		return;
	}
	if ( ofs != -1 ) {
		common->Printf( "\nGL_PROGRAM_ERROR_POSITION_ARB != -1 without error\n" );
		return;
	}

	progs[progIndex].loaded = true;
	common->Printf( "\n" );
}

/*
==================
R_FindARBProgram

Returns a GL identifier that can be bound to the given target, parsing
a text file if it hasn't already been loaded.
==================
*/
int R_FindARBProgram( GLenum target, const char *program ) {
	int		i;
	idStr	stripped = program;

	stripped.StripFileExtension();

	// see if it is already loaded
	for ( i = 0 ; progs[i].name[0] ; i++ ) {
		if ( progs[i].target != target ) {
			continue;
		}

		idStr	compare = progs[i].name;
		compare.StripFileExtension();

		if ( !idStr::Icmp( stripped.c_str(), compare.c_str() ) ) {
			return progs[i].ident;
		}
	}

	if ( i == MAX_GLPROGS ) {
		common->Error( "R_FindARBProgram: MAX_GLPROGS" );
	}

	// add it to the list and load it
	progs[i].ident = (program_t)0;	// will be gen'd by R_LoadARBProgram
	progs[i].target = target;
	idStr::Copynz( progs[i].name, program, sizeof( progs[i].name ) );

	R_LoadARBProgram( i );

	return progs[i].ident;
}

/*
==================
R_ReloadARBPrograms_f
==================
*/
void R_ReloadARBPrograms_f( const idCmdArgs &args ) {
	int		i;
	int		nvLoaded;
	int		nv5Loaded;
	int		shadowLoaded;

	common->Printf( "----- R_ReloadARBPrograms -----\n" );
	nvLoaded = 0;
	nv5Loaded = 0;
	shadowLoaded = 0;
	for ( i = 0 ; progs[i].name[0] ; i++ ) {
		R_LoadARBProgram( i );
		if ( progs[i].loaded && R_ARB2NVProgram( progs[i].ident ) ) {
			nvLoaded++;
		}
		if ( progs[i].loaded && R_ARB2NV5Program( progs[i].ident ) ) {
			nv5Loaded++;
		}
		if ( progs[i].loaded && R_ARB2ShadowProgram( progs[i].ident ) ) {
			shadowLoaded++;
		}
	}

	/* The NV interaction path needs all three of its programs; a driver
	   that refuses any of them gets the ARB programs for interactions,
	   and the rest of the tier is unaffected. */
	rb_nvInteraction = arb2Tier >= ARB2_TIER_NV4 && rb_nvParmBuffer != 0 && nvLoaded == 3;
	if ( arb2Tier >= ARB2_TIER_NV4 && !rb_nvInteraction ) {
		common->Printf( "ARB2: NV interaction programs not accepted, interactions use the ARB programs\n" );
	}

	/* The batched path draws what it cannot batch through the NV4
	   programs, so it needs those as well as its own. */
	rb_nv5.programs = arb2Tier >= ARB2_TIER_NV5 && rb_nvInteraction && rb_nv5.idBuffer != 0 && nv5Loaded == 3;
	if ( arb2Tier >= ARB2_TIER_NV5 && !rb_nv5.programs ) {
		common->Printf( "ARB2: NV5 batched interactions not available, interactions use %s\n",
			rb_nvInteraction ? "the NV4 programs" : "the ARB programs" );
	}

	r_gpuShadowVolumes = rb_nv5.programs && rb_gs.buffers && shadowLoaded == 3;
	if ( rb_gs.buffers && !r_gpuShadowVolumes ) {
		common->Printf( "ARB2: NV5 shadow volume programs not available, shadow volumes are built on the CPU\n" );
	}
}

/*
==================
R_ARB2_SelectTier

The highest tier the context offers, capped by arb2TierRequest.  Called with
the context current, before the programs load; the parameter buffer the
NV tiers read is made here, so a context that refuses it stays on ARB.
==================
*/
static void R_ARB2_SelectTier( void ) {
	static const char * const tierNames[] = {
		"ARB assembly", "NV_gpu_program4", "NV_gpu_program5"
	};
	int		avail;
	int		want;
	int		tier;

	avail = ARB2_TIER_ARB;
	if ( glConfig.ARBVertexBufferObjectAvailable && glCaps.nvGpuProgram4 ) {
		avail = ARB2_TIER_NV4;
		// the batched path wants at least eight texture units of its own
		if ( glCaps.nvBatchDraw && glConfig.maxTextureImageUnits >= ARB2_NV5_SLOT_UNIT + 8 ) {
			avail = ARB2_TIER_NV5;
		}
	}
	want = arb2TierRequest;
	tier = ( want < 0 || want > avail ) ? avail : want;
	if ( want > avail ) {
		common->Printf( "ARB2: %s asked for, which this context lacks\n", tierNames[want] );
	}

	if ( tier >= ARB2_TIER_NV4 ) {
		while ( qglGetError() != GL_NO_ERROR ) {
		}
		qglGenBuffersARB( 1, &rb_nvParmBuffer );
		qglBindBufferARB( GL_ARRAY_BUFFER_ARB, rb_nvParmBuffer );
		qglBufferDataARB( GL_ARRAY_BUFFER_ARB, sizeof( rb_nvRec.v ), NULL, GL_DYNAMIC_DRAW_ARB );
		qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
		vertexCache.InvalidateBindings();
		if ( qglGetError() != GL_NO_ERROR || rb_nvParmBuffer == 0 ) {
			common->Printf( "ARB2: no parameter buffer for %s\n", tierNames[tier] );
			rb_nvParmBuffer = 0;
			tier = ARB2_TIER_ARB;
		}
		ARB2_NVREC_RESET( &rb_nvRec );
	}

	if ( tier >= ARB2_TIER_NV5 ) {
		GLuint	offsets[ARB2_NV5_BATCH];
		int		units;
		int		i;

		units = glConfig.maxTextureImageUnits < ARB2_NV5_SLOT_UNIT + ARB2_NV5_MAX_SLOTS
			? glConfig.maxTextureImageUnits : ARB2_NV5_SLOT_UNIT + ARB2_NV5_MAX_SLOTS;
		rb_nv5.slots = units - ARB2_NV5_SLOT_UNIT;
		R_ARB2NV5_BuildPrograms( units - 1 );
		RB_ARB2NV5_ResetSlots();

		for ( i = 0; i < ARB2_NV5_BATCH; i++ ) {
			offsets[i] = (GLuint)( i * ARB2_NV5_REC_BYTES );
		}
		qglGenBuffersARB( 1, &rb_nv5.idBuffer );
		qglBindBufferARB( GL_ARRAY_BUFFER_ARB, rb_nv5.idBuffer );
		qglBufferDataARB( GL_ARRAY_BUFFER_ARB, sizeof( offsets ), offsets, GL_STATIC_DRAW_ARB );
		qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
		vertexCache.InvalidateBindings();
		if ( qglGetError() != GL_NO_ERROR || rb_nv5.idBuffer == 0 ) {
			common->Printf( "ARB2: no record offset buffer for %s\n", tierNames[tier] );
			rb_nv5.idBuffer = 0;
			tier = ARB2_TIER_NV4;
		}
	}

	if ( tier >= ARB2_TIER_NV5 && glCaps.nvGpuShadows ) {
		RB_GS_Init();
	}

	arb2Tier = tier;
	common->Printf( "ARB2 program tier: %s\n", tierNames[tier] );
}

/*
==================
R_ARB2_Init

==================
*/
void R_ARB2_Init( void ) {
	glConfig.allowARB2Path = false;
	arb2Tier = ARB2_TIER_ARB;

	// a new context: whatever buffers the old one had went with it
	rb_nvParmBuffer = 0;
	rb_nvInteraction = false;
	memset( &rb_nv5, 0, sizeof( rb_nv5 ) );
	RB_GS_Reset();

	common->Printf( "ARB2 renderer: " );

	if ( !glConfig.ARBVertexProgramAvailable || !glConfig.ARBFragmentProgramAvailable ) {
		common->Printf( "Not available.\n" );
		return;
	}

	common->Printf( "Available.\n" );

	glConfig.allowARB2Path = true;
	R_ARB2_SelectTier();
}
