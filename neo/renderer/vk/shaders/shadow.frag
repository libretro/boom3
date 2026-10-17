#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// the stencil alone is written; the colour is masked off
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
