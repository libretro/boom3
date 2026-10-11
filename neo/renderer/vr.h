#ifndef __RENDERER_VR_H__
#define __RENDERER_VR_H__

#include "renderer/RenderWorld.h"

#ifdef BOOM3_VR
bool VR_Active( void );
void VR_GetEyeRenderView( int eye, const renderView_t *game, renderView_t *out, float fovTan[4] );
void VR_BindTargetForView( int vrView );   // 0 = UI, 1 = left, 2 = right
#else
#define VR_Active() false

#endif
#endif
