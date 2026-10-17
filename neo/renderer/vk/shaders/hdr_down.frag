#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// the convolution pyramid's downsample: vertexParms[1].xy the texel
layout(location = 0) in vec2 var_TexCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_scene;
void main() {
  vec3 a = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2(-2.0,  2.0)).rgb;
  vec3 b = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2( 0.0,  2.0)).rgb;
  vec3 c = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2( 2.0,  2.0)).rgb;
  vec3 d = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2(-2.0,  0.0)).rgb;
  vec3 e = texture(u_scene, var_TexCoord).rgb;
  vec3 f = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2( 2.0,  0.0)).rgb;
  vec3 g = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2(-2.0, -2.0)).rgb;
  vec3 h = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2( 0.0, -2.0)).rgb;
  vec3 i = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2( 2.0, -2.0)).rgb;
  vec3 j = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2(-1.0,  1.0)).rgb;
  vec3 k = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2( 1.0,  1.0)).rgb;
  vec3 l = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2(-1.0, -1.0)).rgb;
  vec3 m = texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * vec2( 1.0, -1.0)).rgb;
  vec3 o = e * 0.125;
  o += (a + c + g + i) * 0.03125;
  o += (b + d + f + h) * 0.0625;
  o += (j + k + l + m) * 0.125;
  fragColor = vec4(o, 1.0);
}
