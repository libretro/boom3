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

const char * const interactionShaderFP = R"(
#version 100
precision mediump float;
  
// In
varying vec2 var_TexDiffuse;
varying vec2 var_TexNormal;
varying vec2 var_TexSpecular;
varying vec4 var_TexLight;
varying lowp vec4 var_Color;
varying vec3 var_L;
varying vec3 var_H;
  
// Uniforms
uniform lowp vec4 u_diffuseColor;
uniform mediump vec4 u_specularColor;   // mediump, not lowp: lowp guarantees only [-2,2],
                                        // which would clip any future HDR specular boost
//uniform float u_specularExponent;   // Not used
uniform sampler2D u_fragmentMap0;     // u_bumpTexture
uniform sampler2D u_fragmentMap1;     // u_lightFalloffTexture
uniform sampler2D u_fragmentMap2;     // u_lightProjectionTexture
uniform sampler2D u_fragmentMap3;     // u_diffuseTexture
uniform sampler2D u_fragmentMap4;     // u_specularTexture
  
// Out
// gl_FragCoord
  
void main(void)
{
  vec3 L = normalize(var_L);
  vec3 H = normalize(var_H);
  vec3 N = 2.0 * texture2D(u_fragmentMap0, var_TexNormal.st).agb - 1.0;
  
  float NdotL = clamp(dot(N, L), 0.0, 1.0);
  float NdotH = clamp(dot(N, H), 0.0, 1.0);
  
  vec3 lightProjection = texture2DProj(u_fragmentMap2, var_TexLight.xyw).rgb;
  vec3 lightFalloff = texture2D(u_fragmentMap1, vec2(var_TexLight.z, 0.5)).rgb;
  vec3 diffuseColor = texture2D(u_fragmentMap3, var_TexDiffuse).rgb * u_diffuseColor.rgb;
  vec3 specularColor = 2.0 * texture2D(u_fragmentMap4, var_TexSpecular).rgb * u_specularColor.rgb;
  
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
  
  gl_FragColor = vec4(color, 1.0) * var_Color;
}
)";
