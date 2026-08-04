/*
===========================================================================
HDR Direct Light Projection (doom_hdr_particle_lights).

Bright particle systems become real light sources. Doom 3's lighting
is fully dynamic, so the honest implementation is the idiomatic one:
the renderer collects the brightest particle clusters each frame and
drives a small pool of genuine engine lights from them - full
interaction-path illumination, which means real diffuse falloff AND
real specular highlights on the bulkheads next to a spark shower,
through the exact shading path every other light uses (including the
HDR specular boost). No screen-space fakery: a screen-space term has
no surface normals and cannot honestly claim a specular highlight.

Collection: R_AddDrawSurf already sees every particle surface
(DFRM_PARTICLE deforms) with its bounds; the K largest emissive
clusters by projected size are kept per frame. Submission: the pool's
lightDefs update at the START of the next view - one frame of lag,
invisible on sparks, unavoidable since this frame's light list is
built before its particle models are. Lights are shadowless (sparks
casting stencil shadow volumes would cost far more than they say)
and specular-enabled (the point of the feature). Unused pool slots
park far below the world at unit radius.

Restart not required: the pool is created lazily and the option is
live - set to Disabled and the slots park on the next frame.
===========================================================================
*/

#include "renderer/tr_local.h"
#include "renderer/RenderWorld_local.h"

int hdr_particle_light_count_opt = 0;   /* 0/2/4, from the core option */

#define PLIGHT_MAX 4

typedef struct {
	idVec3	origin;
	float	score;      /* projected-size proxy: bounds radius / distance */
	float	radius;
} plightCand_t;

static plightCand_t plCand[PLIGHT_MAX];
static int          plCandCount;
static qhandle_t    plHandle[PLIGHT_MAX] = { -1, -1, -1, -1 };
static idRenderWorldLocal *plWorld;

/* called from R_AddDrawSurf for particle-deform surfaces */
void R_ParticleLightCollect( const srfTriangles_t *tri, const viewEntity_t *space ) {
	if ( hdr_particle_light_count_opt <= 0 || tri == NULL || space == NULL ) {
		return;
	}
	idVec3 localCenter = ( tri->bounds[0] + tri->bounds[1] ) * 0.5f;
	idVec3 worldCenter;
	R_LocalPointToGlobal( space->modelMatrix, localCenter, worldCenter );

	float boundsRadius = ( tri->bounds[1] - tri->bounds[0] ).Length() * 0.5f;
	if ( boundsRadius < 1.0f ) {
		return;
	}
	float dist = ( worldCenter - tr.viewDef->renderView.vieworg ).Length();
	if ( dist < 1.0f ) {
		dist = 1.0f;
	}
	float score = boundsRadius / dist;

	/* keep the K best by score */
	int worst = -1;
	if ( plCandCount < hdr_particle_light_count_opt && plCandCount < PLIGHT_MAX ) {
		worst = plCandCount++;
	} else {
		float worstScore = score;
		int i;
		for ( i = 0; i < plCandCount; i++ ) {
			if ( plCand[i].score < worstScore ) {
				worstScore = plCand[i].score;
				worst = i;
			}
		}
	}
	if ( worst >= 0 ) {
		plCand[worst].origin = worldCenter;
		plCand[worst].score  = score;
		/* light radius follows the cluster, clamped sane */
		float r = boundsRadius * 2.0f;
		if ( r < 48.0f )  r = 48.0f;
		if ( r > 240.0f ) r = 240.0f;
		plCand[worst].radius = r;
	}
}

/* called at the start of R_RenderView for the primary world: drive the
   pool from LAST frame's candidates, then reset collection */
void R_ParticleLightSubmit( viewDef_t *parms ) {
	extern bool hdr_output_active;
	int active = ( hdr_output_active && parms->renderWorld == tr.primaryWorld )
		? hdr_particle_light_count_opt : 0;
	int i;

	if ( plWorld != parms->renderWorld ) {
		/* world changed (map change frees all lightDefs) - forget handles */
		for ( i = 0; i < PLIGHT_MAX; i++ ) {
			plHandle[i] = -1;
		}
		plWorld = parms->renderWorld;
	}
	if ( active <= 0 && plHandle[0] < 0 ) {
		plCandCount = 0;
		return;   /* fully off and nothing to park */
	}

	for ( i = 0; i < PLIGHT_MAX; i++ ) {
		renderLight_t rl;
		memset( &rl, 0, sizeof( rl ) );
		rl.axis.Identity();
		rl.pointLight = true;
		rl.noShadows = true;
		rl.noSpecular = false;      /* specular on bulkheads is the point */
		if ( i < plCandCount && i < active ) {
			rl.origin = plCand[i].origin;
			rl.lightRadius.Set( plCand[i].radius, plCand[i].radius, plCand[i].radius );
			/* warm spark energy, deliberately modest: these accent, not
			   illuminate. Overbright-safe under the float targets. */
			rl.shaderParms[SHADERPARM_RED]   = 0.9f;
			rl.shaderParms[SHADERPARM_GREEN] = 0.55f;
			rl.shaderParms[SHADERPARM_BLUE]  = 0.25f;
		} else {
			rl.origin.Set( 0.0f, 0.0f, -65536.0f );
			rl.lightRadius.Set( 1.0f, 1.0f, 1.0f );
			/* zero color: parked slot contributes nothing */
		}
		if ( plHandle[i] < 0 ) {
			if ( i < active ) {
				plHandle[i] = parms->renderWorld->AddLightDef( &rl );
			}
		} else {
			parms->renderWorld->UpdateLightDef( plHandle[i], &rl );
		}
	}
	plCandCount = 0;
}
