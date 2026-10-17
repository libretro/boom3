// What every program shares: the descriptor set, the vertex attribute
// locations, the per-view and per-draw blocks (vk_backend.h's
// vkb_view_block_t and vkb_draw_block_t, std140), the HDR epilogue and
// the clip-space conversion.
//
// The engine's projection is OpenGL's: clip z in -w..w, y up.  The
// backend draws with a flipped viewport for the y, and engine_clip() moves
// z into Vulkan's 0..w so the depth buffer holds what it holds under GL.

#define ATTR_VERTEX    0
#define ATTR_COLOR     1
#define ATTR_TEXCOORD  2
#define ATTR_NORMAL    3
#define ATTR_TANGENT   4
#define ATTR_BITANGENT 5

layout(set = 0, binding = 0, std140) uniform View
{
    mat4 viewProjection;
    mat4 projection;
    mat4 view;
    vec4 viewport;        // x, y, 1/w, 1/h
} u_view;

layout(set = 0, binding = 1, std140) uniform Draw
{
    mat4 modelMatrix;
    mat4 modelViewMatrix;
    mat4 textureMatrix;
    mat4 lightProjection;
    vec4 glColor;
    vec4 colorModulateAdd;  // x modulate, y add, z depth hack, w alpha test
    vec4 hdrParms;          // x brightness with the encoding fold, y clamp mode, w 1/gamma
    vec4 localViewOrigin;
    vec4 localLightOrigin;
    vec4 bumpMatrixS;
    vec4 bumpMatrixT;
    vec4 diffuseMatrixS;
    vec4 diffuseMatrixT;
    vec4 specularMatrixS;
    vec4 specularMatrixT;
    vec4 diffuseColor;
    vec4 specularColor;
    vec4 fogColor;
    vec4 clipPlane;
    vec4 screenParms;
    vec4 vertexParms[4];
} u_draw;

#define TEX_BINDING 2

vec4 engine_clip(vec4 pos)
{
    pos.z = 0.5 * pos.z + 0.5 * pos.w;
    return pos;
}

// the model and weapon depth hacks, applied in the engine's clip space
vec4 world_to_clip(vec4 worldPos)
{
    vec4 pos = u_view.viewProjection * worldPos;
    pos.z += u_draw.colorModulateAdd.z * worldPos.w;
    return engine_clip(pos);
}

// the vertex colour as the stage reads it: ignored, modulated, inverse
vec4 stage_color(vec4 attrColor)
{
    if (u_draw.colorModulateAdd.x == 0.0)
        return vec4(u_draw.colorModulateAdd.y);
    return attrColor * u_draw.colorModulateAdd.x + vec4(u_draw.colorModulateAdd.y);
}

// the HDR / gamma epilogue the ARB2 path injects into its stages
vec3 dhewm3Epilogue(vec3 c)
{
    c *= u_draw.hdrParms.x;
    if (u_draw.hdrParms.y > 1.5) {
        c = max(c, 0.0);
    } else if (u_draw.hdrParms.y > 0.5) {
        c = max(c, 0.0);
        float L = dot(c, vec3(0.2126, 0.7152, 0.0722));
        float peak = max(c.r, max(c.g, c.b));
        float t = clamp((peak - 1.0) / max(peak - L, 0.0001), 0.0, 1.0);
        c = clamp(mix(c, vec3(L), t), 0.0, 1.0);
    } else {
        c = clamp(c, 0.0, 1.0);
    }
    if (u_draw.hdrParms.w == 1.0)
        return c;
    return pow(c, vec3(u_draw.hdrParms.w));
}
