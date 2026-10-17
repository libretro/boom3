#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// the scene into the frame image as it is: the 24-bit path's composite
// when the scene was drawn at the frame's size, a box filter when it was
// supersampled (screenParms.zw the scale)
layout(location = 0) in vec2 var_TexCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_scene;

void main()
{
    fragColor = texture(u_scene, var_TexCoord) * u_draw.glColor;
}
