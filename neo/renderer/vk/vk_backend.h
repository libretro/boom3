/*
 * vk_backend: the Vulkan renderer's device side, in C.
 *
 * The engine's backend (draw_vulkan.cpp) describes a frame to this layer
 * in its own terms - images, vertex blocks, a program and the state bits
 * of each draw - and this layer owns everything Vulkan: the device the
 * core negotiated with the frontend, memory, images and their uploads,
 * the buffers the vertex cache streams through, the pipelines, descriptor
 * sets and command buffers, and the frame's handover to the frontend
 * through the libretro Vulkan interface.
 *
 * Threads: every call is made from the thread that owns the frame
 * (vkb_begin_frame to vkb_end_frame).  Nothing here reads the engine's
 * globals.
 */
#ifndef VK_BACKEND_H
#define VK_BACKEND_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct retro_hw_render_interface_vulkan;
struct retro_hw_render_context_negotiation_interface_vulkan;

/* --- the device ---------------------------------------------------------- */

/* The negotiation interface the core hands the frontend before the
 * device exists: it creates the instance and device with the features
 * the renderer wants. */
const struct retro_hw_render_context_negotiation_interface_vulkan *vkb_negotiation(void);

/* Up with the frontend's interface (context_reset), down before the
 * device goes (context_destroy).  width/height are the frame image's. */
int  vkb_init(const struct retro_hw_render_interface_vulkan *iface,
      unsigned width, unsigned height);
void vkb_shutdown(void);
int  vkb_ready(void);

/* the device's limits, for the engine's glConfig */
unsigned vkb_max_texture_size(void);
int  vkb_has_bc(void);     /* BC1/BC3/BC5 textures */
int  vkb_has_anisotropy(void);
const char *vkb_device_name(void);

/* --- images -------------------------------------------------------------- */

enum vkb_format
{
   VKB_FORMAT_RGBA8 = 0,
   VKB_FORMAT_BGRA8,         /* a DDS's 32-bit texels as they are */
   VKB_FORMAT_A8,            /* one byte, read as alpha with white colour */
   VKB_FORMAT_I16,           /* 16-bit intensity: read on every channel */
   VKB_FORMAT_LA16,          /* 16-bit luminance and alpha */
   VKB_FORMAT_BC1,
   VKB_FORMAT_BC2,
   VKB_FORMAT_BC3,
   VKB_FORMAT_BC7,
   VKB_FORMAT_RGBA16F,
   VKB_FORMAT_RGB10A2,
   VKB_FORMAT_RGBA32F,
   VKB_FORMAT_RGBA16,        /* 16-bit normalised: the tone curves' tables */
   VKB_FORMAT_DEPTH,         /* the scene's depth-stencil format */
   VKB_FORMAT_RG32F,         /* two floats: the half resolution particles' depth range */
   VKB_FORMAT_COUNT
};

enum vkb_filter
{
   VKB_FILTER_LINEAR = 0,    /* trilinear with mips, else linear */
   VKB_FILTER_NEAREST,
   VKB_FILTER_LINEAR_NOMIP,  /* linear, mip 0 only */
   VKB_FILTER_LINEAR_MIP_NEAREST,   /* bilinear in the nearest mip */
   VKB_FILTER_COUNT
};

enum vkb_wrap
{
   VKB_WRAP_REPEAT = 0,
   VKB_WRAP_CLAMP,
   VKB_WRAP_CLAMP_TO_ZERO,   /* a black border */
   VKB_WRAP_CLAMP_TO_ZERO_ALPHA
};

/* A texture: 2D or cube, with its mip chain.  An id of 0 is no image.
 * Creation and destruction are free-threaded; uploads are queued and
 * take effect on the frame after they are queued, so a texture loaded
 * on a worker thread reaches the GPU without the frame waiting. */
unsigned vkb_image_create(unsigned width, unsigned height, int levels, int cube,
      enum vkb_format format, enum vkb_filter filter, enum vkb_wrap wrap, int anisotropy);
/* face is 0 for a 2D image, 0..5 for a cube; the data is copied here */
int  vkb_image_upload(unsigned image, int level, int face, const void *data, size_t bytes);
/* a texture the frame renders into or copies from: _currentRender, _currentDepth */
unsigned vkb_image_create_target(unsigned width, unsigned height, enum vkb_format format);
/* the scene image, as a texture the post passes sample; 0 when the frame
 * draws straight into the frontend's image */
unsigned vkb_image_scene_ref(void);
/* The open target's depth, sampled in place by soft particles: ready says
 * whether the target can do it, begin reopens the pass with the depth
 * read-only (until a write to depth or stencil, or the pass's end), and
 * the image reference samples it there. */
int vkb_depth_read_ready(void);
int vkb_depth_read_begin(void);
unsigned vkb_image_target_depth_ref(void);
void vkb_target_extent(unsigned *width, unsigned *height);
/* The scene's depth while a side target draws (vkb_side_begin): sampled
 * in the read-only layout the side passes keep it in. */
unsigned vkb_image_scene_depth_ref(void);
void vkb_image_destroy(unsigned image);
void vkb_image_filter(unsigned image, enum vkb_filter filter, enum vkb_wrap wrap, int anisotropy);

/* --- buffers ------------------------------------------------------------- */

/* Static vertex and index data lives in a few large device-local
 * buffers; a block is named by buffer and offset, uploaded once.  Free
 * returns it after the frames in flight are done with it. */
int  vkb_static_alloc(size_t bytes, int indexes, unsigned *buffer, unsigned *offset);
void vkb_static_upload(unsigned buffer, unsigned offset, const void *data, size_t bytes);
void vkb_static_free(unsigned buffer, unsigned offset, size_t bytes);

/* The frame's ring: host-visible memory the GPU reads as written.  The
 * front end's allocations go to the segment of the frame it is building;
 * the buffer/offset name it for a draw.  NULL when the segment is full.
 * vkb_ring_alloc_frame is the backend's, during a frame's recording. */
void *vkb_ring_alloc(size_t bytes, size_t align, unsigned *buffer, unsigned *offset);
void *vkb_ring_alloc_frame(size_t bytes, size_t align, unsigned *buffer, unsigned *offset);

/* Before a frame's commands run: the frame takes the slot the front end
 * wrote into, and the front end moves on to the next, which waits for
 * the GPU to be done with it.  On the engine's thread. */
void vkb_frame_handoff(void);

/* --- the frame ----------------------------------------------------------- */

/* The frame's render target, chosen at begin: the frontend's image
 * itself (zero copies) or the scene image the composite reads. */
enum vkb_target
{
   VKB_TARGET_FRAME = 0,     /* the image handed to the frontend */
   VKB_TARGET_SCENE          /* the scene image: HDR or supersampled */
};

/* Waits for the GPU to be done with this frame's slot, resets its pools,
 * runs queued uploads, and opens the render pass on the target of the
 * given size (the frame image is width x height from vkb_init; a scene
 * target takes any size). */
int  vkb_begin_frame(enum vkb_target target, unsigned width, unsigned height,
      enum vkb_format sceneFormat);
/* Ends the render pass, runs the composite pass when the scene target was
 * drawn (compositeProgram with its parms), submits, and hands the frame
 * image to the frontend.  Returns 0 when nothing was drawn. */
int  vkb_end_frame(void);
/* submits what the frame has recorded so far, and records on: after the
 * 3D view, so the GPU draws it while the 2D views are recorded */
void vkb_flush(void);
/* after the frontend was given the frame (video_cb) */
void vkb_presented(void);
/* the frame image's format: the frontend reads it as it is, so a PQ
 * frame is 10-bit.  A change remakes the frame images. */
int  vkb_frame_format(enum vkb_format format);

/* The post chain, after the scene and before the frame image: passes
 * on target images (vkb_image_create_target), or on the frame image
 * itself (target 0), each drawn with fullscreen triangles.  Beginning a
 * pass ends the one before it, so its image can be sampled. */
int  vkb_post_begin(unsigned target, unsigned width, unsigned height);
void vkb_post_end(void);
/* The frame image read and written in one pass: the draw's inputImage is
 * the frame image (vkb_image_scene_ref when the scene was drawn into
 * it), each fragment reading its own pixel through the input attachment
 * and writing it back.  The display transform without a second image. */
int  vkb_post_begin_inplace(void);
/* whether this frame's scene was drawn into the frame image itself */
int  vkb_scene_in_frame(void);

/* clears, scissored to the current scissor: colour, depth and stencil to
 * the given value */
void vkb_clear(int color, int depth, int stencil, const float rgba[4], unsigned stencilValue);

/* viewport and scissor in window pixels, origin bottom-left as the
 * engine has them; the backend flips */
void vkb_viewport(int x, int y, int w, int h);
void vkb_scissor(int x, int y, int w, int h);
/* the weapon's halved depth range */
void vkb_depth_range(float near, float far);

/* --- programs and draws --------------------------------------------------- */

enum vkb_program
{
   VKB_PROGRAM_ZFILL_OPAQUE = 0, /* depth fill, the colour alone */
   VKB_PROGRAM_ZFILL,         /* with the stage's alpha test */
   VKB_PROGRAM_ZFILL_CLIP,    /* and the subview clip texture */
   VKB_PROGRAM_DIFFUSE,       /* a map, modulated: the old-style stages and the GUI */
   VKB_PROGRAM_DIFFUSE_GUI,   /* with the GUI's output-transform inverse */
   VKB_PROGRAM_SKYBOX,        /* a cube map by the view direction */
   VKB_PROGRAM_REFLECT,       /* a cube map by the reflection vector */
   VKB_PROGRAM_DIFFUSE_CUBE,  /* a cube map by the normal */
   VKB_PROGRAM_BLIT,          /* the composite's plain copy, scaled */
   VKB_PROGRAM_INTERACTION,   /* a light on a surface, Blinn-Phong */
   VKB_PROGRAM_INTERACTION_PHONG, /* with the Phong specular, exponent in vertexParms[0].x */
   VKB_PROGRAM_SHADOW,        /* a shadow volume into the stencil */
   VKB_PROGRAM_FOG,           /* a fog light */
   VKB_PROGRAM_BLEND_LIGHT,   /* a blend light */
   VKB_PROGRAM_SOFT_PARTICLE, /* a particle faded against _currentDepth */
   VKB_PROGRAM_HEAT_HAZE,     /* heatHaze.vfp */
   VKB_PROGRAM_HEAT_HAZE_MASK,
   VKB_PROGRAM_HEAT_HAZE_MASK_VERTEX,
   VKB_PROGRAM_COLOR_PROCESS, /* colorProcess.vfp */
   VKB_PROGRAM_BUMPY_ENVIRONMENT, /* bumpyEnvironment.vfp */
   VKB_PROGRAM_HDR_BRIGHT,    /* the bloom's bright pass */
   VKB_PROGRAM_HDR_DOWN,      /* the convolution pyramid down, plain and Karis-weighted */
   VKB_PROGRAM_HDR_DOWN_KARIS,
   VKB_PROGRAM_HDR_UP,        /* the pyramid up, additive */
   VKB_PROGRAM_HDR_BLUR,      /* the two-band blur */
   VKB_PROGRAM_HDR_COMPOSITE, /* the display transform: its block is vkb_hdr_block_t */
   VKB_PROGRAM_HDR_COMPOSITE_INPLACE, /* the same on the frame image itself, through the input attachment */
   VKB_PROGRAM_LOWRES_DOWN,   /* half resolution particles: a 2x2's farthest depth and its distance range */
   VKB_PROGRAM_LOWRES_MARK,   /* where the range breaks, into the stencil */
   VKB_PROGRAM_LOWRES_COMPOSITE, /* colour plus the scene times alpha */
   VKB_PROGRAM_SOFT_PARTICLE_KEEP, /* VKB_PROGRAM_SOFT_PARTICLE dropping no fragment */
   VKB_PROGRAM_COUNT
};

/* what the vertex shaders read: idDrawVert, or the shadow volume's vec4 */
enum vkb_vertex_layout
{
   VKB_VERTEX_DRAWVERT = 0,
   VKB_VERTEX_SHADOW
};

enum vkb_cull
{
   VKB_CULL_NONE = 0,
   VKB_CULL_BACK,     /* CT_FRONT_SIDED */
   VKB_CULL_FRONT     /* CT_BACK_SIDED */
};

/* the stencil ops a pipeline carries; the reference and masks are set
 * per draw */
enum vkb_stencil
{
   VKB_STENCIL_OFF = 0,
   VKB_STENCIL_TEST,          /* keep; passes where the stencil is at most the reference */
   VKB_STENCIL_SHADOW_PASS,   /* depth-pass volumes: GL's back faces incr, front decr on pass */
   VKB_STENCIL_SHADOW_FAIL,   /* depth-fail volumes: GL's back faces decr, front incr on depth fail */
   VKB_STENCIL_MARK,          /* the reference written wherever the draw passes */
   VKB_STENCIL_MARKED,        /* passes where the stencil is the reference */
   VKB_STENCIL_UNMARKED,      /* passes where it is not */
   VKB_STENCIL_COUNT
};

/* The per-draw constants every program reads, std140.  The backend fills
 * what the draw needs; the layout is the shaders'. */
typedef struct vkb_draw_block
{
   float modelMatrix[16];
   float modelViewMatrix[16];
   float textureMatrix[16];
   float lightProjection[16];
   float glColor[4];
   float colorModulateAdd[4];   /* x modulate, y add, z depth hack, w alpha test */
   float hdrParms[4];
   float localViewOrigin[4];
   float localLightOrigin[4];
   float bumpMatrixS[4];
   float bumpMatrixT[4];
   float diffuseMatrixS[4];
   float diffuseMatrixT[4];
   float specularMatrixS[4];
   float specularMatrixT[4];
   float diffuseColor[4];
   float specularColor[4];
   float fogColor[4];
   float clipPlane[4];
   float screenParms[4];
   float vertexParms[4][4];
} vkb_draw_block_t;

/* per view, std140 */
typedef struct vkb_view_block
{
   float viewProjection[16];
   float projection[16];
   float view[16];
   float viewport[4];           /* x, y, 1/w, 1/h */
} vkb_view_block_t;

/* A draw's whole description: the pipeline keys, its constants, its
 * textures and its geometry.  The backend resolves the pipeline, writes
 * the block into the ring, binds the descriptor set and records the
 * draw. */
typedef struct vkb_draw
{
   const void *block;           /* vkb_draw_block_t, or a program's own block */
   unsigned blockBytes;         /* 0: sizeof(vkb_draw_block_t) */
   unsigned image[6];           /* the units the program samples; 0 unbound */
   unsigned inputImage;         /* the image read as the input attachment: the in-place pass's own target */
   unsigned vertexBuffer;
   unsigned vertexOffset;       /* bytes; the base vertex is derived from the stride */
   unsigned indexBuffer;
   unsigned indexOffset;        /* bytes */
   unsigned indexCount;
   unsigned stateBits;          /* the engine's GLS_ bits */
   unsigned stencilRef;
   unsigned stencilMask;
   float depthBias[2];          /* polygon offset: units, slope factor */
   float depthMin;              /* the depth bounds test's lower bound: a nearer stored depth drops the pixel; 0 off */
   unsigned char program;       /* enum vkb_program */
   unsigned char layout;        /* enum vkb_vertex_layout */
   unsigned char cull;          /* enum vkb_cull */
   unsigned char stencil;       /* enum vkb_stencil */
   unsigned char shortIndexes;  /* 16-bit indexes, else 32 */
   unsigned char lineMode;
   unsigned char mirror;        /* the view is mirrored: the shadow ops swap faces */
   unsigned char shadowCaps;    /* with shadowVolume: the caps as well as the sil planes */
   unsigned char transmit;      /* alpha keeps what is left of the destination: the colour's
                                   destination factor applied to it alone, written whatever
                                   the alpha mask (the half resolution particles' target) */
   unsigned shadowVolume;       /* a volume vkb_shadow_queue built: its indexes and counts replace the draw's */
} vkb_draw_t;

void vkb_set_view(const vkb_view_block_t *view);
void vkb_draw(const vkb_draw_t *draw);

/* The pass a draw runs in, for warming its pipeline. */
enum vkb_pass
{
   VKB_PASS_SCENE = 0,  /* the scene or frame image, depth attached */
   VKB_PASS_POST,       /* a colour-only post pass (vkb_post_begin) */
   VKB_PASS_INPLACE     /* the frame image read and written in place */
};

/* Builds the pipeline a draw would bind, ahead of the draw: the draw
 * filled as for vkb_draw, on a colour target of the given format, in
 * the given pass.  Nothing is drawn.  Every pipeline a frame binds is
 * meant to have been warmed at init or as its material loaded;
 * vkb_pipeline_misses counts the ones that were not, and is meant to
 * read 0. */
void vkb_warm(const vkb_draw_t *draw, enum vkb_format format, enum vkb_pass pass);
unsigned vkb_pipeline_misses(void);
/* the sides' discipline (vk_internal.h): a thread that touched what the
 * other owned, and render side uploads the reserved staging could not
 * take; both meant to stay 0 */
unsigned vkb_owner_violations(void);
unsigned vkb_upload_spills(void);
unsigned vkb_pipeline_count(void);
/* a post pass's fullscreen triangle: no geometry, the block and textures */
void vkb_draw_fullscreen(const vkb_draw_t *draw);

/* --- shadow volumes built on the GPU ------------------------------------- */

/* What R_CreateVertexProgramTurboShadowVolume would build from a surface,
 * built by compute at the start of the frame it is drawn in from the
 * CPU's face planes: the same indexes, in no particular order, which the
 * stencil counts the same. */
typedef struct vkb_shadow_volume
{
   unsigned vertexBuffer;       /* the shadow cache: per vertex w 1, then w 0 */
   unsigned vertexOffset;       /* bytes */
   unsigned indexBuffer;        /* the surface's triangles */
   unsigned indexOffset;        /* bytes */
   unsigned numIndexes;
   unsigned silBuffer;          /* per sil edge v1 v2 p1 p2, p2 0xffffffff when dangling */
   unsigned silOffset;          /* bytes */
   unsigned numSilEdges;
   unsigned planeBuffer;        /* the CPU's face planes, an idPlane per triangle */
   unsigned planeOffset;        /* bytes */
   unsigned cullPlanes;         /* bit i: cull against clipPlanes[i] */
   float lightOrigin[3];        /* facing's, in surface space */
   float clipPlanes[6][4];      /* the light frustum, in surface space */
   unsigned char shortIndexes;
   unsigned char dotOrder;      /* R_ShadowDotOrder */
} vkb_shadow_volume_t;

/* whether the device builds them */
int  vkb_shadows_available(void);
/* For the frame the front end is writing (before vkb_frame_handoff): the
 * volume named by key is built at that frame's start, once however often
 * it is queued.  0 when it cannot be built. */
unsigned vkb_shadow_queue(const void *key, const vkb_shadow_volume_t *volume);
/* During the frame's recording: the handle for vkb_draw's shadowVolume,
 * 0 when the volume was not queued for this frame. */
unsigned vkb_shadow_find(const void *key);

/* the frame's scene copies for the stages that read them */
void vkb_copy_color(unsigned image, int x, int y, int w, int h);
void vkb_copy_depth(unsigned image, int x, int y, int w, int h);
/* vkb_copy_color of the part px, py, pw, ph of that rectangle (from its
 * corner) alone, into an image that holds the rest; all of it into one
 * that holds nothing yet */
void vkb_copy_color_part(unsigned image, int x, int y, int w, int h, int px, int py, int pw, int ph);

/* A side target: a colour image and a depth image of one size, both from
 * vkb_image_create_target (the depth VKB_FORMAT_DEPTH), drawn with the
 * scene's pipelines.  The scene's pass closes, its depth goes read-only
 * for vkb_image_scene_depth_ref, and a pass on these opens with the
 * viewport and scissor on all of them; another begin moves to another
 * pair.  End reopens the scene's pass, its depth writable again, with the
 * viewport and scissor on all of it. */
int  vkb_side_begin(unsigned color, unsigned depth);
void vkb_side_end(void);

/* the composite: the scene image into the frame image with the given
 * program, its parms, and a LUT image when the program takes one */
void vkb_set_composite(enum vkb_program program, const float parms[4][4], unsigned lut);

#ifdef __cplusplus
}
#endif

#endif
