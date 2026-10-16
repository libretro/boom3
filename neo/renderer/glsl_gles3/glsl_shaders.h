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

#ifndef D3WASM_GLSL_SHADERS_H
#define D3WASM_GLSL_SHADERS_H

// One namespace per set, so both sets can be in one core
namespace glsl_es3 {

// Main Light Interaction
// gouraud
extern const char* const interactionShaderVP;
extern const char* const interactionShaderFP;
// blinn (uses interactionShaderVP)
extern const char* const interactionBlinnShaderFP;
// phong
extern const char* const interactionPhongShaderVP;
extern const char* const interactionPhongShaderFP;
// Fog
extern const char* const fogShaderVP;
extern const char* const blendLightShaderVP;
extern const char* const fogShaderFP;
// Depth Buffer
// no clip planes
extern const char* const zfillShaderVP;
extern const char* const zfillShaderFP;
extern const char* const zfillOpaqueShaderFP;
// clip planes
extern const char* const zfillClipShaderVP;
extern const char* const zfillClipShaderFP;
// Ambient Surfaces
// diffuse mapping (default diffuse surfaces)
extern const char* const diffuseMapShaderVP;
extern const char* const diffuseMapShaderFP;
extern const char* const diffuseMapGuiShaderFP;
// cube mapping (skybox/wobblesky, diffusecube, reflection)
extern const char* const diffuseCubeShaderVP;
extern const char* const skyboxCubeShaderVP;
extern const char* const reflectionCubeShaderVP;
extern const char* const cubeMapShaderFP;
// custom material stages (glprogs/*.vfp)
extern const char* const heatHazeShaderVP;
extern const char* const heatHazeShaderFP;
extern const char* const heatHazeMaskShaderFP;
extern const char* const heatHazeMaskVertexShaderFP;
extern const char* const colorProcessShaderVP;
extern const char* const colorProcessShaderFP;
extern const char* const bumpyEnvironmentShaderVP;
extern const char* const bumpyEnvironmentShaderFP;
// Shadows
extern const char* const stencilShadowShaderVP;
extern const char* const stencilShadowShaderFP;

}

#endif //D3WASM_GLSL_SHADERS_H
