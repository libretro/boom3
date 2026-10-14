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

const char * const stencilShadowShaderVP = R"(
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
        
// In
in highp vec4 attr_Vertex;
        
// Uniforms
layout(std140) uniform ViewMatrices
{
    uniform highp mat4 u_viewMatrices[NUM_VIEWS];
    uniform highp vec4 u_eyeClip[NUM_VIEWS];   // per eye clip-space xy scale, xy offset * w
};
layout(std140) uniform ProjectionMatrix
{
    uniform highp mat4 u_projectionMatrix;
};
uniform highp mat4 u_modelMatrix;
uniform vec4 u_lightOrigin;
        
// Out
// gl_Position
        
void main()
{
  gl_Position = u_projectionMatrix * (u_viewMatrices[VIEW_ID]  * (u_modelMatrix * (attr_Vertex.w * u_lightOrigin + attr_Vertex - u_lightOrigin)));
  gl_Position.xy = gl_Position.xy * u_eyeClip[VIEW_ID].xy + gl_Position.w * u_eyeClip[VIEW_ID].zw;
}
)";
