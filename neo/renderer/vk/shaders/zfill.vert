#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// the depth fill: a texcoord for the alpha test, and for a subview the
// clip plane's distance read from the clip notch texture (CLIP)

layout(location = ATTR_VERTEX) in vec3 attr_Vertex;
layout(location = ATTR_TEXCOORD) in vec2 attr_TexCoord;

layout(location = 0) out vec2 var_TexDiffuse;
layout(location = 1) out vec2 var_TexClip;

void main()
{
    vec4 vertex = vec4(attr_Vertex, 1.0);
    var_TexDiffuse = (u_draw.textureMatrix * vec4(attr_TexCoord, 0.0, 1.0)).xy;
    var_TexClip = vec2(dot(u_draw.clipPlane, vertex), 0.5);
    gl_Position = world_to_clip(u_draw.modelMatrix * vertex);
}
