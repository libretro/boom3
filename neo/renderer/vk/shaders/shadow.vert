#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// a shadow volume: w = 0 vertexes are projected away from the light

layout(location = ATTR_VERTEX) in vec4 attr_Vertex;

void main()
{
    vec4 vertex = attr_Vertex.w * u_draw.localLightOrigin + attr_Vertex - u_draw.localLightOrigin;
    gl_Position = world_to_clip(u_draw.modelMatrix * vertex);
}
