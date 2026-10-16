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

const char * const colorProcessShaderVP = R"(
#version 100
precision mediump float;

// glprogs/colorProcess.vfp: the scene's colour and desaturation weights
// from vertex parms 0 and 1.
// In
attribute highp vec4 attr_Vertex;

// Uniforms
uniform highp mat4 u_modelViewProjectionMatrix;
uniform vec4 u_vertexParms[4];

// Out
// gl_Position
varying vec4 var_ColorScale;
varying vec4 var_LumaScale;

void main()
{
  var_ColorScale = 1.0 - u_vertexParms[0];
  var_LumaScale = u_vertexParms[1] * u_vertexParms[0];

  gl_Position = u_modelViewProjectionMatrix * attr_Vertex;
}
)";

}
