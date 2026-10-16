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

const char * const bumpyEnvironmentShaderVP = R"(
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

// glprogs/bumpyEnvironment.vfp: the eye vector and the tangent frame taken
// to world space by the model matrix, for a per-pixel reflection through a
// normal map.
// In
in highp vec4 attr_Vertex;
in lowp vec4 attr_Color;
in vec4 attr_TexCoord;
in vec3 attr_Tangent;
in vec3 attr_Bitangent;
in vec3 attr_Normal;

// Uniforms
// Per view: projection * view, with the eye's clip placement folded in
layout(std140) uniform ViewMatrices
{
    uniform highp mat4 u_viewProjectionMatrices[NUM_VIEWS];
};
// The weapon and model depth hacks: clip z moves by this per unit of eye w
uniform highp float u_depthHack;
uniform highp mat4 u_modelMatrix;
uniform vec4 u_viewOrigin;

// Out
// gl_Position
out vec2 var_TexCoord;
out vec3 var_ToEye;
out vec3 var_Tangent;
out vec3 var_Bitangent;
out vec3 var_Normal;
out lowp vec4 var_Color;

void main()
{
  var_TexCoord = attr_TexCoord.xy;

  highp mat3 model = mat3(u_modelMatrix);
  var_ToEye = model * (u_viewOrigin.xyz - attr_Vertex.xyz);
  var_Tangent = model * attr_Tangent;
  var_Bitangent = model * attr_Bitangent;
  var_Normal = model * attr_Normal;

  var_Color = attr_Color;

  highp vec4 worldPos = u_modelMatrix * attr_Vertex;
  gl_Position = u_viewProjectionMatrices[VIEW_ID] * worldPos;
  gl_Position.z += u_depthHack * worldPos.w;
}
)";

}
