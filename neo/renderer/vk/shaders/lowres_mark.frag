#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// the pixels whose composite reads 2x2s across a break in the scene's
// depth: the distance range of the four half resolution pixels around
// it wider than screenParms.z of its nearest.  screenParms.xy 1/the
// range image's size

layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_range;

void main()
{
    vec2 f = floor(gl_FragCoord.xy * 0.5 - 0.5) + 0.5;
    vec2 a = texture(u_range, f * u_draw.screenParms.xy).xy;
    vec2 b = texture(u_range, (f + vec2(1.0, 0.0)) * u_draw.screenParms.xy).xy;
    vec2 c = texture(u_range, (f + vec2(0.0, 1.0)) * u_draw.screenParms.xy).xy;
    vec2 d = texture(u_range, (f + vec2(1.0, 1.0)) * u_draw.screenParms.xy).xy;
    float n = min(min(a.x, b.x), min(c.x, d.x));
    float x = max(max(a.y, b.y), max(c.y, d.y));
    if (x - n < u_draw.screenParms.z * n)
        discard;
    fragColor = vec4(0.0);
}
