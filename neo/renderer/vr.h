#ifndef __RENDERER_VR_H__
#define __RENDERER_VR_H__

#include "renderer/RenderWorld.h"

#ifdef BOOM3_VR
bool VR_Active( void );
void VR_GetEyeRenderView( int eye, const renderView_t *game, renderView_t *out, float fovTan[4] );
void VR_BindTargetForView( int vrView );   // 0 = UI, 1 = left, 2 = right
// A hand's grip (aim false) or pointing (aim true) pose in the game world,
// placed against the game's view the way the eyes are: false while that
// hand is not tracked, or the frontend has no controller poses.
// hand 0 is the right, 1 the left.
bool VR_GetHandPose( int hand, bool aim, const idVec3 &viewOrigin, const idMat3 &viewAxis,
	idVec3 &origin, idMat3 &axis );
// A full-screen fade (rgba, not premultiplied) for this frame's eyes.  A 2D
// quad would land on the head-locked UI panel - a box that fades - so the
// eyes take it whole instead.  Cleared once presented.
void VR_SetScreenFade( const float rgba[4] );
#ifdef HAVE_OPENGLES3
// 3 = both eyes.  Multiview targets can't be read from, so framebuffer copies
// go through one layer of the bound target between these two.
void VR_BeginLayerRead( int layer );
void VR_BeginSceneRead( void );
void VR_EndLayerRead( void );
#endif
#else
#define VR_Active() false

#endif
#endif
