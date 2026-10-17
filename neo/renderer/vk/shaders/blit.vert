#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// a fullscreen triangle, no vertex data
layout(location = 0) out vec2 var_TexCoord;

void main()
{
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    // the viewport is flipped for the engine's y-up clip space, so the
    // triangle's bottom samples the scene's last row
    var_TexCoord = vec2(uv.x, 1.0 - uv.y);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
