#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// a fog light's texcoords from lightProjection: 0, 1 the fog planes,
// 2, 3 the enter planes
// BLEND: a blend light: the projection divided by its w, the falloff by 3

layout(location = ATTR_VERTEX) in vec3 attr_Vertex;

layout(location = 0) out vec2 var_TexFog;
layout(location = 1) out vec2 var_TexFogEnter;

void main()
{
    vec4 vertex = vec4(attr_Vertex, 1.0);
#if BLEND
    var_TexFog = vec2(dot(u_draw.lightProjection[0], vertex), dot(u_draw.lightProjection[1], vertex))
        / dot(u_draw.lightProjection[2], vertex);
    var_TexFogEnter = vec2(dot(u_draw.lightProjection[3], vertex), 0.5);
#else
    var_TexFog = vec2(dot(u_draw.lightProjection[0], vertex), dot(u_draw.lightProjection[1], vertex));
    var_TexFogEnter = vec2(dot(u_draw.lightProjection[2], vertex), dot(u_draw.lightProjection[3], vertex));
#endif
    gl_Position = world_to_clip(u_draw.modelMatrix * vertex);
}
