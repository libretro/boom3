#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// a particle faded where it nears the scene's depth (#3878):
// vertexParms[0] the radius, 1/fade range and 1/radius; vertexParms[1]
// the channels the fade applies to; screenParms.xy 1/depth image size

layout(location = 0) in vec2 var_TexCoord;
layout(location = 1) in vec4 var_Color;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = TEX_BINDING + 0) uniform sampler2D u_map0;
layout(set = 0, binding = TEX_BINDING + 1) uniform sampler2D u_depth;

void main()
{
    const vec2 depth_consts = vec2(0.33333333, -0.33316667);
    float scene_depth = min(texture(u_depth, gl_FragCoord.xy * u_draw.screenParms.xy).r, 0.9994);
    scene_depth = 1.0 / (scene_depth * depth_consts.x + depth_consts.y);
    float particle_depth = 1.0 / (gl_FragCoord.z * depth_consts.x + depth_consts.y);
    float fade = clamp((particle_depth - scene_depth + u_draw.vertexParms[0].x) * u_draw.vertexParms[0].y, 0.0, 1.0);
    float near_fade = clamp(particle_depth * -u_draw.vertexParms[0].z, 0.0, 1.0);
    vec4 channels = clamp(vec4(fade * near_fade) + u_draw.vertexParms[1], 0.0, 1.0);
    vec4 c = texture(u_map0, var_TexCoord) * channels * u_draw.glColor * var_Color;
    fragColor = vec4(c.rgb * u_draw.hdrParms.x, c.a);
}
