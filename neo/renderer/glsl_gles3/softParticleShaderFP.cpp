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

const char * const softParticleShaderFP = R"(
#version 300 es
precision mediump float;

// Soft particles, as ARB2's soft_particle.vfp (The Dark Mod #3878): the
// particle fades where the scene's depth comes within its radius, and
// where it comes within its radius of the eye.
//   u_fragmentMap0 the particle's map, u_fragmentMap1 _currentDepth
//   u_screenParms.xy  window to _currentDepth texcoord
//   u_vertexParms[0]  radius, 1/fadeRange, 1/radius
//   u_vertexParms[1]  the channels the fade leaves alone, as 1
//   u_hdrParms.x      the HDR scene encoding fold; no gamma or clamp here
// depth_consts recover eye depth in game units from the depth buffer, for
// the engine's near-infinite projection.

// In
in vec2 var_TexCoord;
in lowp vec4 var_Color;

// Uniforms
uniform sampler2D u_fragmentMap0;
// the depth needs the sampler's full precision, not a fragment sampler's default
uniform highp sampler2D u_fragmentMap1;
uniform mediump vec4 u_glColor;
uniform vec4 u_screenParms;
uniform vec4 u_vertexParms[4];
uniform mediump vec4 u_hdrParms;

// Out
layout(location = 0) out vec4 fragColor;

void main()
{
  const vec2 depth_consts = vec2(0.33333333, -0.33316667);
  highp float scene_depth = min(texture(u_fragmentMap1, gl_FragCoord.xy * u_screenParms.xy).r, 0.9994);
  scene_depth = 1.0 / (scene_depth * depth_consts.x + depth_consts.y);
  highp float particle_depth = 1.0 / (gl_FragCoord.z * depth_consts.x + depth_consts.y);

  // depth is negative into the screen: how much of the particle's volume
  // is in front of the scene, and how far it is from the eye
  float fade = clamp((particle_depth - scene_depth + u_vertexParms[0].x) * u_vertexParms[0].y, 0.0, 1.0);
  float near_fade = clamp(particle_depth * -u_vertexParms[0].z, 0.0, 1.0);
  vec4 channels = clamp(vec4(fade * near_fade) + u_vertexParms[1], 0.0, 1.0);

  vec4 c = texture(u_fragmentMap0, var_TexCoord) * channels * u_glColor * var_Color;
  fragColor = vec4(c.rgb * u_hdrParms.x, c.a);
}
)";

}
