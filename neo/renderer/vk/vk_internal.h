/*
 * vk_internal: what the vk_backend units share - the device, its entry
 * points, memory, and the per-frame state.  Nothing outside vk/ includes
 * this.
 */
#ifndef VK_INTERNAL_H
#define VK_INTERNAL_H

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "../../sys/libretro-common/include/libretro.h"
#include "../../sys/libretro-common/include/libretro_vulkan.h"
#include "vk_thread.h"
#include "../range_alloc.h"
#include "vk_backend.h"

/* --- the entry points, loaded through the frontend's proc addresses -------- */

#define VKB_INSTANCE_PROCS(X) \
   X(vkGetDeviceProcAddr) \
   X(vkGetPhysicalDeviceProperties) \
   X(vkGetPhysicalDeviceFeatures) \
   X(vkGetPhysicalDeviceMemoryProperties) \
   X(vkGetPhysicalDeviceQueueFamilyProperties) \
   X(vkGetPhysicalDeviceFormatProperties) \
   X(vkEnumerateDeviceExtensionProperties) \
   X(vkCreateDevice)

#define VKB_DEVICE_PROCS(X) \
   X(vkGetDeviceQueue) \
   X(vkDeviceWaitIdle) \
   X(vkQueueSubmit) \
   X(vkQueueWaitIdle) \
   X(vkAllocateMemory) \
   X(vkFreeMemory) \
   X(vkMapMemory) \
   X(vkUnmapMemory) \
   X(vkFlushMappedMemoryRanges) \
   X(vkCreateBuffer) \
   X(vkDestroyBuffer) \
   X(vkGetBufferMemoryRequirements) \
   X(vkBindBufferMemory) \
   X(vkCreateImage) \
   X(vkDestroyImage) \
   X(vkGetImageMemoryRequirements) \
   X(vkBindImageMemory) \
   X(vkCreateImageView) \
   X(vkDestroyImageView) \
   X(vkCreateSampler) \
   X(vkDestroySampler) \
   X(vkCreateShaderModule) \
   X(vkDestroyShaderModule) \
   X(vkCreatePipelineLayout) \
   X(vkDestroyPipelineLayout) \
   X(vkCreateGraphicsPipelines) \
   X(vkDestroyPipeline) \
   X(vkCreatePipelineCache) \
   X(vkDestroyPipelineCache) \
   X(vkCreateRenderPass) \
   X(vkDestroyRenderPass) \
   X(vkCreateFramebuffer) \
   X(vkDestroyFramebuffer) \
   X(vkCreateDescriptorSetLayout) \
   X(vkDestroyDescriptorSetLayout) \
   X(vkCreateDescriptorPool) \
   X(vkDestroyDescriptorPool) \
   X(vkResetDescriptorPool) \
   X(vkAllocateDescriptorSets) \
   X(vkUpdateDescriptorSets) \
   X(vkCreateCommandPool) \
   X(vkDestroyCommandPool) \
   X(vkResetCommandPool) \
   X(vkAllocateCommandBuffers) \
   X(vkFreeCommandBuffers) \
   X(vkBeginCommandBuffer) \
   X(vkEndCommandBuffer) \
   X(vkCreateFence) \
   X(vkDestroyFence) \
   X(vkWaitForFences) \
   X(vkResetFences) \
   X(vkGetFenceStatus) \
   X(vkCreateSemaphore) \
   X(vkDestroySemaphore) \
   X(vkCmdPipelineBarrier) \
   X(vkCmdCopyBufferToImage) \
   X(vkCmdCopyImage) \
   X(vkCmdCopyBuffer) \
   X(vkCmdBlitImage) \
   X(vkCmdBeginRenderPass) \
   X(vkCmdEndRenderPass) \
   X(vkCmdNextSubpass) \
   X(vkCmdBindPipeline) \
   X(vkCmdBindDescriptorSets) \
   X(vkCmdBindVertexBuffers) \
   X(vkCmdBindIndexBuffer) \
   X(vkCmdDrawIndexed) \
   X(vkCmdDraw) \
   X(vkCmdSetViewport) \
   X(vkCmdSetScissor) \
   X(vkCmdSetDepthBias) \
   X(vkCmdSetStencilReference) \
   X(vkCmdSetStencilCompareMask) \
   X(vkCmdSetStencilWriteMask) \
   X(vkCmdClearAttachments) \
   X(vkCmdPushConstants) \
   X(vkCmdExecuteCommands)

typedef struct vkb_procs
{
#define VKB_DECLARE(name) PFN_##name name;
   VKB_INSTANCE_PROCS(VKB_DECLARE)
   VKB_DEVICE_PROCS(VKB_DECLARE)
#undef VKB_DECLARE
} vkb_procs_t;

extern vkb_procs_t vk;

/* --- the device ---------------------------------------------------------- */

#define VKB_FRAMES 3          /* frames in flight, the ring's segments */
#define VKB_MAX_IMAGES 4096
#define VKB_MAX_STATIC_BUFFERS 64
#define VKB_STATIC_BUFFER_BYTES (16u * 1024u * 1024u)
/* a segment starts at a multiple of every vertex stride (base-vertex
 * draws) and of the block alignment: just under 32 MiB */
#define VKB_RING_SEGMENT_BYTES (8738u * 3840u)
#define VKB_STAGING_BYTES (32u * 1024u * 1024u)
#define VKB_DRAW_BLOCK_ALIGN 256
#define VKB_BLOCK_RANGE 1024          /* the largest block a program reads */
#define VKB_TARGET_POST 0x40          /* a pipeline key's target: a colour-only pass */
#define VKB_MAX_TEXTURE_UNITS 6
#define VKB_PIPELINE_SLOTS 1024

/* a resource's piece of a memory block (vk_memory.c) */
typedef struct vkb_mem
{
   VkDeviceMemory memory;
   VkDeviceSize offset;
   VkDeviceSize size;
   unsigned short block;
   unsigned char valid;
} vkb_mem_t;

typedef struct vkb_image
{
   VkImage image;
   VkImageView view;
   vkb_mem_t mem;
   VkSampler sampler;
   unsigned width;
   unsigned height;
   unsigned char levels;
   unsigned char cube;
   unsigned char format;
   unsigned char target;       /* a render target: colour attachment, transfer dst */
   unsigned char layoutReady;  /* transitioned to shader read once */
   unsigned char used;
   VkFramebuffer framebuffer;  /* a target's, for the post passes */
   unsigned char inTransfer;   /* during a flush: already moved to transfer */
   unsigned char uploadPending; /* an upload queued for the next flush */
} vkb_image_t;

/* an upload waiting for a command buffer: its bytes sit in the staging
 * arena */
typedef struct vkb_upload
{
   VkBuffer staging;
   VkDeviceSize stagingOffset;
   unsigned image;
   unsigned bytes;
   unsigned char level;
   unsigned char face;
} vkb_upload_t;

typedef struct vkb_static_buffer
{
   VkBuffer buffer;
   vkb_mem_t mem;
   unsigned char *mapped;      /* when the device lets the CPU at it */
   range_alloc_t ranges;
   unsigned char indexes;
   unsigned char used;
} vkb_static_buffer_t;

/* a block freed while the GPU may still read it: given back after the
 * frames in flight */
typedef struct vkb_deferred_free
{
   unsigned buffer;
   unsigned offset;
   unsigned size;
   unsigned frame;
} vkb_deferred_free_t;

#define VKB_FRAME_CMDS 8          /* command buffers a frame may submit in turn */

typedef struct vkb_frame
{
   VkCommandPool pool;
   VkCommandBuffer cmds[VKB_FRAME_CMDS];
   unsigned numCmds;           /* allocated */
   unsigned cmdUsed;           /* taken this frame */
   VkFence fence;
   VkSemaphore done;           /* signalled when the frame's rendering is done */
   VkDescriptorPool descriptors;
   unsigned ringUsed;
   unsigned char submitted;
} vkb_frame_t;

typedef struct vkb_pipeline_key
{
   unsigned stateBits;
   unsigned char program;
   unsigned char layout;
   unsigned char cull;
   unsigned char stencil;
   unsigned char target;       /* the render pass's format set */
   unsigned char lineMode;
   unsigned char polygonOffset;
   unsigned char mirror;
} vkb_pipeline_key_t;

typedef struct vkb_pipeline_slot
{
   vkb_pipeline_key_t key;
   VkPipeline pipeline;
} vkb_pipeline_slot_t;

typedef struct vkb_render_target
{
   VkImage color;
   VkImageView colorView;
   vkb_mem_t colorMem;
   VkImage depth;
   VkImageView depthView;
   vkb_mem_t depthMem;
   VkFramebuffer framebuffer;
   VkRenderPass renderPass;
   unsigned width;
   unsigned height;
   unsigned char format;       /* enum vkb_format of the colour */
   unsigned char layoutReady;
   unsigned char depthReady;   /* the depth image left its undefined layout */
} vkb_target_t;

typedef struct vkb_state
{
   const struct retro_hw_render_interface_vulkan *iface;
   VkInstance instance;
   VkPhysicalDevice gpu;
   VkDevice device;
   VkQueue queue;
   unsigned queueFamily;
   VkPhysicalDeviceProperties properties;
   VkPhysicalDeviceMemoryProperties memoryProperties;
   VkPhysicalDeviceFeatures features;
   VkPipelineCache pipelineCache;
   VkPipelineLayout pipelineLayout;
   VkDescriptorSetLayout setLayout;
   VkShaderModule modules[VKB_PROGRAM_COUNT][2];
   VkRenderPass renderPass[VKB_FORMAT_COUNT];   /* by colour format */
   VkRenderPass postPass[VKB_FORMAT_COUNT];     /* colour only, for the post chain */
   vkb_target_t postTarget;                     /* the post pass's target image, as a target */
   unsigned postImage;
   VkSampler samplers[VKB_FILTER_COUNT][4][2];   /* filter, wrap, anisotropy */

   /* the frame images handed to the frontend, one per sync index */
   vkb_target_t frameImage[8];
   struct retro_vulkan_image frameInfo[8];
   unsigned frameWidth;
   unsigned frameHeight;
   unsigned char frameFormat;

   /* the scene target for HDR or supersampled frames */
   vkb_target_t scene;

   vkb_frame_t frames[VKB_FRAMES];
   unsigned frameIndex;         /* the slot the frame records into */
   unsigned allocIndex;         /* the slot the engine's front end writes into: the next frame's */
   unsigned frameCount;
   unsigned syncIndex;          /* the frontend's, this frame */
   VkSemaphore unpresented;     /* handed to set_image, not yet shown: the next submit waits it */

   /* the ring: one host-visible buffer, VKB_FRAMES segments */
   VkBuffer ring;
   VkDeviceMemory ringMemory;
   unsigned char *ringMapped;
   unsigned ringCoherent;

   vkb_static_buffer_t statics[VKB_MAX_STATIC_BUFFERS];
   vkb_deferred_free_t *deferred;
   unsigned deferredCount;
   unsigned deferredCapacity;

   vkb_image_t images[VKB_MAX_IMAGES];
   unsigned whiteImage;         /* what an unbound unit samples */

   vkb_upload_t *uploads;
   unsigned uploadCount;
   unsigned uploadCapacity;

   /* the transfer submit between frames */
   VkCommandPool uploadPool;
   VkCommandBuffer uploadCmd;
   VkFence uploadFence;

   vkb_pipeline_slot_t pipelines[VKB_PIPELINE_SLOTS];
   unsigned pipelineCount;

   /* the frame being recorded */
   VkCommandBuffer cmd;
   vkb_target_t *target;
   unsigned char targetKind;    /* enum vkb_target, or VKB_TARGET_POST */
   unsigned char frameComposited; /* the post chain drew the frame image */
   unsigned char inRenderPass;
   unsigned char frameOpen;
   unsigned char drawn;
   VkPipeline boundPipeline;
   VkDescriptorSet boundSet;
   unsigned boundImages[VKB_MAX_TEXTURE_UNITS];
   unsigned boundSamplers[VKB_MAX_TEXTURE_UNITS];
   unsigned boundVertexBuffer;
   unsigned boundIndexBuffer;
   unsigned boundIndexShort;
   unsigned viewOffset;         /* the view block's offset in the ring */
   unsigned stencilRef;
   unsigned stencilMask;
   float depthBias[2];
   int viewport[4];
   int scissor[4];
   float depthRange[2];

   /* the composite */
   unsigned char compositeProgram;
   float compositeParms[4][4];
   unsigned compositeLut;

   char deviceName[256];
   unsigned char ready;
   unsigned char hasBC;
} vkb_state_t;

extern vkb_state_t vkb;

/* vk_device.c */
int  vkb_memory_type(unsigned bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid);
int  vkb_alloc_memory(const VkMemoryRequirements *req, VkMemoryPropertyFlags want,
      VkDeviceMemory *memory, void **mapped);

/* vk_memory.c */
int  vkb_memory_init(void);
void vkb_memory_shutdown(void);
int  vkb_mem_alloc(const VkMemoryRequirements *req, VkMemoryPropertyFlags want, int linear,
      vkb_mem_t *out, void **mapped);
void vkb_mem_free(const vkb_mem_t *m);
int  vkb_mem_coherent(const vkb_mem_t *m);
void vkb_mem_flush(const vkb_mem_t *m, VkDeviceSize offset, VkDeviceSize bytes);
unsigned char *vkb_staging_alloc(size_t bytes, VkBuffer *buffer, VkDeviceSize *offset);
void vkb_staging_flush_memory(void);
void vkb_staging_retire(int immediate);
void vkb_staging_frame_done(void);
size_t vkb_staging_pending(void);
int  vkb_staging_should_flush(void);
void vkb_transfer_now(void);

/* vk_image.c */
VkFormat vkb_vk_format(enum vkb_format format);
VkRenderPass vkb_post_render_pass(enum vkb_format colorFormat);
int  vkb_image_post_framebuffer(unsigned image, VkFramebuffer *framebuffer, vkb_image_t **im);
VkFormat vkb_depth_stencil_format(void);
int  vkb_images_init(void);
void vkb_images_shutdown(void);
void vkb_images_flush_uploads(VkCommandBuffer cmd);
void vkb_images_flush_uploads_locked(VkCommandBuffer cmd);
void vkb_images_frame_done(void);
VkSampler vkb_sampler(enum vkb_filter filter, enum vkb_wrap wrap, int anisotropy, int mips);
int  vkb_target_create(vkb_target_t *t, unsigned width, unsigned height, enum vkb_format format,
      int frontend);
void vkb_target_destroy(vkb_target_t *t);
void vkb_target_view_info(const vkb_target_t *t, VkImageViewCreateInfo *info);
/* the image ids that wrap the scene target's colour and depth */
unsigned vkb_image_scene_depth_ref(void);
const vkb_image_t *vkb_image_get(unsigned image);

/* vk_buffer.c */
int  vkb_buffers_init(void);
void vkb_buffers_shutdown(void);
void vkb_buffers_frame_begin(unsigned frameIndex);
void vkb_buffers_flush(VkCommandBuffer cmd);
void vkb_buffers_flush_locked(VkCommandBuffer cmd);
int  vkb_buffers_pending(void);
VkBuffer vkb_static_vk_buffer(unsigned buffer);

/* vk_device.c: the render pass on the current target */
void vkb_render_pass_begin(void);
void vkb_render_pass_end(void);

/* vk_draw.c */
/* around a copy out of the target: the pass closes and reopens with the
 * viewport and scissor kept */
void vkb_leave_render_pass(void);
void vkb_enter_render_pass(void);

/* vk_pipeline.c */
int  vkb_pipelines_init(void);
void vkb_pipelines_shutdown(void);
VkPipeline vkb_pipeline(const vkb_pipeline_key_t *key);
VkRenderPass vkb_render_pass(enum vkb_format colorFormat, int frontend);

#endif
