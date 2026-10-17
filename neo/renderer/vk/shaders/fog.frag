#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// the fog or projection map times the enter or falloff map, in the
// light's colour

layout(location = 0) in vec2 var_TexFog;
layout(location = 1) in vec2 var_TexFogEnter;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_map0;
layout(set = 0, binding = TEX_BINDING + 1) uniform sampler2D u_map1;

void main()
{
    fragColor = texture(u_map0, var_TexFog) * texture(u_map1, var_TexFogEnter) * vec4(u_draw.fogColor.rgb, 1.0);
}
