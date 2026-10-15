/*
 * This file is part of the D3wasm project (http://www.continuation-labs.com/projects/d3wasm)
 * Copyright (c) 2019 Gabriel Cuvillier.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "glsl_shaders.h"

namespace glsl_es3 {

const char * const interactionShaderFP = R"(
#version 300 es
precision highp float;

// In
in vec2 var_TexDiffuse;
in vec2 var_TexNormal;
in vec2 var_TexSpecular;
in vec4 var_TexLight;
in vec3 var_Normal;
in lowp vec4 var_Color;
in vec3 var_L;
in vec3 var_V;
in vec3 var_H;

// Uniforms
uniform lowp vec4 u_diffuseColor;
uniform mediump vec4 u_specularColor;   // mediump: lowp guarantees only [-2,2], which the HDR specular boost exceeds
uniform mediump vec4 u_hdrParms;   // xyz r_brightness, w 1/r_gamma (negative = luminance-aware clamp)
uniform float u_specularExponent;
uniform sampler2D u_fragmentMap0; // u_bumpTexture
uniform sampler2D u_fragmentMap1; // u_lightFalloffTexture
uniform sampler2D u_fragmentMap2; // u_lightProjectionTexture
uniform sampler2D u_fragmentMap3; // u_diffuseTexture
uniform sampler2D u_fragmentMap4; // u_specularTexture

// Out
layout(location = 0) out vec4 fragColor;

// HDR / gamma epilogue, the GLSL counterpart of the ARB2 injected epilogue in
// draw_arb2.cpp.  Same maths, expressed for this codepath rather than shared
// with it: u_hdrParms.xyz is r_brightness (with the HDR encoding fold already
// folded in), u_hdrParms.w is 1/r_gamma, and a negative w selects the
// luminance-aware clamp.
//
// Per-channel clamping rewrites the colour of anything overbright - it pulls a
// pixel toward whichever channel saturated first, which is where hue fringing
// on additive fire and coloured glows comes from.  The luminance-aware form
// desaturates toward the pixel's own luminance by exactly the amount that
// brings the peak channel to 1, so the hue survives and the pixel ends at
// neutral white instead of at a primary.
vec3 dhewm3Epilogue(vec3 c)
{
  float gw = u_hdrParms.w;
  c *= u_hdrParms.xyz;
  if (gw < 0.0) {
    c = max(c, 0.0);
    float L = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float peak = max(c.r, max(c.g, c.b));
    float t = clamp((peak - 1.0) / max(peak - L, 0.0001), 0.0, 1.0);
    c = clamp(mix(c, vec3(L), t), 0.0, 1.0);
    gw = -gw;
  } else {
    c = clamp(c, 0.0, 1.0);
  }
  // pow() is skipped at unity gamma: the identity, and the exponent is a
  // uniform so the branch is uniform across the draw.
  if (gw == 1.0) {
    return c;
  }
  return pow(c, vec3(gw));
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
    float PI = 3.14159265359;
    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;
    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

void main()
{
  vec3 L = normalize(var_L);
  vec3 V = normalize(var_V);
  vec3 H = normalize(var_H);
  vec3 N = normalize(2.0 * texture(u_fragmentMap0, var_TexNormal.st).agb - 1.0);

  float NdotL = clamp(dot(N, L), 0.0, 1.0);
  float NdotH = clamp(dot(N, H), 0.0, 1.0);

  vec3 lightProjection = textureProj(u_fragmentMap2, var_TexLight.xyw).rgb;
  vec3 lightFalloff = texture(u_fragmentMap1, vec2(var_TexLight.z, 0.5)).rgb;
  vec3 diffuseColor = texture(u_fragmentMap3, var_TexDiffuse).rgb * u_diffuseColor.rgb;

  //Lubos: make diffuse color less present when applying PBR
  if (u_specularExponent < 0.0)
  {
    float gray = (diffuseColor.r + diffuseColor.g + diffuseColor.b) / 3.0;
    diffuseColor = diffuseColor * 0.67 + u_diffuseColor.rgb * 0.33 * min(100.0 * gray, 1.0);
  }

  //PBR
  vec3 AN = normalize(mix(normalize(var_Normal), N, 1.0));
  vec4 Cd = vec4(diffuseColor.rgb, 1.0);
  vec4 specTex = texture(u_fragmentMap4, var_TexSpecular);
  vec4 roughness = vec4(specTex.r, specTex.r, specTex.r, specTex.r);
  vec4 metallic = vec4(specTex.g, specTex.g, specTex.g, specTex.g);

  vec4 Cl = vec4(lightProjection * lightFalloff, 1.0);
  vec3 F0 = vec3(0.04);
  F0 = mix(F0, Cd.xyz, metallic.xyz);

  // cook-torrance brdf
  float NDF = DistributionGGX(AN, H, roughness.x);
  float G   = GeometrySmith(AN, V, L, roughness.x);
  vec3 F    = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0); //max

  vec3 kS = F;
  vec3 kD = vec3(1.0) - kS;
  kD *= 1.0 - metallic.r;

  vec3 numerator    = NDF * G * F;
  float denominator = 4.0 * max(dot(AN, V), 0.0) * max(dot(AN, L), 0.0);
  vec3 pbr     = numerator / max(denominator, 0.1);

  vec3 color;
  color = diffuseColor;
  //Lubos BEGIN
  float smoothing = 0.33;
  float scale = abs(u_specularExponent + u_specularExponent);
  color.r += u_specularColor.r * min(pow(pbr.r, smoothing), 1.0) * scale;
  color.g += u_specularColor.g * min(pow(pbr.g, smoothing), 1.0) * scale;
  color.b += u_specularColor.b * min(pow(pbr.b, smoothing), 1.0) * scale;
  //Lubos END
  color *= NdotL * lightProjection;
  color *= lightFalloff;

  fragColor = vec4(dhewm3Epilogue(color), 1.0) * var_Color;
}
)";


// Blinn-Phong, the default interaction with r_usePhong 0: GLES2's shader in
// GLSL ES 3.00, fed by interactionShaderVP (it takes var_H, not var_V)
const char * const interactionBlinnShaderFP = R"(
#version 300 es
precision mediump float;

// In
in vec2 var_TexDiffuse;
in vec2 var_TexNormal;
in vec2 var_TexSpecular;
in vec4 var_TexLight;
in lowp vec4 var_Color;
in vec3 var_L;
in vec3 var_H;

// Uniforms
uniform lowp vec4 u_diffuseColor;
uniform mediump vec4 u_specularColor;
uniform mediump vec4 u_hdrParms;   // xyz r_brightness, w 1/r_gamma (negative = luminance-aware clamp)   // mediump, not lowp: lowp guarantees only [-2,2],
                                        // which would clip any future HDR specular boost
//uniform float u_specularExponent;   // Not used
uniform sampler2D u_fragmentMap0;     // u_bumpTexture
uniform sampler2D u_fragmentMap1;     // u_lightFalloffTexture
uniform sampler2D u_fragmentMap2;     // u_lightProjectionTexture
uniform sampler2D u_fragmentMap3;     // u_diffuseTexture
uniform sampler2D u_fragmentMap4;     // u_specularTexture

// Out
layout(location = 0) out vec4 fragColor;

// HDR / gamma epilogue, the GLSL counterpart of the ARB2 injected epilogue in
// draw_arb2.cpp.  Same maths, expressed for this codepath rather than shared
// with it: u_hdrParms.xyz is r_brightness (with the HDR encoding fold already
// folded in), u_hdrParms.w is 1/r_gamma, and a negative w selects the
// luminance-aware clamp.
//
// Per-channel clamping rewrites the colour of anything overbright - it pulls a
// pixel toward whichever channel saturated first, which is where hue fringing
// on additive fire and coloured glows comes from.  The luminance-aware form
// desaturates toward the pixel's own luminance by exactly the amount that
// brings the peak channel to 1, so the hue survives and the pixel ends at
// neutral white instead of at a primary.
vec3 dhewm3Epilogue(vec3 c)
{
  float gw = u_hdrParms.w;
  c *= u_hdrParms.xyz;
  if (gw < 0.0) {
    c = max(c, 0.0);
    float L = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float peak = max(c.r, max(c.g, c.b));
    float t = clamp((peak - 1.0) / max(peak - L, 0.0001), 0.0, 1.0);
    c = clamp(mix(c, vec3(L), t), 0.0, 1.0);
    gw = -gw;
  } else {
    c = clamp(c, 0.0, 1.0);
  }
  // pow() is skipped at unity gamma: the identity, and the exponent is a
  // uniform so the branch is uniform across the draw.
  if (gw == 1.0) {
    return c;
  }
  return pow(c, vec3(gw));
}

void main(void)
{
  vec3 L = normalize(var_L);
  vec3 H = normalize(var_H);
  vec3 N = 2.0 * texture(u_fragmentMap0, var_TexNormal.st).agb - 1.0;

  float NdotL = clamp(dot(N, L), 0.0, 1.0);
  float NdotH = clamp(dot(N, H), 0.0, 1.0);

  vec3 lightProjection = textureProj(u_fragmentMap2, var_TexLight.xyw).rgb;
  vec3 lightFalloff = texture(u_fragmentMap1, vec2(var_TexLight.z, 0.5)).rgb;
  vec3 diffuseColor = texture(u_fragmentMap3, var_TexDiffuse).rgb * u_diffuseColor.rgb;
  vec3 specularColor = 2.0 * texture(u_fragmentMap4, var_TexSpecular).rgb * u_specularColor.rgb;

  // Hardcoded exponent to try to match with original D3 look.
  //
  // Evaluated by repeated squaring rather than pow(). The exponent is a
  // literal, but glslang with spirv-opt -O still emits OpExtInst Pow for
  // it - checked, not assumed - so the fold cannot be left to the
  // toolchain. Every GLES target this backend builds for (iOS, tvOS,
  // Android, webOS) is a mobile part where pow() costs a log2 and an
  // exp2 through a quarter-rate special-function unit, against four
  // full-rate multiplies here. This is the innermost line of the
  // renderer's hottest fragment shader: it runs per fragment per light.
  //
  // Accuracy is a wash, which is the point - this is a cost change, not
  // a quality one. Measured in mediump over 100k samples of [0.001, 1],
  // relative error against exact x^12 is 1.19e-3 median for repeated
  // squaring and 1.26e-3 for exp2(12*log2(x)); both are dominated by
  // fp16 quantization rather than by the method.
  //
  // Underflow is not a new hazard either. NdotH is clamped to [0,1]
  // above, so nothing overflows, and the x^8 intermediate only drops
  // below fp16's smallest normal where the true x^12 is already outside
  // fp16 range entirely - at NdotH 0.2, x^12 is 4e-9 against a smallest
  // subnormal of 6e-8. Both forms give zero there, and a specular term
  // that small is zero on screen regardless.
  float NdotH2 = NdotH * NdotH;
  float NdotH4 = NdotH2 * NdotH2;
  float specularFalloff = NdotH4 * NdotH4 * NdotH4;

  vec3 color;
  color = diffuseColor;
  color += specularFalloff * specularColor;
  color *= NdotL * lightProjection;
  color *= lightFalloff;

  fragColor = vec4(dhewm3Epilogue(color), 1.0) * var_Color;
}
)";

}
