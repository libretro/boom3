#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// the half resolution particles onto the scene: what they add, and in
// alpha what is left of the scene, which the blend multiplies it by.
// screenParms.xy half of 1/the image's size

layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_accum;

void main()
{
    fragColor = texture(u_accum, gl_FragCoord.xy * u_draw.screenParms.xy);
}
