#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// heatHaze.vfp: the distortion scaled by the eye distance;
// vertexParms[0].xy the normal map's scroll, [1].xy the magnitude

layout(location = ATTR_VERTEX) in vec3 attr_Vertex;
layout(location = ATTR_COLOR) in vec4 attr_Color;
layout(location = ATTR_TEXCOORD) in vec2 attr_TexCoord;

layout(location = 0) out vec2 var_TexCoord0;
layout(location = 1) out vec2 var_TexCoord1;
layout(location = 2) out vec2 var_Deform;
layout(location = 3) out vec4 var_Color;

void main()
{
    vec4 vertex = vec4(attr_Vertex, 1.0);
    var_TexCoord0 = attr_TexCoord;
    var_TexCoord1 = attr_TexCoord + u_draw.vertexParms[0].xy;
    float eyeZ = (u_draw.modelViewMatrix * vertex).z;
    vec4 p = vec4(1.0, 0.0, eyeZ, 1.0);
    float r1 = dot(p, vec4(u_view.projection[0][0], u_view.projection[1][0], u_view.projection[2][0], u_view.projection[3][0]));
    float r2 = max(dot(p, vec4(u_view.projection[0][3], u_view.projection[1][3], u_view.projection[2][3], u_view.projection[3][3])), 1.0);
    // the screen's y runs down here: the distortion keeps its direction
    var_Deform = min(r1 / r2, 0.02) * u_draw.vertexParms[1].xy * vec2(1.0, -1.0);
    var_Color = attr_Color;
    gl_Position = world_to_clip(u_draw.modelMatrix * vertex);
}
