#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// colorProcess.vfp: vertexParms[0] the desaturation, [1] the tint

layout(location = ATTR_VERTEX) in vec3 attr_Vertex;

layout(location = 0) out vec4 var_ColorScale;
layout(location = 1) out vec4 var_LumaScale;

void main()
{
    var_ColorScale = 1.0 - u_draw.vertexParms[0];
    var_LumaScale = u_draw.vertexParms[1] * u_draw.vertexParms[0];
    gl_Position = world_to_clip(u_draw.modelMatrix * vec4(attr_Vertex, 1.0));
}
