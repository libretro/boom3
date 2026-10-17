#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// bumpyEnvironment.vfp: the reflection through the normal map, in world
// space

layout(location = ATTR_VERTEX) in vec3 attr_Vertex;
layout(location = ATTR_TEXCOORD) in vec2 attr_TexCoord;
layout(location = ATTR_NORMAL) in vec3 attr_Normal;
layout(location = ATTR_TANGENT) in vec3 attr_Tangent;
layout(location = ATTR_BITANGENT) in vec3 attr_Bitangent;

layout(location = 0) out vec2 var_TexCoord;
layout(location = 1) out vec3 var_ToEye;
layout(location = 2) out vec3 var_Tangent;
layout(location = 3) out vec3 var_Bitangent;
layout(location = 4) out vec3 var_Normal;

void main()
{
    mat3 model = mat3(u_draw.modelMatrix);
    var_TexCoord = attr_TexCoord;
    var_ToEye = model * (u_draw.localViewOrigin.xyz - attr_Vertex);
    var_Tangent = model * attr_Tangent;
    var_Bitangent = model * attr_Bitangent;
    var_Normal = model * attr_Normal;
    gl_Position = world_to_clip(u_draw.modelMatrix * vec4(attr_Vertex, 1.0));
}
