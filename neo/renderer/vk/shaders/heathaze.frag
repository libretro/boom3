#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// MASK 0: heatHaze.vfp, 1: with a mask map, 2: the mask times the
// vertex colour.  Map 0 _currentRender, 1 the normal map, 2 the mask;
// screenParms.xy 1/viewport, zw the viewport's share of _currentRender.

layout(location = 0) in vec2 var_TexCoord0;
layout(location = 1) in vec2 var_TexCoord1;
layout(location = 2) in vec2 var_Deform;
layout(location = 3) in vec4 var_Color;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_currentRender;
layout(set = 0, binding = TEX_BINDING + 1) uniform sampler2D u_normalMap;
#if MASK
layout(set = 0, binding = TEX_BINDING + 2) uniform sampler2D u_mask;
#endif

void main()
{
    vec4 localNormal = texture(u_normalMap, var_TexCoord1);
    localNormal.x = localNormal.a;
    localNormal = localNormal * 2.0 - 1.0;
#if MASK
    vec4 mask = texture(u_mask, var_TexCoord0);
#if MASK == 2
    mask.xy = mask.xy * var_Color.xy - 0.01;
#else
    mask.xy -= 0.01;
#endif
    if (mask.x < 0.0 || mask.y < 0.0)
        discard;
    localNormal *= mask;
#endif
    vec2 screen = gl_FragCoord.xy * u_draw.screenParms.xy;
    screen = clamp(localNormal.xy * var_Deform + screen, 0.0, 1.0) * u_draw.screenParms.zw;
    fragColor = vec4(dhewm3Epilogue(texture(u_currentRender, screen).rgb), 1.0);
}
