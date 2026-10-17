#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// PHONG: the specular by the reflection vector and the exponent in
// vertexParms[0].x; else Blinn-Phong with the fixed exponent of 12

layout(location = 0) in vec2 var_TexDiffuse;
layout(location = 1) in vec2 var_TexNormal;
layout(location = 2) in vec2 var_TexSpecular;
layout(location = 3) in vec4 var_TexLight;
layout(location = 4) in vec4 var_Color;
layout(location = 5) in vec3 var_L;
layout(location = 6) in vec3 var_H;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_bump;
layout(set = 0, binding = TEX_BINDING + 1) uniform sampler2D u_lightFalloff;
layout(set = 0, binding = TEX_BINDING + 2) uniform sampler2D u_lightProjection;
layout(set = 0, binding = TEX_BINDING + 3) uniform sampler2D u_diffuse;
layout(set = 0, binding = TEX_BINDING + 4) uniform sampler2D u_specular;

void main()
{
    vec3 L = normalize(var_L);
#if PHONG
    vec3 V = normalize(var_H);
    vec3 N = normalize(2.0 * texture(u_bump, var_TexNormal).agb - 1.0);
#else
    vec3 H = normalize(var_H);
    vec3 N = 2.0 * texture(u_bump, var_TexNormal).agb - 1.0;
#endif
    float NdotL = clamp(dot(N, L), 0.0, 1.0);

    vec3 lightProjection = textureProj(u_lightProjection, var_TexLight.xyw).rgb;
    vec3 lightFalloff = texture(u_lightFalloff, vec2(var_TexLight.z, 0.5)).rgb;
    vec3 diffuseColor = texture(u_diffuse, var_TexDiffuse).rgb * u_draw.diffuseColor.rgb;
    vec3 specularColor = 2.0 * texture(u_specular, var_TexSpecular).rgb * u_draw.specularColor.rgb;

#if PHONG
    vec3 R = -reflect(L, N);
    float RdotV = clamp(dot(R, V), 0.0, 1.0);
    float specularFalloff = pow(RdotV, u_draw.vertexParms[0].x);
#else
    float NdotH = clamp(dot(N, H), 0.0, 1.0);
    float NdotH2 = NdotH * NdotH;
    float NdotH4 = NdotH2 * NdotH2;
    float specularFalloff = NdotH4 * NdotH4 * NdotH4;
#endif

    vec3 color = diffuseColor + specularFalloff * specularColor;
    color *= NdotL * lightProjection;
    color *= lightFalloff;
    fragColor = vec4(dhewm3Epilogue(color), 1.0) * var_Color;
}
