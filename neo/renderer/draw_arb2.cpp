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
*/
#define RB_ENV_CACHE_SIZE ( PP_COLOR_ADD + 1 )
#define RB_FRAG_ENV_CACHE_SIZE 2

static float	rb_envCache[RB_ENV_CACHE_SIZE][4];
static bool	rb_envCacheValid[RB_ENV_CACHE_SIZE];

static float	rb_fragEnvCache[RB_FRAG_ENV_CACHE_SIZE][4];
static bool	rb_fragEnvCacheValid[RB_FRAG_ENV_CACHE_SIZE];

static ID_INLINE void RB_EnvCacheReset( void ) {
	memset( rb_envCacheValid, 0, sizeof( rb_envCacheValid ) );
	memset( rb_fragEnvCacheValid, 0, sizeof( rb_fragEnvCacheValid ) );
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
=============
RB_ARB2_CreateDrawInteractions

=============
*/
void RB_ARB2_CreateDrawInteractions( const drawSurf_t *surf ) {
	if ( !surf ) {
		return;
	}

	// perform setup here that will be constant for all interactions
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	// bind the vertex program
	if ( r_testARBProgram.GetBool() ) {
		qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_TEST );
		qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_TEST );
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

	// texture 0 is the normalization cube map for the vector towards the light
	GL_SelectTextureNoClient( 0 );
	if ( backEnd.vLight->lightShader->IsAmbientLight() ) {
		globalImages->ambientNormalMap->Bind();
	} else {
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
	RB_UpdateSpecularBoost();

	for ( ; surf ; surf=surf->nextOnLight ) {
		// perform setup here that will not change over multiple interaction passes

		// set the vertex pointers
		idDrawVert	*ac = (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );
		qglColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), ac->color );
		qglVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
		qglVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
		qglVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
		qglVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), ac->st.ToFloatPtr() );
		qglVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );

		// this may cause RB_ARB2_DrawInteraction to be exacuted multiple
		// times with different colors and images if the surface or light have multiple layers
		RB_CreateSingleDrawInteractions( surf, RB_ARB2_DrawInteraction );
	}

	RB_EnvCacheReset();

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
==================
RB_ARB2_DrawInteractions
==================
*/
void RB_ARB2_DrawInteractions( void ) {
	viewLight_t		*vLight;

	GL_SelectTexture( 0 );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );

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
			qglEnable( GL_VERTEX_PROGRAM_ARB );
			qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
			RB_StencilShadowPass( vLight->globalShadows );
			RB_ARB2_CreateDrawInteractions( vLight->localInteractions );
			qglEnable( GL_VERTEX_PROGRAM_ARB );
			qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
			RB_StencilShadowPass( vLight->localShadows );
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

void R_LoadARBProgram( int progIndex ) {
	int		ofs;
	int		err;
	char	*buffer;
	char	*start = NULL, *end;

#if D3_INTEGRATE_SOFTPART_SHADERS
	if ( progs[progIndex].ident == VPROG_SOFT_PARTICLE || progs[progIndex].ident == FPROG_SOFT_PARTICLE
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
			: (progs[progIndex].ident == FPROG_SOFT_PARTICLE) ? softpartFShader
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

		// copy to stack memory and free
		buffer = (char *)_alloca( strlen( fileBuffer ) + 1 );
		strcpy( buffer, fileBuffer );
		fileSystem->FreeFile( fileBuffer );
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
		start = strstr( buffer, "!!ARBvp" );
	}
	if ( progs[progIndex].target == GL_FRAGMENT_PROGRAM_ARB ) {
		if ( !glConfig.ARBFragmentProgramAvailable ) {
			common->Printf( ": GL_FRAGMENT_PROGRAM_ARB not available\n" );
			return;
		}
		start = strstr( buffer, "!!ARBfp" );
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
	if ( progs[progIndex].ident == FPROG_INTERACTION && r_spectralMix.GetBool() ) {
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
	if ( progs[progIndex].ident == FPROG_INTERACTION && r_specularAA.GetBool() ) {
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


	end = strstr( start, "END" );

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

	common->Printf( "----- R_ReloadARBPrograms -----\n" );
	for ( i = 0 ; progs[i].name[0] ; i++ ) {
		R_LoadARBProgram( i );
	}
}

/*
==================
R_ARB2_Init

==================
*/
void R_ARB2_Init( void ) {
	glConfig.allowARB2Path = false;

	common->Printf( "ARB2 renderer: " );

	if ( !glConfig.ARBVertexProgramAvailable || !glConfig.ARBFragmentProgramAvailable ) {
		common->Printf( "Not available.\n" );
		return;
	}

	common->Printf( "Available.\n" );

	glConfig.allowARB2Path = true;
}
