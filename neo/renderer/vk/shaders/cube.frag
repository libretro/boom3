#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 var_TexCoord;
layout(location = 1) in vec4 var_Color;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform samplerCube u_cube0;

void main()
{
    vec4 t = texture(u_cube0, var_TexCoord);
    vec4 c = t * u_draw.glColor * var_Color;
    fragColor = vec4(dhewm3Epilogue(c.rgb), c.a);
}
