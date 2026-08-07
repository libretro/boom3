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

const char * const interactionPhongShaderFP = R"(
#version 100
precision mediump float;

// In
varying vec2 var_TexDiffuse;
varying vec2 var_TexNormal;
varying vec2 var_TexSpecular;
varying vec4 var_TexLight;
varying lowp vec4 var_Color;
varying vec3 var_L;
varying vec3 var_V;
  
// Uniforms
uniform lowp vec4 u_diffuseColor;
uniform mediump vec4 u_hdrParms;   // xyz r_brightness, w 1/r_gamma (negative = luminance-aware clamp)
uniform lowp vec4 u_specularColor;
uniform float u_specularExponent;
uniform sampler2D u_fragmentMap0; // u_bumpTexture
uniform sampler2D u_fragmentMap1; // u_lightFalloffTexture
uniform sampler2D u_fragmentMap2; // u_lightProjectionTexture
uniform sampler2D u_fragmentMap3; // u_diffuseTexture
uniform sampler2D u_fragmentMap4; // u_specularTexture

// Out
// gl_FragCoord
  
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
  vec3 V = normalize(var_V);
  vec3 N = normalize(2.0 * texture2D(u_fragmentMap0, var_TexNormal.st).agb - 1.0);
  
  float NdotL = clamp(dot(N, L), 0.0, 1.0);

  vec3 lightProjection = texture2DProj(u_fragmentMap2, var_TexLight.xyw).rgb;
  vec3 lightFalloff = texture2D(u_fragmentMap1, vec2(var_TexLight.z, 0.5)).rgb;
  vec3 diffuseColor = texture2D(u_fragmentMap3, var_TexDiffuse).rgb * u_diffuseColor.rgb;
  vec3 specularColor = 2.0 * texture2D(u_fragmentMap4, var_TexSpecular).rgb * u_specularColor.rgb;
  
  vec3 R = -reflect(L, N);
  float RdotV = clamp(dot(R, V), 0.0, 1.0);
  float specularFalloff = pow(RdotV, u_specularExponent);
  
  vec3 color;
  color = diffuseColor;
  color += specularFalloff * specularColor;
  color *= NdotL * lightProjection;
  color *= lightFalloff;
  
  gl_FragColor = vec4(dhewm3Epilogue(color), 1.0) * var_Color;
}
)";
