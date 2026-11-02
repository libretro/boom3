// The shadow volume build's resources: R_CreateVertexProgramTurboShadowVolume's
// indexes, written by compute into the frame's index buffer.

layout(std430, set = 0, binding = 0) readonly buffer ShadowCache { vec4 sc[]; };   // vertex v at 2v: w 1, then w 0
layout(std430, set = 0, binding = 1) readonly buffer Indexes { uint ix[]; };
layout(std430, set = 0, binding = 2) readonly buffer SilEdges { uvec4 sil[]; };   // v1 v2 p1 p2, p2 ~0 when dangling
layout(std430, set = 0, binding = 3) buffer Facing { uint facing[]; };
layout(std430, set = 0, binding = 4) writeonly buffer Out { uint outIndex[]; };
// the light, planes and masks as float bits, then the sil and cap draws' indexed indirect commands
layout(std430, set = 0, binding = 5) buffer Parms { uint parms[]; };

layout(push_constant) uniform Volume {
    uint vertexBase;    // vec4s
    uint indexBase;     // indexes
    uint shortIndexes;
    uint numFaces;
    uint numEdges;
    uint silBase;       // edges
    uint facingBase;
    uint parmBase;      // uints
} pc;

// floats: the light 0-3, planes 0-3 x y z d 4-19, planes 4-5 in lanes 0-1 20-35; masks 36-37
#define PARM_SIL_DRAW (pc.parmBase + 40u)
#define PARM_CAP_DRAW (pc.parmBase + 45u)
