#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec4 var_ColorScale;
layout(location = 1) in vec4 var_LumaScale;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_currentRender;

void main()
{
    vec2 screen = gl_FragCoord.xy * u_draw.screenParms.xy * u_draw.screenParms.zw;
    vec4 src = texture(u_currentRender, screen);
    float luma = (src.x + src.y + src.z) * 0.33;
    vec3 c = src.xyz * var_ColorScale.xyz + luma * var_LumaScale.xyz;
    fragColor = vec4(dhewm3Epilogue(c), 1.0);
}
