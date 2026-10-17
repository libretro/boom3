#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec2 var_TexCoord;
layout(location = 1) in vec3 var_ToEye;
layout(location = 2) in vec3 var_Tangent;
layout(location = 3) in vec3 var_Bitangent;
layout(location = 4) in vec3 var_Normal;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform samplerCube u_cube;
layout(set = 0, binding = TEX_BINDING + 1) uniform sampler2D u_normalMap;

void main()
{
    vec4 localNormal = texture(u_normalMap, var_TexCoord);
    localNormal.x = localNormal.a;
    vec3 n = normalize(localNormal.xyz * 2.0 - 1.0);
    vec3 globalNormal = var_Tangent * n.x + var_Bitangent * n.y + var_Normal * n.z;
    vec3 globalEye = normalize(var_ToEye);
    vec3 r = dot(globalEye, globalNormal) * globalNormal * 2.0 - globalEye;
    fragColor = vec4(dhewm3Epilogue(texture(u_cube, r).rgb), 1.0);
}
