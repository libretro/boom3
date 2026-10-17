#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// a light's interaction with a surface: the light and half vectors in
// tangent space, the light's projection, the three maps' texcoords
// PHONG: the view vector instead of the half vector

layout(location = ATTR_VERTEX) in vec3 attr_Vertex;
layout(location = ATTR_COLOR) in vec4 attr_Color;
layout(location = ATTR_TEXCOORD) in vec2 attr_TexCoord;
layout(location = ATTR_NORMAL) in vec3 attr_Normal;
layout(location = ATTR_TANGENT) in vec3 attr_Tangent;
layout(location = ATTR_BITANGENT) in vec3 attr_Bitangent;

layout(location = 0) out vec2 var_TexDiffuse;
layout(location = 1) out vec2 var_TexNormal;
layout(location = 2) out vec2 var_TexSpecular;
layout(location = 3) out vec4 var_TexLight;
layout(location = 4) out vec4 var_Color;
layout(location = 5) out vec3 var_L;
layout(location = 6) out vec3 var_H;

void main()
{
    vec4 vertex = vec4(attr_Vertex, 1.0);
    vec4 texCoord = vec4(attr_TexCoord, 0.0, 1.0);
    mat3 M = mat3(attr_Tangent, attr_Bitangent, attr_Normal);

    var_TexNormal.x = dot(u_draw.bumpMatrixS, texCoord);
    var_TexNormal.y = dot(u_draw.bumpMatrixT, texCoord);
    var_TexDiffuse.x = dot(u_draw.diffuseMatrixS, texCoord);
    var_TexDiffuse.y = dot(u_draw.diffuseMatrixT, texCoord);
    var_TexSpecular.x = dot(u_draw.specularMatrixS, texCoord);
    var_TexSpecular.y = dot(u_draw.specularMatrixT, texCoord);

    var_TexLight.x = dot(u_draw.lightProjection[0], vertex);
    var_TexLight.y = dot(u_draw.lightProjection[1], vertex);
    var_TexLight.z = dot(u_draw.lightProjection[2], vertex);
    var_TexLight.w = dot(u_draw.lightProjection[3], vertex);

    vec3 L = u_draw.localLightOrigin.xyz - attr_Vertex;
    vec3 V = u_draw.localViewOrigin.xyz - attr_Vertex;
#if PHONG
    var_H = V * M;
#else
    var_H = (normalize(L) + normalize(V)) * M;
#endif
    var_L = L * M;
    var_Color = stage_color(attr_Color);
    gl_Position = world_to_clip(u_draw.modelMatrix * vertex);
}
