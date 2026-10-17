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

const char * const colorProcessShaderFP = R"(
#version 300 es
precision mediump float;

// glprogs/colorProcess.vfp: _currentRender (map 0) mixed with its own
// luminance.  u_screenParms: xy window to 0..1, zw the render's
// power-of-two adjust.
// In
in vec4 var_ColorScale;
in vec4 var_LumaScale;
flat in mediump float var_ViewHalf;

// Uniforms
uniform sampler2D u_fragmentMap0;
uniform vec4 u_screenParms;
uniform mediump vec4 u_hdrParms;

// Out
layout(location = 0) out vec4 fragColor;

// HDR / gamma epilogue, as the other stage programs have it: u_hdrParms.x
// r_brightness with the HDR encoding fold in, y the clamp - 0 per channel,
// 1 luminance-aware, 2 none above 1 - and w 1/r_gamma.
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
  vec2 screen = gl_FragCoord.xy * u_screenParms.xy * u_screenParms.zw;
  screen.y += var_ViewHalf;
  vec4 src = texture(u_fragmentMap0, screen);
  float luma = (src.x + src.y + src.z) * 0.33;
  vec3 c = src.xyz * var_ColorScale.xyz + luma * var_LumaScale.xyz;
  fragColor = vec4(dhewm3Epilogue(c), 1.0);
}
)";

}
