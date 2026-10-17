#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// an old-style stage with an explicit texcoord, and the GUI

layout(location = ATTR_VERTEX) in vec3 attr_Vertex;
layout(location = ATTR_COLOR) in vec4 attr_Color;
layout(location = ATTR_TEXCOORD) in vec2 attr_TexCoord;

layout(location = 0) out vec2 var_TexCoord;
layout(location = 1) out vec4 var_Color;

void main()
{
    vec4 vertex = vec4(attr_Vertex, 1.0);
    var_TexCoord = (u_draw.textureMatrix * vec4(attr_TexCoord, 0.0, 1.0)).xy;
    var_Color = stage_color(attr_Color);
    gl_Position = world_to_clip(u_draw.modelMatrix * vertex);
}
