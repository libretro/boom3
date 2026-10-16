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

namespace glsl_es2 {

const char * const cubeMapShaderFP = R"(
#version 100
precision mediump float;

// In
varying vec3 var_TexCoord;
varying lowp vec4 var_Color;

// Uniforms
uniform samplerCube u_fragmentCubeMap0;
uniform mediump vec4 u_glColor;
uniform mediump vec4 u_hdrParms;   // x r_brightness, y clamp mode, w 1/r_gamma

// HDR / gamma epilogue, as the interaction programs have it and as the ARB2
// path injects into its stage programs: u_hdrParms.x r_brightness with the
// HDR encoding fold in (not for filter blends), y the clamp - 0 per channel,
// 1 luminance-aware, 2 none above 1 - and w 1/r_gamma.  Alpha is coverage
// and passes through.
vec3 dhewm3Epilogue(vec3 c)
{
  c *= u_hdrParms.x;
  if (u_hdrParms.y > 1.5) {
    c = max(c, 0.0);
  } else if (u_hdrParms.y > 0.5) {
    c = max(c, 0.0);
    float L = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float peak = max(c.r, max(c.g, c.b));
    float t = clamp((peak - 1.0) / max(peak - L, 0.0001), 0.0, 1.0);
    c = clamp(mix(c, vec3(L), t), 0.0, 1.0);
  } else {
    c = clamp(c, 0.0, 1.0);
  }
  if (u_hdrParms.w == 1.0) {
    return c;
  }
  return pow(c, vec3(u_hdrParms.w));
}

void main()
{
  vec4 t = textureCube(u_fragmentCubeMap0, var_TexCoord);
  vec4 c = t * u_glColor * var_Color;
  gl_FragColor = vec4(dhewm3Epilogue(c.rgb), c.a);
}
)";

}
