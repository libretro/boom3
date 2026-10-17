#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// a cube map stage: MODE 0 the skybox (the direction from the eye),
// 1 the reflection of the eye vector, 2 the normal

layout(location = ATTR_VERTEX) in vec3 attr_Vertex;
layout(location = ATTR_COLOR) in vec4 attr_Color;
layout(location = ATTR_NORMAL) in vec3 attr_Normal;

layout(location = 0) out vec3 var_TexCoord;
layout(location = 1) out vec4 var_Color;

void main()
{
    vec4 vertex = vec4(attr_Vertex, 1.0);
#if MODE == 0
    var_TexCoord = (u_draw.textureMatrix * (vertex - u_draw.localViewOrigin)).xyz;
#elif MODE == 1
    var_TexCoord = (u_draw.textureMatrix * reflect(normalize(u_draw.modelViewMatrix * vertex),
                                                  u_draw.modelViewMatrix * vec4(attr_Normal, 0.0))).xyz;
#else
    var_TexCoord = (u_draw.textureMatrix * vec4(attr_Normal, 0.0)).xyz;
#endif
    var_Color = stage_color(attr_Color);
    gl_Position = world_to_clip(u_draw.modelMatrix * vertex);
}
