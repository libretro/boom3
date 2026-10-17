#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// GUI: the texel through the output transform's inverse (map1, one row)
layout(location = 0) in vec2 var_TexCoord;
layout(location = 1) in vec4 var_Color;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_map0;
#if GUI
layout(set = 0, binding = TEX_BINDING + 1) uniform sampler2D u_map1;
#endif

void main()
{
    vec4 t = texture(u_map0, var_TexCoord);
#if GUI
    t.r = texture(u_map1, vec2(t.r, 0.5)).r;
    t.g = texture(u_map1, vec2(t.g, 0.5)).r;
    t.b = texture(u_map1, vec2(t.b, 0.5)).r;
#endif
    vec4 c = t * u_draw.glColor * var_Color;
    fragColor = vec4(dhewm3Epilogue(c.rgb), c.a);
}
