#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// the pyramid's first downsample, Karis-weighted: vertexParms[1].xy the texel
layout(location = 0) in vec2 var_TexCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_scene;
float kw(vec3 c) { return 1.0 / (1.0 + max(c.r, max(c.g, c.b))); }
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
  vec3 acc = vec3(0.0); float ws = 0.0; float w;
  w = kw(a) * 0.03125; acc += a * w; ws += w;
  w = kw(b) * 0.06250; acc += b * w; ws += w;
  w = kw(c) * 0.03125; acc += c * w; ws += w;
  w = kw(d) * 0.06250; acc += d * w; ws += w;
  w = kw(e) * 0.12500; acc += e * w; ws += w;
  w = kw(f) * 0.06250; acc += f * w; ws += w;
  w = kw(g) * 0.03125; acc += g * w; ws += w;
  w = kw(h) * 0.06250; acc += h * w; ws += w;
  w = kw(i) * 0.03125; acc += i * w; ws += w;
  w = kw(j) * 0.12500; acc += j * w; ws += w;
  w = kw(k) * 0.12500; acc += k * w; ws += w;
  w = kw(l) * 0.12500; acc += l * w; ws += w;
  w = kw(m) * 0.12500; acc += m * w; ws += w;
  fragColor = vec4(acc / ws, 1.0);
}
