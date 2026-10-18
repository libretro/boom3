/*
 * vk_device: the device the core negotiates with the frontend, its memory,
 * and the frame cycle - begin, draws, end and the handover of the frame
 * image through the libretro Vulkan interface.
 */
#include <string.h>

#include "vk_internal.h"

vkb_procs_t vk;
vkb_state_t vkb;

static int vkb_frame_images_make(enum vkb_format format);

/* --- negotiation ----------------------------------------------------------- */

static VkApplicationInfo appInfo;
static PFN_vkGetInstanceProcAddr getInstanceProcAddr;

static const VkApplicationInfo *vkb_application_info(void)
{
   memset(&appInfo, 0, sizeof(appInfo));
   appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
   appInfo.pApplicationName = "boom3";
   appInfo.pEngineName      = "idTech4";
   appInfo.apiVersion       = VK_API_VERSION_1_1;
   return &appInfo;
}

static int vkb_load_instance_procs(VkInstance instance)
{
#define VKB_LOAD(name) \
   vk.name = (PFN_##name)getInstanceProcAddr(instance, #name); \
   if (!vk.name) return 0;
   VKB_INSTANCE_PROCS(VKB_LOAD)
#undef VKB_LOAD
   return 1;
}

static int vkb_load_device_procs(VkDevice device)
{
#define VKB_LOAD(name) \
   vk.name = (PFN_##name)vk.vkGetDeviceProcAddr(device, #name); \
   if (!vk.name) return 0;
   VKB_DEVICE_PROCS(VKB_LOAD)
#undef VKB_LOAD
   return 1;
}

/* The device: one graphics queue, the features the renderer uses when
 * the GPU has them.  The frontend's wrapper makes the device so it can
 * add what it needs. */
static bool vkb_create_device2(struct retro_vulkan_context *context, VkInstance instance,
      VkPhysicalDevice gpu, VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr gipa,
      retro_vulkan_create_device_wrapper_t wrapper, void *opaque)
{
   VkQueueFamilyProperties families[32];
   uint32_t numFamilies = 32, family = 0, i;
   VkPhysicalDeviceFeatures have, want;
   VkDeviceQueueCreateInfo qci;
   VkDeviceCreateInfo dci;
   VkDevice device;
   float priority = 1.0f;
   (void)surface;

   getInstanceProcAddr = gipa;
   if (!vkb_load_instance_procs(instance))
      return false;

   vk.vkGetPhysicalDeviceQueueFamilyProperties(gpu, &numFamilies, families);
   for (i = 0; i < numFamilies; i++)
      if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
      {
         family = i;
         break;
      }
   if (i == numFamilies)
   {
      for (i = 0; i < numFamilies; i++)
         if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
         {
            family = i;
            break;
         }
      if (i == numFamilies)
         return false;
   }

   vk.vkGetPhysicalDeviceFeatures(gpu, &have);
   memset(&want, 0, sizeof(want));
   want.depthClamp           = have.depthClamp;
   want.fillModeNonSolid     = have.fillModeNonSolid;
   want.samplerAnisotropy    = have.samplerAnisotropy;
   want.textureCompressionBC = have.textureCompressionBC;
   want.shaderClipDistance   = have.shaderClipDistance;

   memset(&qci, 0, sizeof(qci));
   qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
   qci.queueFamilyIndex = family;
   qci.queueCount       = 1;
   qci.pQueuePriorities = &priority;

   memset(&dci, 0, sizeof(dci));
   dci.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
   dci.queueCreateInfoCount = 1;
   dci.pQueueCreateInfos    = &qci;
   dci.pEnabledFeatures     = &want;

   device = wrapper(gpu, opaque, &dci);
   if (!device)
      return false;
   if (!vkb_load_device_procs(device))
      return false;

   vkb.features = want;
   memset(context, 0, sizeof(*context));
   context->gpu                             = gpu;
   context->device                          = device;
   context->queue_family_index              = family;
   context->presentation_queue_family_index = family;
   vk.vkGetDeviceQueue(device, family, 0, &context->queue);
   context->presentation_queue = context->queue;
   return true;
}

static struct retro_hw_render_context_negotiation_interface_vulkan negotiation;

const struct retro_hw_render_context_negotiation_interface_vulkan *vkb_negotiation(void)
{
   memset(&negotiation, 0, sizeof(negotiation));
   negotiation.interface_type       = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN;
   negotiation.interface_version    = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION;
   negotiation.get_application_info = vkb_application_info;
   negotiation.create_device2       = vkb_create_device2;
   return &negotiation;
}

/* --- memory ---------------------------------------------------------------- */

int vkb_memory_type(unsigned bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid)
{
   unsigned i;
   for (i = 0; i < vkb.memoryProperties.memoryTypeCount; i++)
   {
      VkMemoryPropertyFlags flags = vkb.memoryProperties.memoryTypes[i].propertyFlags;
      if ((bits & (1u << i)) && (flags & want) == want && !(flags & avoid))
         return (int)i;
   }
   return -1;
}

/* Memory for one object, mapped when host visible was asked for. */
int vkb_alloc_memory(const VkMemoryRequirements *req, VkMemoryPropertyFlags want,
      VkDeviceMemory *memory, void **mapped)
{
   VkMemoryAllocateInfo mai;
   int type = vkb_memory_type(req->memoryTypeBits, want, 0);
   if (type < 0 && (want & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
      type = vkb_memory_type(req->memoryTypeBits, want & ~VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
   if (type < 0)
      type = vkb_memory_type(req->memoryTypeBits, 0, 0);
   if (type < 0)
      return 0;
   memset(&mai, 0, sizeof(mai));
   mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   mai.allocationSize  = req->size;
   mai.memoryTypeIndex = (uint32_t)type;
   if (vk.vkAllocateMemory(vkb.device, &mai, NULL, memory) != VK_SUCCESS)
      return 0;
   if (mapped)
   {
      *mapped = NULL;
      if (want & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
         if (vk.vkMapMemory(vkb.device, *memory, 0, req->size, 0, mapped) != VK_SUCCESS)
         {
            vk.vkFreeMemory(vkb.device, *memory, NULL);
            *memory = VK_NULL_HANDLE;
            return 0;
         }
   }
   return 1;
}

/* --- the frames ------------------------------------------------------------ */

static int vkb_frames_init(void)
{
   unsigned i;
   for (i = 0; i < VKB_FRAMES; i++)
   {
      vkb_frame_t *f = &vkb.frames[i];
      VkCommandPoolCreateInfo pci;
      VkCommandBufferAllocateInfo cai;
      VkFenceCreateInfo fci;
      VkSemaphoreCreateInfo sci;
      VkDescriptorPoolSize sizes[3];
      VkDescriptorPoolCreateInfo dpi;

      memset(&pci, 0, sizeof(pci));
      pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
      pci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
      pci.queueFamilyIndex = vkb.queueFamily;
      if (vk.vkCreateCommandPool(vkb.device, &pci, NULL, &f->pool) != VK_SUCCESS)
         return 0;
      memset(&cai, 0, sizeof(cai));
      cai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      cai.commandPool        = f->pool;
      cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cai.commandBufferCount = 1;
      if (vk.vkAllocateCommandBuffers(vkb.device, &cai, &f->cmds[0]) != VK_SUCCESS)
         return 0;
      f->numCmds = 1;
      memset(&fci, 0, sizeof(fci));
      fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      fci.flags = 0;             /* waited only after a submit */
      if (vk.vkCreateFence(vkb.device, &fci, NULL, &f->fence) != VK_SUCCESS)
         return 0;
      memset(&sci, 0, sizeof(sci));
      sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
      if (vk.vkCreateSemaphore(vkb.device, &sci, NULL, &f->done) != VK_SUCCESS)
         return 0;

      sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      sizes[0].descriptorCount = 2 * 16384;
      sizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      sizes[1].descriptorCount = VKB_MAX_TEXTURE_UNITS * 16384;
      sizes[2].type            = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
      sizes[2].descriptorCount = 64;
      memset(&dpi, 0, sizeof(dpi));
      dpi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      dpi.maxSets       = 16384;
      dpi.poolSizeCount = 3;
      dpi.pPoolSizes    = sizes;
      if (vk.vkCreateDescriptorPool(vkb.device, &dpi, NULL, &f->descriptors) != VK_SUCCESS)
         return 0;
   }
   return 1;
}

static void vkb_frames_shutdown(void)
{
   unsigned i;
   for (i = 0; i < VKB_FRAMES; i++)
   {
      vkb_frame_t *f = &vkb.frames[i];
      if (f->descriptors)
         vk.vkDestroyDescriptorPool(vkb.device, f->descriptors, NULL);
      if (f->done)
         vk.vkDestroySemaphore(vkb.device, f->done, NULL);
      if (f->fence)
         vk.vkDestroyFence(vkb.device, f->fence, NULL);
      if (f->pool)
         vk.vkDestroyCommandPool(vkb.device, f->pool, NULL);
      memset(f, 0, sizeof(*f));
   }
}

/* the ring: one buffer, a segment per frame in flight, mapped for good */
static int vkb_ring_init(void)
{
   VkBufferCreateInfo bci;
   VkMemoryRequirements req;
   void *mapped = NULL;
   int type;
   memset(&bci, 0, sizeof(bci));
   bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   bci.size  = (VkDeviceSize)VKB_RING_SEGMENT_BYTES * VKB_FRAMES;
   bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
      | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
   if (vk.vkCreateBuffer(vkb.device, &bci, NULL, &vkb.ring) != VK_SUCCESS)
      return 0;
   vk.vkGetBufferMemoryRequirements(vkb.device, vkb.ring, &req);
   /* device-local host-visible memory where the GPU offers it (ReBAR,
    * UMA): the GPU then reads the frame's data where the CPU wrote it */
   type = vkb_memory_type(req.memoryTypeBits,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
         | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
   if (type < 0)
      type = vkb_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
   if (type < 0)
      return 0;
   {
      VkMemoryAllocateInfo mai;
      memset(&mai, 0, sizeof(mai));
      mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      mai.allocationSize  = req.size;
      mai.memoryTypeIndex = (uint32_t)type;
      if (vk.vkAllocateMemory(vkb.device, &mai, NULL, &vkb.ringMemory) != VK_SUCCESS)
         return 0;
   }
   if (vk.vkBindBufferMemory(vkb.device, vkb.ring, vkb.ringMemory, 0) != VK_SUCCESS)
      return 0;
   if (vk.vkMapMemory(vkb.device, vkb.ringMemory, 0, req.size, 0, &mapped) != VK_SUCCESS)
      return 0;
   vkb.ringMapped   = (unsigned char *)mapped;
   vkb.ringCoherent = 1;
   return 1;
}

static void vkb_ring_shutdown(void)
{
   if (vkb.ringMapped)
      vk.vkUnmapMemory(vkb.device, vkb.ringMemory);
   if (vkb.ring)
      vk.vkDestroyBuffer(vkb.device, vkb.ring, NULL);
   if (vkb.ringMemory)
      vk.vkFreeMemory(vkb.device, vkb.ringMemory, NULL);
   vkb.ring       = VK_NULL_HANDLE;
   vkb.ringMemory = VK_NULL_HANDLE;
   vkb.ringMapped = NULL;
}

static void *vkb_ring_alloc_slot(unsigned slot, size_t bytes, size_t align, unsigned *buffer, unsigned *offset)
{
   vkb_frame_t *f = &vkb.frames[slot];
   unsigned base = slot * VKB_RING_SEGMENT_BYTES;
   unsigned at;
   if (!vkb.ready || !align)
      return NULL;
   at = (unsigned)((f->ringUsed + align - 1) / align * align);
   /* a block's descriptor reads VKB_BLOCK_RANGE from its offset, so the
    * segment keeps that much past every allocation */
   if (at + bytes + VKB_BLOCK_RANGE > VKB_RING_SEGMENT_BYTES)
      return NULL;
   f->ringUsed = (unsigned)(at + bytes);
   *buffer = 0;              /* buffer 0 is the ring */
   *offset = base + at;
   return vkb.ringMapped + base + at;
}

void *vkb_ring_alloc(size_t bytes, size_t align, unsigned *buffer, unsigned *offset)
{
   return vkb_ring_alloc_slot(vkb.allocIndex, bytes, align, buffer, offset);
}

void *vkb_ring_alloc_frame(size_t bytes, size_t align, unsigned *buffer, unsigned *offset)
{
   return vkb_ring_alloc_slot(vkb.frameIndex, bytes, align, buffer, offset);
}

/* --- init and shutdown ------------------------------------------------------ */

/* A slot for the front end to write into: its last frame off the GPU,
 * its ring segment, command buffers and descriptor sets reused from
 * here.  Then what the frames in flight let go. */
static void vkb_acquire_slot(unsigned slot)
{
   vkb_frame_t *f = &vkb.frames[slot];
   if (f->submitted)
   {
      vk.vkWaitForFences(vkb.device, 1, &f->fence, VK_TRUE, (uint64_t)-1);
      vk.vkResetFences(vkb.device, 1, &f->fence);
      f->submitted = 0;
   }
   vk.vkResetCommandPool(vkb.device, f->pool, 0);
   vk.vkResetDescriptorPool(vkb.device, f->descriptors, 0);
   f->ringUsed = 0;
   vkb_buffers_frame_begin(slot);
   vkb_images_frame_done();
}

void vkb_frame_handoff(void)
{
   if (!vkb.ready || vkb.frameOpen)
      return;
   vkb.frameIndex = vkb.allocIndex;
   vkb.allocIndex = (vkb.allocIndex + 1) % VKB_FRAMES;
   vkb_acquire_slot(vkb.allocIndex);
}

int vkb_init(const struct retro_hw_render_interface_vulkan *iface, unsigned width, unsigned height)
{
   memset(&vkb, 0, sizeof(vkb));
   if (!iface || iface->interface_type != RETRO_HW_RENDER_INTERFACE_VULKAN
         || iface->interface_version < 5)
      return 0;
   vkb.iface       = iface;
   vkb.instance    = iface->instance;
   vkb.gpu         = iface->gpu;
   vkb.device      = iface->device;
   vkb.queue       = iface->queue;
   vkb.queueFamily = iface->queue_index;
   getInstanceProcAddr = iface->get_instance_proc_addr;
   if (!vkb_load_instance_procs(vkb.instance) || !vkb_load_device_procs(vkb.device))
      return 0;
   vk.vkGetPhysicalDeviceProperties(vkb.gpu, &vkb.properties);
   vk.vkGetPhysicalDeviceMemoryProperties(vkb.gpu, &vkb.memoryProperties);
   memcpy(vkb.deviceName, vkb.properties.deviceName, sizeof(vkb.deviceName) - 1);
   vkb.deviceName[sizeof(vkb.deviceName) - 1] = '\0';
   vkb.hasBC = vkb.features.textureCompressionBC;
   {
      VkFormatProperties fp;
      vk.vkGetPhysicalDeviceFormatProperties(vkb.gpu, VK_FORMAT_BC3_UNORM_BLOCK, &fp);
      if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
         vkb.hasBC = 0;
   }

   {
      VkPipelineCacheCreateInfo pci;
      memset(&pci, 0, sizeof(pci));
      pci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
      vk.vkCreatePipelineCache(vkb.device, &pci, NULL, &vkb.pipelineCache);
   }

   if (!vkt_lock_init() || !vkb_frames_init() || !vkb_memory_init() || !vkb_ring_init() || !vkb_images_init()
         || !vkb_buffers_init() || !vkb_pipelines_init())
   {
      vkb_shutdown();
      return 0;
   }

   /* the frame images, one per sync index the frontend can hand out */
   vkb.frameWidth  = width;
   vkb.frameHeight = height;
   if (!vkb_frame_images_make(VKB_FORMAT_RGBA8))
   {
      vkb_shutdown();
      return 0;
   }
   vkb.ready = 1;
   {
      static const unsigned char white[4] = { 255, 255, 255, 255 };
      vkb.whiteImage = vkb_image_create(1, 1, 1, 0, VKB_FORMAT_RGBA8, VKB_FILTER_LINEAR_NOMIP, VKB_WRAP_REPEAT, 0);
      vkb_image_upload(vkb.whiteImage, 0, 0, white, sizeof(white));
   }
   vkb.frameIndex = 0;
   vkb.allocIndex = 0;
   return 1;
}

void vkb_shutdown(void)
{
   unsigned i;
   if (vkb.device)
      vk.vkDeviceWaitIdle(vkb.device);
   for (i = 0; i < 8; i++)
      vkb_target_destroy(&vkb.frameImage[i]);
   vkb_target_destroy(&vkb.scene);
   vkb_pipelines_shutdown();
   vkb_buffers_shutdown();
   vkb_images_shutdown();
   vkb_ring_shutdown();
   vkb_memory_shutdown();
   vkb_frames_shutdown();
   if (vkb.pipelineCache)
      vk.vkDestroyPipelineCache(vkb.device, vkb.pipelineCache, NULL);
   vkt_lock_free();
   memset(&vkb, 0, sizeof(vkb));
}

int vkb_ready(void) { return vkb.ready; }
int vkb_scene_in_frame(void) { return vkb.frameOpen && vkb.sceneTarget == &vkb.frameImage[vkb.syncIndex]; }
unsigned vkb_max_texture_size(void) { return vkb.properties.limits.maxImageDimension2D; }
int vkb_has_bc(void) { return vkb.hasBC; }
int vkb_has_anisotropy(void) { return vkb.features.samplerAnisotropy; }
const char *vkb_device_name(void) { return vkb.deviceName; }

/* --- the frame ------------------------------------------------------------- */

static void vkb_image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
      VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
      VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
   VkImageMemoryBarrier b;
   memset(&b, 0, sizeof(b));
   b.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
   b.srcAccessMask               = srcAccess;
   b.dstAccessMask               = dstAccess;
   b.oldLayout                   = from;
   b.newLayout                   = to;
   b.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
   b.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
   b.image                       = image;
   b.subresourceRange.aspectMask = aspect;
   b.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
   b.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
   vk.vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &b);
}

void vkb_render_pass_begin(void)
{
   VkRenderPassBeginInfo rbi;
   vkb_target_t *t = vkb.target;
   const int inplace = vkb.targetKind == VKB_TARGET_INPLACE;
   if (vkb.inRenderPass)
      return;
   /* the target's first use leaves the undefined layout; afterwards the
    * frontend (frame image) or the composite (scene) left it readable.
    * The in-place pass reads and writes it in the general layout. */
   vkb_image_barrier(vkb.cmd, t->color, VK_IMAGE_ASPECT_COLOR_BIT,
         t->layoutReady ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
         inplace ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         VK_ACCESS_SHADER_READ_BIT,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | (inplace ? VK_ACCESS_INPUT_ATTACHMENT_READ_BIT : 0),
         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | (inplace ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : 0));
   if (!t->depthReady && t->depth)
   {
      vkb_image_barrier(vkb.cmd, t->depth, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
      t->depthReady = 1;
   }
   memset(&rbi, 0, sizeof(rbi));
   rbi.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rbi.renderPass               = inplace ? vkb.inplacePass[t->format] : t->renderPass;
   rbi.framebuffer              = inplace ? t->inplaceFramebuffer : t->framebuffer;
   rbi.renderArea.extent.width  = t->width;
   rbi.renderArea.extent.height = t->height;
   vk.vkCmdBeginRenderPass(vkb.cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
   vkb.inRenderPass    = 1;
   vkb.boundPipeline   = VK_NULL_HANDLE;
   vkb.boundSet        = VK_NULL_HANDLE;
   vkb.boundVertexBuffer = 0xffffffffu;
   vkb.boundIndexBuffer  = 0xffffffffu;
   memset(vkb.boundImages, 0xff, sizeof(vkb.boundImages));
   vkb.boundInput = 0xffffffffu;
   vkb_viewport(0, 0, (int)t->width, (int)t->height);
   vkb_scissor(0, 0, (int)t->width, (int)t->height);
}

void vkb_render_pass_end(void)
{
   if (!vkb.inRenderPass)
      return;
   vk.vkCmdEndRenderPass(vkb.cmd);
   vkb.inRenderPass = 0;
   /* readable by the frontend or the composite */
   vkb_image_barrier(vkb.cmd, vkb.target->color, VK_IMAGE_ASPECT_COLOR_BIT,
         vkb.targetKind == VKB_TARGET_INPLACE ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
   vkb.target->layoutReady = 1;
   if (vkb.targetKind == VKB_TARGET_POST && vkb.postImage)
      vkb.images[vkb.postImage].layoutReady = 1;
}

/* --- the post chain --------------------------------------------------------- */

int vkb_post_begin(unsigned target, unsigned width, unsigned height)
{
   if (!vkb.frameOpen)
      return 0;
   vkb_render_pass_end();
   if (target == 0)
   {
      vkb_target_t *frame = &vkb.frameImage[vkb.syncIndex];
      if (!frame->color)
         return 0;
      vkb.target     = frame;
      vkb.targetKind = VKB_TARGET_FRAME;
      vkb.postImage  = 0;
      vkb.frameComposited = 1;
   }
   else
   {
      VkFramebuffer fb;
      vkb_image_t *im;
      if (!vkb_image_post_framebuffer(target, &fb, &im))
         return 0;
      memset(&vkb.postTarget, 0, sizeof(vkb.postTarget));
      vkb.postTarget.color       = im->image;
      vkb.postTarget.colorView   = im->view;
      vkb.postTarget.framebuffer = fb;
      vkb.postTarget.renderPass  = vkb_post_render_pass((enum vkb_format)im->format);
      vkb.postTarget.width       = im->width;
      vkb.postTarget.height      = im->height;
      vkb.postTarget.format      = im->format;
      vkb.postTarget.layoutReady = im->layoutReady;
      vkb.target     = &vkb.postTarget;
      vkb.targetKind = VKB_TARGET_POST;
      vkb.postImage  = target;
   }
   vkb_render_pass_begin();
   if (width && height)
   {
      vkb_viewport(0, 0, (int)width, (int)height);
      vkb_scissor(0, 0, (int)width, (int)height);
   }
   return 1;
}

void vkb_post_end(void)
{
   vkb_render_pass_end();
}

int vkb_post_begin_inplace(void)
{
   vkb_target_t *frame = &vkb.frameImage[vkb.syncIndex];
   if (!vkb.frameOpen || !frame->color || !frame->inplaceFramebuffer)
      return 0;
   vkb_render_pass_end();
   vkb.target     = frame;
   vkb.targetKind = VKB_TARGET_INPLACE;
   vkb.postImage  = 0;
   vkb.frameComposited = 1;
   vkb_render_pass_begin();
   return 1;
}

/* the frame images in a format: remade when it changes */
static int vkb_frame_images_make(enum vkb_format format)
{
   unsigned mask = vkb.iface->get_sync_index_mask(vkb.iface->handle);
   unsigned i;
   for (i = 0; i < 8; i++)
      if (mask & (1u << i))
         if (!vkb_target_create(&vkb.frameImage[i], vkb.frameWidth, vkb.frameHeight, format, 1))
            return 0;
   vkb.frameFormat = (unsigned char)format;
   return 1;
}

int vkb_frame_format(enum vkb_format format)
{
   unsigned i;
   if (!vkb.ready || vkb.frameOpen)
      return 0;
   if (vkb.frameFormat == (unsigned char)format)
      return 1;
   vk.vkDeviceWaitIdle(vkb.device);
   for (i = 0; i < 8; i++)
      vkb_target_destroy(&vkb.frameImage[i]);
   return vkb_frame_images_make(format);
}

int vkb_begin_frame(enum vkb_target target, unsigned width, unsigned height,
      enum vkb_format sceneFormat)
{
   vkb_frame_t *f;
   VkCommandBufferBeginInfo bi;
   if (!vkb.ready || vkb.frameOpen)
      return 0;
   f = &vkb.frames[vkb.frameIndex];
   vkb.syncIndex = vkb.iface->get_sync_index(vkb.iface->handle) & 7;

   /* the frontend is done with this sync index's frame image */
   vkb.iface->wait_sync_index(vkb.iface->handle);

   if (target == VKB_TARGET_SCENE)
   {
      if (!vkb.scene.color || vkb.scene.width != width || vkb.scene.height != height
            || vkb.scene.format != (unsigned char)sceneFormat)
      {
         vk.vkDeviceWaitIdle(vkb.device);
         vkb_target_destroy(&vkb.scene);
         if (!vkb_target_create(&vkb.scene, width, height, sceneFormat, 0))
            return 0;
      }
      vkb.target = &vkb.scene;
   }
   else
      vkb.target = &vkb.frameImage[vkb.syncIndex];
   vkb.targetKind = (unsigned char)target;
   vkb.sceneTarget = vkb.target;
   if (!vkb.target->color)
      return 0;

   f->cmdUsed = 1;
   vkb.cmd = f->cmds[0];
   memset(&bi, 0, sizeof(bi));
   bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   if (vk.vkBeginCommandBuffer(vkb.cmd, &bi) != VK_SUCCESS)
      return 0;
   vkb.frameOpen = 1;
   vkb.drawn     = 0;
   vkb.frameComposited = 0;
   vkb.compositeProgram = VKB_PROGRAM_BLIT;
   vkb.compositeLut     = 0;

   /* textures and static data queued since the last frame */
   vkb_images_flush_uploads(vkb.cmd);
   vkb_buffers_flush(vkb.cmd);

   vkb_render_pass_begin();
   return 1;
}

/* the scene into the frame image: a fullscreen triangle through the
 * composite program */
static void vkb_composite(void)
{
   vkb_target_t *frame = &vkb.frameImage[vkb.syncIndex];
   vkb_draw_block_t block;
   vkb_draw_t d;
   unsigned sceneImage = vkb_image_scene_ref();
   if (!frame->color)
      return;
   vkb.target = frame;
   vkb.targetKind = VKB_TARGET_FRAME;
   vkb_render_pass_begin();
   memset(&block, 0, sizeof(block));
   memcpy(block.vertexParms, vkb.compositeParms, sizeof(block.vertexParms));
   block.screenParms[0] = 1.0f / (float)vkb.sceneTarget->width;
   block.screenParms[1] = 1.0f / (float)vkb.sceneTarget->height;
   block.screenParms[2] = (float)vkb.sceneTarget->width / (float)frame->width;
   block.screenParms[3] = (float)vkb.sceneTarget->height / (float)frame->height;
   block.glColor[0] = block.glColor[1] = block.glColor[2] = block.glColor[3] = 1.0f;
   memset(&d, 0, sizeof(d));
   d.block      = &block;
   d.image[0]   = sceneImage;
   d.image[1]   = vkb.compositeLut;
   d.indexCount = 3;
   d.program    = vkb.compositeProgram;
   d.layout     = VKB_VERTEX_DRAWVERT;
   d.cull       = VKB_CULL_NONE;
   d.stateBits  = 0x00010000u /* GLS_DEPTHFUNC_ALWAYS */ | 0x00000100u /* GLS_DEPTHMASK */;
   vkb_draw_fullscreen(&d);
   vkb_render_pass_end();
}

/* The frame's commands so far onto the queue, so the GPU starts on the
 * scene while the rest of the frame is recorded.  The render pass
 * resumes in a fresh command buffer; the last submit's fence covers
 * every earlier one, the queue running them in order. */
void vkb_flush(void)
{
   vkb_frame_t *f = &vkb.frames[vkb.frameIndex];
   VkSubmitInfo si;
   VkCommandBufferBeginInfo bi;
   int viewport[4], scissor[4];
   if (!vkb.frameOpen || !vkb.drawn)
      return;
   if (f->cmdUsed == f->numCmds)
   {
      VkCommandBufferAllocateInfo cai;
      if (f->numCmds == VKB_FRAME_CMDS)
         return;
      memset(&cai, 0, sizeof(cai));
      cai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      cai.commandPool        = f->pool;
      cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cai.commandBufferCount = 1;
      if (vk.vkAllocateCommandBuffers(vkb.device, &cai, &f->cmds[f->numCmds]) != VK_SUCCESS)
         return;
      f->numCmds++;
   }
   memcpy(viewport, vkb.viewport, sizeof(viewport));
   scissor[0] = vkb.scissor[0];
   scissor[1] = (int)vkb.target->height - vkb.scissor[1] - vkb.scissor[3];
   scissor[2] = vkb.scissor[2];
   scissor[3] = vkb.scissor[3];
   vkb_render_pass_end();
   vk.vkEndCommandBuffer(vkb.cmd);
   memset(&si, 0, sizeof(si));
   si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   si.commandBufferCount = 1;
   si.pCommandBuffers    = &vkb.cmd;
   vkb.iface->lock_queue(vkb.iface->handle);
   vk.vkQueueSubmit(vkb.queue, 1, &si, VK_NULL_HANDLE);
   vkb.iface->unlock_queue(vkb.iface->handle);

   vkb.cmd = f->cmds[f->cmdUsed++];
   memset(&bi, 0, sizeof(bi));
   bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   vk.vkBeginCommandBuffer(vkb.cmd, &bi);
   vkb_render_pass_begin();
   vkb_viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
   vkb_scissor(scissor[0], scissor[1], scissor[2], scissor[3]);
}

/* the frontend took the last frame handed to it: its semaphore is theirs */
void vkb_presented(void)
{
   vkb.unpresented = VK_NULL_HANDLE;
}

int vkb_end_frame(void)
{
   vkb_frame_t *f = &vkb.frames[vkb.frameIndex];
   VkSubmitInfo si;
   vkb_target_t *frame;
   VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
   if (!vkb.frameOpen)
      return 0;
   vkb_render_pass_end();
   if (vkb.targetKind == VKB_TARGET_SCENE && !vkb.frameComposited)
      vkb_composite();
   frame = &vkb.frameImage[vkb.syncIndex];
   vk.vkEndCommandBuffer(vkb.cmd);

   memset(&si, 0, sizeof(si));
   si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   si.commandBufferCount   = 1;
   si.pCommandBuffers      = &vkb.cmd;
   si.signalSemaphoreCount = 1;
   si.pSignalSemaphores    = &f->done;
   /* a frame the frontend was never shown - more than one swap in a
    * retro_run - left its semaphore signaled: this submit takes it */
   if (vkb.unpresented)
   {
      si.waitSemaphoreCount = 1;
      si.pWaitSemaphores    = &vkb.unpresented;
      si.pWaitDstStageMask  = &waitStage;
   }
   vkb.iface->lock_queue(vkb.iface->handle);
   vk.vkQueueSubmit(vkb.queue, 1, &si, f->fence);
   vkb.iface->unlock_queue(vkb.iface->handle);
   f->submitted = 1;

   /* the frontend waits the frame's semaphore before it reads the image */
   vkb.frameInfo[vkb.syncIndex].image_view   = frame->colorView;
   vkb.frameInfo[vkb.syncIndex].image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   vkb_target_view_info(frame, &vkb.frameInfo[vkb.syncIndex].create_info);
   vkb.iface->set_image(vkb.iface->handle, &vkb.frameInfo[vkb.syncIndex], 1, &f->done,
         VK_QUEUE_FAMILY_IGNORED);
   vkb.unpresented = f->done;

   vkb.frameOpen = 0;
   vkb.cmd       = VK_NULL_HANDLE;
   vkt_lock();
   vkb.frameCount++;
   vkt_unlock();
   return 1;
}

void vkb_clear(int color, int depth, int stencil, const float rgba[4], unsigned stencilValue)
{
   VkClearAttachment att[2];
   VkClearRect rect;
   unsigned n = 0;
   if (!vkb.inRenderPass)
      return;
   if (color)
   {
      att[n].aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
      att[n].colorAttachment = 0;
      att[n].clearValue.color.float32[0] = rgba ? rgba[0] : 0.0f;
      att[n].clearValue.color.float32[1] = rgba ? rgba[1] : 0.0f;
      att[n].clearValue.color.float32[2] = rgba ? rgba[2] : 0.0f;
      att[n].clearValue.color.float32[3] = rgba ? rgba[3] : 1.0f;
      n++;
   }
   if (depth || stencil)
   {
      att[n].aspectMask = (depth ? VK_IMAGE_ASPECT_DEPTH_BIT : 0)
         | (stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
      att[n].colorAttachment = 0;
      att[n].clearValue.depthStencil.depth   = 1.0f;
      att[n].clearValue.depthStencil.stencil = stencilValue;
      n++;
   }
   rect.rect.offset.x      = vkb.scissor[0];
   rect.rect.offset.y      = vkb.scissor[1];
   rect.rect.extent.width  = (uint32_t)vkb.scissor[2];
   rect.rect.extent.height = (uint32_t)vkb.scissor[3];
   rect.baseArrayLayer     = 0;
   rect.layerCount         = 1;
   vk.vkCmdClearAttachments(vkb.cmd, n, att, 1, &rect);
}

/* The engine's window coordinates have y up from the bottom; Vulkan's
 * framebuffer has y down from the top.  A negative-height viewport flips
 * the clip space so the engine's projection draws as it does under GL,
 * and the rectangles are mirrored into the framebuffer's rows. */
void vkb_viewport(int x, int y, int w, int h)
{
   VkViewport vp;
   int th = (int)vkb.target->height;
   vkb.viewport[0] = x;
   vkb.viewport[1] = y;
   vkb.viewport[2] = w;
   vkb.viewport[3] = h;
   vp.x        = (float)x;
   vp.y        = (float)(th - y);
   vp.width    = (float)w;
   vp.height   = -(float)h;
   vp.minDepth = vkb.depthRange[0];
   vp.maxDepth = vkb.depthRange[1] > vkb.depthRange[0] ? vkb.depthRange[1] : 1.0f;
   if (vkb.inRenderPass)
      vk.vkCmdSetViewport(vkb.cmd, 0, 1, &vp);
}

void vkb_scissor(int x, int y, int w, int h)
{
   VkRect2D sc;
   int th = (int)vkb.target->height;
   if (w < 0) w = 0;
   if (h < 0) h = 0;
   vkb.scissor[0] = x;
   vkb.scissor[1] = th - y - h;
   vkb.scissor[2] = w;
   vkb.scissor[3] = h;
   if (vkb.scissor[0] < 0) { vkb.scissor[2] += vkb.scissor[0]; vkb.scissor[0] = 0; }
   if (vkb.scissor[1] < 0) { vkb.scissor[3] += vkb.scissor[1]; vkb.scissor[1] = 0; }
   if (vkb.scissor[2] < 0) vkb.scissor[2] = 0;
   if (vkb.scissor[3] < 0) vkb.scissor[3] = 0;
   sc.offset.x      = vkb.scissor[0];
   sc.offset.y      = vkb.scissor[1];
   sc.extent.width  = (uint32_t)vkb.scissor[2];
   sc.extent.height = (uint32_t)vkb.scissor[3];
   if (vkb.inRenderPass)
      vk.vkCmdSetScissor(vkb.cmd, 0, 1, &sc);
}

void vkb_depth_range(float near, float far)
{
   vkb.depthRange[0] = near;
   vkb.depthRange[1] = far;
   vkb_viewport(vkb.viewport[0], vkb.viewport[1], vkb.viewport[2], vkb.viewport[3]);
}

void vkb_set_composite(enum vkb_program program, const float parms[4][4], unsigned lut)
{
   vkb.compositeProgram = (unsigned char)program;
   if (parms)
      memcpy(vkb.compositeParms, parms, sizeof(vkb.compositeParms));
   else
      memset(vkb.compositeParms, 0, sizeof(vkb.compositeParms));
   vkb.compositeLut = lut;
}
