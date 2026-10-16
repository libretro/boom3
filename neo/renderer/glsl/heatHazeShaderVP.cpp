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

const char * const heatHazeShaderVP = R"(
#version 100
precision mediump float;

// The heat haze stages' vertex program (glprogs/heatHaze*.vfp): the
// surface texcoords, the same scrolled by vertex parm 0, and the deform
// magnitude of vertex parm 1 scaled down with the eye distance.
// In
attribute highp vec4 attr_Vertex;
attribute lowp vec4 attr_Color;
attribute vec4 attr_TexCoord;

// Uniforms
uniform highp mat4 u_modelViewProjectionMatrix;
uniform mat4 u_modelViewMatrix;
uniform highp mat4 u_projectionMatrix;
uniform vec4 u_vertexParms[4];

// Out
// gl_Position
varying vec2 var_TexCoord0;
varying vec2 var_TexCoord1;
varying vec2 var_Deform;
varying lowp vec4 var_Color;

void main()
{
  var_TexCoord0 = attr_TexCoord.xy;
  var_TexCoord1 = attr_TexCoord.xy + u_vertexParms[0].xy;

  // the deform scaled by the projection of one unit at the vertex's depth,
  // never below one unit of depth and never above 0.02 of the screen
  highp float eyeZ = (u_modelViewMatrix * attr_Vertex).z;
  highp vec4 p = vec4(1.0, 0.0, eyeZ, 1.0);
  highp float r1 = dot(p, vec4(u_projectionMatrix[0][0], u_projectionMatrix[1][0], u_projectionMatrix[2][0], u_projectionMatrix[3][0]));
  highp float r2 = max(dot(p, vec4(u_projectionMatrix[0][3], u_projectionMatrix[1][3], u_projectionMatrix[2][3], u_projectionMatrix[3][3])), 1.0);
  var_Deform = min(r1 / r2, 0.02) * u_vertexParms[1].xy;

  var_Color = attr_Color;

  gl_Position = u_modelViewProjectionMatrix * attr_Vertex;
}
)";

}
