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
   VKB_FORMAT_DEPTH,         /* the scene's depth-stencil format */
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
 * pointer is written until vkb_end_frame; the buffer/offset name it for
 * a draw.  NULL when the frame's segment is full. */
void *vkb_ring_alloc(size_t bytes, size_t align, unsigned *buffer, unsigned *offset);

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
   VKB_STENCIL_TEST,          /* keep, test against the reference */
   VKB_STENCIL_SHADOW_PASS,   /* depth-pass volumes: front incr, back decr on pass */
   VKB_STENCIL_SHADOW_FAIL,   /* depth-fail volumes: front decr, back incr on fail */
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
   const vkb_draw_block_t *block;
   unsigned image[6];           /* the units the program samples; 0 unbound */
   unsigned vertexBuffer;
   unsigned vertexOffset;       /* bytes; the base vertex is derived from the stride */
   unsigned indexBuffer;
   unsigned indexOffset;        /* bytes */
   unsigned indexCount;
   unsigned stateBits;          /* the engine's GLS_ bits */
   unsigned stencilRef;
   unsigned stencilMask;
   unsigned char program;       /* enum vkb_program */
   unsigned char layout;        /* enum vkb_vertex_layout */
   unsigned char cull;          /* enum vkb_cull */
   unsigned char stencil;       /* enum vkb_stencil */
   unsigned char shortIndexes;  /* 16-bit indexes, else 32 */
   unsigned char polygonOffset; /* the stage's polygon offset, as units */
   unsigned char lineMode;
   unsigned char mirror;        /* the view is mirrored: front and back swap */
} vkb_draw_t;

void vkb_set_view(const vkb_view_block_t *view);
void vkb_draw(const vkb_draw_t *draw);

/* the frame's scene copies for the stages that read them */
void vkb_copy_color(unsigned image, int x, int y, int w, int h);
void vkb_copy_depth(unsigned image, int x, int y, int w, int h);

/* the composite: the scene image into the frame image with the given
 * program, its parms, and a LUT image when the program takes one */
void vkb_set_composite(enum vkb_program program, const float parms[4][4], unsigned lut);

#ifdef __cplusplus
}
#endif

#endif
