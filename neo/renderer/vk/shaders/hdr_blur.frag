#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// the two-band blur: vertexParms[1].xy the texel-scaled direction
layout(location = 0) in vec2 var_TexCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_scene;
void main() {
  vec3 c = texture(u_scene, var_TexCoord).rgb * 0.24458;
  c += texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * 1.36268).rgb * 0.31802;
  c += texture(u_scene, var_TexCoord - u_draw.vertexParms[1].xy * 1.36268).rgb * 0.31802;
  c += texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * 3.21159).rgb * 0.05717;
  c += texture(u_scene, var_TexCoord - u_draw.vertexParms[1].xy * 3.21159).rgb * 0.05717;
  c += texture(u_scene, var_TexCoord + u_draw.vertexParms[1].xy * 5.11234).rgb * 0.00251;
  c += texture(u_scene, var_TexCoord - u_draw.vertexParms[1].xy * 5.11234).rgb * 0.00251;
  fragColor = vec4(c, 1.0);
}
