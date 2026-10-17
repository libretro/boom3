#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// ALPHA_TEST: the stage's map against colorModulateAdd.w
// CLIP: times the clip notch texture
layout(location = 0) in vec2 var_TexDiffuse;
layout(location = 1) in vec2 var_TexClip;
layout(location = 0) out vec4 fragColor;

#if ALPHA_TEST
layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_map0;
#endif
#if CLIP
layout(set = 0, binding = TEX_BINDING + 1) uniform sampler2D u_map1;
#endif

void main()
{
#if ALPHA_TEST || CLIP
    float a = 1.0;
#if ALPHA_TEST
    a *= texture(u_map0, var_TexDiffuse).a;
#endif
#if CLIP
    a *= texture(u_map1, var_TexClip).a;
#endif
    if (u_draw.colorModulateAdd.w > a)
        discard;
#endif
    fragColor = u_draw.glColor;
}
