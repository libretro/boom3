#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// the pyramid's upsample, added into the level above: vertexParms[1].xy
// the texel, vertexParms[0].x the radius
layout(location = 0) in vec2 var_TexCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_scene;
void main() {
  vec2 o = u_draw.vertexParms[1].xy * u_draw.vertexParms[0].x;
  vec3 s = texture(u_scene, var_TexCoord + vec2(-o.x, -o.y)).rgb;
  s += texture(u_scene, var_TexCoord + vec2( 0.0, -o.y)).rgb * 2.0;
  s += texture(u_scene, var_TexCoord + vec2( o.x, -o.y)).rgb;
  s += texture(u_scene, var_TexCoord + vec2(-o.x,  0.0)).rgb * 2.0;
  s += texture(u_scene, var_TexCoord).rgb * 4.0;
  s += texture(u_scene, var_TexCoord + vec2( o.x,  0.0)).rgb * 2.0;
  s += texture(u_scene, var_TexCoord + vec2(-o.x,  o.y)).rgb;
  s += texture(u_scene, var_TexCoord + vec2( 0.0,  o.y)).rgb * 2.0;
  s += texture(u_scene, var_TexCoord + vec2( o.x,  o.y)).rgb;
  fragColor = vec4(s * 0.0625, 1.0);
}
