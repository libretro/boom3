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

extern idCVar r_useCarmacksReverse;
extern idCVar r_useStencilOpSeparate;

#ifdef HAVE_OPENGLES
#include "renderer/gles_compat.h"
#endif
#include "renderer/gl_caps.h"
#include "renderer/glsl_translate.h"
#include "renderer/vr.h"

/*
=====================
RB_BakeTextureMatrixIntoTexgen
=====================
*/
void RB_BakeTextureMatrixIntoTexgen( idPlane lightProject[3], const float *textureMatrix ) {
	float	genMatrix[16];
	float	final[16];

	genMatrix[0] = lightProject[0][0];
	genMatrix[4] = lightProject[0][1];
	genMatrix[8] = lightProject[0][2];
	genMatrix[12] = lightProject[0][3];

	genMatrix[1] = lightProject[1][0];
	genMatrix[5] = lightProject[1][1];
	genMatrix[9] = lightProject[1][2];
	genMatrix[13] = lightProject[1][3];

	genMatrix[2] = 0;
	genMatrix[6] = 0;
	genMatrix[10] = 0;
	genMatrix[14] = 0;

	genMatrix[3] = lightProject[2][0];
	genMatrix[7] = lightProject[2][1];
	genMatrix[11] = lightProject[2][2];
	genMatrix[15] = lightProject[2][3];

	myGlMultMatrix( genMatrix, textureMatrix, final );

	lightProject[0][0] = final[0];
	lightProject[0][1] = final[4];
	lightProject[0][2] = final[8];
	lightProject[0][3] = final[12];

	lightProject[1][0] = final[1];
	lightProject[1][1] = final[5];
	lightProject[1][2] = final[9];
	lightProject[1][3] = final[13];
}

// the same on the rows of a matrix, which are laid out as planes are
void RB_BakeTextureMatrixIntoTexgen( idMat4 &lightProject, const float *textureMatrix ) {
	RB_BakeTextureMatrixIntoTexgen( reinterpret_cast<idPlane *>( lightProject.ToFloatPtr() ), textureMatrix );
}

/*
   The two HDR helpers below are shared by both backends: draw_arb2.cpp
   folds them into fragment env parameters, draw_gles2.cpp into
   uniforms.  They sit above the ARB-only region for that reason.
   Defined inside it, a GLES build compiles the callers and not the
   callees - a link error on every GLES target and nothing at all on
   desktop, which is how it reached CI.

   Neither touches GL; both are arithmetic over the HDR globals and two
   cvars.
*/
/*
==================
RB_HDRGammaBrightness

The value for env[21].xyz (PP_GAMMA_BRIGHTNESS). The injected gamma
epilogue consumes it as

    stored = pow( env[21].xyz * c, env[21].w )      // env[21].w == 1/r_gamma

so a fold placed there lands BEFORE the gamma encode, while hdr_present
undoes it AFTER, in the stored domain:

    lin = srgbToLinear( stored * (1/s) )

Folding with a bare s gives stored = s^(1/g) * (c*b)^(1/g), and the two
cancel only at r_gamma == 1. Folding with s^g gives

    stored = ( c * b * s^g )^(1/g) = s * (c*b)^(1/g)

which cancels exactly at any gamma. It also pins the encoded headroom at
a constant 2x of the stored range instead of letting it drift with the
brightness slider: the MUL_SAT ceiling moves to c*b == 1/s^g, which is
precisely the input that stores as 1.0.

r_gamma is CVAR_ARCHIVE and reachable from the in-game brightness menu,
so the non-unity path is not theoretical - at r_gamma 1.5 the old
expression was 26% bright, at 2.0 it was 41% bright, at 0.7 it was 23%
dark, all of it clipping into the roll-off.

Filter-blend stages ship unscaled: a filter multiplies against a
destination that already carries the fold, so scaling the source as well
would square it.

Bit-identical to the pre-HDR path whenever s == 1 (HDR off, or the
headroom option disabled), and bit-identical to the previous HDR path at
r_gamma == 1, which is the default.
==================
*/
/*
===================
RB_HDRSpecularBoost

Scale for the interaction specular constant under HDR output, shared by
both renderers - draw_arb2.cpp for ARB programs and draw_gles2.cpp for
the GLSL path.  Only the value is shared; each backend applies it in its
own dialect.

The gate is the part worth keeping in one place.  The boost only helps
where the pipeline has room above 1.0 for the result, which is a float
scene target WITH unbounded blending, or the encoding fold.  A float
target on its own is not enough: with true blending off the epilogue
clamps every pass at 1.0 exactly as an integer target does, and boosting
into that ceiling turns highlights into flat white.  That distinction
was once wrong in draw_arb2.cpp precisely because the rule lived next to
one backend; there is now one copy for both to get wrong together or not
at all.
===================
*/
float RB_HDRSpecularBoost( void ) {
	extern bool  hdr_output_active;
	extern float hdr_specular_gain;
	extern float hdr_scene_encode_scale;
	extern bool  hdr_fp16_scene;
	extern bool  hdr_fp32_scene;
	extern bool  hdr_unbounded_blend;

	const bool floatHeadroom = ( hdr_fp16_scene || hdr_fp32_scene ) && hdr_unbounded_blend;
	const bool foldHeadroom  = hdr_scene_encode_scale != 1.0f;

	return ( hdr_output_active && ( floatHeadroom || foldHeadroom ) )
			? hdr_specular_gain : 1.0f;
}

float RB_HDRGammaBrightness( bool filterBlend ) {
	extern float hdr_scene_encode_scale;

	const float b = r_brightness.GetFloat();
	const float s = hdr_scene_encode_scale;

	if ( filterBlend || s == 1.0f ) {
		return b;
	}

	/* pow(s, gamma) is the only transcendental here, and both of its
	 * arguments are fixed for the duration of a frame - the encode scale
	 * is set from a core option and r_gamma from a cvar, neither of
	 * which moves while the backend is drawing.  The callers are not:
	 * the material stage paths call this per stage, per surface, so the
	 * pow was being evaluated thousands of times a frame for one value.
	 *
	 * Memoized on the exact bits of both arguments, so a recomputation
	 * happens whenever either actually changes and the returned value is
	 * always the one powf() would have produced for the current
	 * arguments.  Backend-thread only, like every RB_ function. */
	const float g = r_gamma.GetFloat();
	static float	memoS = 0.0f;
	static float	memoG = 0.0f;
	static float	memoPow = 0.0f;
	static bool	memoValid = false;

	if ( !memoValid
			|| memcmp( &memoS, &s, sizeof( float ) ) != 0
			|| memcmp( &memoG, &g, sizeof( float ) ) != 0 ) {
		memoPow = idMath::Pow( s, g );
		memoS = s;
		memoG = g;
		memoValid = true;
	}

	return b * memoPow;
}

/*
===================
RB_GLSLEpilogueParms

u_hdrParms for the GLSL renderers' epilogue, which does what the epilogue
draw_arb2.cpp injects into every ARB2 program: x r_brightness with the
encoding fold (RB_HDRGammaBrightness), y the clamp - 0 per channel, 1
luminance-aware, 2 none above 1 on a float scene target with true blending,
as the ARB2 variants choose - z as x, w 1/r_gamma.
===================
*/
void RB_GLSLEpilogueParms( bool filterBlend, float hp[4] ) {
	extern bool hdr_luma_clamp;
	extern bool hdr_fp16_scene;
	extern bool hdr_fp32_scene;
	extern bool hdr_unbounded_blend;
	const bool inShader = r_gammaInShader.GetBool();
	const bool unbounded = ( hdr_fp16_scene || hdr_fp32_scene ) && hdr_unbounded_blend;

	hp[0] = inShader ? RB_HDRGammaBrightness( filterBlend ) : 1.0f;
	hp[1] = unbounded ? 2.0f : hdr_luma_clamp ? 1.0f : 0.0f;
	hp[2] = hp[0];
	hp[3] = inShader ? 1.0f / r_gamma.GetFloat() : 1.0f;
}

/*
===================
RB_FilterBlend

Whether a stage's blend multiplies the destination by the source: such a
source is a factor, not scene energy, so the encoding fold stays off it.
===================
*/
bool RB_FilterBlend( int drawStateBits ) {
	const int src = drawStateBits & GLS_SRCBLEND_BITS;
	const int dst = drawStateBits & GLS_DSTBLEND_BITS;
	return src == GLS_SRCBLEND_DST_COLOR || src == GLS_SRCBLEND_ONE_MINUS_DST_COLOR
		|| dst == GLS_DSTBLEND_SRC_COLOR || dst == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR;
}

/*
=====================
RB_MaterialReadsCurrentDepth / RB_ViewDepthReaders

What reads _currentDepth: soft particles (a surface flagged
DSF_SOFT_PARTICLE with a softening radius), any material stage or
fragment program image that is the image itself (surfaces and lights
alike), and r_showDepth.  A view with none of them never samples the copy.
Soft particles of a post-process material count with the others: post-
process surfaces write depth while they draw.
=====================
*/
bool RB_MaterialReadsCurrentDepth( const idMaterial *material ) {
	int i, j;
	if ( material == NULL ) {
		return false;
	}
	for ( i = 0; i < material->GetNumStages(); i++ ) {
		const shaderStage_t *stage = material->GetStage( i );
		if ( stage->texture.image == globalImages->currentDepthImage ) {
			return true;
		}
		if ( stage->newStage ) {
			for ( j = 0; j < stage->newStage->numFragmentProgramImages; j++ ) {
				if ( stage->newStage->fragmentProgramImages[j] == globalImages->currentDepthImage ) {
					return true;
				}
			}
		}
	}
	return false;
}

enum {
	RB_DEPTH_READ_SOFT	= 1,	// soft particles drawn before the post-process surfaces
	RB_DEPTH_READ_OTHER	= 2		// anything else
};

static int RB_ViewDepthReaders( void ) {
	const viewDef_t *view = backEnd.viewDef;
	const viewLight_t *vLight;
	const idMaterial *last = NULL;
	int readers = 0;
	int i;
	if ( r_showDepth.GetBool() ) {
		return RB_DEPTH_READ_OTHER;
	}
	for ( i = 0; i < view->numDrawSurfs; i++ ) {
		const drawSurf_t *surf = view->drawSurfs[i];
		if ( ( surf->dsFlags & DSF_SOFT_PARTICLE ) && surf->particle_radius > 0.0f ) {
			if ( surf->material->GetSort() >= SS_POST_PROCESS ) {
				return RB_DEPTH_READ_OTHER;
			}
			readers |= RB_DEPTH_READ_SOFT;
		}
		// surfaces sharing a material sit together in the sorted list
		if ( surf->material != last ) {
			last = surf->material;
			if ( RB_MaterialReadsCurrentDepth( last ) ) {
				return RB_DEPTH_READ_OTHER;
			}
		}
	}
	for ( vLight = view->viewLights; vLight; vLight = vLight->next ) {
		if ( RB_MaterialReadsCurrentDepth( vLight->lightShader ) ) {
			return RB_DEPTH_READ_OTHER;
		}
	}
	return readers;
}

bool RB_ViewReadsCurrentDepth( void ) {
	return RB_ViewDepthReaders() != 0;
}

/*
=====================
RB_DepthCaptureWanted

Whether the view's depth is copied into _currentDepth after the depth
fill: capture on, or soft particles asking for it, in a view that is not
the lightgem's and that reads the copy.
=====================
*/
bool RB_DepthCaptureWanted( void ) {
	const bool capture = r_enableDepthCapture.GetInteger() == 1
		|| ( r_enableDepthCapture.GetInteger() == -1 && r_useSoftParticles.GetBool() );
	return capture && backEnd.viewDef->renderView.viewID >= 0 && RB_ViewReadsCurrentDepth();
}

/*
=====================
RB_DepthCaptureInPlace / RB_BindDepthInPlace / RB_UnbindDepthInPlace

The GL renderers' soft particles read the view's own depth texture instead
of a copy when nothing else reads the copy, the view starts at the
framebuffer's corner (so a pixel's texel is the one the copy would put
there) and a texture barrier orders the depth fill and the stencil passes
before the reads.  Between the barrier and the last soft particle nothing
writes depth or stencil: soft particles draw before the post-process
surfaces, the only ones that write depth after the fill, and the stencil
ops are KEEP once the shadows are done.

RB_DepthCaptureInPlace decides it for every view at the capture point,
and says whether the copy can be skipped.  The texture stays bound only
while soft particles draw.
=====================
*/
/* every renderer's condition: the view's depth is wanted, by soft
 * particles alone, and the view starts at the target's corner */
bool RB_DepthSoftOnly( void ) {
	const viewDef_t *view = backEnd.viewDef;
	return view->viewport.x1 == 0 && view->viewport.y1 == 0
		&& RB_DepthCaptureWanted()
		&& RB_ViewDepthReaders() == RB_DEPTH_READ_SOFT;
}

/*
=====================
RB_CurrentRenderCopyRect

The part of the view the post-process surfaces can read from _currentRender,
so the copy before them takes only that.  The heat haze programs read their
own pixel moved by a normal map's -1..1 times the deform their vertex
program clamps to 0.02 of the view times the stage's magnitude (local[1]);
the colour process reads its own pixel.  A surface draws inside its scissor
rectangle, so its reach is that rectangle grown by the deform, plus two
pixels for the bilinear taps.

rect is x, y, width, height from the view's corner; a width of 0 means
nothing reads the copy.  False where the whole view is copied: a surface
whose reads are not known (another program, a fixed-function stage, a
program ARB2 loaded from text that is not the game's own), anything before
the post-process surfaces reading _currentRender, a view off the target's
corner, no scissoring, or a projection whose deform the clamp leaves
unbounded.
=====================
*/
static bool RB_StageReadsCurrentRender( const shaderStage_t *stage ) {
	int j;
	if ( stage->texture.image == globalImages->currentRenderImage ) {
		return true;
	}
	if ( stage->newStage ) {
		for ( j = 0; j < stage->newStage->numFragmentProgramImages; j++ ) {
			if ( stage->newStage->fragmentProgramImages[j] == globalImages->currentRenderImage ) {
				return true;
			}
		}
	}
	return false;
}

static bool RB_MaterialReadsCurrentRender( const idMaterial *material ) {
	int i;
	if ( material == NULL ) {
		return false;
	}
	for ( i = 0; i < material->GetNumStages(); i++ ) {
		if ( RB_StageReadsCurrentRender( material->GetStage( i ) ) ) {
			return true;
		}
	}
	return false;
}

bool RB_CurrentRenderCopyRect( drawSurf_t **surfs, int numSurfs, int rect[4] ) {
	const viewDef_t *view = backEnd.viewDef;
	const float *proj = view->projectionMatrix;
	const int vw = view->viewport.x2 - view->viewport.x1 + 1;
	const int vh = view->viewport.y2 - view->viewport.y1 + 1;
	const viewLight_t *vLight;
	int x1 = vw, y1 = vh, x2 = -1, y2 = -1;
	int i, j;

	if ( view->viewport.x1 != 0 || view->viewport.y1 != 0 || vw <= 0 || vh <= 0
		|| !r_useScissor.GetBool() || proj[8] != 0.0f || proj[12] != 0.0f ) {
		return false;
	}
	// what draws before the copy reads the last one
	for ( i = 0; i < view->numDrawSurfs; i++ ) {
		const idMaterial *m = view->drawSurfs[i]->material;
		if ( m->GetSort() < SS_POST_PROCESS && RB_MaterialReadsCurrentRender( m ) ) {
			return false;
		}
	}
	for ( vLight = view->viewLights; vLight; vLight = vLight->next ) {
		if ( RB_MaterialReadsCurrentRender( vLight->lightShader ) ) {
			return false;
		}
	}
	for ( i = 0; i < numSurfs; i++ ) {
		const drawSurf_t *surf = surfs[i];
		const idMaterial *material = surf->material;
		const float *regs = surf->shaderRegisters;
		int mx = -1, my = -1;
		for ( j = 0; j < material->GetNumStages(); j++ ) {
			const shaderStage_t *stage = material->GetStage( j );
			const newShaderStage_t *ns = stage->newStage;
			float dx, dy;
			if ( !RB_StageReadsCurrentRender( stage ) ) {
				continue;
			}
			if ( ns == NULL ) {
				return false;
			}
#ifdef BOOM3_ARB2_RENDERER
			if ( !R_GLSLRenderer() && ( !R_ARBProgramIsStock( ns->vertexProgram )
				|| !R_ARBProgramIsStock( ns->fragmentProgram ) ) ) {
				return false;
			}
#endif
			switch ( ns->glslProgram ) {
			case GLSL_STAGE_COLOR_PROCESS:
				dx = dy = 0.0f;
				break;
			case GLSL_STAGE_HEAT_HAZE:
			case GLSL_STAGE_HEAT_HAZE_MASK:
			case GLSL_STAGE_HEAT_HAZE_MASK_VERTEX:
				if ( ns->numVertexParms < 2 ) {
					return false;
				}
				dx = 0.02f * idMath::Fabs( regs[ns->vertexParms[1][0]] ) * vw;
				dy = 0.02f * idMath::Fabs( regs[ns->vertexParms[1][1]] ) * vh;
				break;
			default:
				return false;
			}
			// not a number, or past the view: the whole copy
			if ( !( dx <= (float)vw ) || !( dy <= (float)vh ) ) {
				return false;
			}
			if ( (int)dx + 3 > mx ) {
				mx = (int)dx + 3;
			}
			if ( (int)dy + 3 > my ) {
				my = (int)dy + 3;
			}
		}
		if ( mx < 0 ) {
			continue;
		}
		// an empty scissor the Vulkan renderer widens to the view's
		if ( surf->scissorRect.x2 < surf->scissorRect.x1 || surf->scissorRect.y2 < surf->scissorRect.y1 ) {
			return false;
		}
		if ( surf->scissorRect.x1 - mx < x1 ) {
			x1 = surf->scissorRect.x1 - mx;
		}
		if ( surf->scissorRect.y1 - my < y1 ) {
			y1 = surf->scissorRect.y1 - my;
		}
		if ( surf->scissorRect.x2 + mx > x2 ) {
			x2 = surf->scissorRect.x2 + mx;
		}
		if ( surf->scissorRect.y2 + my > y2 ) {
			y2 = surf->scissorRect.y2 + my;
		}
	}
	if ( x2 < x1 || y2 < y1 ) {
		rect[0] = rect[1] = rect[2] = rect[3] = 0;
		return true;
	}
	if ( x1 < 0 ) {
		x1 = 0;
	}
	if ( y1 < 0 ) {
		y1 = 0;
	}
	if ( x2 > vw - 1 ) {
		x2 = vw - 1;
	}
	if ( y2 > vh - 1 ) {
		y2 = vh - 1;
	}
	rect[0] = x1;
	rect[1] = y1;
	rect[2] = x2 - x1 + 1;
	rect[3] = y2 - y1 + 1;
	return true;
}

/*
=====================
RB_SoftDepthBounds / RB_SoftDepthBoundsEnd

The GL renderers' soft particles behind the scene, rejected before they
shade: the depth bounds test drops a pixel whose scene depth is nearer
than the surface's softDepthMin, where every fragment fades to zero, under
the blends that leave the colour as it was for those fragments.  The test
compares the depth the particles also read, which nothing writes between
the fill and them.  True when the test is on for the draw.
=====================
*/
bool RB_SoftDepthBounds( const drawSurf_t *surf, int drawStateBits ) {
	if ( !glConfig.depthBoundsTestAvailable || surf->softDepthMin <= 0.0f
		|| !RB_SOFT_FADE_DROPS( drawStateBits ) ) {
		return false;
	}
	qglEnable( GL_DEPTH_BOUNDS_TEST_EXT );
	qglDepthBoundsEXT( surf->softDepthMin, 1.0 );
	return true;
}

void RB_SoftDepthBoundsEnd( void ) {
	qglDisable( GL_DEPTH_BOUNDS_TEST_EXT );
}

static GLuint	rbDepthInPlace;			// the view's depth texture, 0 when the copy serves
static float	rbDepthInPlaceRecip[2];	// window position to its texcoord
static bool		rbDepthBarrierDone;		// the barrier for this view's reads is issued

bool RB_DepthCaptureInPlace( void ) {
	int w = 0, h = 0;
	GLuint tex;

	rbDepthInPlace = 0;
	rbDepthBarrierDone = false;
	if ( R_VulkanRenderer() || !glCaps.textureBarrier || !RB_DepthSoftOnly() ) {
		return false;
	}
	tex = GLimp_ViewDepthTexture( &w, &h );
	if ( tex == 0 || w <= 0 || h <= 0 ) {
		return false;
	}
	rbDepthInPlace = tex;
	rbDepthInPlaceRecip[0] = 1.0f / w;
	rbDepthInPlaceRecip[1] = 1.0f / h;
	return true;
}

bool RB_DepthInPlace( float recip[2] ) {
	if ( rbDepthInPlace == 0 ) {
		return false;
	}
	if ( recip ) {
		recip[0] = rbDepthInPlaceRecip[0];
		recip[1] = rbDepthInPlaceRecip[1];
	}
	return true;
}

void RB_BindDepthInPlace( void ) {
	tmu_t *tmu = &backEnd.glState.tmu[backEnd.glState.currenttmu];
	if ( !rbDepthBarrierDone ) {
		glCaps.TextureBarrier();
		rbDepthBarrierDone = true;
	}
	// the unit cleared first, so its cache describes what is bound
	globalImages->BindNull();
	qglBindTexture( GL_TEXTURE_2D, rbDepthInPlace );
	tmu->current2DMap = rbDepthInPlace;
	if ( R_GLSLRenderer() ) {
		// what the GLSL renderers' BindNull unbinds
		tmu->textureType = TT_2D;
	}
}

void RB_UnbindDepthInPlace( void ) {
	tmu_t *tmu = &backEnd.glState.tmu[backEnd.glState.currenttmu];
	// unbound, so no later draw that writes depth can sample it
	qglBindTexture( GL_TEXTURE_2D, 0 );
	tmu->current2DMap = -1;
}

/*
=====================
Half resolution particles

r_particleHalfRes draws a run of particle surfaces into a target half the
view's width and height.  Its colour gathers what the run adds and its
alpha what is left of the scene behind the run - each blend's destination
factor applied to alpha alone (GL_State) - so one blend, colour plus the
scene times alpha, puts the run on the scene.  The target's depth is the
farthest of each 2x2 of the view's, so a half resolution pixel draws a
particle any of its four would.

Where the scene's depth breaks inside the 2x2s a pixel's composite reads
from, the half resolution answer is not that pixel's: those pixels are
marked in the stencil, left out of the composite, and drawn again by the
run at full resolution.  The marks use values the shadow counts around
128 do not reach, and nothing after the shader passes tests the stencil.

A surface goes into a run when it is a particle system's and every stage
it draws blends without reading the destination, onto it scaled by ZERO,
ONE or the source's alpha; writes neither depth nor only some colours;
tests depth as LESS; and reads no window position: no program stage, no
screen texgen, no _currentRender or _currentDepth image (soft particles
read the depth through their own path, at twice the scale while the run
draws, RB_LowResScale).  A view qualifies when it is the 3D view at the
corner of a target whose depth is a texture (GLimp_ViewDepthTexture).
=====================
*/
bool RB_LowResEligible( const drawSurf_t *surf ) {
	const idMaterial *material = surf->material;
	int i;
	if ( !( surf->dsFlags & DSF_PARTICLE ) || material == NULL
		|| material->GetSort() >= SS_POST_PROCESS || material->SuppressInSubview() ) {
		return false;
	}
	for ( i = 0; i < material->GetNumStages(); i++ ) {
		const shaderStage_t *stage = material->GetStage( i );
		const int bits = stage->drawStateBits;
		const int src = bits & GLS_SRCBLEND_BITS;
		const int dst = bits & GLS_DSTBLEND_BITS;
		if ( stage->lighting != SL_AMBIENT
			|| ( bits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) ) {
			continue;
		}
		if ( stage->newStage
			|| !( bits & GLS_DEPTHMASK )
			|| ( bits & ( GLS_REDMASK | GLS_GREENMASK | GLS_BLUEMASK | GLS_DEPTHFUNC_EQUAL ) )
			|| stage->texture.texgen == TG_SCREEN || stage->texture.texgen == TG_SCREEN2
			|| stage->texture.texgen == TG_GLASSWARP
			|| stage->texture.image == globalImages->currentRenderImage
			|| stage->texture.image == globalImages->currentDepthImage ) {
			return false;
		}
		if ( src != GLS_SRCBLEND_ZERO && src != GLS_SRCBLEND_ONE
			&& src != GLS_SRCBLEND_SRC_ALPHA && src != GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA ) {
			return false;
		}
		if ( dst != GLS_DSTBLEND_ZERO && dst != GLS_DSTBLEND_ONE
			&& dst != GLS_DSTBLEND_SRC_ALPHA && dst != GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) {
			return false;
		}
	}
	return true;
}

int r_programCount;
int r_programMisses;
int r_programWarm;

void R_ProgramBuilt( bool deliberate ) {
	r_programCount++;
	if ( !deliberate && !r_programWarm ) {
		r_programMisses++;
	}
}

#ifndef HAVE_OPENGLES

enum {
	RB_LOWRES_DOWN,		// the depth's farthest and the distance range, per 2x2
	RB_LOWRES_MARK,		// stencil marks where the range breaks
	RB_LOWRES_COMPOSITE,	// colour plus the scene times alpha
	RB_LOWRES_PROGRAMS
};

typedef struct {
	drawSurf_t	*surfs;				// the run with its scissors halved
	GLuint		fboDown, fboDraw;	// distance range + depth; colour + depth
	GLuint		accum, range, depth;
	GLuint		vbo;				// the GLSL programs' triangle
	GLuint		prog[RB_LOWRES_PROGRAMS];
	GLint		parm[RB_LOWRES_PROGRAMS];
	GLuint		arb[RB_LOWRES_PROGRAMS];
	int			numSurfs;			// surfs' room
	int			w, h;				// the targets'
	int			mark;				// the stencil value of the last run
	bool		failed;				// the context cannot, for good
	bool		active;				// the run is drawing at half resolution
	bool		redraw;				// the run is drawing its marked pixels at full resolution
} rbLowRes_t;

static rbLowRes_t rbLowRes;

#ifdef BOOM3_ARB2_RENDERER
void RB_SetProgramEnvironment( bool isPostProcess );

static const char * const rbLowResARB[RB_LOWRES_PROGRAMS] = {
	// local[0]: 1/depth width, 1/depth height, projection [10] and [14]
	"!!ARBfp1.0\n"
	"PARAM p = program.local[0];\n"
	"PARAM eps = { -0.000001, -0.000001, 0, 0 };\n"
	"TEMP c, t, d, n, l;\n"
	"MUL c.xy, fragment.position, 2.0;\n"
	"ADD t.xy, c, { -0.5, -0.5, 0, 0 };\n"
	"MUL t.xy, t, p;\n"
	"TEX t, t, texture[0], 2D;\n"
	"MOV d.x, t.x;\n"
	"ADD t.xy, c, { 0.5, -0.5, 0, 0 };\n"
	"MUL t.xy, t, p;\n"
	"TEX t, t, texture[0], 2D;\n"
	"MOV d.y, t.x;\n"
	"ADD t.xy, c, { -0.5, 0.5, 0, 0 };\n"
	"MUL t.xy, t, p;\n"
	"TEX t, t, texture[0], 2D;\n"
	"MOV d.z, t.x;\n"
	"ADD t.xy, c, { 0.5, 0.5, 0, 0 };\n"
	"MUL t.xy, t, p;\n"
	"TEX t, t, texture[0], 2D;\n"
	"MOV d.w, t.x;\n"
	"MIN n.x, d.x, d.y;\n"
	"MIN n.z, d.z, d.w;\n"
	"MIN n.x, n.x, n.z;\n"
	"MAX n.y, d.x, d.y;\n"
	"MAX n.w, d.z, d.w;\n"
	"MAX n.y, n.y, n.w;\n"
	"MOV result.depth.z, n.y;\n"
	"MAD l.xy, n, 2.0, -1.0;\n"
	"ADD l.xy, l, p.z;\n"
	"MIN l.xy, l, eps;\n"
	"RCP l.x, l.x;\n"
	"RCP l.y, l.y;\n"
	"MUL result.color, l.xyyy, p.w;\n"
	"END\n",
	// local[0]: 1/width and 1/height of the range, the edge fraction
	"!!ARBfp1.0\n"
	"PARAM q = program.local[0];\n"
	"TEMP f, u, a, b, s;\n"
	"MAD u.xy, fragment.position, 0.5, -0.5;\n"
	"FLR f.xy, u;\n"
	"ADD f.xy, f, 0.5;\n"
	"MUL u.xy, f, q;\n"
	"TEX a, u, texture[0], 2D;\n"
	"ADD u.xy, f, { 1, 0, 0, 0 };\n"
	"MUL u.xy, u, q;\n"
	"TEX b, u, texture[0], 2D;\n"
	"MIN s.x, a.x, b.x;\n"
	"MAX s.y, a.y, b.y;\n"
	"ADD u.xy, f, { 0, 1, 0, 0 };\n"
	"MUL u.xy, u, q;\n"
	"TEX a, u, texture[0], 2D;\n"
	"ADD u.xy, f, { 1, 1, 0, 0 };\n"
	"MUL u.xy, u, q;\n"
	"TEX b, u, texture[0], 2D;\n"
	"MIN a.x, a.x, b.x;\n"
	"MAX a.y, a.y, b.y;\n"
	"MIN s.x, s.x, a.x;\n"
	"MAX s.y, s.y, a.y;\n"
	"SUB s.y, s.y, s.x;\n"
	"MUL s.x, s.x, q.z;\n"
	"SUB s.x, s.y, s.x;\n"
	"KIL s.x;\n"
	"MOV result.color, { 0, 0, 0, 0 };\n"
	"END\n",
	// local[0]: half of 1/width and 1/height of the colour
	"!!ARBfp1.0\n"
	"PARAM q = program.local[0];\n"
	"TEMP u;\n"
	"MUL u, fragment.position, q;\n"
	"TEX result.color, u, texture[0], 2D;\n"
	"END\n"
};
#endif

static const char rbLowResVS[] =
	"#version 100\n"
	"attribute vec2 attr_Vertex;\n"
	"void main() {\n"
	"  gl_Position = vec4( attr_Vertex, 0.0, 1.0 );\n"
	"}\n";

static const char * const rbLowResFS[RB_LOWRES_PROGRAMS] = {
	"#version 100\n"
	"precision highp float;\n"
	"uniform sampler2D u_map;\n"
	"uniform vec4 u_parm;\n"
	"void main() {\n"
	"  vec2 c = gl_FragCoord.xy * 2.0;\n"
	"  float a = texture2D( u_map, ( c + vec2( -0.5, -0.5 ) ) * u_parm.xy ).x;\n"
	"  float b = texture2D( u_map, ( c + vec2( 0.5, -0.5 ) ) * u_parm.xy ).x;\n"
	"  float d = texture2D( u_map, ( c + vec2( -0.5, 0.5 ) ) * u_parm.xy ).x;\n"
	"  float e = texture2D( u_map, ( c + vec2( 0.5, 0.5 ) ) * u_parm.xy ).x;\n"
	"  float n = min( min( a, b ), min( d, e ) );\n"
	"  float f = max( max( a, b ), max( d, e ) );\n"
	"  vec2 l = min( vec2( n, f ) * 2.0 - 1.0 + u_parm.z, vec2( -0.000001 ) );\n"
	"  gl_FragDepth = f;\n"
	"  gl_FragColor = vec4( u_parm.w / l, 0.0, 1.0 );\n"
	"}\n",
	"#version 100\n"
	"precision highp float;\n"
	"uniform sampler2D u_map;\n"
	"uniform vec4 u_parm;\n"
	"void main() {\n"
	"  vec2 f = floor( gl_FragCoord.xy * 0.5 - 0.5 ) + 0.5;\n"
	"  vec2 a = texture2D( u_map, f * u_parm.xy ).xy;\n"
	"  vec2 b = texture2D( u_map, ( f + vec2( 1.0, 0.0 ) ) * u_parm.xy ).xy;\n"
	"  vec2 c = texture2D( u_map, ( f + vec2( 0.0, 1.0 ) ) * u_parm.xy ).xy;\n"
	"  vec2 d = texture2D( u_map, ( f + vec2( 1.0, 1.0 ) ) * u_parm.xy ).xy;\n"
	"  float n = min( min( a.x, b.x ), min( c.x, d.x ) );\n"
	"  float x = max( max( a.y, b.y ), max( c.y, d.y ) );\n"
	"  if ( x - n < u_parm.z * n ) {\n"
	"    discard;\n"
	"  }\n"
	"  gl_FragColor = vec4( 0.0 );\n"
	"}\n",
	"#version 100\n"
	"precision highp float;\n"
	"uniform sampler2D u_map;\n"
	"uniform vec4 u_parm;\n"
	"void main() {\n"
	"  gl_FragColor = texture2D( u_map, gl_FragCoord.xy * u_parm.xy );\n"
	"}\n"
};

float RB_LowResScale( void ) {
	return rbLowRes.active ? 2.0f : 1.0f;
}

bool RB_LowResRedraw( void ) {
	return rbLowRes.redraw;
}

void RB_LowResForget( void ) {
	Mem_Free( rbLowRes.surfs );
	memset( &rbLowRes, 0, sizeof( rbLowRes ) );
}

static GLuint RB_LowResShader( GLenum type, const char *text ) {
	idStr source( text );
	GLuint shader;
	GLint ok = 0;
	R_GLSLSourceForContext( source, type );
	text = source.c_str();
	shader = glCreateShader( type );
	glShaderSource( shader, 1, &text, NULL );
	glCompileShader( shader );
	glGetShaderiv( shader, GL_COMPILE_STATUS, &ok );
	if ( !ok ) {
		char log[1024];
		glGetShaderInfoLog( shader, sizeof( log ), NULL, log );
		common->Printf( "half resolution particles: %s\n", log );
		glDeleteShader( shader );
		return 0;
	}
	return shader;
}

/* the passes' programs, on first use */
bool RB_LowResPrograms( void ) {
	int i;
#ifdef BOOM3_ARB2_RENDERER
	if ( !R_GLSLRenderer() ) {
		if ( rbLowRes.arb[0] ) {
			return true;
		}
		qglGenProgramsARB( RB_LOWRES_PROGRAMS, rbLowRes.arb );
		for ( i = 0; i < RB_LOWRES_PROGRAMS; i++ ) {
			GLint err = -1;
			qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, rbLowRes.arb[i] );
			qglProgramStringARB( GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
				(GLsizei)strlen( rbLowResARB[i] ), rbLowResARB[i] );
			R_ProgramBuilt( false );
			qglGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, &err );
			if ( err != -1 ) {
				common->Printf( "half resolution particles: %s\n",
					(const char *)glGetString( GL_PROGRAM_ERROR_STRING_ARB ) );
				rbLowRes.failed = true;
			}
		}
		qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, 0 );
		return !rbLowRes.failed;
	}
#endif
	if ( rbLowRes.prog[0] ) {
		return true;
	}
	{
		static const float tri[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
		const GLuint vs = RB_LowResShader( GL_VERTEX_SHADER, rbLowResVS );
		for ( i = 0; i < RB_LOWRES_PROGRAMS; i++ ) {
			const GLuint fs = RB_LowResShader( GL_FRAGMENT_SHADER, rbLowResFS[i] );
			GLint ok = 0;
			rbLowRes.prog[i] = glCreateProgram();
			if ( vs == 0 || fs == 0 ) {
				rbLowRes.failed = true;
				continue;
			}
			glAttachShader( rbLowRes.prog[i], vs );
			glAttachShader( rbLowRes.prog[i], fs );
			glBindAttribLocation( rbLowRes.prog[i], 0, "attr_Vertex" );
			glLinkProgram( rbLowRes.prog[i] );
			R_ProgramBuilt( false );
			glDeleteShader( fs );
			glGetProgramiv( rbLowRes.prog[i], GL_LINK_STATUS, &ok );
			if ( !ok ) {
				rbLowRes.failed = true;
				continue;
			}
			glUseProgram( rbLowRes.prog[i] );
			glUniform1i( glGetUniformLocation( rbLowRes.prog[i], "u_map" ), 0 );
			rbLowRes.parm[i] = glGetUniformLocation( rbLowRes.prog[i], "u_parm" );
		}
		if ( vs ) {
			glDeleteShader( vs );
		}
		glUseProgram( 0 );
		backEnd.glState.currentProgram = NULL;
		glGenBuffers( 1, &rbLowRes.vbo );
		glBindBuffer( GL_ARRAY_BUFFER, rbLowRes.vbo );
		glBufferData( GL_ARRAY_BUFFER, sizeof( tri ), tri, GL_STATIC_DRAW );
		vertexCache.InvalidateBindings();
	}
	return !rbLowRes.failed;
}

static GLuint RB_LowResTexture( GLenum internal, GLenum format, GLenum type, GLint filter, int w, int h ) {
	GLuint tex;
	glGenTextures( 1, &tex );
	glBindTexture( GL_TEXTURE_2D, tex );
	glTexImage2D( GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, NULL );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	return tex;
}

static GLuint RB_LowResFramebuffer( GLuint color ) {
	GLuint fbo;
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0 );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, rbLowRes.depth, 0 );
	if ( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
		rbLowRes.failed = true;
	}
	return fbo;
}

/* the targets at w x h; leaves the view's framebuffer bound */
static bool RB_LowResTargets( int w, int h ) {
	tmu_t *tmu = &backEnd.glState.tmu[backEnd.glState.currenttmu];
	if ( rbLowRes.fboDraw && rbLowRes.w == w && rbLowRes.h == h ) {
		return true;
	}
	if ( rbLowRes.fboDraw ) {
		const GLuint fbos[2] = { rbLowRes.fboDown, rbLowRes.fboDraw };
		const GLuint texs[3] = { rbLowRes.accum, rbLowRes.range, rbLowRes.depth };
		glDeleteFramebuffers( 2, fbos );
		glDeleteTextures( 3, texs );
	}
	rbLowRes.w = w;
	rbLowRes.h = h;
	rbLowRes.accum = RB_LowResTexture( GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, w, h );
	rbLowRes.range = RB_LowResTexture( GL_RG32F, GL_RG, GL_FLOAT, GL_NEAREST, w, h );
	rbLowRes.depth = RB_LowResTexture( GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST, w, h );
	glBindTexture( GL_TEXTURE_2D, 0 );
	tmu->current2DMap = -1;
	rbLowRes.fboDown = RB_LowResFramebuffer( rbLowRes.range );
	rbLowRes.fboDraw = RB_LowResFramebuffer( rbLowRes.accum );
	glBindFramebuffer( GL_FRAMEBUFFER, GLimp_ViewFramebuffer() );
	return !rbLowRes.failed;
}

/* one of the programs over the bound target, reading tex on unit 0 */
static void RB_LowResPass( int prog, GLuint tex, const float parm[4] ) {
	tmu_t *tmu;
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	tmu = &backEnd.glState.tmu[0];
	glBindTexture( GL_TEXTURE_2D, tex );
	tmu->current2DMap = tex;
#ifdef BOOM3_ARB2_RENDERER
	if ( !R_GLSLRenderer() ) {
		qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, rbLowRes.arb[prog] );
		qglEnable( GL_FRAGMENT_PROGRAM_ARB );
		qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, parm );
		qglMatrixMode( GL_PROJECTION );
		qglPushMatrix();
		qglLoadIdentity();
		qglMatrixMode( GL_MODELVIEW );
		qglPushMatrix();
		qglLoadIdentity();
		qglBegin( GL_TRIANGLES );
		qglVertex2f( -1.0f, -1.0f );
		qglVertex2f( 3.0f, -1.0f );
		qglVertex2f( -1.0f, 3.0f );
		qglEnd();
		qglPopMatrix();
		qglMatrixMode( GL_PROJECTION );
		qglPopMatrix();
		qglMatrixMode( GL_MODELVIEW );
		qglDisable( GL_FRAGMENT_PROGRAM_ARB );
	} else
#endif
	{
		GLint enabled[6];
		int a;
		tmu->textureType = TT_2D;
		glUseProgram( rbLowRes.prog[prog] );
		glUniform4fv( rbLowRes.parm[prog], 1, parm );
		for ( a = 0; a < 6; a++ ) {
			glGetVertexAttribiv( a, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled[a] );
			if ( a > 0 && enabled[a] ) {
				glDisableVertexAttribArray( a );
			}
		}
		glEnableVertexAttribArray( 0 );
		glBindBuffer( GL_ARRAY_BUFFER, rbLowRes.vbo );
		vertexCache.InvalidateBindings();
		glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 0, NULL );
		glDrawArrays( GL_TRIANGLES, 0, 3 );
		for ( a = 0; a < 6; a++ ) {
			if ( a == 0 && !enabled[a] ) {
				glDisableVertexAttribArray( a );
			} else if ( a > 0 && enabled[a] ) {
				glEnableVertexAttribArray( a );
			}
		}
		glUseProgram( 0 );
		backEnd.glState.currentProgram = NULL;
	}
	glBindTexture( GL_TEXTURE_2D, 0 );
	tmu->current2DMap = -1;
}

/* the program environment the run's draws read, at the current scale */
static void RB_LowResEnvironment( void ) {
#ifdef BOOM3_ARB2_RENDERER
	if ( !R_GLSLRenderer() ) {
		RB_SetProgramEnvironment( false );
	}
#endif
}

int RB_LowResRun( drawSurf_t **drawSurfs, int first, int numDrawSurfs, void (*draw)( const drawSurf_t *surf ) ) {
	const viewDef_t *view = backEnd.viewDef;
	const float *proj = view->projectionMatrix;
	GLint stencil[7];
	GLboolean stencilTest;
	GLuint depthTex, sceneFbo;
	float parm[4];
	int end, i, vw, vh, lw, lh, dw = 0, dh = 0;
	int x1, y1, x2, y2, lx1, ly1, lx2, ly2;

	if ( !r_particleHalfRes || rbLowRes.failed || !RB_LowResEligible( drawSurfs[first] ) ) {
		return 0;
	}
	if ( view->viewEntitys == NULL || view->isSubview || view->renderView.viewID < 0
		|| view->viewport.x1 != 0 || view->viewport.y1 != 0
		|| R_VulkanRenderer() || glCaps.es || glCaps.major < 3 || VR_Active() ) {
		return 0;
	}
	vw = view->viewport.x2 + 1;
	vh = view->viewport.y2 + 1;
	depthTex = GLimp_ViewDepthTexture( &dw, &dh );
	if ( depthTex == 0 || dw < vw || dh < vh ) {
		return 0;
	}
	lw = ( vw + 1 ) >> 1;
	lh = ( vh + 1 ) >> 1;
	if ( !RB_LowResPrograms() || !RB_LowResTargets( lw, lh ) ) {
		return 0;
	}

	// the run, and the part of the view it can draw on
	x1 = vw;
	y1 = vh;
	x2 = y2 = -1;
	for ( end = first; end < numDrawSurfs && RB_LowResEligible( drawSurfs[end] ); end++ ) {
		const idScreenRect &r = r_useScissor.GetBool() ? drawSurfs[end]->scissorRect : view->scissor;
		x1 = Min( x1, (int)r.x1 );
		y1 = Min( y1, (int)r.y1 );
		x2 = Max( x2, (int)r.x2 );
		y2 = Max( y2, (int)r.y2 );
	}
	x1 = Max( x1, Max( 0, (int)view->scissor.x1 ) );
	y1 = Max( y1, Max( 0, (int)view->scissor.y1 ) );
	x2 = Min( x2, Min( vw - 1, (int)view->scissor.x2 ) );
	y2 = Min( y2, Min( vh - 1, (int)view->scissor.y2 ) );
	if ( x1 > x2 || y1 > y2 ) {
		return end - first;		// every surface is scissored away
	}
	// a pixel's composite reads the 2x2 of half resolution pixels around it
	lx1 = Max( 0, ( x1 >> 1 ) - 1 );
	ly1 = Max( 0, ( y1 >> 1 ) - 1 );
	lx2 = Min( lw - 1, ( x2 >> 1 ) + 1 );
	ly2 = Min( lh - 1, ( y2 >> 1 ) + 1 );

	if ( rbLowRes.numSurfs < end - first ) {
		Mem_Free( rbLowRes.surfs );
		rbLowRes.numSurfs = Max( 64, end - first );
		rbLowRes.surfs = (drawSurf_t *)Mem_Alloc( rbLowRes.numSurfs * sizeof( drawSurf_t ) );
	}

	sceneFbo = GLimp_ViewFramebuffer();
	stencilTest = glIsEnabled( GL_STENCIL_TEST );
	glGetIntegerv( GL_STENCIL_FUNC, &stencil[0] );
	glGetIntegerv( GL_STENCIL_REF, &stencil[1] );
	glGetIntegerv( GL_STENCIL_VALUE_MASK, &stencil[2] );
	glGetIntegerv( GL_STENCIL_FAIL, &stencil[3] );
	glGetIntegerv( GL_STENCIL_PASS_DEPTH_FAIL, &stencil[4] );
	glGetIntegerv( GL_STENCIL_PASS_DEPTH_PASS, &stencil[5] );
	glGetIntegerv( GL_STENCIL_WRITEMASK, &stencil[6] );

	// the farthest depth and the distance range of each 2x2, and the colour
	// cleared to nothing added over all of the scene
	glBindFramebuffer( GL_FRAMEBUFFER, rbLowRes.fboDown );
	glViewport( 0, 0, lw, lh );
	qglScissor( lx1, ly1, lx2 + 1 - lx1, ly2 + 1 - ly1 );
	qglDisable( GL_STENCIL_TEST );
	GL_Cull( CT_TWO_SIDED );
	GL_State( GLS_DEPTHFUNC_ALWAYS );
	parm[0] = 1.0f / dw;
	parm[1] = 1.0f / dh;
	parm[2] = proj[10];
	parm[3] = proj[14];
	RB_LowResPass( RB_LOWRES_DOWN, depthTex, parm );
	glBindFramebuffer( GL_FRAMEBUFFER, rbLowRes.fboDraw );
	qglClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	qglClear( GL_COLOR_BUFFER_BIT );

	// the run at half resolution
	for ( i = first; i < end; i++ ) {
		drawSurf_t *half = &rbLowRes.surfs[i - first];
		*half = *drawSurfs[i];
		half->scissorRect.x1 = drawSurfs[i]->scissorRect.x1 >> 1;
		half->scissorRect.y1 = drawSurfs[i]->scissorRect.y1 >> 1;
		half->scissorRect.x2 = drawSurfs[i]->scissorRect.x2 >> 1;
		half->scissorRect.y2 = drawSurfs[i]->scissorRect.y2 >> 1;
	}
	rbLowRes.active = true;
	backEnd.glState.lowResBlend = true;
	backEnd.glState.forceGlState = true;
	RB_LowResEnvironment();
	backEnd.currentScissor.Clear();
	qglScissor( lx1, ly1, lx2 + 1 - lx1, ly2 + 1 - ly1 );
	for ( i = 0; i < end - first; i++ ) {
		draw( &rbLowRes.surfs[i] );
	}
	rbLowRes.active = false;
	backEnd.glState.lowResBlend = false;
	backEnd.glState.forceGlState = true;
	RB_LowResEnvironment();

	// back on the scene: mark where the 2x2s break, put the rest on
	glBindFramebuffer( GL_FRAMEBUFFER, sceneFbo );
	qglViewport( tr.viewportOffset[0] + view->viewport.x1, tr.viewportOffset[1] + view->viewport.y1,
		vw - view->viewport.x1, vh - view->viewport.y1 );
	qglScissor( x1, y1, x2 + 1 - x1, y2 + 1 - y1 );
	rbLowRes.mark = rbLowRes.mark % 64 + 1;
	qglEnable( GL_STENCIL_TEST );
	qglStencilMask( 255 );
	qglStencilFunc( GL_ALWAYS, rbLowRes.mark, 255 );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );
	GL_Cull( CT_TWO_SIDED );
	GL_State( GLS_DEPTHMASK | GLS_DEPTHFUNC_ALWAYS | GLS_COLORMASK | GLS_ALPHAMASK );
	parm[0] = 1.0f / lw;
	parm[1] = 1.0f / lh;
	parm[2] = RB_LOWRES_EDGE;
	parm[3] = 0.0f;
	RB_LowResPass( RB_LOWRES_MARK, rbLowRes.range, parm );
	qglStencilFunc( GL_NOTEQUAL, rbLowRes.mark, 255 );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_SRC_ALPHA | GLS_DEPTHMASK | GLS_DEPTHFUNC_ALWAYS | GLS_ALPHAMASK );
	parm[0] = 0.5f / lw;
	parm[1] = 0.5f / lh;
	parm[2] = parm[3] = 0.0f;
	RB_LowResPass( RB_LOWRES_COMPOSITE, rbLowRes.accum, parm );

	// the marked pixels at full resolution; the marks were written into
	// the depth texture soft particles read, so their barrier is due again
	qglStencilFunc( GL_EQUAL, rbLowRes.mark, 255 );
	rbDepthBarrierDone = false;
	backEnd.currentScissor.Clear();
	rbLowRes.redraw = true;
	for ( i = first; i < end; i++ ) {
		draw( drawSurfs[i] );
	}
	rbLowRes.redraw = false;

	if ( stencilTest ) {
		qglEnable( GL_STENCIL_TEST );
	} else {
		qglDisable( GL_STENCIL_TEST );
	}
	qglStencilFunc( stencil[0], stencil[1], stencil[2] );
	qglStencilOp( stencil[3], stencil[4], stencil[5] );
	qglStencilMask( stencil[6] );
	backEnd.currentScissor.Clear();
	return end - first;
}

#else

int RB_LowResRun( drawSurf_t **drawSurfs, int first, int numDrawSurfs, void (*draw)( const drawSurf_t *surf ) ) {
	return 0;
}

float RB_LowResScale( void ) {
	return 1.0f;
}

bool RB_LowResRedraw( void ) {
	return false;
}

void RB_LowResForget( void ) {
}

bool RB_LowResPrograms( void ) {
	return false;
}

#endif

#ifdef BOOM3_ARB2_RENDERER

/*
================
RB_PrepareStageTexturing
================
*/
void RB_PrepareStageTexturing( const shaderStage_t *pStage,  const drawSurf_t *surf, idDrawVert *ac ) {
	// set privatePolygonOffset if necessary
	if ( pStage->privatePolygonOffset ) {
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset );
	}

	// set the texture matrix if needed
	if ( pStage->texture.hasMatrix ) {
		RB_LoadShaderTextureMatrix( surf->shaderRegisters, &pStage->texture );
	}

	// texgens
	if ( pStage->texture.texgen == TG_DIFFUSE_CUBE ) {
		qglTexCoordPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, normal ) );
	}
	if ( pStage->texture.texgen == TG_SKYBOX_CUBE || pStage->texture.texgen == TG_WOBBLESKY_CUBE ) {
		qglTexCoordPointer( 3, GL_FLOAT, 0, vertexCache.Position( surf->dynamicTexCoords ) );
	}
	if ( pStage->texture.texgen == TG_SCREEN ) {
		qglEnable( GL_TEXTURE_GEN_S );
		qglEnable( GL_TEXTURE_GEN_T );
		qglEnable( GL_TEXTURE_GEN_Q );

		float	mat[16], plane[4];
		myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat );

		plane[0] = mat[0];
		plane[1] = mat[4];
		plane[2] = mat[8];
		plane[3] = mat[12];
		qglTexGenfv( GL_S, GL_OBJECT_PLANE, plane );

		plane[0] = mat[1];
		plane[1] = mat[5];
		plane[2] = mat[9];
		plane[3] = mat[13];
		qglTexGenfv( GL_T, GL_OBJECT_PLANE, plane );

		plane[0] = mat[3];
		plane[1] = mat[7];
		plane[2] = mat[11];
		plane[3] = mat[15];
		qglTexGenfv( GL_Q, GL_OBJECT_PLANE, plane );
	}

	if ( pStage->texture.texgen == TG_SCREEN2 ) {
		qglEnable( GL_TEXTURE_GEN_S );
		qglEnable( GL_TEXTURE_GEN_T );
		qglEnable( GL_TEXTURE_GEN_Q );

		float	mat[16], plane[4];
		myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat );

		plane[0] = mat[0];
		plane[1] = mat[4];
		plane[2] = mat[8];
		plane[3] = mat[12];
		qglTexGenfv( GL_S, GL_OBJECT_PLANE, plane );

		plane[0] = mat[1];
		plane[1] = mat[5];
		plane[2] = mat[9];
		plane[3] = mat[13];
		qglTexGenfv( GL_T, GL_OBJECT_PLANE, plane );

		plane[0] = mat[3];
		plane[1] = mat[7];
		plane[2] = mat[11];
		plane[3] = mat[15];
		qglTexGenfv( GL_Q, GL_OBJECT_PLANE, plane );
	}

	if ( pStage->texture.texgen == TG_GLASSWARP ) {
		if ( tr.backEndRenderer == BE_ARB2 /*|| tr.backEndRenderer == BE_NV30*/ ) {
			qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_GLASSWARP );
			qglEnable( GL_FRAGMENT_PROGRAM_ARB );

			GL_SelectTexture( 2 );
			globalImages->scratchImage->Bind();

			GL_SelectTexture( 1 );
			globalImages->scratchImage2->Bind();

			qglEnable( GL_TEXTURE_GEN_S );
			qglEnable( GL_TEXTURE_GEN_T );
			qglEnable( GL_TEXTURE_GEN_Q );

			float	mat[16], plane[4];
			myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat );

			plane[0] = mat[0];
			plane[1] = mat[4];
			plane[2] = mat[8];
			plane[3] = mat[12];
			qglTexGenfv( GL_S, GL_OBJECT_PLANE, plane );

			plane[0] = mat[1];
			plane[1] = mat[5];
			plane[2] = mat[9];
			plane[3] = mat[13];
			qglTexGenfv( GL_T, GL_OBJECT_PLANE, plane );

			plane[0] = mat[3];
			plane[1] = mat[7];
			plane[2] = mat[11];
			plane[3] = mat[15];
			qglTexGenfv( GL_Q, GL_OBJECT_PLANE, plane );

			GL_SelectTexture( 0 );
		}
	}

	if ( pStage->texture.texgen == TG_REFLECT_CUBE ) {
		if ( tr.backEndRenderer == BE_ARB2 ) {
			// see if there is also a bump map specified
			const shaderStage_t *bumpStage = surf->material->GetBumpStage();
			if ( bumpStage ) {
				// per-pixel reflection mapping with bump mapping
				GL_SelectTexture( 1 );
				bumpStage->texture.image->Bind();
				GL_SelectTexture( 0 );

				qglNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, normal ) );
				qglVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, tangents[1] ) );
				qglVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, tangents[0] ) );

				qglEnableVertexAttribArrayARB( 9 );
				qglEnableVertexAttribArrayARB( 10 );
				qglEnableClientState( GL_NORMAL_ARRAY );

				// Program env 5, 6, 7, 8 have been set in RB_SetProgramEnvironmentSpace

				qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_BUMPY_ENVIRONMENT );
				qglEnable( GL_FRAGMENT_PROGRAM_ARB );
				qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_BUMPY_ENVIRONMENT );
				qglEnable( GL_VERTEX_PROGRAM_ARB );
			} else {
				// per-pixel reflection mapping without a normal map
				qglNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, normal ) );
				qglEnableClientState( GL_NORMAL_ARRAY );

				qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_ENVIRONMENT );
				qglEnable( GL_FRAGMENT_PROGRAM_ARB );
				qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_ENVIRONMENT );
				qglEnable( GL_VERTEX_PROGRAM_ARB );
			}
		} else {
			qglEnable( GL_TEXTURE_GEN_S );
			qglEnable( GL_TEXTURE_GEN_T );
			qglEnable( GL_TEXTURE_GEN_R );
			qglTexGenf( GL_S, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_EXT );
			qglTexGenf( GL_T, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_EXT );
			qglTexGenf( GL_R, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_EXT );
			qglEnableClientState( GL_NORMAL_ARRAY );
			qglNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, normal ) );

			qglMatrixMode( GL_TEXTURE );
			float	mat[16];

			R_TransposeGLMatrix( backEnd.viewDef->worldSpace.modelViewMatrix, mat );

			qglLoadMatrixf( mat );
			qglMatrixMode( GL_MODELVIEW );
		}
	}
}

/*
================
RB_FinishStageTexturing
================
*/
void RB_FinishStageTexturing( const shaderStage_t *pStage, const drawSurf_t *surf, idDrawVert *ac ) {
	// unset privatePolygonOffset if necessary
	if ( pStage->privatePolygonOffset && !surf->material->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}

	if ( pStage->texture.texgen == TG_DIFFUSE_CUBE || pStage->texture.texgen == TG_SKYBOX_CUBE
		|| pStage->texture.texgen == TG_WOBBLESKY_CUBE ) {
		qglTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, st ) );
	}

	if ( pStage->texture.texgen == TG_SCREEN ) {
		qglDisable( GL_TEXTURE_GEN_S );
		qglDisable( GL_TEXTURE_GEN_T );
		qglDisable( GL_TEXTURE_GEN_Q );
	}
	if ( pStage->texture.texgen == TG_SCREEN2 ) {
		qglDisable( GL_TEXTURE_GEN_S );
		qglDisable( GL_TEXTURE_GEN_T );
		qglDisable( GL_TEXTURE_GEN_Q );
	}

	if ( pStage->texture.texgen == TG_GLASSWARP ) {
		if ( tr.backEndRenderer == BE_ARB2 /*|| tr.backEndRenderer == BE_NV30*/ ) {
			GL_SelectTexture( 2 );
			globalImages->BindNull();

			GL_SelectTexture( 1 );
			if ( pStage->texture.hasMatrix ) {
				RB_LoadShaderTextureMatrix( surf->shaderRegisters, &pStage->texture );
			}
			qglDisable( GL_TEXTURE_GEN_S );
			qglDisable( GL_TEXTURE_GEN_T );
			qglDisable( GL_TEXTURE_GEN_Q );
			qglDisable( GL_FRAGMENT_PROGRAM_ARB );
			globalImages->BindNull();
			GL_SelectTexture( 0 );
		}
	}

	if ( pStage->texture.texgen == TG_REFLECT_CUBE ) {
		if ( tr.backEndRenderer == BE_ARB2 ) {
			// see if there is also a bump map specified
			const shaderStage_t *bumpStage = surf->material->GetBumpStage();
			if ( bumpStage ) {
				// per-pixel reflection mapping with bump mapping
				GL_SelectTexture( 1 );
				globalImages->BindNull();
				GL_SelectTexture( 0 );

				qglDisableVertexAttribArrayARB( 9 );
				qglDisableVertexAttribArrayARB( 10 );
			} else {
				// per-pixel reflection mapping without bump mapping
			}

			qglDisableClientState( GL_NORMAL_ARRAY );
			qglDisable( GL_FRAGMENT_PROGRAM_ARB );
			qglDisable( GL_VERTEX_PROGRAM_ARB );
			// Fixme: Hack to get around an apparent bug in ATI drivers.  Should remove as soon as it gets fixed.
			qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 ); // FIXME ...
		} else {
			qglDisable( GL_TEXTURE_GEN_S );
			qglDisable( GL_TEXTURE_GEN_T );
			qglDisable( GL_TEXTURE_GEN_R );
			qglTexGenf( GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
			qglTexGenf( GL_T, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
			qglTexGenf( GL_R, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
			qglDisableClientState( GL_NORMAL_ARRAY );

			qglMatrixMode( GL_TEXTURE );
			qglLoadIdentity();
			qglMatrixMode( GL_MODELVIEW );
		}
	}

	if ( pStage->texture.hasMatrix ) {
		qglMatrixMode( GL_TEXTURE );
		qglLoadIdentity();
		qglMatrixMode( GL_MODELVIEW );
	}
}

/*
=============================================================================================

FILL DEPTH BUFFER

=============================================================================================
*/


/*
==================
RB_T_FillDepthBuffer
==================
*/
void RB_T_FillDepthBuffer( const drawSurf_t *surf ) {
	int			stage;
	const idMaterial	*shader;
	const shaderStage_t *pStage;
	const float	*regs;
	float		color[4];
	const srfTriangles_t	*tri;

	tri = surf->geo;
	shader = surf->material;

	// update the clip plane if needed
	if ( backEnd.viewDef->numClipPlanes && surf->space != backEnd.currentSpace ) {
		GL_SelectTexture( 1 );

		idPlane	plane;

		R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.viewDef->clipPlanes[0], plane );
		plane[3] += 0.5;	// the notch is in the middle
		qglTexGenfv( GL_S, GL_OBJECT_PLANE, plane.ToFloatPtr() );
		GL_SelectTexture( 0 );
	}

	if ( !shader->IsDrawn() ) {
		return;
	}

	// some deforms may disable themselves by setting numIndexes = 0
	if ( !tri->numIndexes ) {
		return;
	}

	// translucent surfaces don't put anything in the depth buffer and don't
	// test against it, which makes them fail the mirror clip plane operation
	if ( shader->Coverage() == MC_TRANSLUCENT ) {
		return;
	}

	if ( !tri->ambientCache ) {
		common->Printf( "RB_T_FillDepthBuffer: !tri->ambientCache\n" );
		return;
	}

	// get the expressions for conditionals / color / texcoords
	regs = surf->shaderRegisters;

	// if all stages of a material have been conditioned off, don't do anything
	for ( stage = 0; stage < shader->GetNumStages() ; stage++ ) {
		pStage = shader->GetStage(stage);
		// check the stage enable condition
		if ( regs[ pStage->conditionRegister ] != 0 ) {
			break;
		}
	}
	if ( stage == shader->GetNumStages() ) {
		return;
	}

	// set polygon offset if necessary
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	// subviews will just down-modulate the color buffer by overbright
	if ( shader->GetSort() == SS_SUBVIEW ) {
		GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS );
		color[0] =
		color[1] =
		color[2] = ( 1.0 / backEnd.overBright );
		color[3] = 1;
	} else {
		// others just draw black
		color[0] = 0;
		color[1] = 0;
		color[2] = 0;
		color[3] = 1;
	}

	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	qglVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, xyz ) );
	qglTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, st ) );

	bool drawSolid = false;

	if ( shader->Coverage() == MC_OPAQUE ) {
		drawSolid = true;
	}

	// we may have multiple alpha tested stages
	if ( shader->Coverage() == MC_PERFORATED ) {
		// if the only alpha tested stages are condition register omitted,
		// draw a normal opaque surface
		bool	didDraw = false;

		qglEnable( GL_ALPHA_TEST );
		// perforated surfaces may have multiple alpha tested stages
		for ( stage = 0; stage < shader->GetNumStages() ; stage++ ) {
			pStage = shader->GetStage(stage);

			if ( !pStage->hasAlphaTest ) {
				continue;
			}

			// check the stage enable condition
			if ( regs[ pStage->conditionRegister ] == 0 ) {
				continue;
			}

			// if we at least tried to draw an alpha tested stage,
			// we won't draw the opaque surface
			didDraw = true;

			// set the alpha modulate
			color[3] = regs[ pStage->color.registers[3] ];

			// skip the entire stage if alpha would be black
			if ( color[3] <= 0 ) {
				continue;
			}
			qglColor4fv( color );

			qglAlphaFunc( GL_GREATER, regs[ pStage->alphaTestRegister ] );

			// bind the texture
			pStage->texture.image->Bind();

			// set texture matrix and texGens
			RB_PrepareStageTexturing( pStage, surf, ac );

			// draw it
			RB_DrawElementsWithCounters( tri );

			RB_FinishStageTexturing( pStage, surf, ac );
		}
		qglDisable( GL_ALPHA_TEST );
		if ( !didDraw ) {
			drawSolid = true;
		}
	}

	// draw the entire surface solid
	if ( drawSolid ) {
		qglColor4fv( color );
		globalImages->whiteImage->Bind();

		// draw it
		RB_DrawElementsWithCounters( tri );
	}


	// reset polygon offset
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}

	// reset blending
	if ( shader->GetSort() == SS_SUBVIEW ) {
		GL_State( GLS_DEPTHFUNC_LESS );
	}

}


void RB_SetProgramEnvironment( bool isPostProcess ); // so RB_STD_FillDepthBuffer() can use it


/*
=====================
RB_STD_FillDepthBuffer

If we are rendering a subview with a near clip plane, use a second texture
to force the alpha test to fail when behind that clip plane
=====================
*/
void RB_STD_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	// if we are just doing 2D rendering, no need to fill the depth buffer
	if ( !backEnd.viewDef->viewEntitys ) {
		return;
	}

	// enable the second texture for mirror plane clipping if needed
	if ( backEnd.viewDef->numClipPlanes ) {
		GL_SelectTexture( 1 );
		globalImages->alphaNotchImage->Bind();
		qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
		qglEnable( GL_TEXTURE_GEN_S );
		qglTexCoord2f( 1, 0.5 );
	}

	// the first texture will be used for alpha tested surfaces
	GL_SelectTexture( 0 );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );

	// decal surfaces may enable polygon offset
	qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() );

	GL_State( GLS_DEPTHFUNC_LESS );

	// Enable stencil test if we are going to be using it for shadows.
	// If we didn't do this, it would be legal behavior to get z fighting
	// from the ambient pass and the light passes.
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );

	RB_RenderDrawSurfListWithFunction( drawSurfs, numDrawSurfs, RB_T_FillDepthBuffer );

	const bool inPlace = RB_DepthCaptureInPlace();

	// Make the early depth pass available to shaders. #3877
	bool getDepthCapture = r_enableDepthCapture.GetInteger() == 1
		|| (r_enableDepthCapture.GetInteger() == -1 && r_useSoftParticles.GetBool());

	if ( getDepthCapture && backEnd.viewDef->renderView.viewID >= 0 ) // Suppress for lightgem rendering passes
	{
		/* The copy is a full-viewport depth read every view; the pixels
		 * are copied only for a view that has something to read them.
		 * Otherwise only the image's size follows the view, so the
		 * program environment reports what it always reported.  A view
		 * whose soft particles read its depth in place needs neither. */
		if ( !inPlace ) {
			globalImages->currentDepthImage->CopyDepthbuffer( backEnd.viewDef->viewport.x1,
															  backEnd.viewDef->viewport.y1,
															  backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
															  backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1,
															  true, RB_DepthCaptureWanted() );
		}
		bool isPostProcess = false;
		RB_SetProgramEnvironment( isPostProcess );
	}

	if ( backEnd.viewDef->numClipPlanes ) {
		GL_SelectTexture( 1 );
		globalImages->BindNull();
		qglDisable( GL_TEXTURE_GEN_S );
		GL_SelectTexture( 0 );
	}

}

/*
=============================================================================================

SHADER PASSES

=============================================================================================
*/

/*
==================
RB_SetProgramEnvironment

Sets variables that can be used by all vertex programs

[SteveL #3877] Note on the use of fragment program environmental variables.
Parameters 0 and 1 are set here to allow conversion of screen coordinates to
texture coordinates, for use when sampling _currentRender.
Those same parameters 0 and 1, plus 2 and 3, are given entirely different
meanings in draw_arb2.cpp while light interactions are being drawn.
This function is called again before currentRender size is needed by post processing
effects are done, so there's no clash.
Only parameters 0..3 were in use before #3877 - and in dhewm3 also 4, for gamma in shader.
Now I've used a new parameter 22 for the size of _currentDepth. It's needed throughout,
including by light interactions, and its size might in theory differ from _currentRender.
Parameters 23 and 24 are used by soft particles #3878. Note these can be freely reused by different draw calls.
==================
*/
void RB_SetProgramEnvironment( bool isPostProcess ) {
	float	parm[4];
	int		pot;

	if ( !glConfig.ARBVertexProgramAvailable ) {
		return;
	}

	// screen power of two correction factor, assuming the copy to _currentRender
	// also copied an extra row and column for the bilerp
	int	 w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	pot = globalImages->currentRenderImage->uploadWidth;
	parm[0] = (float)w / pot;

	int	 h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	pot = globalImages->currentRenderImage->uploadHeight;
	parm[1] = (float)h / pot;

	parm[2] = 0;
	parm[3] = 1;
	qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 0, parm );

	qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, parm );

	// window coord to 0.0 to 1.0 conversion
	parm[0] = 1.0 / w;
	parm[1] = 1.0 / h;
	parm[2] = 0;
	parm[3] = 1;
	qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 1, parm );

	// DG: brightness and gamma in shader as program.env[4]
	if ( r_gammaInShader.GetBool() ) {
		// program.env[4].xyz are all r_brightness, program.env[4].w is 1.0/r_gamma
		if ( !isPostProcess ) {
			parm[0] = parm[1] = parm[2] = RB_HDRGammaBrightness( false );
			parm[3] = 1.0/r_gamma.GetFloat(); // 1.0/gamma so the shader doesn't have to do this calculation
		} else {
			// don't apply gamma/brightness in postprocess passes to avoid applying them twice
			// (setting them to 1.0 makes them no-ops)
			parm[0] = parm[1] = parm[2] = parm[3] = 1.0f;
		}
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, PP_GAMMA_BRIGHTNESS, parm );
	}

	// #3877: Allow shaders to access depth buffer. 
	// Two useful ratios are packed into this parm: [0] and [1] hold the x,y multipliers you need to map a screen 
	// coordinate (fragment position) to the depth image: those are simply the reciprocal of the depth 
	// image size, which has been rounded up to a power of two. Slots [3] and [4] hold the ratio of the depth image
	// size to the current render image size. These sizes can differ if the game crops the render viewport temporarily 
	// during post-processing effects. The depth render is smaller during the effect too, but the depth image doesn't 
	// need to be downsized, whereas the current render image does get downsized when it's captured by the game after 
	// the skybox render pass. The ratio is needed to map between the two render images.
	if ( !RB_DepthInPlace( parm ) ) {
		parm[0] = 1.0f / globalImages->currentDepthImage->uploadWidth;
		parm[1] = 1.0f / globalImages->currentDepthImage->uploadHeight;
	}
	parm[0] *= RB_LowResScale();
	parm[1] *= RB_LowResScale();
	parm[2] = static_cast<float>(globalImages->currentRenderImage->uploadWidth) / globalImages->currentDepthImage->uploadWidth;
	parm[3] = static_cast<float>(globalImages->currentRenderImage->uploadHeight) / globalImages->currentDepthImage->uploadHeight;
	qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, PP_CURDEPTH_RECIPR, parm );

	//
	// set eye position in global space
	//
	parm[0] = backEnd.viewDef->renderView.vieworg[0];
	parm[1] = backEnd.viewDef->renderView.vieworg[1];
	parm[2] = backEnd.viewDef->renderView.vieworg[2];
	parm[3] = 1.0;
	qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 1, parm );
}

/*
==================
RB_SetProgramEnvironmentSpace

Sets variables related to the current space that can be used by all vertex programs
==================
*/
void RB_SetProgramEnvironmentSpace( void ) {
	if ( !glConfig.ARBVertexProgramAvailable ) {
		return;
	}

	const struct viewEntity_s *space = backEnd.currentSpace;
	float	parm[4];

	// set eye position in local space
	R_GlobalPointToLocal( space->modelMatrix, backEnd.viewDef->renderView.vieworg, *(idVec3 *)parm );
	parm[3] = 1.0;
	qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 5, parm );

	// we need the model matrix without it being combined with the view matrix
	// so we can transform local vectors to global coordinates
	parm[0] = space->modelMatrix[0];
	parm[1] = space->modelMatrix[4];
	parm[2] = space->modelMatrix[8];
	parm[3] = space->modelMatrix[12];
	qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 6, parm );
	parm[0] = space->modelMatrix[1];
	parm[1] = space->modelMatrix[5];
	parm[2] = space->modelMatrix[9];
	parm[3] = space->modelMatrix[13];
	qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 7, parm );
	parm[0] = space->modelMatrix[2];
	parm[1] = space->modelMatrix[6];
	parm[2] = space->modelMatrix[10];
	parm[3] = space->modelMatrix[14];
	qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 8, parm );
}

/*
==================
RB_STD_T_RenderShaderPasses

This is also called for the generated 2D rendering
==================
*/
void RB_STD_T_RenderShaderPasses( const drawSurf_t *surf ) {
	int			stage;
	const idMaterial	*shader;
	const shaderStage_t *pStage;
	const float	*regs;
	float		color[4];
	const srfTriangles_t	*tri;

	tri = surf->geo;
	shader = surf->material;

	if ( !shader->HasAmbient() ) {
		return;
	}

	if ( shader->IsPortalSky() ) {
		return;
	}

	// change the matrix if needed
	if ( surf->space != backEnd.currentSpace ) {
		qglLoadMatrixf( surf->space->modelViewMatrix );
		backEnd.currentSpace = surf->space;
		RB_SetProgramEnvironmentSpace();
	}

	// change the scissor if needed
	if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
		backEnd.currentScissor = surf->scissorRect;
		qglScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}

	// some deforms may disable themselves by setting numIndexes = 0
	if ( !tri->numIndexes ) {
		return;
	}

	if ( !tri->ambientCache ) {
		common->Printf( "RB_T_RenderShaderPasses: !tri->ambientCache\n" );
		return;
	}

	// check whether we're drawing a soft particle surface #3878
	const bool soft_particle = ( surf->dsFlags & DSF_SOFT_PARTICLE ) != 0;
	
	// get the expressions for conditionals / color / texcoords
	regs = surf->shaderRegisters;

	// set face culling appropriately
	GL_Cull( shader->GetCullType() );

	// set polygon offset if necessary
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	if ( surf->space->weaponDepthHack ) {
		RB_EnterWeaponDepthHack();
	}

	if ( surf->space->modelDepthHack != 0.0f && !soft_particle ) // #3878 soft particles don't want modelDepthHack, which is
	{                                                            // an older way to slightly "soften" particles
		RB_EnterModelDepthHack( surf->space->modelDepthHack );
	}

	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	qglVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, xyz ) );
	qglTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, st ) );

	for ( stage = 0; stage < shader->GetNumStages() ; stage++ ) {
		pStage = shader->GetStage(stage);

		// check the enable condition
		if ( regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}

		// skip the stages involved in lighting
		if ( pStage->lighting != SL_AMBIENT ) {
			continue;
		}

		// skip if the stage is ( GL_ZERO, GL_ONE ), which is used for some alpha masks
		if ( ( pStage->drawStateBits & (GLS_SRCBLEND_BITS|GLS_DSTBLEND_BITS) ) == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) ) {
			continue;
		}

		// determine the blend mode (used by soft particles #3878)
		const int src_blend = pStage->drawStateBits & GLS_SRCBLEND_BITS;

		// see if we are a new-style stage
		newShaderStage_t *newStage = pStage->newStage;
		if ( newStage ) {
			//--------------------------
			//
			// new style stages
			//
			//--------------------------

			// completely skip the stage if we don't have the capability
			if ( tr.backEndRenderer != BE_ARB2 ) {
				continue;
			}
			if ( r_skipNewAmbient.GetBool() ) {
				continue;
			}
			qglColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DRAWVERT( ac, color ) );
			qglVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, tangents[0] ) );
			qglVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DRAWVERT( ac, tangents[1] ) );
			qglNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, normal ) );

			qglEnableClientState( GL_COLOR_ARRAY );
			qglEnableVertexAttribArrayARB( 9 );
			qglEnableVertexAttribArrayARB( 10 );
			qglEnableClientState( GL_NORMAL_ARRAY );

			GL_State( pStage->drawStateBits );

			qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, newStage->vertexProgram );
			qglEnable( GL_VERTEX_PROGRAM_ARB );

			// megaTextures bind a lot of images and set a lot of parameters
			if ( newStage->megaTexture ) {
				newStage->megaTexture->SetMappingForSurface( tri );
				idVec3	localViewer;
				R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewer );
				newStage->megaTexture->BindForViewOrigin( localViewer );
			}

			for ( int i = 0 ; i < newStage->numVertexParms ; i++ ) {
				float	parm[4];
				parm[0] = regs[ newStage->vertexParms[i][0] ];
				parm[1] = regs[ newStage->vertexParms[i][1] ];
				parm[2] = regs[ newStage->vertexParms[i][2] ];
				parm[3] = regs[ newStage->vertexParms[i][3] ];
				qglProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, i, parm );
			}

			for ( int i = 0 ; i < newStage->numFragmentProgramImages ; i++ ) {
				if ( newStage->fragmentProgramImages[i] ) {
					GL_SelectTexture( i );
					newStage->fragmentProgramImages[i]->Bind();
				}
			}
			qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, newStage->fragmentProgram );
			qglEnable( GL_FRAGMENT_PROGRAM_ARB );

			// the epilogue's brightness and gamma, blend-aware like the other stages
			if ( r_gammaInShader.GetBool() ) {
				float nparm[4];
				if ( surf->material->GetSort() >= SS_POST_PROCESS ) {
					/* a post-process stage reads the scene back, which
					   already carries the brightness and the encoding:
					   neither applies a second time */
					nparm[0] = nparm[1] = nparm[2] = nparm[3] = 1.0f;
				} else {
					nparm[0] = nparm[1] = nparm[2] = RB_HDRGammaBrightness( RB_FilterBlend( pStage->drawStateBits ) );
					nparm[3] = 1.0f / r_gamma.GetFloat();
				}
				qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, PP_GAMMA_BRIGHTNESS, nparm );
			}

			// draw it
			RB_DrawElementsWithCounters( tri );

			for ( int i = 1 ; i < newStage->numFragmentProgramImages ; i++ ) {
				if ( newStage->fragmentProgramImages[i] ) {
					GL_SelectTexture( i );
					globalImages->BindNull();
				}
			}
			if ( newStage->megaTexture ) {
				newStage->megaTexture->Unbind();
			}

			GL_SelectTexture( 0 );

			qglDisable( GL_VERTEX_PROGRAM_ARB );
			qglDisable( GL_FRAGMENT_PROGRAM_ARB );
			// Fixme: Hack to get around an apparent bug in ATI drivers.  Should remove as soon as it gets fixed.
			qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 ); // FIXME: ...

			qglDisableClientState( GL_COLOR_ARRAY );
			qglDisableVertexAttribArrayARB( 9 );
			qglDisableVertexAttribArrayARB( 10 );
			qglDisableClientState( GL_NORMAL_ARRAY );
			continue;
		}
		else if ( soft_particle 
				 && surf->particle_radius > 0.0f 
				 && ( src_blend == GLS_SRCBLEND_ONE || src_blend == GLS_SRCBLEND_SRC_ALPHA ) 
				 && tr.backEndRenderer == BE_ARB2
				 && !r_skipNewAmbient.GetBool() )
		{
			// SteveL #3878. Particles are automatically softened by the engine, unless they have shader programs of 
			// their own (i.e. are "newstages" handled above). This section comes after the newstage part so that if a
			// designer has specified their own shader programs, those will be used instead of the soft particle program.
			if ( pStage->vertexColor == SVC_IGNORE )
			{
				// Ignoring vertexColor is not recommended for particles. The particle system uses vertexColor for fading.
				// However, there are existing particle effects that don't use it, in which case we default to using the 
				// rgb color modulation specified in the material like the "old stages" do below. 
				color[0] = regs[pStage->color.registers[0]];
				color[1] = regs[pStage->color.registers[1]];
				color[2] = regs[pStage->color.registers[2]];
				color[3] = regs[pStage->color.registers[3]];
				/* The soft-particle path sets colour separately from the
				 * old-stage path below, so the emissive headroom has to
				 * be applied here too - a fire or a spark is additive
				 * light like any other, and having one of the two paths
				 * scale and the other not would make the same effect
				 * change brightness depending on which one drew it. */
				{
					extern bool  hdr_output_active;
					extern float hdr_emissive_gain;
					extern float hdr_particle_gain;
					const int blend = pStage->drawStateBits
							& ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
					/* A muzzle flash and a monitor are both additive
					 * stages, but they want gains an order apart: a
					 * screen reads as a light at 4x, while Filmic's
					 * crosstalk does not engage until about 78 in scene
					 * terms, which would make that screen a lamp.  The
					 * material's deform separates them - particles are
					 * transient energy, surfaces are not. */
					const deform_t dfm = surf->material->Deform();
					const float gain = ( dfm == DFRM_PARTICLE || dfm == DFRM_PARTICLE2 )
							? hdr_particle_gain : hdr_emissive_gain;
					if ( hdr_output_active && gain != 1.0f
							&& ( blend & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE ) {
						color[0] *= gain;
						color[1] *= gain;
						color[2] *= gain;
					}
				}
				qglColor4fv( color );
			}
			else
			{
				// A properly set-up particle shader
				qglColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DRAWVERT( ac, color ) );
				qglEnableClientState( GL_COLOR_ARRAY );
			}

			GL_State( pStage->drawStateBits | GLS_DEPTHFUNC_ALWAYS ); // Disable depth clipping. The fragment program will
																	  // handle it to allow overdraw.

			qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_SOFT_PARTICLE );
			qglEnable( GL_VERTEX_PROGRAM_ARB );

			// Bind image and _currentDepth
			GL_SelectTexture( 0 );
			pStage->texture.image->Bind();
			GL_SelectTexture( 1 );
			const bool depthInPlace = RB_DepthInPlace( NULL );
			if ( depthInPlace ) {
				RB_BindDepthInPlace();
			} else {
				globalImages->currentDepthImage->Bind();
			}

			qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, RB_LowResRedraw() ? FPROG_SOFT_PARTICLE_KEEP : FPROG_SOFT_PARTICLE );
			qglEnable( GL_FRAGMENT_PROGRAM_ARB );

			// DG: some particle materials (at least the muzzle flash in dentonmod) set the
			//     texture matrix (with scroll, translate, scale, centerScale, shear or rotate).
			//     Support that like in R_SetDrawInteraction() (the soft particle shader only
			//     uses one texture, so I set the diffuse matrix for it)
			idVec4 texMatrix[2];
			if ( pStage->texture.hasMatrix ) {
				texMatrix[0][0] = regs[pStage->texture.matrix[0][0]];
				texMatrix[0][1] = regs[pStage->texture.matrix[0][1]];
				texMatrix[0][2] = 0;
				texMatrix[0][3] = regs[pStage->texture.matrix[0][2]];

				texMatrix[1][0] = regs[pStage->texture.matrix[1][0]];
				texMatrix[1][1] = regs[pStage->texture.matrix[1][1]];
				texMatrix[1][2] = 0;
				texMatrix[1][3] = regs[pStage->texture.matrix[1][2]];

				// we attempt to keep scrolls from generating incredibly large texture values, but
				// center rotations and center scales can still generate offsets that need to be > 1
				if ( texMatrix[0][3] < -40 || texMatrix[0][3] > 40 ) {
					texMatrix[0][3] -= (int)texMatrix[0][3];
				}
				if ( texMatrix[1][3] < -40 || texMatrix[1][3] > 40 ) {
					texMatrix[1][3] -= (int)texMatrix[1][3];
				}
			} else {
				texMatrix[0].Set( 1, 0, 0, 0 );
				texMatrix[1].Set( 0, 1, 0, 0 );
			}
			qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_DIFFUSE_MATRIX_S, texMatrix[0].ToFloatPtr() );
			qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_DIFFUSE_MATRIX_T, texMatrix[1].ToFloatPtr() );

			// program.env[23] is the particle radius, given as { radius, 1/(faderange), 1/radius }
			float fadeRange = 1.0f;
			// fadeRange is the particle diameter for alpha blends (like smoke), but the particle radius for additive
			// blends (light glares), because additive effects work differently. Fog is half as apparent when a wall
			// is in the middle of it. Light glares lose no visibility when they have something to reflect off. See 
			// The Dark Mod issue #3878 for diagram
			if ( src_blend == GLS_SRCBLEND_SRC_ALPHA ) // an alpha blend material
			{
				fadeRange = surf->particle_radius * 2.0f;
			}
			else if ( src_blend == GLS_SRCBLEND_ONE ) // an additive (blend add) material
			{
				fadeRange = surf->particle_radius;
			}

			/* w: whether a fragment the fade takes to zero may be dropped */
			float parm[4] = {
				surf->particle_radius,
				1.0f / ( fadeRange ),
				1.0f / surf->particle_radius,
				RB_SOFT_FADE_DROPS( pStage->drawStateBits ) ? 1.0f : 0.0f
			};
			qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, PP_PARTICLE_RADIUS, parm );

			// program.env[24] is the color channel mask. It gets added to the fade multiplier, so adding 1
			//    to a channel will make sure it doesn't get faded at all. Particles with additive blend 
			//    need their RGB channels modifying to blend them out. Particles with an alpha blend need 
			//    their alpha channel modifying.
			if ( src_blend == GLS_SRCBLEND_SRC_ALPHA ) // an alpha blend material
			{
				parm[0] = parm[1] = parm[2] = 1.0f; // Leave the rgb channels at full strength when fading
				parm[3] = 0.0f;						// but fade the alpha channel
			}
			else if ( src_blend == GLS_SRCBLEND_ONE ) // an additive (blend add) material
			{
				parm[0] = parm[1] = parm[2] = 0.0f; // Fade the rgb channels but
				parm[3] = 1.0f;						// leave the alpha channel at full strength
			}
			qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, PP_PARTICLE_COLCHAN_MASK, parm );

			/*
			   program.env[25] is the HDR scene encoding fold. This
			   program carries "nodhewm3gammahack" so R_LoadARBProgram
			   skips the gamma epilogue for it - and the epilogue is
			   where every other stage picks the fold up. Without this
			   the particle writes at full scale into a scene stored at
			   hdr_scene_encode_scale and hdr_present's 1/s expand
			   returns it at 2x: additive gives scene + 2*src, alpha
			   gives 2a*src + (1-a)*scene. r_useSoftParticles and
			   doom_hdr_headroom both default on, so smoke, fire, steam
			   and glare sprites were blown out in the shipped 30-bit
			   configuration. 1.0 outside 30-bit HDR, so no change there.
			*/
			{
				extern float hdr_scene_encode_scale;
				const float s = hdr_scene_encode_scale;
				float fold[4] = { s, s, s, 1.0f };
				qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, PP_HDR_SCENE_FOLD, fold );
			}
			
			// draw it
			const bool depthBounds = RB_SoftDepthBounds( surf, pStage->drawStateBits );
			RB_DrawElementsWithCounters( tri );
			if ( depthBounds ) {
				RB_SoftDepthBoundsEnd();
			}

			// Clean up GL state
			GL_SelectTexture( 1 );
			if ( depthInPlace ) {
				RB_UnbindDepthInPlace();
			}
			globalImages->BindNull();
			GL_SelectTexture( 0 );
			globalImages->BindNull();
			
			qglDisable( GL_VERTEX_PROGRAM_ARB );
			qglDisable( GL_FRAGMENT_PROGRAM_ARB );

			if ( pStage->vertexColor != SVC_IGNORE ) {
				qglDisableClientState( GL_COLOR_ARRAY );
			}
			continue;
		}

		//--------------------------
		//
		// old style stages
		//
		//--------------------------

		// set the color
		color[0] = regs[ pStage->color.registers[0] ];
		color[1] = regs[ pStage->color.registers[1] ];
		color[2] = regs[ pStage->color.registers[2] ];
		color[3] = regs[ pStage->color.registers[3] ];

		// skip the entire stage if an add would be black
		if ( ( pStage->drawStateBits & (GLS_SRCBLEND_BITS|GLS_DSTBLEND_BITS) ) == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE )
			&& color[0] <= 0 && color[1] <= 0 && color[2] <= 0 ) {
			continue;
		}

		// skip the entire stage if a blend would be completely transparent
		if ( ( pStage->drawStateBits & (GLS_SRCBLEND_BITS|GLS_DSTBLEND_BITS) ) == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
			&& color[3] <= 0 ) {
			continue;
		}

		/* Emissive headroom.
		 *
		 * An additive stage is light the surface emits rather than light
		 * it reflects: the glow layers on monitors, keypads and light
		 * panels.  Its texture white is 1.0, the same value a fully lit
		 * diffuse wall reaches, so it lands on paper white and never
		 * enters the headroom - a screen in a dark corridor comes out no
		 * brighter than the corridor's lit surfaces.  Measured through
		 * ACES 2.0 at a 1000 nit peak, 1.0 is 107 nits and 4.0 is 355.
		 *
		 * Only additive stages are touched.  A diffuse stage scaled this
		 * way would just be a brightness control, and the alpha is left
		 * alone because it is coverage, not light. */
		{
			extern bool  hdr_output_active;
			extern float hdr_emissive_gain;
			extern float hdr_particle_gain;
			const int blend = pStage->drawStateBits
					& ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
			/* particles take their own gain - see the note at the
			 * soft-particle site above */
			const deform_t dfm = surf->material->Deform();
			const float gain = ( dfm == DFRM_PARTICLE || dfm == DFRM_PARTICLE2 )
					? hdr_particle_gain : hdr_emissive_gain;

			if ( hdr_output_active && gain != 1.0f
					&& ( blend & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE ) {
				color[0] *= gain;
				color[1] *= gain;
				color[2] *= gain;
			}
		}

		// select the vertex color source
		if ( pStage->vertexColor == SVC_IGNORE ) {
			qglColor4fv( color );
		} else {
			qglColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DRAWVERT( ac, color ) );
			qglEnableClientState( GL_COLOR_ARRAY );

			if ( pStage->vertexColor == SVC_INVERSE_MODULATE ) {
				GL_TexEnv( GL_COMBINE_ARB );
				qglTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE );
				qglTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE );
				qglTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PRIMARY_COLOR_ARB );
				qglTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR );
				qglTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_ONE_MINUS_SRC_COLOR );
				qglTexEnvi( GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1 );
			}

			// for vertex color and modulated color, we need to enable a second
			// texture stage
			if ( color[0] != 1 || color[1] != 1 || color[2] != 1 || color[3] != 1 ) {
				GL_SelectTexture( 1 );

				globalImages->whiteImage->Bind();
				GL_TexEnv( GL_COMBINE_ARB );

				qglTexEnvfv( GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color );

				qglTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE );
				qglTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB );
				qglTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_CONSTANT_ARB );
				qglTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR );
				qglTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR );
				qglTexEnvi( GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1 );

				qglTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE );
				qglTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB );
				qglTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_CONSTANT_ARB );
				qglTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA );
				qglTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_ALPHA_ARB, GL_SRC_ALPHA );
				qglTexEnvi( GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1 );

				GL_SelectTexture( 0 );
			}
		}

		// bind the texture
		RB_BindVariableStageImage( &pStage->texture, regs );

		// set the state
		GL_State( pStage->drawStateBits );

		RB_PrepareStageTexturing( pStage, surf, ac );

		/*
		   Route this fixed-function stage through the replication
		   program so the gamma-in-shader epilogue covers it - the
		   prerequisite for any uniform output-encoding scale, and it
		   makes r_gamma/r_brightness finally apply to GUIs and 2D.
		   Stages whose texturing setup binds its own programs
		   (mirrored/warped surfaces) are left alone; cube-sampled
		   stages take the CUBE variant. The texenv combiners set
		   above become inert under a fragment program, which is fine:
		   the program reproduces their math, and at neutral
		   gamma/brightness the result is bit-identical.
		*/
		bool gammaProg = r_gammaInShader.GetBool()
			&& glConfig.ARBFragmentProgramAvailable;
		bool ownProg = pStage->texture.texgen == TG_REFLECT_CUBE
			|| pStage->texture.texgen == TG_GLASSWARP;
		bool ffProg = gammaProg && !ownProg;

		/*
		   The two texgens above bound their own programs in
		   RB_PrepareStageTexturing (FPROG_ENVIRONMENT,
		   FPROG_GLASSWARP). Those come from glprogs/ and so DO carry
		   the injected epilogue - but nothing here used to set
		   env[21] for them, so they consumed whatever the last
		   interaction or shader stage left behind. Under the
		   blend-aware routing that is either r_brightness or
		   r_brightness folded, depending on draw order: the same bug
		   class the newStage comment above describes fixing, with the
		   ffProg == false branch left open.

		   Glass warp is an SS_POST_PROCESS replace of _currentRender,
		   which already carries the scene's encoding, so it ships
		   unscaled. Reflect-cube is ordinary scene energy and takes
		   the fold.
		*/
		if ( gammaProg && ownProg ) {
			float oparm[4];
			oparm[0] = oparm[1] = oparm[2] = RB_HDRGammaBrightness(
					pStage->texture.texgen == TG_GLASSWARP );
			oparm[3] = 1.0f / r_gamma.GetFloat();
			qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, PP_GAMMA_BRIGHTNESS, oparm );
		}

		if ( ffProg ) {
			bool cube = pStage->texture.image && pStage->texture.image->type == TT_CUBIC;
			/* An in-world GUI is authored in display space, so the
			 * output transform moves it: under ACES 2.0 an authored
			 * 0.10 arrives at 0.064 and the text goes muddy.  Pushing
			 * the texel through the transform's inverse first means the
			 * forward transform puts it back exactly.
			 *
			 * It has to happen here, on the texel, rather than on the
			 * stage colour - the stage colour is the material's
			 * constant modulation and is white for almost every GUI,
			 * so correcting it would darken the screen and fix nothing.
			 *
			 * A separate program, bound only for SS_GUI, so no other
			 * stage pays for the three extra fetches. */
			extern bool HDR_GuiCorrectionActive( unsigned int *tex );
			unsigned int guiLut = 0;
			const bool guiFix = !cube
					&& surf->material->GetSort() == SS_GUI
					&& HDR_GuiCorrectionActive( &guiLut );
			if ( guiFix ) {
				GL_SelectTexture( 1 );
				qglBindTexture( GL_TEXTURE_2D, guiLut );
				GL_SelectTexture( 0 );
			}
			qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB,
					guiFix ? FPROG_FF_GAMMA_GUI
					: cube ? FPROG_FF_GAMMA_CUBE : FPROG_FF_GAMMA );
			qglEnable( GL_FRAGMENT_PROGRAM_ARB );
			float cc[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			float inv[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if ( pStage->vertexColor != SVC_IGNORE ) {
				// fragment.color carries the vertex colors; the
				// register-evaluated constant modulates on top
				cc[0] = color[0]; cc[1] = color[1]; cc[2] = color[2]; cc[3] = color[3];
				if ( pStage->vertexColor == SVC_INVERSE_MODULATE ) {
					inv[0] = 1.0f;
				}
			}
			qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, cc );
			qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 1, inv );
			// brightness/gamma for the injected epilogue
			float parm[4];
			/*
			   The encoding fold must NOT apply to filter blends. A
			   filter stage multiplies against the destination, and the
			   destination already carries the scene's encoding scale -
			   scaling the source as well squares the fold and the
			   surface goes dark by exactly that factor. The source of
			   a filter is a pure per-channel factor, not scene energy;
			   it ships unscaled, the destination keeps the scale, and
			   the product stays uniformly encoded. Additive and alpha
			   blends scale normally (alpha itself is never scaled, so
			   lerp weights are unaffected).
			*/
			int srcB = pStage->drawStateBits & GLS_SRCBLEND_BITS;
			int dstB = pStage->drawStateBits & GLS_DSTBLEND_BITS;
			bool filterBlend =
				srcB == GLS_SRCBLEND_DST_COLOR ||
				srcB == GLS_SRCBLEND_ONE_MINUS_DST_COLOR ||
				dstB == GLS_DSTBLEND_SRC_COLOR ||
				dstB == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR;
			parm[0] = parm[1] = parm[2] = RB_HDRGammaBrightness( filterBlend );
			parm[3] = 1.0f / r_gamma.GetFloat();
			qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, PP_GAMMA_BRIGHTNESS, parm );
		}

		// draw it
		RB_DrawElementsWithCounters( tri );

		if ( ffProg ) {
			qglDisable( GL_FRAGMENT_PROGRAM_ARB );
		}

		RB_FinishStageTexturing( pStage, surf, ac );

		if ( pStage->vertexColor != SVC_IGNORE ) {
			qglDisableClientState( GL_COLOR_ARRAY );

			GL_SelectTexture( 1 );
			GL_TexEnv( GL_MODULATE );
			globalImages->BindNull();
			GL_SelectTexture( 0 );
			GL_TexEnv( GL_MODULATE );
		}
	}

	// reset polygon offset
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
	if ( surf->space->weaponDepthHack || ( !soft_particle && surf->space->modelDepthHack != 0.0f ) ) // #3878 soft particles
	{
		RB_LeaveDepthHack();
	}
}

/*
=====================
RB_STD_DrawShaderPasses

Draw non-light dependent passes
=====================
*/
int RB_STD_DrawShaderPasses( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int				i;

	// only obey skipAmbient if we are rendering a view
	if ( backEnd.viewDef->viewEntitys && r_skipAmbient.GetBool() ) {
		return numDrawSurfs;
	}

	bool isPostProcess = false;

	// if we are about to draw the first surface that needs
	// the rendering in a texture, copy it over
	if ( drawSurfs[0]->material->GetSort() >= SS_POST_PROCESS ) {
		if ( r_skipPostProcess.GetBool() ) {
			return 0;
		}
		isPostProcess = true;

		// only dump if in a 3d view
		if ( backEnd.viewDef->viewEntitys && tr.backEndRenderer == BE_ARB2 ) {
			// only the part the post-process surfaces can read, where that is known
			int rect[4];
			const bool part = RB_CurrentRenderCopyRect( drawSurfs, numDrawSurfs, rect );
			globalImages->currentRenderImage->CopyFramebuffer( backEnd.viewDef->viewport.x1,
				backEnd.viewDef->viewport.y1,  backEnd.viewDef->viewport.x2 -  backEnd.viewDef->viewport.x1 + 1,
				backEnd.viewDef->viewport.y2 -  backEnd.viewDef->viewport.y1 + 1, true, part ? rect : NULL );
		}
		backEnd.currentRenderCopied = true;
	}

	GL_SelectTexture( 1 );
	globalImages->BindNull();

	GL_SelectTexture( 0 );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );

	RB_SetProgramEnvironment( isPostProcess );

	// we don't use RB_RenderDrawSurfListWithFunction()
	// because we want to defer the matrix load because many
	// surfaces won't draw any ambient passes
	backEnd.currentSpace = NULL;
	for (i = 0  ; i < numDrawSurfs ; i++ ) {
		if ( drawSurfs[i]->material->SuppressInSubview() ) {
			continue;
		}

		if ( backEnd.viewDef->isXraySubview && drawSurfs[i]->space->entityDef ) {
			if ( drawSurfs[i]->space->entityDef->parms.xrayIndex != 2 ) {
				continue;
			}
		}

		// we need to draw the post process shaders after we have drawn the fog lights
		if ( drawSurfs[i]->material->GetSort() >= SS_POST_PROCESS
			&& !backEnd.currentRenderCopied ) {
			break;
		}

		{
			const int run = RB_LowResRun( drawSurfs, i, numDrawSurfs, RB_STD_T_RenderShaderPasses );
			if ( run > 0 ) {
				i += run - 1;
				continue;
			}
		}
		RB_STD_T_RenderShaderPasses( drawSurfs[i] );
	}

	GL_Cull( CT_FRONT_SIDED );
	qglColor3f( 1, 1, 1 );

	return i;
}



/*
==============================================================================

BACK END RENDERING OF STENCIL SHADOWS

==============================================================================
*/

/*
=====================
Shadow pass state

Every shadow surface set its depth bounds and its two separate stencil
ops, though most surfaces of a light repeat the previous one's.  What the
current shadow pass last set is kept here and only a change is sent;
RB_StencilShadowPass forgets it when it starts.
=====================
*/
static bool		rbShadowBoundsSet;
static float	rbShadowBoundsMin, rbShadowBoundsMax;
static int		rbShadowStencilOps;		// 0 unknown, else RB_SHADOW_OPS_*

enum {
	RB_SHADOW_OPS_ZFAIL = 1,		// back decr / front incr on depth fail
	RB_SHADOW_OPS_ZPASS,			// back incr / front decr on depth pass
	RB_SHADOW_OPS_ZPASS_PRELOAD		// both decr / incr on fail and pass
};

static void RB_ShadowDepthBounds( float zmin, float zmax ) {
	if ( rbShadowBoundsSet && rbShadowBoundsMin == zmin && rbShadowBoundsMax == zmax ) {
		return;
	}
	qglDepthBoundsEXT( zmin, zmax );
	rbShadowBoundsSet = true;
	rbShadowBoundsMin = zmin;
	rbShadowBoundsMax = zmax;
}

static void RB_ShadowStencilOpSeparate( int ops ) {
	const GLenum firstFace = backEnd.viewDef->isMirror ? GL_FRONT : GL_BACK;
	const GLenum secondFace = backEnd.viewDef->isMirror ? GL_BACK : GL_FRONT;
	if ( rbShadowStencilOps == ops ) {
		return;
	}
	switch ( ops ) {
	case RB_SHADOW_OPS_ZFAIL:
		qglStencilOpSeparate( firstFace, GL_KEEP, tr.stencilDecr, GL_KEEP );
		qglStencilOpSeparate( secondFace, GL_KEEP, tr.stencilIncr, GL_KEEP );
		break;
	case RB_SHADOW_OPS_ZPASS:
		qglStencilOpSeparate( firstFace, GL_KEEP, GL_KEEP, tr.stencilIncr );
		qglStencilOpSeparate( secondFace, GL_KEEP, GL_KEEP, tr.stencilDecr );
		break;
	default:
		qglStencilOpSeparate( firstFace, GL_KEEP, tr.stencilDecr, tr.stencilDecr );
		qglStencilOpSeparate( secondFace, GL_KEEP, tr.stencilIncr, tr.stencilIncr );
		break;
	}
	rbShadowStencilOps = ops;
}

/*
=====================
RB_T_Shadow

the shadow volumes face INSIDE
=====================
*/
static void RB_T_Shadow( const drawSurf_t *surf ) {
	const srfTriangles_t	*tri;

	// set the light position if we are using a vertex program to project the rear surfaces
	if ( tr.backEndRendererHasVertexPrograms && r_useShadowVertexProgram.GetBool()
		&& surf->space != backEnd.currentSpace ) {
		idVec4 localLight;

		R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.vLight->globalLightOrigin, localLight.ToVec3() );
		localLight.w = 0.0f;
		qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_ORIGIN, localLight.ToFloatPtr() );
	}

	tri = surf->geo;

	if ( !tri->shadowCache ) {
		return;
	}

#ifdef BOOM3_ARB2_RENDERER
	if ( tri->gpuShadow ) {
		if ( !RB_ARB2_CaptureGpuShadow( tri ) ) {
			return;
		}
	} else
#endif
	qglVertexPointer( 4, GL_FLOAT, sizeof( shadowCache_t ), vertexCache.Position(tri->shadowCache) );

	// we always draw the sil planes, but we may not need to draw the front or rear caps
	int	numIndexes;
	bool external = false;

	if ( !r_useExternalShadows.GetInteger() ) {
		numIndexes = tri->numIndexes;
	} else if ( r_useExternalShadows.GetInteger() == 2 ) { // force to no caps for testing
		numIndexes = tri->numShadowIndexesNoCaps;
	} else if ( !(surf->dsFlags & DSF_VIEW_INSIDE_SHADOW) ) {
		// if we aren't inside the shadow projection, no caps are ever needed needed
		numIndexes = tri->numShadowIndexesNoCaps;
		external = true;
	} else if ( !backEnd.vLight->viewInsideLight && !(surf->geo->shadowCapPlaneBits & SHADOW_CAP_INFINITE) ) {
		// if we are inside the shadow projection, but outside the light, and drawing
		// a non-infinite shadow, we can skip some caps
		if ( backEnd.vLight->viewSeesShadowPlaneBits & surf->geo->shadowCapPlaneBits ) {
			// we can see through a rear cap, so we need to draw it, but we can skip the
			// caps on the actual surface
			numIndexes = tri->numShadowIndexesNoFrontCaps;
		} else {
			// we don't need to draw any caps
			numIndexes = tri->numShadowIndexesNoCaps;
		}
		external = true;
	} else {
		// must draw everything
		numIndexes = tri->numIndexes;
	}

	// set depth bounds
	if( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() ) {
		RB_ShadowDepthBounds( surf->scissorRect.zmin, surf->scissorRect.zmax );
	}

	// debug visualization
	if ( r_showShadows.GetInteger() ) {
		if ( r_showShadows.GetInteger() == 3 ) {
			if ( external ) {
				qglColor3f( 0.1/backEnd.overBright, 1/backEnd.overBright, 0.1/backEnd.overBright );
			} else {
				// these are the surfaces that require the reverse
				qglColor3f( 1/backEnd.overBright, 0.1/backEnd.overBright, 0.1/backEnd.overBright );
			}
		} else {
			// draw different color for turboshadows
			if ( surf->geo->shadowCapPlaneBits & SHADOW_CAP_INFINITE ) {
				if ( numIndexes == tri->numIndexes ) {
					qglColor3f( 1/backEnd.overBright, 0.1/backEnd.overBright, 0.1/backEnd.overBright );
				} else {
					qglColor3f( 1/backEnd.overBright, 0.4/backEnd.overBright, 0.1/backEnd.overBright );
				}
			} else {
				if ( numIndexes == tri->numIndexes ) {
					qglColor3f( 0.1/backEnd.overBright, 1/backEnd.overBright, 0.1/backEnd.overBright );
				} else if ( numIndexes == tri->numShadowIndexesNoFrontCaps ) {
					qglColor3f( 0.1/backEnd.overBright, 1/backEnd.overBright, 0.6/backEnd.overBright );
				} else {
					qglColor3f( 0.6/backEnd.overBright, 1/backEnd.overBright, 0.1/backEnd.overBright );
				}
			}
		}

		qglStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
		rbShadowStencilOps = 0;
		qglDisable( GL_STENCIL_TEST );
		GL_Cull( CT_TWO_SIDED );
		RB_DrawShadowElementsWithCounters( tri, numIndexes );
		GL_Cull( CT_FRONT_SIDED );
		qglEnable( GL_STENCIL_TEST );

		return;
	}

	// DG: that bloody patent on depth-fail stencil shadows has finally expired on 2019-10-13,
	//     so use them (see https://patents.google.com/patent/US6384822B1/en for expiration status)
	bool useStencilOpSeperate = r_useStencilOpSeparate.GetBool() && qglStencilOpSeparate != NULL;
	if( !r_useCarmacksReverse.GetBool() ) {
		if( useStencilOpSeperate ) {
			// not using z-fail, but using qglStencilOpSeparate()
			GL_Cull( CT_TWO_SIDED );
			if ( !external ) {
				RB_ShadowStencilOpSeparate( RB_SHADOW_OPS_ZPASS_PRELOAD );
				RB_DrawShadowElementsWithCounters( tri, numIndexes );
			}

			RB_ShadowStencilOpSeparate( RB_SHADOW_OPS_ZPASS );

			RB_DrawShadowElementsWithCounters( tri, numIndexes );

		} else { // DG: this is the original code:
			// patent-free work around
			if ( !external ) {
				// "preload" the stencil buffer with the number of volumes
				// that get clipped by the near or far clip plane
				qglStencilOp( GL_KEEP, tr.stencilDecr, tr.stencilDecr );
				GL_Cull( CT_FRONT_SIDED );
				RB_DrawShadowElementsWithCounters( tri, numIndexes );
				qglStencilOp( GL_KEEP, tr.stencilIncr, tr.stencilIncr );
				GL_Cull( CT_BACK_SIDED );
				RB_DrawShadowElementsWithCounters( tri, numIndexes );
			}

			// traditional depth-pass stencil shadows
			qglStencilOp( GL_KEEP, GL_KEEP, tr.stencilIncr );
			GL_Cull( CT_FRONT_SIDED );
			RB_DrawShadowElementsWithCounters( tri, numIndexes );

			qglStencilOp( GL_KEEP, GL_KEEP, tr.stencilDecr );
			GL_Cull( CT_BACK_SIDED );
			RB_DrawShadowElementsWithCounters( tri, numIndexes );
		}
	} else { // use the formerly patented "Carmack's Reverse" Z-Fail code
		if( useStencilOpSeperate ) {
			// Z-Fail with glStencilOpSeparate() which will reduce draw calls
			if ( !external ) { // z-fail
				RB_ShadowStencilOpSeparate( RB_SHADOW_OPS_ZFAIL );
			} else { // depth-pass
				RB_ShadowStencilOpSeparate( RB_SHADOW_OPS_ZPASS );
			}
			GL_Cull( CT_TWO_SIDED );
			RB_DrawShadowElementsWithCounters( tri, numIndexes );

		} else { // Z-Fail without glStencilOpSeparate()

			// LEITH: the (formerly patented) "Carmack's Reverse" code

			// depth-fail/Z-Fail stencil shadows
			if ( !external ) {
				qglStencilOp( GL_KEEP, tr.stencilDecr, GL_KEEP );
				GL_Cull( CT_FRONT_SIDED );
				RB_DrawShadowElementsWithCounters( tri, numIndexes );
				qglStencilOp( GL_KEEP, tr.stencilIncr, GL_KEEP );
				GL_Cull( CT_BACK_SIDED );
				RB_DrawShadowElementsWithCounters( tri, numIndexes );
			}
			// traditional depth-pass stencil shadows
			else {
				qglStencilOp( GL_KEEP, GL_KEEP, tr.stencilIncr );
				GL_Cull( CT_FRONT_SIDED );
				RB_DrawShadowElementsWithCounters( tri, numIndexes );

				qglStencilOp( GL_KEEP, GL_KEEP, tr.stencilDecr );
				GL_Cull( CT_BACK_SIDED );
				RB_DrawShadowElementsWithCounters( tri, numIndexes );
			}
		}
	}
}

/*
=====================
RB_StencilShadowPass

Stencil test should already be enabled, and the stencil buffer should have
been set to 128 on any surfaces that might receive shadows
=====================
*/
void RB_StencilShadowPass( const drawSurf_t *drawSurfs ) {
	if ( !r_shadows.GetBool() ) {
		return;
	}

	if ( !drawSurfs ) {
		return;
	}

	globalImages->BindNull();
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );

	// for visualizing the shadows
	if ( r_showShadows.GetInteger() ) {
		if ( r_showShadows.GetInteger() == 2 ) {
			// draw filled in
			GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_LESS  );
		} else {
			// draw as lines, filling the depth buffer
			GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_POLYMODE_LINE | GLS_DEPTHFUNC_ALWAYS  );
		}
	} else {
		// don't write to the color buffer, just the stencil buffer
		GL_State( GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS );
	}

	if ( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat() ) {
		qglPolygonOffset( r_shadowPolygonFactor.GetFloat(), -r_shadowPolygonOffset.GetFloat() );
		qglEnable( GL_POLYGON_OFFSET_FILL );
	}

	qglStencilFunc( GL_ALWAYS, 1, 255 );

	if ( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() ) {
		qglEnable( GL_DEPTH_BOUNDS_TEST_EXT );
	}

	// nothing of this pass's per-surface state is known yet
	rbShadowBoundsSet = false;
	rbShadowStencilOps = 0;
	RB_RenderDrawSurfChainWithFunction( drawSurfs, RB_T_Shadow );

	GL_Cull( CT_FRONT_SIDED );

	if ( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat() ) {
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}

	if ( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() ) {
		qglDisable( GL_DEPTH_BOUNDS_TEST_EXT );
	}

	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );

	qglStencilFunc( GL_GEQUAL, 128, 255 );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
}



/*
=============================================================================================

BLEND LIGHT PROJECTION

=============================================================================================
*/

/*
=====================
RB_T_BlendLight

=====================
*/
static void RB_T_BlendLight( const drawSurf_t *surf ) {
	const srfTriangles_t *tri;

	tri = surf->geo;

	if ( backEnd.currentSpace != surf->space ) {
		idPlane	lightProject[4];
		int		i;

		for ( i = 0 ; i < 4 ; i++ ) {
			R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[i], lightProject[i] );
		}

		GL_SelectTexture( 0 );
		qglTexGenfv( GL_S, GL_OBJECT_PLANE, lightProject[0].ToFloatPtr() );
		qglTexGenfv( GL_T, GL_OBJECT_PLANE, lightProject[1].ToFloatPtr() );
		qglTexGenfv( GL_Q, GL_OBJECT_PLANE, lightProject[2].ToFloatPtr() );

		GL_SelectTexture( 1 );
		qglTexGenfv( GL_S, GL_OBJECT_PLANE, lightProject[3].ToFloatPtr() );
	}

	// this gets used for both blend lights and shadow draws
	if ( tri->ambientCache ) {
		idDrawVert	*ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
		qglVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DRAWVERT( ac, xyz ) );
	} else if ( tri->shadowCache ) {
		shadowCache_t	*sc = (shadowCache_t *)vertexCache.Position( tri->shadowCache );
		qglVertexPointer( 3, GL_FLOAT, sizeof( shadowCache_t ), RB_MEMBER_AT( sc, shadowCache_t, xyz ) );
	}

	RB_DrawElementsWithCounters( tri );
}


/*
=====================
RB_BlendLight

Dual texture together the falloff and projection texture with a blend
mode to the framebuffer, instead of interacting with the surface texture
=====================
*/
static void RB_BlendLight( const drawSurf_t *drawSurfs,  const drawSurf_t *drawSurfs2 ) {
	const idMaterial	*lightShader;
	const shaderStage_t	*stage;
	int					i;
	const float	*regs;

	if ( !drawSurfs ) {
		return;
	}
	if ( r_skipBlendLights.GetBool() ) {
		return;
	}

	lightShader = backEnd.vLight->lightShader;
	regs = backEnd.vLight->shaderRegisters;

	// texture 1 will get the falloff texture
	GL_SelectTexture( 1 );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	qglEnable( GL_TEXTURE_GEN_S );
	qglTexCoord2f( 0, 0.5 );
	backEnd.vLight->falloffImage->Bind();

	// texture 0 will get the projected texture
	GL_SelectTexture( 0 );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	qglEnable( GL_TEXTURE_GEN_S );
	qglEnable( GL_TEXTURE_GEN_T );
	qglEnable( GL_TEXTURE_GEN_Q );

	for ( i = 0 ; i < lightShader->GetNumStages() ; i++ ) {
		stage = lightShader->GetStage(i);

		if ( !regs[ stage->conditionRegister ] ) {
			continue;
		}

		GL_State( GLS_DEPTHMASK | stage->drawStateBits | GLS_DEPTHFUNC_EQUAL );

		GL_SelectTexture( 0 );
		stage->texture.image->Bind();

		if ( stage->texture.hasMatrix ) {
			RB_LoadShaderTextureMatrix( regs, &stage->texture );
		}

		// get the modulate values from the light, including alpha, unlike normal lights
		backEnd.lightColor[0] = regs[ stage->color.registers[0] ];
		backEnd.lightColor[1] = regs[ stage->color.registers[1] ];
		backEnd.lightColor[2] = regs[ stage->color.registers[2] ];
		backEnd.lightColor[3] = regs[ stage->color.registers[3] ];

		/* Same headroom as an additive material stage, and for the same
		 * reason.  An additive blend light is a glow the volume emits -
		 * the haze around a lamp, the wash off a console bank - and its
		 * colour is capped at the material's 1.0, so it lands on paper
		 * white and no higher however bright the thing it surrounds is.
		 * A filter or modulate blend light darkens, and scaling that
		 * would be a gamma control rather than a light, so only the
		 * additive ones move.  Alpha is coverage and stays put. */
		{
			extern bool  hdr_output_active;
			extern float hdr_emissive_gain;
			const int blend = stage->drawStateBits
					& ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
			if ( hdr_output_active && hdr_emissive_gain != 1.0f
					&& ( blend & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE ) {
				backEnd.lightColor[0] *= hdr_emissive_gain;
				backEnd.lightColor[1] *= hdr_emissive_gain;
				backEnd.lightColor[2] *= hdr_emissive_gain;
			}
		}
		qglColor4fv( backEnd.lightColor );

		RB_RenderDrawSurfChainWithFunction( drawSurfs, RB_T_BlendLight );
		RB_RenderDrawSurfChainWithFunction( drawSurfs2, RB_T_BlendLight );

		if ( stage->texture.hasMatrix ) {
			GL_SelectTexture( 0 );
			qglMatrixMode( GL_TEXTURE );
			qglLoadIdentity();
			qglMatrixMode( GL_MODELVIEW );
		}
	}

	GL_SelectTexture( 1 );
	qglDisable( GL_TEXTURE_GEN_S );
	globalImages->BindNull();

	GL_SelectTexture( 0 );
	qglDisable( GL_TEXTURE_GEN_S );
	qglDisable( GL_TEXTURE_GEN_T );
	qglDisable( GL_TEXTURE_GEN_Q );
}


//========================================================================

static idPlane	fogPlanes[4];

/*
=====================
RB_T_BasicFog

=====================
*/
static void RB_T_BasicFog( const drawSurf_t *surf ) {
	if ( backEnd.currentSpace != surf->space ) {
		idPlane	local;

		GL_SelectTexture( 0 );

		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogPlanes[0], local );
		local[3] += 0.5;
		qglTexGenfv( GL_S, GL_OBJECT_PLANE, local.ToFloatPtr() );

//		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogPlanes[1], local );
//		local[3] += 0.5;
local[0] = local[1] = local[2] = 0; local[3] = 0.5;
		qglTexGenfv( GL_T, GL_OBJECT_PLANE, local.ToFloatPtr() );

		GL_SelectTexture( 1 );

		// GL_S is constant per viewer
		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogPlanes[2], local );
		local[3] += FOG_ENTER;
		qglTexGenfv( GL_T, GL_OBJECT_PLANE, local.ToFloatPtr() );

		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogPlanes[3], local );
		qglTexGenfv( GL_S, GL_OBJECT_PLANE, local.ToFloatPtr() );
	}

	RB_T_RenderTriangleSurface( surf );
}



/*
==================
RB_FogPass
==================
*/
static void RB_FogPass( const drawSurf_t *drawSurfs,  const drawSurf_t *drawSurfs2 ) {
	const srfTriangles_t*frustumTris;
	drawSurf_t			ds;
	const idMaterial	*lightShader;
	const shaderStage_t	*stage;
	const float			*regs;

	// create a surface for the light frustom triangles, which are oriented drawn side out
	frustumTris = backEnd.vLight->frustumTris;

	// if we ran out of vertex cache memory, skip it
	if ( !frustumTris->ambientCache ) {
		return;
	}
	memset( &ds, 0, sizeof( ds ) );
	ds.space = &backEnd.viewDef->worldSpace;
	ds.geo = frustumTris;
	ds.scissorRect = backEnd.viewDef->scissor;

	// find the current color and density of the fog
	lightShader = backEnd.vLight->lightShader;
	regs = backEnd.vLight->shaderRegisters;
	// assume fog shaders have only a single stage
	stage = lightShader->GetStage(0);

	backEnd.lightColor[0] = regs[ stage->color.registers[0] ];
	backEnd.lightColor[1] = regs[ stage->color.registers[1] ];
	backEnd.lightColor[2] = regs[ stage->color.registers[2] ];
	backEnd.lightColor[3] = regs[ stage->color.registers[3] ];

	qglColor3fv( backEnd.lightColor );

	// calculate the falloff planes
	float	a;

	// if they left the default value on, set a fog distance of 500
	if ( backEnd.lightColor[3] <= 1.0 ) {
		a = -0.5f / DEFAULT_FOG_DISTANCE;
	} else {
		// otherwise, distance = alpha color
		a = -0.5f / backEnd.lightColor[3];
	}

	GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );

	// texture 0 is the falloff image
	GL_SelectTexture( 0 );
	globalImages->fogImage->Bind();
	//GL_Bind( tr.whiteImage );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	qglEnable( GL_TEXTURE_GEN_S );
	qglEnable( GL_TEXTURE_GEN_T );
	qglTexCoord2f( 0.5f, 0.5f );		// make sure Q is set

	fogPlanes[0][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[2];
	fogPlanes[0][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[6];
	fogPlanes[0][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[10];
	fogPlanes[0][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[14];

	fogPlanes[1][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[0];
	fogPlanes[1][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[4];
	fogPlanes[1][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[8];
	fogPlanes[1][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[12];


	// texture 1 is the entering plane fade correction
	GL_SelectTexture( 1 );
	globalImages->fogEnterImage->Bind();
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	qglEnable( GL_TEXTURE_GEN_S );
	qglEnable( GL_TEXTURE_GEN_T );

	// T will get a texgen for the fade plane, which is always the "top" plane on unrotated lights
	fogPlanes[2][0] = 0.001f * backEnd.vLight->fogPlane[0];
	fogPlanes[2][1] = 0.001f * backEnd.vLight->fogPlane[1];
	fogPlanes[2][2] = 0.001f * backEnd.vLight->fogPlane[2];
	fogPlanes[2][3] = 0.001f * backEnd.vLight->fogPlane[3];

	// S is based on the view origin
	float s = backEnd.viewDef->renderView.vieworg * fogPlanes[2].Normal() + fogPlanes[2][3];

	fogPlanes[3][0] = 0;
	fogPlanes[3][1] = 0;
	fogPlanes[3][2] = 0;
	fogPlanes[3][3] = FOG_ENTER + s;

	qglTexCoord2f( FOG_ENTER + s, FOG_ENTER );


	// draw it
	RB_RenderDrawSurfChainWithFunction( drawSurfs, RB_T_BasicFog );
	RB_RenderDrawSurfChainWithFunction( drawSurfs2, RB_T_BasicFog );

	// the light frustum bounding planes aren't in the depth buffer, so use depthfunc_less instead
	// of depthfunc_equal
	GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_LESS );
	GL_Cull( CT_BACK_SIDED );
	RB_RenderDrawSurfChainWithFunction( &ds, RB_T_BasicFog );
	GL_Cull( CT_FRONT_SIDED );

	GL_SelectTexture( 1 );
	qglDisable( GL_TEXTURE_GEN_S );
	qglDisable( GL_TEXTURE_GEN_T );
	globalImages->BindNull();

	GL_SelectTexture( 0 );
	qglDisable( GL_TEXTURE_GEN_S );
	qglDisable( GL_TEXTURE_GEN_T );
}


/*
==================
RB_STD_FogAllLights
==================
*/
void RB_STD_FogAllLights( void ) {
	viewLight_t	*vLight;

	if ( r_skipFogLights.GetBool() || r_showOverDraw.GetInteger() != 0
		 || backEnd.viewDef->isXraySubview /* dont fog in xray mode*/
		 ) {
		return;
	}

	qglDisable( GL_STENCIL_TEST );

	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		backEnd.vLight = vLight;

		if ( !vLight->lightShader->IsFogLight() && !vLight->lightShader->IsBlendLight() ) {
			continue;
		}

		if ( vLight->lightShader->IsFogLight() ) {
			RB_FogPass( vLight->globalInteractions, vLight->localInteractions );
		} else if ( vLight->lightShader->IsBlendLight() ) {
			RB_BlendLight( vLight->globalInteractions, vLight->localInteractions );
		}
		qglDisable( GL_STENCIL_TEST );
	}

	qglEnable( GL_STENCIL_TEST );
}

//=========================================================================================

/*
==================
RB_STD_LightScale

Perform extra blending passes to multiply the entire buffer by
a floating point value
==================
*/
void RB_STD_LightScale( void ) {
	float	v, f;

	if ( backEnd.overBright == 1.0f ) {
		return;
	}

	if ( r_skipLightScale.GetBool() ) {
		return;
	}

	// the scissor may be smaller than the viewport for subviews
	if ( r_useScissor.GetBool() ) {
		qglScissor( backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
			backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
			backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );
		backEnd.currentScissor = backEnd.viewDef->scissor;
	}

	// full screen blends
	qglLoadIdentity();
	qglMatrixMode( GL_PROJECTION );
	qglPushMatrix();
	qglLoadIdentity();
	qglOrtho( 0, 1, 0, 1, -1, 1 );

	GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_SRC_COLOR );
	GL_Cull( CT_TWO_SIDED );	// so mirror views also get it
	globalImages->BindNull();
	qglDisable( GL_DEPTH_TEST );
	qglDisable( GL_STENCIL_TEST );

	v = 1;
	while ( idMath::Fabs( v - backEnd.overBright ) > 0.01 ) {	// a little extra slop
		f = backEnd.overBright / v;
		f /= 2;
		if ( f > 1 ) {
			f = 1;
		}
		qglColor3f( f, f, f );
		v = v * f * 2;

		qglBegin( GL_QUADS );
		qglVertex2f( 0,0 );
		qglVertex2f( 0,1 );
		qglVertex2f( 1,1 );
		qglVertex2f( 1,0 );
		qglEnd();
	}


	qglPopMatrix();
	qglEnable( GL_DEPTH_TEST );
	qglMatrixMode( GL_MODELVIEW );
	GL_Cull( CT_FRONT_SIDED );
}

//=========================================================================================

#endif

#ifdef BOOM3_ARB2_RENDERER
/*
=============
RB_ARB2_DrawView
=============
*/
static void RB_ARB2_DrawView( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	// fill the depth buffer and clear color buffer to black except on
	// subviews
	RB_STD_FillDepthBuffer( drawSurfs, numDrawSurfs );

	// main light renderer
	RB_ARB2_DrawInteractions();

	// disable stencil shadow test
	qglStencilFunc( GL_ALWAYS, 128, 255 );

	// uplight the entire screen to crutch up not having better blending range
	RB_STD_LightScale();

	// now draw any non-light dependent shading passes
	int	processed = RB_STD_DrawShaderPasses( drawSurfs, numDrawSurfs );

	// fob and blend lights
	RB_STD_FogAllLights();

	// now draw any post-processing effects using _currentRender
	if ( processed < numDrawSurfs ) {
		RB_STD_DrawShaderPasses( drawSurfs+processed, numDrawSurfs-processed );
	}

	RB_RenderDebugTools( drawSurfs, numDrawSurfs );
}
#endif

/*
==================
R_GLSLStageProgram

The GLSL renderers' program for a custom stage, by the name the material
gives (with or without the extension).
==================
*/
int R_GLSLStageProgram( const char *program ) {
	static const struct { const char *name; int id; } table[] = {
		{ "heatHaze", GLSL_STAGE_HEAT_HAZE },
		{ "heatHazeWithMask", GLSL_STAGE_HEAT_HAZE_MASK },
		{ "heatHazeWithMaskAndVertex", GLSL_STAGE_HEAT_HAZE_MASK_VERTEX },
		{ "colorProcess", GLSL_STAGE_COLOR_PROCESS },
		{ "bumpyEnvironment", GLSL_STAGE_BUMPY_ENVIRONMENT },
	};
	idStr stripped = program;
	int i;

	stripped.StripFileExtension();
	for ( i = 0; i < (int)( sizeof( table ) / sizeof( table[0] ) ); i++ ) {
		if ( !idStr::Icmp( stripped.c_str(), table[i].name ) ) {
			return table[i].id;
		}
	}
	return GLSL_STAGE_NONE;
}

#ifdef BOOM3_GLSL_RENDERERS
/*
=============
GLSL renderer dispatch

The GLSL renderers each live in their own namespace; these hand each call
to the one the context runs.
=============
*/
#if defined(BOOM3_GLSL_ES2_RENDERER) && defined(BOOM3_GLSL_ES3_RENDERER)
#define RB_GLSL_CALL( call )	( glConfig.renderer == R_RENDERER_ES2 ? gles2::call : gles3::call )
#elif defined(BOOM3_GLSL_ES2_RENDERER)
#define RB_GLSL_CALL( call )	( gles2::call )
#else
#define RB_GLSL_CALL( call )	( gles3::call )
#endif

void R_ReloadGLSLPrograms_f( const idCmdArgs &args ) {
	RB_GLSL_CALL( R_ReloadGLSLPrograms_f( args ) );
}

int R_GLSLTarget( void ) {
#ifdef HAVE_OPENGLES
	return GLSL_TARGET_ES;
#else
	// GLSL 3.30 wherever the context has it, except that the ES 2
	// renderer keeps to GLSL 1.20 on a compatibility profile
	const bool glsl330 = glCaps.major > 3 || ( glCaps.major == 3 && glCaps.minor >= 3 );
	if ( glCaps.core || ( glsl330 && glConfig.renderer != R_RENDERER_ES2 ) ) {
		return GLSL_TARGET_CORE;
	}
	return GLSL_TARGET_COMPAT;
#endif
}

void R_GLSLSourceForContext( idStr &source, GLenum type ) {
#ifndef HAVE_OPENGLES
	char *desktop = glsl_translate( source.c_str(),
		type == GL_VERTEX_SHADER ? GLSL_STAGE_VERTEX : GLSL_STAGE_FRAGMENT,
		R_GLSLTarget() );
	if ( !desktop ) {
		common->FatalError( "R_GLSLSourceForContext: this shader has no form for this context" );
	}
	source = desktop;
	free( desktop );
#endif
}

void RB_GLSL_PrepareShaders( void ) {
	RB_GLSL_CALL( RB_GLSL_PrepareShaders() );
}

void RB_GLSL_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	RB_GLSL_CALL( RB_GLSL_FillDepthBuffer( drawSurfs, numDrawSurfs ) );
}

void RB_GLSL_DrawInteractions( void ) {
	RB_GLSL_CALL( RB_GLSL_DrawInteractions() );
}

int RB_GLSL_DrawShaderPasses( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	return RB_GLSL_CALL( RB_GLSL_DrawShaderPasses( drawSurfs, numDrawSurfs ) );
}

void RB_GLSL_FogAllLights( void ) {
	RB_GLSL_CALL( RB_GLSL_FogAllLights() );
}

// the uniform buffer binding points, which only the ES 3 renderers use
void RB_GLSL_EndFrame( void ) {
#ifdef BOOM3_GLSL_ES3_RENDERER
	if ( glConfig.renderer != R_RENDERER_ES2 ) {
		gles3::RB_GLSL_EndFrame();
	}
#endif
}

#ifndef BOOM3_ARB2_RENDERER
// no ARB programs without the ARB2 renderer: a non-zero handle keeps the
// material stages that name them valid
int R_FindARBProgram( GLenum target, const char *program ) {
	return 1;
}
#endif

/*
=============
RB_GLSL_DrawView
=============
*/
static void RB_GLSL_DrawView( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	// fill the depth buffer and clear color buffer to black except on
	// subviews
	RB_GLSL_FillDepthBuffer( drawSurfs, numDrawSurfs );

	// main light renderer
	RB_GLSL_DrawInteractions();

	// now draw any non-light dependent shading passes
	const int processed = RB_GLSL_DrawShaderPasses( drawSurfs, numDrawSurfs );

	RB_GLSL_FogAllLights();

	// now draw any post-processing effects using _currentRender
	if ( processed < numDrawSurfs ) {
		RB_GLSL_DrawShaderPasses( drawSurfs + processed, numDrawSurfs - processed );
	}
}
#endif

/*
=============
RB_STD_DrawView

=============
*/
void	RB_STD_DrawView( void ) {
	drawSurf_t	 **drawSurfs;
	int			numDrawSurfs;

	backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;

	drawSurfs = (drawSurf_t **)&backEnd.viewDef->drawSurfs[0];
	numDrawSurfs = backEnd.viewDef->numDrawSurfs;

	// clear the z buffer, set the projection matrix, etc
	RB_BeginDrawingView();

#ifdef BOOM3_GLSL_RENDERERS
	if ( R_GLSLRenderer() ) {
		// Setup GLSL shader state
		RB_GLSL_PrepareShaders();

		// decide how much overbrighting we are going to do
		RB_DetermineLightScale();

		RB_GLSL_DrawView( drawSurfs, numDrawSurfs );
		return;
	}
#endif

#ifdef BOOM3_ARB2_RENDERER
	// decide how much overbrighting we are going to do
	RB_DetermineLightScale();

	RB_ARB2_DrawView( drawSurfs, numDrawSurfs );
#endif
}
