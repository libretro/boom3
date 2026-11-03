#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// the half resolution particles' depth: the farthest of a 2x2 of the
// scene's, and the eye distance range across the 2x2 in colour.
// screenParms.xy 1/the scene depth's size, .z and .w the projection's
// [10] and [14]

layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_depth;

void main()
{
    vec2 c = gl_FragCoord.xy * 2.0;
    float a = texture(u_depth, (c + vec2(-0.5, -0.5)) * u_draw.screenParms.xy).r;
    float b = texture(u_depth, (c + vec2(0.5, -0.5)) * u_draw.screenParms.xy).r;
    float d = texture(u_depth, (c + vec2(-0.5, 0.5)) * u_draw.screenParms.xy).r;
    float e = texture(u_depth, (c + vec2(0.5, 0.5)) * u_draw.screenParms.xy).r;
    float n = min(min(a, b), min(d, e));
    float f = max(max(a, b), max(d, e));
    vec2 l = min(vec2(n, f) * 2.0 - 1.0 + u_draw.screenParms.z, vec2(-0.000001));
    gl_FragDepth = f;
    fragColor = vec4(u_draw.screenParms.w / l, 0.0, 1.0);
}
