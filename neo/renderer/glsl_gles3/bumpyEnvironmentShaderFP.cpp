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

const char * const bumpyEnvironmentShaderFP = R"(
#version 300 es
precision mediump float;

// glprogs/bumpyEnvironment.vfp: the reflection of the eye vector about the
// normal map's normal (map 1), in world space, looked up in the cube map
// (cube map 0).
// In
in vec2 var_TexCoord;
in vec3 var_ToEye;
in vec3 var_Tangent;
in vec3 var_Bitangent;
in vec3 var_Normal;
in lowp vec4 var_Color;

// Uniforms
uniform samplerCube u_fragmentCubeMap0;
uniform sampler2D u_fragmentMap1;
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
  vec4 localNormal = texture(u_fragmentMap1, var_TexCoord);
  localNormal.x = localNormal.a;
  vec3 n = normalize(localNormal.xyz * 2.0 - 1.0);
  vec3 globalNormal = var_Tangent * n.x + var_Bitangent * n.y + var_Normal * n.z;
  vec3 globalEye = normalize(var_ToEye);
  vec3 r = dot(globalEye, globalNormal) * globalNormal * 2.0 - globalEye;
  vec3 c = texture(u_fragmentCubeMap0, r).xyz;
  fragColor = vec4(dhewm3Epilogue(c), 1.0);
}
)";

}
