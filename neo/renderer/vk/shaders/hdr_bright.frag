#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// the bloom's bright pass (libretro.cpp's hdr_bright_fs_src):
// vertexParms[0] threshold, knee, firefly knee, 1/encode scale;
// vertexParms[1].xy the texel
layout(location = 0) in vec2 var_TexCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_scene;
vec3 srgbToLinear(vec3 c) {
  return pow(abs(c), vec3(2.4));
}
void main() {
  vec3 s0 = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2(-1.0, -1.0)).rgb;
  vec3 s1 = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2( 1.0, -1.0)).rgb;
  vec3 s2 = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2(-1.0,  1.0)).rgb;
  vec3 s3 = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2( 1.0,  1.0)).rgb;
  vec3 lin = srgbToLinear(0.25 * (s0 + s1 + s2 + s3) * u_draw.vertexParms[0].w);
  float lum = dot(lin, vec3(0.2126, 0.7152, 0.0722));
  float sk = clamp(lum - u_draw.vertexParms[0].x + u_draw.vertexParms[0].y, 0.0, 2.0 * u_draw.vertexParms[0].y);
  sk = sk * sk / (4.0 * u_draw.vertexParms[0].y);
  vec3 b = lin * (max(sk, lum - u_draw.vertexParms[0].x) / max(lum, 1e-5)) / (1.0 - u_draw.vertexParms[0].x);
  float l = dot(b, vec3(0.2126, 0.7152, 0.0722));
  fragColor = vec4(b / (1.0 + l * u_draw.vertexParms[0].z), 1.0);
}
