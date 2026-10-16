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

const char * const bumpyEnvironmentShaderVP = R"(
#version 100
precision mediump float;

// glprogs/bumpyEnvironment.vfp: the eye vector and the tangent frame taken
// to world space by the model matrix, for a per-pixel reflection through a
// normal map.
// In
attribute highp vec4 attr_Vertex;
attribute lowp vec4 attr_Color;
attribute vec4 attr_TexCoord;
attribute vec3 attr_Tangent;
attribute vec3 attr_Bitangent;
attribute vec3 attr_Normal;

// Uniforms
uniform highp mat4 u_modelViewProjectionMatrix;
uniform highp mat4 u_modelMatrix;
uniform vec4 u_viewOrigin;

// Out
// gl_Position
varying vec2 var_TexCoord;
varying vec3 var_ToEye;
varying vec3 var_Tangent;
varying vec3 var_Bitangent;
varying vec3 var_Normal;
varying lowp vec4 var_Color;

void main()
{
  var_TexCoord = attr_TexCoord.xy;

  highp mat3 model = mat3(u_modelMatrix);
  var_ToEye = model * (u_viewOrigin.xyz - attr_Vertex.xyz);
  var_Tangent = model * attr_Tangent;
  var_Bitangent = model * attr_Bitangent;
  var_Normal = model * attr_Normal;

  var_Color = attr_Color;

  gl_Position = u_modelViewProjectionMatrix * attr_Vertex;
}
)";

}
