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

const char * const blendLightShaderVP = R"(
#version 300 es

// Multiview: the renderer defines BOOM3_MULTIVIEW when drawing both eyes at once
#define NUM_VIEWS 2
#ifdef BOOM3_MULTIVIEW
#extension GL_OVR_multiview2 : enable
layout(num_views=NUM_VIEWS) in;
#define VIEW_ID gl_ViewID_OVR
#else
#define VIEW_ID 0
#endif

precision mediump float;

// The same vertex must land on the same depth in every program: the
// interaction passes test it for equality against the depth fill
invariant gl_Position;

// In
in highp vec4 attr_Vertex;

// Uniforms
// Per view: projection * view, with the eye's clip placement folded in
layout(std140) uniform ViewMatrices
{
    uniform highp mat4 u_viewProjectionMatrices[NUM_VIEWS];
};
// The weapon and model depth hacks: clip z moves by this per unit of eye w
uniform highp float u_depthHack;
uniform highp mat4 u_modelMatrix;
uniform mat4 u_fogMatrix;

// Out
// gl_Position
out vec2 var_TexFog;
out vec2 var_TexFogEnter;

void main()
{
  highp vec4 worldPos = u_modelMatrix * attr_Vertex;
  gl_Position = u_viewProjectionMatrices[VIEW_ID] * worldPos;
  gl_Position.z += u_depthHack * worldPos.w;

  // What will be computed:
  //
  // vec4 tc;
  // tc.x = dot( u_fogMatrix[0], attr_Vertex );
  // tc.y = dot( u_fogMatrix[1], attr_Vertex );
  // tc.z = 0.0;
  // tc.w = dot( u_fogMatrix[2], attr_Vertex );
  // var_TexFog.xy = tc.xy / tc.w;
  //
  // var_TexFogEnter.x = dot( u_fogMatrix[3], attr_Vertex );
  // var_TexFogEnter.y = 0.5;

  // Optimized version:
  //
  var_TexFog = vec2(dot( u_fogMatrix[0], attr_Vertex ), dot( u_fogMatrix[1], attr_Vertex )) / dot( u_fogMatrix[2], attr_Vertex );
  var_TexFogEnter = vec2( dot( u_fogMatrix[3], attr_Vertex ), 0.5 );
}
)";

}
